// Load for the pipeline profile (run_pipeline_profile.sh): events from the
// two capture sources dftracer has -- intercepted POSIX I/O and the C API --
// so a dftracer built with -DDFTRACER_ENABLE_PROFILING=ON can report where
// each event's overhead goes (capture, aggregation, serialization,
// compression, store; see src/dftracer/core/common/profiler.h).
//
// Built twice from this file: without dftracer (the baseline: POSIX calls
// are not intercepted and the annotations compile to nothing) and with
// -DWITH_DFTRACER. The traced binary then runs in whichever mode the
// environment selects, in-process or producer/consumer.
//
// Each of T threads makes N iterations between two barriers. One iteration
// is --posix=P small I/O calls (alternating pwrite and pread of --io-bytes
// on the thread's own file) and --api=A empty C API regions, so one
// iteration yields P + A events. Output is one CSV line:
//   label,threads,iters,posix,api,ns_per_iter_p50,ns_per_iter_max,
//   ns_per_event_p50,init_ms,fini_ms
// where ns_per_* is over threads (each thread's time / its iterations).

#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef WITH_DFTRACER
#include <dftracer/dftracer.h>
#else
#define DFTRACER_C_INIT(log_file, data_dirs, process_id)
#define DFTRACER_C_FINI()
#define DFTRACER_C_FUNCTION_START()
#define DFTRACER_C_FUNCTION_END()
#define DFTRACER_C_FUNCTION_UPDATE_INT(key, val)
#endif

static uint64_t iters = 100000;
static uint64_t warmup = 1000;
static int threads = 1;
static int posix_ops = 1;
static int api_ops = 1;
static size_t io_bytes = 64;
static int with_meta = 0;
static int first_cpu = -1;
static const char* dir = ".";
static pthread_barrier_t barrier;

struct result {
  uint64_t busy;
} __attribute__((aligned(64)));
static struct result* results;

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// An API event: an empty region, so its cost is the annotation's.
__attribute__((noinline)) static uint64_t api_op(uint64_t x) {
  DFTRACER_C_FUNCTION_START();
  if (with_meta) {
    DFTRACER_C_FUNCTION_UPDATE_INT("x", (int)x);
  }
  uint64_t r = x * 2654435761ull + 1;
  __asm__ volatile("" : "+r"(r));
  DFTRACER_C_FUNCTION_END();
  return r;
}

static uint64_t iteration(int fd, char* buf, uint64_t i) {
  uint64_t acc = 0;
  for (int p = 0; p < posix_ops; ++p) {
    const off_t off =
        (off_t)((i * (uint64_t)posix_ops + (uint64_t)p) % 64) * (off_t)io_bytes;
    ssize_t n = (p & 1) ? pread(fd, buf, io_bytes, off)
                        : pwrite(fd, buf, io_bytes, off);
    acc += (uint64_t)n;
  }
  for (int a = 0; a < api_ops; ++a) acc += api_op(i + (uint64_t)a);
  return acc;
}

static void* worker(void* arg) {
  const int id = (int)(intptr_t)arg;
  if (first_cpu >= 0) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(first_cpu + id, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  }
  char path[4096];
  snprintf(path, sizeof(path), "%s/pipeline_profile.%d.%d.dat", dir,
           (int)getpid(), id);
  const int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
  if (fd < 0) {
    perror(path);
    exit(1);
  }
  char* buf = malloc(io_bytes);
  memset(buf, 'x', io_bytes);
  uint64_t acc = 0;
  for (uint64_t i = 0; i < warmup; ++i) acc += iteration(fd, buf, i);
  pthread_barrier_wait(&barrier);
  const uint64_t t0 = now_ns();
  for (uint64_t i = 0; i < iters; ++i) acc += iteration(fd, buf, i);
  results[id].busy = now_ns() - t0;
  pthread_barrier_wait(&barrier);
  close(fd);
  unlink(path);
  free(buf);
  __asm__ volatile("" : : "r"(acc));
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
    else if (!strncmp(argv[i], "--iters=", 8))
      iters = strtoull(argv[i] + 8, NULL, 10);
    else if (!strncmp(argv[i], "--warmup=", 9))
      warmup = strtoull(argv[i] + 9, NULL, 10);
    else if (!strncmp(argv[i], "--posix=", 8))
      posix_ops = atoi(argv[i] + 8);
    else if (!strncmp(argv[i], "--api=", 6))
      api_ops = atoi(argv[i] + 6);
    else if (!strncmp(argv[i], "--io-bytes=", 11))
      io_bytes = strtoull(argv[i] + 11, NULL, 10);
    else if (!strncmp(argv[i], "--meta=", 7))
      with_meta = atoi(argv[i] + 7);
    else if (!strncmp(argv[i], "--first-cpu=", 12))
      first_cpu = atoi(argv[i] + 12);
    else if (!strncmp(argv[i], "--dir=", 6))
      dir = argv[i] + 6;
    else if (!strncmp(argv[i], "--label=", 8))
      label = argv[i] + 8;
    else {
      fprintf(stderr,
              "usage: %s [--threads=T] [--iters=N] [--warmup=W] [--posix=P] "
              "[--api=A] [--io-bytes=B] [--meta=0|1] [--first-cpu=C] "
              "[--dir=DIR] [--label=NAME]\n",
              argv[0]);
      return 2;
    }
  }
  if (io_bytes == 0) io_bytes = 1;
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

  double* per_iter = malloc(sizeof(double) * (size_t)threads);
  for (int t = 0; t < threads; ++t)
    per_iter[t] = (double)results[t].busy / (double)iters;
  qsort(per_iter, (size_t)threads, sizeof(double), cmp_double);
  const double p50 = per_iter[threads / 2];
  const int events = posix_ops + api_ops;
  printf("%s,%d,%llu,%d,%d,%.2f,%.2f,%.2f,%.3f,%.3f\n", label, threads,
         (unsigned long long)iters, posix_ops, api_ops, p50,
         per_iter[threads - 1], events > 0 ? p50 / events : 0.0,
         (double)(i1 - i0) / 1e6, (double)(f1 - f0) / 1e6);
  free(per_iter);
  free(tids);
  free(results);
  return 0;
}
