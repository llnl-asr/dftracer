#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"

/* Relative paths inside the data dir are traced, and a relative path that
   leaves it with ".." is not. */
int main(int argc, char* argv[]) {
  DFT_CHECK(argc > 1);
  char dir[4096];
  snprintf(dir, sizeof(dir), "%s/relative_path", argv[1]);
  mkdir(argv[1], 0755);
  mkdir(dir, 0755);
  DFT_CHECK(chdir(dir) == 0);

  int fd = open("a.dat", O_CREAT | O_WRONLY | O_TRUNC, 0644);
  DFT_CHECK(fd >= 0);
  DFT_CHECK(write(fd, "a", 1) == 1);
  DFT_CHECK(close(fd) == 0);
  DFT_CHECK(access("./a.dat", R_OK) == 0);
  DFT_CHECK(access("../relative_path//a.dat", R_OK) == 0);
  FILE* f = fopen("b.txt", "w");
  DFT_CHECK(f != NULL);
  DFT_CHECK(fclose(f) == 0);
  DFT_CHECK(rename("b.txt", "sub_b.txt") == 0);

  fd = open("../../relative_path_outside.dat", O_CREAT | O_WRONLY, 0644);
  DFT_CHECK(fd >= 0);
  DFT_CHECK(write(fd, "o", 1) == 1);
  DFT_CHECK(close(fd) == 0);
  DFT_CHECK(unlink("../../relative_path_outside.dat") == 0);

  DFT_CHECK(unlink("a.dat") == 0);
  DFT_CHECK(unlink("./sub_b.txt") == 0);
  return 0;
}
