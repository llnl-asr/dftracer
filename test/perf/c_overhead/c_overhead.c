// Per-call overhead of the dftracer C API on a near-empty function.
//
// Built twice from this file: without dftracer (the annotations compile to
// nothing: the baseline) and with -DWITH_DFTRACER. The traced binary then runs
// in whichever mode the environment selects -- the original in-process path
// or producer/consumer mode (DFTRACER_ENABLE_PC_MODE=1) -- so every mode runs
// the same machine code and only the buffer manager behind it differs.
//
// Each of T threads calls tiny_op() N times between two barriers, as fast as
// it can or, with --rate=R, paced to R calls per second (in batches of 64;
// time spent waiting is not counted as call time). --work-ns=W makes each call
// spin for W ns inside the traced region, so events last W ns and back-to-back
// calls set the rate; --seconds=S runs for S seconds instead of N calls (N is
// then the calls each thread made). Output is one CSV line:
//   label,threads,ops_per_thread,meta,ns_per_op_p50,ns_per_op_max,
//   agg_Mops,init_ms,fini_ms
// where ns_per_op_* is over threads (each thread's time in calls / N) and
// agg_Mops is all calls / (last thread end - first thread start).

#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef WITH_DFTRACER
#include <dftracer/dftracer.h>
#else
#define DFTRACER_C_INIT(log_file, data_dirs, process_id)
#define DFTRACER_C_FINI()
#define DFTRACER_C_FUNCTION_START()
#define DFTRACER_C_FUNCTION_END()
#define DFTRACER_C_FUNCTION_UPDATE_INT(key, val)
#define DFTRACER_C_FUNCTION_UPDATE_STR(key, val)
#endif

static uint64_t ops = 1000000;
static uint64_t warmup = 10000;
static int threads = 1;
static int with_meta = 0;
// --payload=N: also attach an N-character string, to size the event.
static char* payload = NULL;
static int first_cpu = -1;
static pthread_barrier_t barrier;
static double rate = 0;       // calls per second per thread; 0 = unpaced
static uint64_t work_ns = 0;  // spun inside each traced call
static double seconds = 0;    // run for this long instead of `ops` calls

struct result {
  uint64_t t0, t1;
  uint64_t busy;  // ns spent in calls
  uint64_t done;  // calls made
  uint64_t sink;
} __attribute__((aligned(64)));
static struct result* results;

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// The traced "operation": a multiply-add, so the cost measured is the
// annotation's, not the work's.
__attribute__((noinline)) static uint64_t tiny_op(uint64_t x) {
  DFTRACER_C_FUNCTION_START();
  if (with_meta) {
    DFTRACER_C_FUNCTION_UPDATE_INT("x", (int)x);
  }
  if (payload != NULL) {
    DFTRACER_C_FUNCTION_UPDATE_STR("payload", payload);
  }
  uint64_t r = x * 2654435761ull + 1;
  __asm__ volatile("" : "+r"(r));
  if (work_ns != 0) {
    const uint64_t w0 = now_ns();
    while (now_ns() - w0 < work_ns) {
    }
  }
  DFTRACER_C_FUNCTION_END();
  return r;
}

static void* worker(void* arg) {
  const int id = (int)(intptr_t)arg;
  if (first_cpu >= 0) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(first_cpu + id, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  }
  uint64_t acc = 0;
  for (uint64_t i = 0; i < warmup; ++i) acc += tiny_op(i);
  pthread_barrier_wait(&barrier);
  const uint64_t t0 = now_ns();
  uint64_t busy = 0, done = ops;
  if (seconds > 0) {
    const uint64_t deadline = t0 + (uint64_t)(seconds * 1e9);
    uint64_t i = 0;
    while (now_ns() < deadline) acc += tiny_op(i++);
    done = i;
    busy = now_ns() - t0;
  } else if (rate <= 0) {
    for (uint64_t i = 0; i < ops; ++i) acc += tiny_op(i);
    busy = now_ns() - t0;
  } else {
    const double gap_ns = 1e9 / rate;
    for (uint64_t i = 0; i < ops;) {
      const uint64_t b0 = now_ns();
      const uint64_t end = i + 64 < ops ? i + 64 : ops;
      for (; i < end; ++i) acc += tiny_op(i);
      busy += now_ns() - b0;
      const uint64_t due = t0 + (uint64_t)(i * gap_ns);
      const uint64_t now = now_ns();
      if (now < due) {
        struct timespec ts = {(time_t)((due - now) / 1000000000ull),
                              (long)((due - now) % 1000000000ull)};
        nanosleep(&ts, NULL);
      }
    }
  }
  const uint64_t t1 = now_ns();
  pthread_barrier_wait(&barrier);
  results[id].t0 = t0;
  results[id].t1 = t1;
  results[id].busy = busy;
  results[id].done = done;
  results[id].sink = acc;
  return NULL;
}

