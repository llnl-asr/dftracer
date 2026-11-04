#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"

/* Threads that open and close traced files at the same time. Each one gets
   fd numbers, FILE* addresses and DIR* fds that another thread just
   released, so a table entry cleared after the real close, or a FILE* table
   that is not safe to share, drops events or crashes (issue #396). The CMake
   test checks that the trace holds exactly one event for each call. */
#ifndef NTHREADS
#define NTHREADS 8
#endif
#ifndef NFILES
#define NFILES 100
#endif
#ifndef ROUNDS
#define ROUNDS 10
#endif

static char dir[4096];

static void* worker(void* arg) {
  (void)arg;
  char path[4096];
  char buf[64];
  struct stat st;
  for (int r = 0; r < ROUNDS; ++r) {
    for (int i = 0; i < NFILES; ++i) {
      snprintf(path, sizeof(path), "%s/f%d.dat", dir, i);
      DFT_CHECK(stat(path, &st) == 0);
      int fd = open(path, O_RDONLY);
      DFT_CHECK(fd >= 0);
      DFT_CHECK(pread(fd, buf, sizeof(buf), 0) == (ssize_t)sizeof(buf));
      DFT_CHECK(close(fd) == 0);
      FILE* fp = fopen(path, "r");
      DFT_CHECK(fp != NULL);
      DFT_CHECK(fread(buf, 1, sizeof(buf), fp) == sizeof(buf));
      DFT_CHECK(fclose(fp) == 0);
      DIR* d = opendir(dir);
      DFT_CHECK(d != NULL);
      DFT_CHECK(readdir(d) != NULL);
      DFT_CHECK(closedir(d) == 0);
    }
  }
  return NULL;
}

int main(int argc, char* argv[]) {
  DFT_CHECK(argc > 1);
  snprintf(dir, sizeof(dir), "%s/%s", argv[1], basename(argv[0]));
  mkdir(argv[1], 0755);
  DFT_CHECK(mkdir(dir, 0755) == 0 || access(dir, F_OK) == 0);

  char path[4096];
  char buf[64];
  memset(buf, 'x', sizeof(buf));
  for (int i = 0; i < NFILES; ++i) {
    snprintf(path, sizeof(path), "%s/f%d.dat", dir, i);
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    DFT_CHECK(fd >= 0);
    DFT_CHECK(write(fd, buf, sizeof(buf)) == (ssize_t)sizeof(buf));
    DFT_CHECK(close(fd) == 0);
  }

  pthread_t threads[NTHREADS];
  for (int t = 0; t < NTHREADS; ++t)
    DFT_CHECK(pthread_create(&threads[t], NULL, worker, NULL) == 0);
  for (int t = 0; t < NTHREADS; ++t)
    DFT_CHECK(pthread_join(threads[t], NULL) == 0);
  return 0;
}
