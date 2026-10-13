#define _GNU_SOURCE
#include <aio.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <wchar.h>

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      fprintf(stderr, "failed: %s (%d)\n", #cond, __LINE__); \
      return 1;                                              \
    }                                                        \
  } while (0)

static int count_entry(const char* path, const struct stat* sb, int type,
                       struct FTW* ftw) {
  (void)path;
  (void)sb;
  (void)type;
  (void)ftw;
  return 0;
}

int main(int argc, char* argv[]) {
  CHECK(argc > 1);
  char dir[4096];
  char path[4096];
  char path2[4096];
  char buf[64];
  memset(buf, 'x', sizeof(buf));
  snprintf(dir, sizeof(dir), "%s/extra_io", argv[1]);
  mkdir(argv[1], 0755);
  CHECK(mkdir(dir, 0755) == 0 || access(dir, F_OK) == 0);
  int dfd = open(dir, O_RDONLY | O_DIRECTORY);
  CHECK(dfd >= 0);

  snprintf(path, sizeof(path), "%s/a.dat", dir);
  int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
  CHECK(fd >= 0);
  struct iovec iov = {buf, sizeof(buf)};
  CHECK(pwritev2(fd, &iov, 1, 0, 0) == (ssize_t)sizeof(buf));
  CHECK(preadv2(fd, &iov, 1, 0, 0) == (ssize_t)sizeof(buf));
  CHECK(fallocate(fd, 0, 0, 4096) == 0 || errno == EOPNOTSUPP);
  readahead(fd, 0, 4096);
  CHECK(sync_file_range(fd, 0, 0, SYNC_FILE_RANGE_WRITE) == 0);
  CHECK(syncfs(fd) == 0);
  sync();
  CHECK(lockf(fd, F_TLOCK, 0) == 0);
  int avail = 0;
  CHECK(ioctl(fd, FIONREAD, &avail) == 0);
  struct statfs sfs;
  CHECK(fstatfs(fd, &sfs) == 0);
  CHECK(statfs(path, &sfs) == 0);
  struct statx stx;
  CHECK(statx(AT_FDCWD, path, 0, STATX_SIZE, &stx) == 0);
  CHECK(euidaccess(path, R_OK) == 0);
  struct timeval tv[2] = {{0, 0}, {0, 0}};
  struct timespec ts[2] = {{0, UTIME_NOW}, {0, UTIME_NOW}};
  CHECK(utimes(path, tv) == 0);
  CHECK(lutimes(path, tv) == 0);
  CHECK(futimes(fd, tv) == 0);
  CHECK(futimesat(dfd, "a.dat", tv) == 0);
  CHECK(utimensat(AT_FDCWD, path, ts, 0) == 0);
  CHECK(futimens(fd, ts) == 0);
  lchmod(path, 0644);
  fsetxattr(fd, "user.dft", "1", 1, 0);
  fgetxattr(fd, "user.dft", buf, sizeof(buf));
  setxattr(path, "user.dft", "1", 1, 0);
  getxattr(path, "user.dft", buf, sizeof(buf));
  lsetxattr(path, "user.dft", "1", 1, 0);
  lgetxattr(path, "user.dft", buf, sizeof(buf));
  listxattr(path, buf, sizeof(buf));
  llistxattr(path, buf, sizeof(buf));
  flistxattr(fd, buf, sizeof(buf));
  removexattr(path, "user.dft");
  lremovexattr(path, "user.dft");
  fremovexattr(fd, "user.dft");

  int pfd[2];
  CHECK(pipe2(pfd, O_CLOEXEC) == 0);
  loff_t off = 0;
  CHECK(splice(fd, &off, pfd[1], NULL, 8, 0) == 8);
  close(pfd[0]);
  close(pfd[1]);

  struct aiocb cb;
  memset(&cb, 0, sizeof(cb));
  cb.aio_fildes = fd;
  cb.aio_buf = buf;
  cb.aio_nbytes = 8;
  CHECK(aio_write(&cb) == 0);
  const struct aiocb* list[1] = {&cb};
  while (aio_error(&cb) == EINPROGRESS) aio_suspend(list, 1, NULL);
  CHECK(aio_return(&cb) == 8);

  snprintf(path2, sizeof(path2), "%s/b.dat", dir);
  CHECK(renameat2(AT_FDCWD, path, AT_FDCWD, path2, 0) == 0);
  CHECK(mkfifoat(dfd, "fifo", 0644) == 0 || errno == EEXIST);

  snprintf(path, sizeof(path), "%s/tmpXXXXXX", dir);
  int tfd = mkstemp(path);
  CHECK(tfd >= 0);
  CHECK(write(tfd, "t", 1) == 1);
  CHECK(close(tfd) == 0);
  snprintf(path, sizeof(path), "%s/tmpXXXXXX.s", dir);
  tfd = mkostemps(path, 2, O_CLOEXEC);
  CHECK(tfd >= 0);
  CHECK(close(tfd) == 0);
  snprintf(path, sizeof(path), "%s/dXXXXXX", dir);
  CHECK(mkdtemp(path) != NULL);

  int dfd2 = open(dir, O_RDONLY | O_DIRECTORY);
  CHECK(dfd2 >= 0);
  DIR* d = fdopendir(dfd2);
  CHECK(d != NULL);
  long loc = telldir(d);
  struct dirent entry;
  struct dirent* result;
  CHECK(readdir_r(d, &entry, &result) == 0);
  seekdir(d, loc);
  CHECK(closedir(d) == 0);
  struct dirent** names;
  int n = scandir(dir, &names, NULL, NULL);
  CHECK(n > 0);
  for (int i = 0; i < n; i++) free(names[i]);
  free(names);
  n = scandirat(dfd, ".", &names, NULL, NULL);
  CHECK(n > 0);
  for (int i = 0; i < n; i++) free(names[i]);
  free(names);
  struct dirent64** names64;
  n = scandirat64(dfd, ".", &names64, NULL, NULL);
  CHECK(n > 0);
  for (int i = 0; i < n; i++) free(names64[i]);
  free(names64);
  CHECK(nftw(dir, count_entry, 4, FTW_PHYS) == 0);

  void* map = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(map != MAP_FAILED);
  map = mremap(map, 4096, 8192, MREMAP_MAYMOVE);
  CHECK(map != MAP_FAILED);
  munmap(map, 8192);

  snprintf(path, sizeof(path), "%s/w.txt", dir);
  FILE* f = fopen(path, "w");
  CHECK(f != NULL);
  CHECK(fputws(L"wide\n", f) >= 0);
  CHECK(fputwc(L'x', f) == L'x');
  CHECK(fwprintf(f, L" %d\n", 7) > 0);
  CHECK(fflush_unlocked(f) == 0);
  CHECK(fclose(f) == 0);
  f = fopen(path, "r");
  CHECK(f != NULL);
  wchar_t wbuf[16];
  CHECK(fgetws(wbuf, 16, f) != NULL);
  wint_t wc = fgetwc(f);
  CHECK(wc == L'x');
  CHECK(ungetwc(wc, f) == wc);
  CHECK(fgetwc(f) == L'x');
  int value = 0;
  CHECK(fwscanf(f, L"%d", &value) == 1 && value == 7);
  CHECK(feof_unlocked(f) == 0);
  CHECK(ferror_unlocked(f) == 0);
  CHECK(fileno_unlocked(f) >= 0);
  CHECK(fcloseall() == 0);

  CHECK(fchdir(dfd) == 0);
  CHECK(close(dfd) == 0);
  CHECK(close(fd) == 0);
  return 0;
}
