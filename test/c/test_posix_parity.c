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
  char path2[4096];
  snprintf(dir, sizeof(dir), "%s/parity", argv[1]);
  mkdir(argv[1], 0755);
  CHECK(mkdir(dir, 0755) == 0 || access(dir, F_OK) == 0);
  int dfd = open(dir, O_RDONLY | O_DIRECTORY);
  CHECK(dfd >= 0);

  CHECK(mkdirat(dfd, "sub", 0755) == 0);
  int fd = openat(dfd, "a.dat", O_CREAT | O_RDWR, 0644);
  CHECK(fd >= 0);
  CHECK(ftruncate64(fd, 8192) == 0);
  CHECK(fchmod(fd, 0600) == 0);
  CHECK(fchown(fd, (uid_t)-1, (gid_t)-1) == 0);
  CHECK(fcntl64(fd, F_GETFL) >= 0);

  void* map = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  CHECK(map != MAP_FAILED);
  ((char*)map)[0] = 'x';
  CHECK(madvise(map, 8192, MADV_NORMAL) == 0);
  CHECK(mprotect(map, 8192, PROT_READ | PROT_WRITE) == 0);
  CHECK(msync(map, 8192, MS_SYNC) == 0);
  mlock(map, 4096);
  munlock(map, 4096);
  mlockall(MCL_CURRENT);
  munlockall();
  CHECK(munmap(map, 8192) == 0);

  char* part = mmap(NULL, 8192, PROT_READ, MAP_PRIVATE, fd, 0);
  CHECK(part != MAP_FAILED);
  CHECK(munmap(part, 4096) == 0);
  CHECK(munmap(part + 4096, 4096) == 0);

  void* anon = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(anon != MAP_FAILED);
  CHECK(munmap(anon, 4096) == 0);
  close(fd);

  int fd64 = openat64(dfd, "b.dat", O_CREAT | O_RDWR, 0644);
  CHECK(fd64 >= 0);
  close(fd64);
  snprintf(path, sizeof(path), "%s/b.dat", dir);
  CHECK(truncate64(path, 4096) == 0);
  CHECK(fchmodat(dfd, "b.dat", 0644, 0) == 0);
  CHECK(fchownat(dfd, "b.dat", (uid_t)-1, (gid_t)-1, 0) == 0);

  struct stat sb;
  struct stat64 sb64;
  CHECK(stat(path, &sb) == 0);
  CHECK(lstat(path, &sb) == 0);
  CHECK(stat64(path, &sb64) == 0);
  CHECK(fstatat(dfd, "b.dat", &sb, 0) == 0);
  CHECK(fstat(dfd, &sb) == 0);

  CHECK(renameat(dfd, "a.dat", dfd, "c.dat") == 0);

  snprintf(path, sizeof(path), "%s/f.fifo", dir);
  CHECK(mknod(path, S_IFIFO | 0600, 0) == 0);
  unlink(path);
  snprintf(path, sizeof(path), "%s/b.dat", dir);

  DIR* d = opendir(dir);
  CHECK(d != NULL);
  CHECK(readdir(d) != NULL);
  rewinddir(d);
  CHECK(readdir64(d) != NULL);
  CHECK(closedir(d) == 0);

  int pfd[2];
  CHECK(pipe(pfd) == 0);
  close(pfd[0]);
  close(pfd[1]);
  char cwd[4096];
  CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
  CHECK(sysconf(_SC_PAGESIZE) > 0);

  snprintf(path2, sizeof(path2), "%s/s.txt", dir);
  FILE* f = fopen(path2, "w+");
  CHECK(f != NULL);
  CHECK(fileno(f) >= 0);
  fputs("1 2\n", f);
  CHECK(fseeko(f, 0, SEEK_SET) == 0);
  CHECK(ftello(f) == 0);
  CHECK(fseeko64(f, 0, SEEK_SET) == 0);
  CHECK(ftello64(f) == 0);
  fpos64_t pos;
  CHECK(fgetpos64(f, &pos) == 0);
  CHECK(fsetpos64(f, &pos) == 0);
  int a = 0;
  int b = 0;
  CHECK(fscanf(f, "%d %d", &a, &b) == 2);
  fclose(f);

  int fd2 = open(path2, O_RDONLY);
  CHECK(fd2 >= 0);
  FILE* g = fdopen(fd2, "r");
  CHECK(g != NULL);
  fclose(g);

  FILE* t = tmpfile();
  CHECK(t != NULL);
  fclose(t);
  FILE* t64 = tmpfile64();
  CHECK(t64 != NULL);
  fclose(t64);

  CHECK(unlinkat(dfd, "b.dat", 0) == 0);
  CHECK(unlinkat(dfd, "c.dat", 0) == 0);
  CHECK(unlinkat(dfd, "s.txt", 0) == 0);
  CHECK(unlinkat(dfd, "sub", AT_REMOVEDIR) == 0);
  close(dfd);
  rmdir(dir);
  return 0;
}
