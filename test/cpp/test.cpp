//
// Created by hariharan on 8/8/22.
//

#include <assert.h>
#include <dftracer/dftracer.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#include <string>

#include "ignore_result.h"

void foo() {
  DFTRACER_CPP_FUNCTION();

  DFTRACER_CPP_FUNCTION_UPDATE("key", 0);
  DFTRACER_CPP_FUNCTION_UPDATE("key", "0");
  usleep(1000);
  {
    DFTRACER_CPP_REGION(CUSTOM);
    DFTRACER_CPP_REGION_UPDATE(CUSTOM, "key", "0");
    usleep(1000);
    DFTRACER_CPP_REGION_START(CUSTOM_BLOCK);
    DFTRACER_CPP_REGION_UPDATE(CUSTOM, "key", 0);
    DFTRACER_CPP_REGION_DYN_UPDATE(CUSTOM_BLOCK, "key", 0);
    usleep(1000);
    DFTRACER_CPP_REGION_END(CUSTOM_BLOCK);
  }
}

int main(int argc, char* argv[]) {
  int init = 0;
  if (argc > 2) {
    if (strcmp(argv[2], "1") == 0) {
      DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
      init = 1;
    }
  }
  DFTRACER_CPP_METADATA(meta, "key", "value");
  char filename[1024];
  sprintf(filename, "%s/demofile.txt", argv[1]);
  char filename_link[1024];
  sprintf(filename_link, "%s/demofile_link.txt", argv[1]);
  foo();
  DFT_IGNORE(truncate(filename, 0));
  FILE* fh = fopen(filename, "w+");
  if (fh != nullptr) {
    fwrite("hello", sizeof("hello"), 1, fh);
    fwrite("hello", sizeof("hello"), 1, fh);
    fwrite("hello", sizeof("hello"), 1, fh);
    fwrite("hello", sizeof("hello"), 1, fh);
    fclose(fh);
  }
  DFT_IGNORE(link(filename, filename_link));
  unlink(filename_link);
  DFT_IGNORE(symlink(filename, filename_link));
  chmod(filename, S_ISUID);
  DFT_IGNORE(chown(filename, 0, 0));
  DFT_IGNORE(lchown(filename, 0, 0));
  struct utimbuf utimbuf1 = {};
  utime(filename, &utimbuf1);
  char dir[1024];
  sprintf(dir, "%s", argv[1]);
  int dd = open(dir, O_PATH);
  assert(dd != -1);
  fcntl(dd, F_DUPFD);
  fcntl(dd, F_GETFD);
  fcntl(dd, F_GETOWN_EX);
  int dd2 = -1;
  dup2(dd, dd2);
  umask(0);
  mkfifo(filename, 0);
  DFT_IGNORE(symlinkat(filename, dd, filename_link));
  DFT_IGNORE(faccessat(dd, "demofile.txt", O_RDONLY, 0));
  DFT_IGNORE(linkat(dd, "demofile.txt", dd, "demofile_link2.txt", 0));
  DFT_IGNORE(chdir(dir));
  int fd = openat(dd, "demofile.txt", O_RDONLY);
  if (fd != -1) close(fd);
  fd = openat(dd, "demofile2.txt", O_WRONLY | O_CREAT, 777);
  if (fd != -1) close(fd);
  fd = openat(dd, "demofile2.txt", O_WRONLY | O_CREAT, 777);
  if (fd != -1) close(fd);
  close(dd);
  sprintf(filename, "%s/demofile2.txt", argv[1]);
  fd = creat64(filename, O_RDWR);
  if (fd != -1) close(fd);
  fd = open(filename, O_RDWR);
  int set_offset = lseek(fd, 1, SEEK_SET);
  (void)set_offset;
  char buf[1] = {0};
  DFT_IGNORE(pread(fd, buf, 1, 1));
  DFT_IGNORE(pread64(fd, buf, 1, 1));
  DFT_IGNORE(pwrite(fd, buf, 1, 1));
  DFT_IGNORE(pwrite64(fd, buf, 1, 1));
  fsync(fd);
  fdatasync(fd);
  DFT_IGNORE(readlinkat(fd, filename, buf, 1));
  DFT_IGNORE(ftruncate(fd, 0));
  close(fd);
  remove(filename);
  remove(filename_link);
  remove("demofile_link2.txt");
  if (init == 1) {
    printf("Calling Finalize");
    DFTRACER_CPP_FINI();
  }
  return 0;
}