static int cmp_double(const void* a, const void* b) {
  double x = *(const double*)a, y = *(const double*)b;
  return (x > y) - (x < y);
}

int main(int argc, char** argv) {
  const char* label = "run";
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], "--threads=", 10))
      threads = atoi(argv[i] + 10);
    else if (!strncmp(argv[i], "--ops=", 6))
      ops = strtoull(argv[i] + 6, NULL, 10);
    else if (!strncmp(argv[i], "--warmup=", 9))
      warmup = strtoull(argv[i] + 9, NULL, 10);
    else if (!strncmp(argv[i], "--meta=", 7))
      with_meta = atoi(argv[i] + 7);
    else if (!strncmp(argv[i], "--payload=", 10)) {
      const size_t n = strtoull(argv[i] + 10, NULL, 10);
      if (n > 0) {
        payload = malloc(n + 1);
        memset(payload, 'p', n);
        payload[n] = '\0';
      }
    } else if (!strncmp(argv[i], "--first-cpu=", 12))
      first_cpu = atoi(argv[i] + 12);
    else if (!strncmp(argv[i], "--label=", 8))
      label = argv[i] + 8;
    else if (!strncmp(argv[i], "--rate=", 7))
      rate = atof(argv[i] + 7);
    else if (!strncmp(argv[i], "--work-ns=", 10))
      work_ns = strtoull(argv[i] + 10, NULL, 10);
    else if (!strncmp(argv[i], "--seconds=", 10))
      seconds = atof(argv[i] + 10);
    else {
      fprintf(stderr,
              "usage: %s [--threads=T] [--ops=N] [--warmup=W] [--meta=0|1] "
              "[--payload=BYTES] "
              "[--first-cpu=C] [--rate=CALLS_PER_S] [--work-ns=NS] "
              "[--seconds=S] [--label=NAME]\n",
              argv[0]);
      return 2;
    }
  }
  results = aligned_alloc(64, sizeof(struct result) * (size_t)threads);
  pthread_barrier_init(&barrier, NULL, (unsigned)threads);

  const uint64_t i0 = now_ns();
  DFTRACER_C_INIT(NULL, NULL, NULL);
  const uint64_t i1 = now_ns();

  pthread_t* tids = malloc(sizeof(pthread_t) * (size_t)threads);
  for (int t = 0; t < threads; ++t)
    pthread_create(&tids[t], NULL, worker, (void*)(intptr_t)t);
  for (int t = 0; t < threads; ++t) pthread_join(tids[t], NULL);

  const uint64_t f0 = now_ns();
  DFTRACER_C_FINI();
  const uint64_t f1 = now_ns();

  double* per_op = malloc(sizeof(double) * (size_t)threads);
  uint64_t start = UINT64_MAX, end = 0, sink = 0, total = 0;
  for (int t = 0; t < threads; ++t) {
    per_op[t] = (double)results[t].busy / (double)results[t].done;
    total += results[t].done;
    if (results[t].t0 < start) start = results[t].t0;
    if (results[t].t1 > end) end = results[t].t1;
    sink += results[t].sink;
  }
  qsort(per_op, (size_t)threads, sizeof(double), cmp_double);
  const double agg = (double)total / ((double)(end - start) * 1e-9) / 1e6;
  printf("%s,%d,%llu,%d,%.2f,%.2f,%.3f,%.2f,%.2f\n", label, threads,
         (unsigned long long)(total / (uint64_t)threads), with_meta,
         per_op[threads / 2], per_op[threads - 1], agg, (i1 - i0) * 1e-6,
         (f1 - f0) * 1e-6);
  if (sink == 42) puts("");  // keep the work observable
  return 0;
}
