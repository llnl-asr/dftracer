#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "check.h"

/* Forks while another thread holds the trace locks. A child that inherits a
   held lock hangs. */
#ifndef NFORKS
#define NFORKS 100
#endif
#define CHILD_TIMEOUT_SECONDS 10

static char path[4096];
static int stop = 0;

static void* writer(void* arg) {
  (void)arg;
  char buf[64];
  memset(buf, 'x', sizeof(buf));
  while (!__atomic_load_n(&stop, __ATOMIC_RELAXED)) {
    int fd = open(path, O_WRONLY);
    DFT_CHECK(fd >= 0);
    DFT_CHECK(pwrite(fd, buf, sizeof(buf), 0) == (ssize_t)sizeof(buf));
    DFT_CHECK(close(fd) == 0);
  }
  return NULL;
}

static int exited_in_time(pid_t pid) {
  for (int i = 0; i < CHILD_TIMEOUT_SECONDS * 100; ++i) {
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid)
      return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    usleep(10000);
  }
  kill(pid, SIGKILL);
  waitpid(pid, NULL, 0);
  return 0;
}

int main(int argc, char* argv[]) {
  DFT_CHECK(argc > 1);
  mkdir(argv[1], 0755);
  snprintf(path, sizeof(path), "%s/fork_flush.dat", argv[1]);
  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  DFT_CHECK(fd >= 0);
  DFT_CHECK(close(fd) == 0);

  pthread_t thread;
  DFT_CHECK(pthread_create(&thread, NULL, writer, NULL) == 0);
  int stuck = 0;
  for (int i = 0; i < NFORKS; ++i) {
    pid_t pid = fork();
    DFT_CHECK(pid >= 0);
    if (pid == 0) {
      int child_fd = open(path, O_RDONLY);
      if (child_fd >= 0) close(child_fd);
      _exit(0);
    }
    if (!exited_in_time(pid)) {
      stuck = i + 1;
      break;
    }
  }
  __atomic_store_n(&stop, 1, __ATOMIC_RELAXED);
  DFT_CHECK(pthread_join(thread, NULL) == 0);
  if (stuck > 0) fprintf(stderr, "child %d of %d hung\n", stuck, NFORKS);
  DFT_CHECK(stuck == 0);
  return 0;
}
