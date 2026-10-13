#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      fprintf(stderr, "failed: %s (%d)\n", #cond, __LINE__); \
      return 1;                                              \
    }                                                        \
  } while (0)

int main(int argc, char* argv[]) {
  CHECK(argc > 1);
  char dir[4096];
  char path[4096];
  /* The plain and fortify builds run in parallel, so each gets its own dir. */
  snprintf(dir, sizeof(dir), "%s/%s", argv[1], basename(argv[0]));
  mkdir(argv[1], 0755);
  CHECK(mkdir(dir, 0755) == 0 || access(dir, F_OK) == 0);

  snprintf(path, sizeof(path), "%s/a.dat", dir);
  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  CHECK(fd >= 0);
  int fd_dup = dup(fd);
  int fd_dup2 = dup2(fd, 100);
  int fd_dup3 = dup3(fd, 101, O_CLOEXEC);
  int fd_fcntl = fcntl(fd, F_DUPFD, 0);
  CHECK(fd_dup >= 0 && fd_dup2 == 100 && fd_dup3 == 101 && fd_fcntl >= 0);
  CHECK(write(fd, "a", 1) == 1);
  CHECK(write(fd_dup, "b", 1) == 1);
  CHECK(write(fd_dup2, "c", 1) == 1);
  CHECK(write(fd_dup3, "d", 1) == 1);
  CHECK(write(fd_fcntl, "e", 1) == 1);
  CHECK(close(fd_fcntl) == 0);
  CHECK(close(fd_dup3) == 0);
  CHECK(close(fd_dup2) == 0);
  CHECK(close(fd_dup) == 0);
  CHECK(close(fd) == 0);

  snprintf(path, sizeof(path), "%s/b.dat", dir);
  fd = creat(path, 0644);
  CHECK(fd >= 0);
  CHECK(write(fd, "f", 1) == 1);
  CHECK(close(fd) == 0);

  snprintf(path, sizeof(path), "%s/c.txt", dir);
  FILE* f = fopen(path, "w");
  CHECK(f != NULL);
  int sfd = fileno(f);
  CHECK(write(sfd, "g", 1) == 1);
  CHECK(fprintf(f, "%s %d\n", "line", 1) > 0);
  CHECK(fclose(f) == 0);

  snprintf(path, sizeof(path), "%s/d.txt", dir);
  f = fopen("/dev/null", "w");
  CHECK(f != NULL);
  f = freopen(path, "w", f);
  CHECK(f != NULL);
  CHECK(write(fileno(f), "i", 1) == 1);
  CHECK(fclose(f) == 0);

  /* libc closed the streams' fd, so an untraced file that reuses the number
     must not be logged under c.txt or d.txt. */
  int untraced = open("/dev/null", O_WRONLY);
  CHECK(untraced == sfd);
  CHECK(write(untraced, "h", 1) == 1);
  CHECK(close(untraced) == 0);
  return 0;
}
