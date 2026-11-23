// Makes traced calls of a known length and writes, for each, the wall clock
// just before and just after it, so check_timing.py can hold the trace's ts
// and dur to them. The POSIX calls block on a FIFO until a second thread
// opens and writes it; the C API regions sleep.
//
// usage: timing <work dir> <expected file>
// The expected file has one line per call: <name> <before_us> <after_us>,
// then "run <start_us> <end_us>" for the whole traced run.

#define _GNU_SOURCE
#include <dftracer/dftracer.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define NAP_US 20000
#define ROUNDS 5

static char fifo[4096];

static uint64_t wall_us(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static void* writer(void* arg) {
  (void)arg;
  usleep(NAP_US);
  int fd = open(fifo, O_WRONLY);
  if (fd < 0) return (void*)1;
  usleep(NAP_US);
  ssize_t n = write(fd, "x", 1);
  close(fd);
  return n == 1 ? NULL : (void*)1;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <work dir> <expected file>\n", argv[0]);
    return 2;
  }
  snprintf(fifo, sizeof(fifo), "%s/fifo", argv[1]);
  FILE* out = fopen(argv[2], "w");
  if (out == NULL) return 2;
  const uint64_t start = wall_us();
  DFTRACER_C_INIT(NULL, NULL, NULL);
  int rc = 0;
  for (int i = 0; i < ROUNDS && rc == 0; i++) {
    unlink(fifo);
    if (mkfifo(fifo, 0600) != 0) return 2;
    pthread_t thread;
    pthread_create(&thread, NULL, writer, NULL);
    uint64_t before = wall_us();
    int fd = open(fifo, O_RDONLY);
    uint64_t after = wall_us();
    fprintf(out, "open %llu %llu\n", (unsigned long long)before,
            (unsigned long long)after);
    char c;
    before = wall_us();
    ssize_t n = read(fd, &c, 1);
    after = wall_us();
    fprintf(out, "read %llu %llu\n", (unsigned long long)before,
            (unsigned long long)after);
    close(fd);
    void* thread_rc;
    pthread_join(thread, &thread_rc);
    if (fd < 0 || n != 1 || thread_rc != NULL) rc = 1;

    before = wall_us();
    DFTRACER_C_REGION_START(nap);
    usleep(NAP_US);
    DFTRACER_C_REGION_END(nap);
    after = wall_us();
    fprintf(out, "nap %llu %llu\n", (unsigned long long)before,
            (unsigned long long)after);
  }
  unlink(fifo);
  DFTRACER_C_FINI();
  fprintf(out, "run %llu %llu\n", (unsigned long long)start,
          (unsigned long long)wall_us());
  fclose(out);
  return rc;
}
