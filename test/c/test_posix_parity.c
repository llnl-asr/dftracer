#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "check.h"

int main(int argc, char* argv[]) {
  DFT_CHECK(argc > 1);
  char dir[4096];
  char path[4096];
  char path2[4096];
  snprintf(dir, sizeof(dir), "%s/parity", argv[1]);
  mkdir(argv[1], 0755);
  DFT_CHECK(mkdir(dir, 0755) == 0 || access(dir, F_OK) == 0);
  int dfd = open(dir, O_RDONLY | O_DIRECTORY);
  DFT_CHECK(dfd >= 0);

  DFT_CHECK(mkdirat(dfd, "sub", 0755) == 0);
  int fd = openat(dfd, "a.dat", O_CREAT | O_RDWR, 0644);
  DFT_CHECK(fd >= 0);
  DFT_CHECK(ftruncate64(fd, 8192) == 0);
  DFT_CHECK(fchmod(fd, 0600) == 0);
  DFT_CHECK(fchown(fd, (uid_t)-1, (gid_t)-1) == 0);
  DFT_CHECK(fcntl64(fd, F_GETFL) >= 0);

  void* map = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  DFT_CHECK(map != MAP_FAILED);
  ((char*)map)[0] = 'x';
  DFT_CHECK(madvise(map, 8192, MADV_NORMAL) == 0);
  DFT_CHECK(mprotect(map, 8192, PROT_READ | PROT_WRITE) == 0);
  DFT_CHECK(msync(map, 8192, MS_SYNC) == 0);
  mlock(map, 4096);
  munlock(map, 4096);
  mlockall(MCL_CURRENT);
  munlockall();
  DFT_CHECK(munmap(map, 8192) == 0);

  char* part = mmap(NULL, 8192, PROT_READ, MAP_PRIVATE, fd, 0);
  DFT_CHECK(part != MAP_FAILED);
  DFT_CHECK(munmap(part, 4096) == 0);
  DFT_CHECK(munmap(part + 4096, 4096) == 0);

  void* anon = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  DFT_CHECK(anon != MAP_FAILED);
  DFT_CHECK(munmap(anon, 4096) == 0);
  close(fd);

  int fd64 = openat64(dfd, "b.dat", O_CREAT | O_RDWR, 0644);
  DFT_CHECK(fd64 >= 0);
  close(fd64);
  snprintf(path, sizeof(path), "%s/b.dat", dir);
  DFT_CHECK(truncate64(path, 4096) == 0);
  DFT_CHECK(fchmodat(dfd, "b.dat", 0644, 0) == 0);
  DFT_CHECK(fchownat(dfd, "b.dat", (uid_t)-1, (gid_t)-1, 0) == 0);

  struct stat sb;
  struct stat64 sb64;
  DFT_CHECK(stat(path, &sb) == 0);
  DFT_CHECK(lstat(path, &sb) == 0);
  DFT_CHECK(stat64(path, &sb64) == 0);
  DFT_CHECK(fstatat(dfd, "b.dat", &sb, 0) == 0);
  DFT_CHECK(fstat(dfd, &sb) == 0);

  DFT_CHECK(renameat(dfd, "a.dat", dfd, "c.dat") == 0);

  snprintf(path, sizeof(path), "%s/f.fifo", dir);
  DFT_CHECK(mknod(path, S_IFIFO | 0600, 0) == 0);
  unlink(path);
  snprintf(path, sizeof(path), "%s/b.dat", dir);

  DIR* d = opendir(dir);
  DFT_CHECK(d != NULL);
  DFT_CHECK(readdir(d) != NULL);
  rewinddir(d);
  DFT_CHECK(readdir64(d) != NULL);
  DFT_CHECK(closedir(d) == 0);

  int pfd[2];
  DFT_CHECK(pipe(pfd) == 0);
  close(pfd[0]);
  close(pfd[1]);
  char cwd[4096];
  DFT_CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
  DFT_CHECK(sysconf(_SC_PAGESIZE) > 0);

  snprintf(path2, sizeof(path2), "%s/s.txt", dir);
  FILE* f = fopen(path2, "w+");
  DFT_CHECK(f != NULL);
  DFT_CHECK(fileno(f) >= 0);
  fputs("1 2\n", f);
  DFT_CHECK(fseeko(f, 0, SEEK_SET) == 0);
  DFT_CHECK(ftello(f) == 0);
  DFT_CHECK(fseeko64(f, 0, SEEK_SET) == 0);
  DFT_CHECK(ftello64(f) == 0);
  fpos64_t pos;
  DFT_CHECK(fgetpos64(f, &pos) == 0);
  DFT_CHECK(fsetpos64(f, &pos) == 0);
  int a = 0;
  int b = 0;
  DFT_CHECK(fscanf(f, "%d %d", &a, &b) == 2);
  fclose(f);

  int fd2 = open(path2, O_RDONLY);
  DFT_CHECK(fd2 >= 0);
  FILE* g = fdopen(fd2, "r");
  DFT_CHECK(g != NULL);
  fclose(g);

  FILE* t = tmpfile();
  DFT_CHECK(t != NULL);
  fclose(t);
  FILE* t64 = tmpfile64();
  DFT_CHECK(t64 != NULL);
  fclose(t64);

  DFT_CHECK(unlinkat(dfd, "b.dat", 0) == 0);
  DFT_CHECK(unlinkat(dfd, "c.dat", 0) == 0);
  DFT_CHECK(unlinkat(dfd, "s.txt", 0) == 0);
  DFT_CHECK(unlinkat(dfd, "sub", AT_REMOVEDIR) == 0);
  close(dfd);
  rmdir(dir);
  return 0;
}
