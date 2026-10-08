#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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
  char dir[PATH_MAX];
  char path[PATH_MAX];
  char link[PATH_MAX];
  char buf[256];
  snprintf(dir, sizeof(dir), "%s/fortify", argv[1]);
  mkdir(argv[1], 0755);
  CHECK(mkdir(dir, 0755) == 0 || access(dir, F_OK) == 0);
  snprintf(path, sizeof(path), "%s/a.txt", dir);
  snprintf(link, sizeof(link), "%s/a.lnk", dir);

  FILE* w = fopen(path, "w");
  CHECK(w != NULL);
  fputs("line one\nline two\n", w);
  fclose(w);
  unlink(link);
  CHECK(symlink(path, link) == 0);

  /* Non-constant lengths and flags keep the fortified entry points. */
  volatile size_t len = 8;
  volatile int flags = O_RDONLY;
  int dfd = open(dir, O_RDONLY | O_DIRECTORY);
  CHECK(dfd >= 0);

  int fd = open(path, flags);
  CHECK(fd >= 0);
  CHECK(read(fd, buf, len) > 0);
  CHECK(pread(fd, buf, len, 0) > 0);
  CHECK(pread64(fd, buf, len, 0) > 0);
  close(fd);

  fd = open64(path, flags);
  CHECK(fd >= 0);
  close(fd);
  fd = openat(dfd, "a.txt", flags);
  CHECK(fd >= 0);
  close(fd);
  fd = openat64(dfd, "a.txt", flags);
  CHECK(fd >= 0);
  close(fd);

  CHECK(readlink(link, buf, len + 200) > 0);
  CHECK(readlinkat(dfd, "a.lnk", buf, len + 200) > 0);
  CHECK(getcwd(buf, len + 200) != NULL);
  char resolved_buf[PATH_MAX];
  char* resolved = realpath(path, resolved_buf);
  CHECK(resolved != NULL);

  FILE* r = fopen(path, "r");
  CHECK(r != NULL);
  CHECK(fgets(buf, len + 8, r) != NULL);
  CHECK(fread(buf, 1, len, r) > 0);
  fclose(r);

  unlink(link);
  unlink(path);
  close(dfd);
  rmdir(dir);
  return 0;
}
