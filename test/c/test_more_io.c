#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "check.h"

static int call_vdprintf(int fd, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int ret = vdprintf(fd, format, args);
  va_end(args);
  return ret;
}

int main(int argc, char* argv[]) {
  DFT_CHECK(argc > 1);
  char dir[4096];
  char path[4096];
  char buf[64];
  /* The plain and fortify builds run in parallel, so each gets its own dir. */
  snprintf(dir, sizeof(dir), "%s/%s", argv[1], basename(argv[0]));
  mkdir(argv[1], 0755);
  DFT_CHECK(mkdir(dir, 0755) == 0 || access(dir, F_OK) == 0);

  snprintf(path, sizeof(path), "%s/a.txt", dir);
  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  DFT_CHECK(fd >= 0);
  DFT_CHECK(dprintf(fd, "%s %d\n", "line", 1) > 0);
  DFT_CHECK(call_vdprintf(fd, "%s %d\n", "line", 2) > 0);

  snprintf(path, sizeof(path), "%s/b.txt", dir);
  FILE* f = fopen(path, "w+");
  DFT_CHECK(f != NULL);
  /* A volatile size keeps glibc's inline fwrite_unlocked macro out. */
  volatile size_t size = 16;
  memset(buf, 'x', sizeof(buf));
  DFT_CHECK(fwrite_unlocked(buf, 1, size, f) == size);
  DFT_CHECK(fputs_unlocked("line\n", f) >= 0);
  DFT_CHECK(fputc_unlocked('z', f) == 'z');
  rewind(f);
  DFT_CHECK(fread_unlocked(buf, 1, size, f) == size);
  DFT_CHECK(fgets_unlocked(buf, sizeof(buf), f) != NULL);
  DFT_CHECK(fgetc_unlocked(f) == 'z');
  DFT_CHECK(fclose(f) == 0);

  /* A vfork child shares the tracer's memory but not the fd table. Its
     close calls must not untrack fd in the parent, so the write after it is
     still traced. */
  pid_t pid = vfork();
  if (pid == 0) {
    dup2(fd, 50);
    close(fd);
#if defined(__GLIBC__) && __GLIBC_PREREQ(2, 34)
    close_range(50, 50, 0);
#endif
    _exit(0);
  }
  DFT_CHECK(pid > 0);
  DFT_CHECK(waitpid(pid, NULL, 0) == pid);
  DFT_CHECK(write(fd, "v", 1) == 1);

#if defined(__GLIBC__) && __GLIBC_PREREQ(2, 34)
  int fd_range = dup(fd);
  DFT_CHECK(fd_range >= 0);
  DFT_CHECK(close_range(fd_range, fd_range, 0) == 0);
  int fd_from = dup(fd);
  DFT_CHECK(fd_from > fd);
  closefrom(fd_from);
#endif
  DFT_CHECK(close(fd) == 0);
  return 0;
}
