#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"

int main(int argc, char* argv[]) {
  DFT_CHECK(argc > 1);
  char dir[PATH_MAX];
  char path[PATH_MAX];
  char link[PATH_MAX];
  char buf[256];
  snprintf(dir, sizeof(dir), "%s/fortify", argv[1]);
  mkdir(argv[1], 0755);
  DFT_CHECK(mkdir(dir, 0755) == 0 || access(dir, F_OK) == 0);
  snprintf(path, sizeof(path), "%s/a.txt", dir);
  snprintf(link, sizeof(link), "%s/a.lnk", dir);

  FILE* w = fopen(path, "w");
  DFT_CHECK(w != NULL);
  fputs("line one\nline two\n", w);
  fclose(w);
  unlink(link);
  DFT_CHECK(symlink(path, link) == 0);

  /* Non-constant lengths and flags keep the fortified entry points. */
  volatile size_t len = 8;
  volatile int flags = O_RDONLY;
  int dfd = open(dir, O_RDONLY | O_DIRECTORY);
  DFT_CHECK(dfd >= 0);

  int fd = open(path, flags);
  DFT_CHECK(fd >= 0);
  DFT_CHECK(read(fd, buf, len) > 0);
  DFT_CHECK(pread(fd, buf, len, 0) > 0);
  DFT_CHECK(pread64(fd, buf, len, 0) > 0);
  close(fd);

  fd = open64(path, flags);
  DFT_CHECK(fd >= 0);
  close(fd);
  fd = openat(dfd, "a.txt", flags);
  DFT_CHECK(fd >= 0);
  close(fd);
  fd = openat64(dfd, "a.txt", flags);
  DFT_CHECK(fd >= 0);
  close(fd);

  DFT_CHECK(readlink(link, buf, len + 200) > 0);
  DFT_CHECK(readlinkat(dfd, "a.lnk", buf, len + 200) > 0);
  DFT_CHECK(getcwd(buf, len + 200) != NULL);
  char resolved_buf[PATH_MAX];
  char* resolved = realpath(path, resolved_buf);
  DFT_CHECK(resolved != NULL);

  FILE* r = fopen(path, "r");
  DFT_CHECK(r != NULL);
  DFT_CHECK(fgets(buf, len + 8, r) != NULL);
  DFT_CHECK(fread(buf, 1, len, r) > 0);
  fclose(r);

  unlink(link);
  unlink(path);
  close(dfd);
  rmdir(dir);
  return 0;
}
