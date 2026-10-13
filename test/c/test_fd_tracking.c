#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"

int main(int argc, char* argv[]) {
  DFT_CHECK(argc > 1);
  char dir[4096];
  char path[4096];
  /* The plain and fortify builds run in parallel, so each gets its own dir. */
  snprintf(dir, sizeof(dir), "%s/%s", argv[1], basename(argv[0]));
  mkdir(argv[1], 0755);
  DFT_CHECK(mkdir(dir, 0755) == 0 || access(dir, F_OK) == 0);

  snprintf(path, sizeof(path), "%s/a.dat", dir);
  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  DFT_CHECK(fd >= 0);
  int fd_dup = dup(fd);
  int fd_dup2 = dup2(fd, 100);
  int fd_dup3 = dup3(fd, 101, O_CLOEXEC);
  int fd_fcntl = fcntl(fd, F_DUPFD, 0);
  DFT_CHECK(fd_dup >= 0 && fd_dup2 == 100 && fd_dup3 == 101 && fd_fcntl >= 0);
  DFT_CHECK(write(fd, "a", 1) == 1);
  DFT_CHECK(write(fd_dup, "b", 1) == 1);
  DFT_CHECK(write(fd_dup2, "c", 1) == 1);
  DFT_CHECK(write(fd_dup3, "d", 1) == 1);
  DFT_CHECK(write(fd_fcntl, "e", 1) == 1);
  DFT_CHECK(close(fd_fcntl) == 0);
  DFT_CHECK(close(fd_dup3) == 0);
  DFT_CHECK(close(fd_dup2) == 0);
  DFT_CHECK(close(fd_dup) == 0);
  DFT_CHECK(close(fd) == 0);

  snprintf(path, sizeof(path), "%s/b.dat", dir);
  fd = creat(path, 0644);
  DFT_CHECK(fd >= 0);
  DFT_CHECK(write(fd, "f", 1) == 1);
  DFT_CHECK(close(fd) == 0);

  snprintf(path, sizeof(path), "%s/c.txt", dir);
  FILE* f = fopen(path, "w");
  DFT_CHECK(f != NULL);
  int sfd = fileno(f);
  DFT_CHECK(write(sfd, "g", 1) == 1);
  DFT_CHECK(fprintf(f, "%s %d\n", "line", 1) > 0);
  DFT_CHECK(fclose(f) == 0);

  snprintf(path, sizeof(path), "%s/d.txt", dir);
  f = fopen("/dev/null", "w");
  DFT_CHECK(f != NULL);
  f = freopen(path, "w", f);
  DFT_CHECK(f != NULL);
  DFT_CHECK(write(fileno(f), "i", 1) == 1);
  DFT_CHECK(fclose(f) == 0);

  /* libc closed the streams' fd, so an untraced file that reuses the number
     must not be logged under c.txt or d.txt. */
  int untraced = open("/dev/null", O_WRONLY);
  DFT_CHECK(untraced == sfd);
  DFT_CHECK(write(untraced, "h", 1) == 1);
  DFT_CHECK(close(untraced) == 0);
  return 0;
}
