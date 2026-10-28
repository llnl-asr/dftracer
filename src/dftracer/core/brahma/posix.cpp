//
// Created by hariharan on 8/16/22.
//
#include <cpp-logger/logger.h>
#include <dftracer/core/brahma/posix.h>
#include <dftracer/core/common/dftracer_main.h>

#include <vector>

static ConstEventNameType CATEGORY = "POSIX";
static TraceEventType TRACE_TYPE = TraceEventType::TRACE_TYPE_LIBC_IO;

std::shared_ptr<brahma::POSIXDFTracer> brahma::POSIXDFTracer::instance =
    nullptr;
bool brahma::POSIXDFTracer::stop_trace = false;
int brahma::POSIXDFTracer::open(const char* pathname, int flags, ...) {
  BRAHMA_MAP_OR_FAIL(open);
  DFT_LOGGER_START(pathname);
  int ret = -1;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    int mode = va_arg(args, int);
    va_end(args);
    DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
    ret = __real_open(pathname, flags, mode);
  } else {
    ret = __real_open(pathname, flags);
  }
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::close(int fd) {
  BRAHMA_MAP_OR_FAIL(close);
  DFT_LOGGER_START(fd);
  // Untrack before the real close: once it returns, another thread can get
  // the same number from open() and its entry must not be cleared.
  if (trace) this->remove_trace(fd);
  int ret = __real_close(fd);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::write(int fd, const void* buf, size_t count) {
  BRAHMA_MAP_OR_FAIL(write);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  ssize_t ret = __real_write(fd, buf, count);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::read(int fd, void* buf, size_t count) {
  BRAHMA_MAP_OR_FAIL(read);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  ssize_t ret = __real_read(fd, buf, count);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

off_t brahma::POSIXDFTracer::lseek(int fd, off_t offset, int whence) {
  BRAHMA_MAP_OR_FAIL(lseek);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(whence, MetadataType::MT_VALUE);
  ssize_t ret = __real_lseek(fd, offset, whence);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::creat(const char* path, mode_t mode) {
  BRAHMA_MAP_OR_FAIL(creat);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_creat(path, mode);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::creat64(const char* path, mode_t mode) {
  BRAHMA_MAP_OR_FAIL(creat64);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_creat64(path, mode);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::open64(const char* path, int flags, ...) {
  BRAHMA_MAP_OR_FAIL(open64);
  DFT_LOGGER_START(path);
  int ret = -1;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    int mode = va_arg(args, int);
    va_end(args);
    DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
    ret = __real_open64(path, flags, mode);
  } else {
    ret = __real_open64(path, flags);
  }
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

off64_t brahma::POSIXDFTracer::lseek64(int fd, off64_t offset, int whence) {
  BRAHMA_MAP_OR_FAIL(lseek64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(whence, MetadataType::MT_VALUE);
  off64_t ret = __real_lseek64(fd, offset, whence);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::pread(int fd, void* buf, size_t count,
                                     off_t offset) {
  BRAHMA_MAP_OR_FAIL(pread);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real_pread(fd, buf, count, offset);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::pread64(int fd, void* buf, size_t count,
                                       off64_t offset) {
  BRAHMA_MAP_OR_FAIL(pread64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real_pread64(fd, buf, count, offset);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::pwrite(int fd, const void* buf, size_t count,
                                      off64_t offset) {
  BRAHMA_MAP_OR_FAIL(pwrite);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real_pwrite(fd, buf, count, offset);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::pwrite64(int fd, const void* buf, size_t count,
                                        off64_t offset) {
  BRAHMA_MAP_OR_FAIL(pwrite64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real_pwrite64(fd, buf, count, offset);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fsync(int fd) {
  BRAHMA_MAP_OR_FAIL(fsync);
  DFT_LOGGER_START(fd);
  int ret = __real_fsync(fd);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fdatasync(int fd) {
  BRAHMA_MAP_OR_FAIL(fdatasync);
  DFT_LOGGER_START(fd);
  int ret = __real_fdatasync(fd);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::openat(int dirfd, const char* pathname, int flags,
                                  ...) {
  BRAHMA_MAP_OR_FAIL(openat);
  DFT_LOGGER_START(dirfd);
  DFT_LOGGER_UPDATE(dirfd);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_HASH(pathname);
  int ret = -1;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    int mode = va_arg(args, int);
    va_end(args);
    DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
    ret = __real_openat(dirfd, pathname, flags, mode);
  } else {
    ret = __real_openat(dirfd, pathname, flags);
  }
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

void* brahma::POSIXDFTracer::mmap(void* addr, size_t length, int prot,
                                  int flags, int fd, off_t offset) {
  BRAHMA_MAP_OR_FAIL(mmap);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(length, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  void* ret = __real_mmap(addr, length, prot, flags, fd, offset);
  size_t address = reinterpret_cast<uintptr_t>(ret);
  DFT_LOGGER_UPDATE_TYPE(address, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

void* brahma::POSIXDFTracer::mmap64(void* addr, size_t length, int prot,
                                    int flags, int fd, off64_t offset) {
  BRAHMA_MAP_OR_FAIL(mmap64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(length, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  void* ret = __real_mmap64(addr, length, prot, flags, fd, offset);
  size_t address = reinterpret_cast<uintptr_t>(ret);
  DFT_LOGGER_UPDATE_TYPE(address, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__xstat(int vers, const char* path,
                                   struct stat* buf) {
  BRAHMA_MAP_OR_FAIL(__xstat);
  DFT_LOGGER_START(path);
  int ret = __real___xstat(vers, path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__xstat64(int vers, const char* path,
                                     struct stat64* buf) {
  BRAHMA_MAP_OR_FAIL(__xstat64);
  DFT_LOGGER_START(path);
  int ret = __real___xstat64(vers, path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__lxstat(int vers, const char* path,
                                    struct stat* buf) {
  BRAHMA_MAP_OR_FAIL(__lxstat);
  DFT_LOGGER_START(path);
  int ret = __real___lxstat(vers, path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__lxstat64(int vers, const char* path,
                                      struct stat64* buf) {
  BRAHMA_MAP_OR_FAIL(__lxstat64);
  DFT_LOGGER_START(path);
  int ret = __real___lxstat64(vers, path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__fxstat(int vers, int fd, struct stat* buf) {
  BRAHMA_MAP_OR_FAIL(__fxstat);
  DFT_LOGGER_START(fd);
  int ret = __real___fxstat(vers, fd, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__fxstat64(int vers, int fd, struct stat64* buf) {
  BRAHMA_MAP_OR_FAIL(__fxstat64);
  DFT_LOGGER_START(fd);
  int ret = __real___fxstat64(vers, fd, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mkdir(const char* pathname, mode_t mode) {
  BRAHMA_MAP_OR_FAIL(mkdir);
  DFT_LOGGER_START(pathname);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_mkdir(pathname, mode);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::rmdir(const char* pathname) {
  BRAHMA_MAP_OR_FAIL(rmdir);
  DFT_LOGGER_START(pathname);
  int ret = __real_rmdir(pathname);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::chdir(const char* path) {
  BRAHMA_MAP_OR_FAIL(chdir);
  DFT_LOGGER_START(path);
  int ret = __real_chdir(path);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::link(const char* oldpath, const char* newpath) {
  BRAHMA_MAP_OR_FAIL(link);
  DFT_LOGGER_START(oldpath);
  DFT_LOGGER_UPDATE_HASH(newpath);
  int ret = __real_link(oldpath, newpath);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::linkat(int fd1, const char* path1, int fd2,
                                  const char* path2, int flag) {
  BRAHMA_MAP_OR_FAIL(linkat);
  DFT_LOGGER_START(fd1);
  DFT_LOGGER_UPDATE(fd1);
  DFT_LOGGER_UPDATE(fd2);
  DFT_LOGGER_UPDATE_HASH(path2);
  DFT_LOGGER_UPDATE_TYPE(flag, MetadataType::MT_VALUE);
  int ret = __real_linkat(fd1, path1, fd2, path2, flag);
  DFT_LOGGER_UPDATE(ret);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::unlink(const char* pathname) {
  BRAHMA_MAP_OR_FAIL(unlink);
  DFT_LOGGER_START(pathname);
  int ret = __real_unlink(pathname);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::symlink(const char* path1, const char* path2) {
  BRAHMA_MAP_OR_FAIL(symlink);
  DFT_LOGGER_START(path1);
  DFT_LOGGER_UPDATE_HASH(path2);
  int ret = __real_symlink(path1, path2);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::symlinkat(const char* path1, int fd,
                                     const char* path2) {
  BRAHMA_MAP_OR_FAIL(symlinkat);
  DFT_LOGGER_START(path1);
  DFT_LOGGER_UPDATE(fd);
  DFT_LOGGER_UPDATE_HASH(path2);
  int ret = __real_symlinkat(path1, fd, path2);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::readlink(const char* path, char* buf,
                                        size_t bufsize) {
  BRAHMA_MAP_OR_FAIL(readlink);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(bufsize, MetadataType::MT_VALUE);
  ssize_t ret = __real_readlink(path, buf, bufsize);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::readlinkat(int fd, const char* path, char* buf,
                                          size_t bufsize) {
  BRAHMA_MAP_OR_FAIL(readlinkat);
  ssize_t ret;
  if (fd != AT_FDCWD) {
    DFT_LOGGER_START(fd);
    DFT_LOGGER_UPDATE(path);
    DFT_LOGGER_UPDATE_TYPE(bufsize, MetadataType::MT_VALUE);
    ret = __real_readlinkat(fd, path, buf, bufsize);
    DFT_LOGGER_END();
  } else {
    DFT_LOGGER_START(path);
    DFT_LOGGER_UPDATE_TYPE(bufsize, MetadataType::MT_VALUE);
    ret = __real_readlinkat(fd, path, buf, bufsize);
    DFT_LOGGER_END();
  }
  return ret;
}

int brahma::POSIXDFTracer::rename(const char* oldpath, const char* newpath) {
  BRAHMA_MAP_OR_FAIL(rename);
  DFT_LOGGER_START(oldpath);
  DFT_LOGGER_UPDATE_HASH(newpath);
  int ret = __real_rename(oldpath, newpath);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::chmod(const char* path, mode_t mode) {
  BRAHMA_MAP_OR_FAIL(chmod);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_chmod(path, mode);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::chown(const char* path, uid_t owner, gid_t group) {
  BRAHMA_MAP_OR_FAIL(chown);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(owner, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(group, MetadataType::MT_VALUE);
  int ret = __real_chown(path, owner, group);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lchown(const char* path, uid_t owner, gid_t group) {
  BRAHMA_MAP_OR_FAIL(lchown);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(owner, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(group, MetadataType::MT_VALUE);
  int ret = __real_lchown(path, owner, group);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::utime(const char* filename, const utimbuf* buf) {
  BRAHMA_MAP_OR_FAIL(utime);
  DFT_LOGGER_START(filename);
  int ret = __real_utime(filename, buf);
  DFT_LOGGER_END();
  return ret;
}

DIR* brahma::POSIXDFTracer::opendir(const char* name) {
  BRAHMA_MAP_OR_FAIL(opendir);
  DFT_LOGGER_START(name);
  DIR* ret = __real_opendir(name);
  DFT_LOGGER_END();
  if (trace && ret != nullptr) this->trace(this->dir_fd(ret), fhash);
  return ret;
}

int brahma::POSIXDFTracer::fcntl(int fd, int cmd, ...) {
  BRAHMA_MAP_OR_FAIL(fcntl);
  if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC || cmd == F_SETFD ||
      cmd == F_SETFL || cmd == F_SETOWN) {  // arg: int
    va_list arg;
    va_start(arg, cmd);
    int val = va_arg(arg, int);
    va_end(arg);
    DFT_LOGGER_START(fd);
    DFT_LOGGER_UPDATE_TYPE(cmd, MetadataType::MT_VALUE);
    int ret = __real_fcntl(fd, cmd, val);
    DFT_LOGGER_END();
    if (trace && (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC))
      this->trace(ret, fhash);
    return ret;
  } else if (cmd == F_GETFD || cmd == F_GETFL || cmd == F_GETOWN) {
    DFT_LOGGER_START(fd);
    DFT_LOGGER_UPDATE_TYPE(cmd, MetadataType::MT_VALUE);
    int ret = __real_fcntl(fd, cmd);
    DFT_LOGGER_END();
    return ret;
  } else if (cmd == F_SETLK || cmd == F_SETLKW || cmd == F_GETLK) {
    va_list arg;
    va_start(arg, cmd);
    struct flock* lk = va_arg(arg, struct flock*);
    va_end(arg);
    DFT_LOGGER_START(fd);
    DFT_LOGGER_UPDATE_TYPE(cmd, MetadataType::MT_VALUE);
    int ret = __real_fcntl(fd, cmd, lk);
    DFT_LOGGER_END();
    return ret;
  } else {  // assume arg: void, cmd==F_GETOWN_EX || cmd==F_SETOWN_EX
            // ||cmd==F_GETSIG || cmd==F_SETSIG)
    DFT_LOGGER_START(fd);
    DFT_LOGGER_UPDATE_TYPE(cmd, MetadataType::MT_VALUE);
    int ret = __real_fcntl(fd, cmd);
    DFT_LOGGER_END();
    return ret;
  }
}

int brahma::POSIXDFTracer::dup(int oldfd) {
  BRAHMA_MAP_OR_FAIL(dup);
  DFT_LOGGER_START(oldfd);
  int ret = __real_dup(oldfd);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

// newfd now refers to oldfd's file, or to an untraced one when fhash is
// NO_HASH_DEFAULT, so its entry is overwritten either way.
int brahma::POSIXDFTracer::dup2(int oldfd, int newfd) {
  BRAHMA_MAP_OR_FAIL(dup2);
  DFT_LOGGER_START(oldfd);
  int ret = __real_dup2(oldfd, newfd);
  DFT_LOGGER_END();
  if (ret >= 0 && ret != oldfd) this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::dup3(int oldfd, int newfd, int flags) {
  BRAHMA_MAP_OR_FAIL(dup3);
  DFT_LOGGER_START(oldfd);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_dup3(oldfd, newfd, flags);
  DFT_LOGGER_END();
  if (ret >= 0) this->trace(ret, fhash);
  return ret;
}

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1U << 2)
#endif

int brahma::POSIXDFTracer::close_range(unsigned int first, unsigned int last,
                                       int flags) {
  BRAHMA_MAP_OR_FAIL(close_range);
  if (in_vfork_child() || (flags & CLOSE_RANGE_CLOEXEC) ||
      !remove_trace_range(first, last))
    return __real_close_range(first, last, flags);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_TYPE(first, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(last, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_close_range(first, last, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

void brahma::POSIXDFTracer::closefrom(int lowfd) {
  BRAHMA_MAP_OR_FAIL(closefrom);
  if (lowfd < 0 || in_vfork_child() ||
      !remove_trace_range(lowfd, static_cast<size_t>(-1))) {
    __real_closefrom(lowfd);
    return;
  }
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_TYPE(lowfd, MetadataType::MT_VALUE);
  __real_closefrom(lowfd);
  DFT_LOGGER_END();
}

int brahma::POSIXDFTracer::mkfifo(const char* pathname, mode_t mode) {
  BRAHMA_MAP_OR_FAIL(mkfifo);
  DFT_LOGGER_START(pathname);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_mkfifo(pathname, mode);
  DFT_LOGGER_END();
  return ret;
}

mode_t brahma::POSIXDFTracer::umask(mode_t mask) {
  BRAHMA_MAP_OR_FAIL(umask);
  DFT_LOGGER_START(mask);
  mode_t ret = __real_umask(mask);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::access(const char* path, int amode) {
  BRAHMA_MAP_OR_FAIL(access);
  DFT_LOGGER_START(path);
  int ret = __real_access(path, amode);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::faccessat(int fd, const char* path, int amode,
                                     int flag) {
  BRAHMA_MAP_OR_FAIL(faccessat);
  DFT_LOGGER_START(fd);
  int ret = __real_faccessat(fd, path, amode, flag);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::remove(const char* pathname) {
  BRAHMA_MAP_OR_FAIL(remove);
  DFT_LOGGER_START(pathname);
  int ret = __real_remove(pathname);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::truncate(const char* pathname, off_t length) {
  BRAHMA_MAP_OR_FAIL(truncate);
  DFT_LOGGER_START(pathname);
  DFT_LOGGER_UPDATE_TYPE(length, MetadataType::MT_VALUE);
  int ret = __real_truncate(pathname, length);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::ftruncate(int fd, off_t length) {
  BRAHMA_MAP_OR_FAIL(ftruncate);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(length, MetadataType::MT_VALUE);
  int ret = __real_ftruncate(fd, length);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::execl(const char* pathname, const char* arg, ...) {
  // NOTE: a va_list cannot be forwarded into another variadic call (this used
  // to pass `args` straight into __real_execl(pathname, arg, args) as if it
  // were a const char* — undefined behavior: on x86_64 glibc a va_list is an
  // opaque pointer to saved register/stack state, and the real execl() would
  // walk its own va_arg list starting from that garbage value looking for a
  // NULL terminator, eventually dereferencing an invalid address and
  // returning EFAULT ("execl failed: Bad address"). Rebuild a real
  // NULL-terminated argv[] by walking the va_list ourselves and dispatch
  // through the array-based real function instead, exactly as execv already
  // does below.
  BRAHMA_MAP_OR_FAIL(execl);
  BRAHMA_MAP_OR_FAIL(execv);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_HASH(pathname);
  DFT_LOGGER_UPDATE_HASH(arg);

  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(arg));
  va_list args;
  va_start(args, arg);
  char* next;
  while ((next = va_arg(args, char*)) != nullptr) {
    argv.push_back(next);
  }
  va_end(args);
  argv.push_back(nullptr);

  // exec replaces this image and does not return when it succeeds: record
  // the call and write out everything buffered first, or it is lost. A
  // flush, not a finalize: if exec fails, tracing goes on. Not in a vfork
  // child, which shares the parent's buffers while the parent's other
  // threads may still be writing to them.
  DFT_LOGGER_END();
  if (!in_vfork_child()) dft_flush();
  return __real_execv(pathname, argv.data());
}

int brahma::POSIXDFTracer::execlp(const char* pathname, const char* arg, ...) {
  // Same va_list-forwarding bug as execl() above — rebuild argv[] and
  // dispatch through execvp() instead of re-splicing the va_list into
  // another variadic call.
  BRAHMA_MAP_OR_FAIL(execlp);
  BRAHMA_MAP_OR_FAIL(execvp);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_HASH(pathname);
  DFT_LOGGER_UPDATE_HASH(arg);

  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(arg));
  va_list args;
  va_start(args, arg);
  char* next;
  while ((next = va_arg(args, char*)) != nullptr) {
    argv.push_back(next);
  }
  va_end(args);
  argv.push_back(nullptr);

  // exec replaces this image and does not return when it succeeds: record
  // the call and write out everything buffered first, or it is lost. A
  // flush, not a finalize: if exec fails, tracing goes on. Not in a vfork
  // child, which shares the parent's buffers while the parent's other
  // threads may still be writing to them.
  DFT_LOGGER_END();
  if (!in_vfork_child()) dft_flush();
  return __real_execvp(pathname, argv.data());
}

int brahma::POSIXDFTracer::execv(const char* pathname, char* const argv[]) {
  BRAHMA_MAP_OR_FAIL(execv);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_HASH(pathname);
  const char* val = argv[0];
  int i = 0;
  while (val != NULL) {
    if (i == 0) {
      const char* arg0 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg0);
    } else if (i == 1) {
      const char* arg1 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg1);
    } else if (i == 2) {
      const char* arg2 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg2);
    } else if (i == 3) {
      const char* arg3 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg3);
    } else if (i == 4) {
      const char* arg4 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg4);
    } else if (i == 5) {
      const char* arg5 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg5);
    } else {
      break;
    }
    i++;
    val = argv[i];
  }

  // exec replaces this image and does not return when it succeeds: record
  // the call and write out everything buffered first, or it is lost. A
  // flush, not a finalize: if exec fails, tracing goes on. Not in a vfork
  // child, which shares the parent's buffers while the parent's other
  // threads may still be writing to them.
  DFT_LOGGER_END();
  if (!in_vfork_child()) dft_flush();
  return __real_execv(pathname, argv);
}

int brahma::POSIXDFTracer::execvp(const char* pathname, char* const argv[]) {
  BRAHMA_MAP_OR_FAIL(execvp);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_HASH(pathname);
  const char* val = argv[0];
  int i = 0;
  while (val != NULL) {
    if (i == 0) {
      const char* arg0 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg0);
    } else if (i == 1) {
      const char* arg1 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg1);
    } else if (i == 2) {
      const char* arg2 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg2);
    } else if (i == 3) {
      const char* arg3 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg3);
    } else if (i == 4) {
      const char* arg4 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg4);
    } else if (i == 5) {
      const char* arg5 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg5);
    } else {
      break;
    }
    i++;
    val = argv[i];
  }
  // exec replaces this image and does not return when it succeeds: record
  // the call and write out everything buffered first, or it is lost. A
  // flush, not a finalize: if exec fails, tracing goes on. Not in a vfork
  // child, which shares the parent's buffers while the parent's other
  // threads may still be writing to them.
  DFT_LOGGER_END();
  if (!in_vfork_child()) dft_flush();
  return __real_execvp(pathname, argv);
}

int brahma::POSIXDFTracer::execvpe(const char* pathname, char* const argv[],
                                   char* const envp[]) {
  BRAHMA_MAP_OR_FAIL(execvpe);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_HASH(pathname);
  const char* val = argv[0];
  int i = 0;
  while (val != NULL) {
    if (i == 0) {
      const char* arg0 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg0);
    } else if (i == 1) {
      const char* arg1 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg1);
    } else if (i == 2) {
      const char* arg2 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg2);
    } else if (i == 3) {
      const char* arg3 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg3);
    } else if (i == 4) {
      const char* arg4 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg4);
    } else if (i == 5) {
      const char* arg5 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg5);
    } else {
      break;
    }
    i++;
    val = argv[i];
  }
  // exec replaces this image and does not return when it succeeds: record
  // the call and write out everything buffered first, or it is lost. A
  // flush, not a finalize: if exec fails, tracing goes on. Not in a vfork
  // child, which shares the parent's buffers while the parent's other
  // threads may still be writing to them.
  DFT_LOGGER_END();
  if (!in_vfork_child()) dft_flush();
  return __real_execvpe(pathname, argv, envp);
}

int brahma::POSIXDFTracer::fork() {
  BRAHMA_MAP_OR_FAIL(fork);
  int ret = __real_fork();
  if (ret == 0) {
    // zero comes for forked childs. This child has not called MPI_Init
    // itself, so tracing code must not touch MPI here (see
    // dftracer_mpi_fork_guard) until/unless it does.
    dftracer_mpi_fork_guard().store(true);
    auto main = dftracer::Singleton<dftracer::DFTracerCore>::get_instance(
        ProfilerStage::PROFILER_INIT, ProfileType::PROFILER_PRELOAD);
    main->reinitialize();
  } else {
    DFT_LOGGER_START_ALWAYS();
    DFT_LOGGER_UPDATE(ret);
    DFT_LOGGER_END();
  }
  return ret;
}

void brahma::POSIXDFTracer::exit(int status) {
  BRAHMA_MAP_OR_FAIL(exit);
  // Finalizing in a vfork child would stop tracing in the parent.
  if (in_vfork_child()) __real_exit(status);
  DFTRACER_LOG_INFO("Calling finalize from exit");
  dft_finalize(true);
  __real_exit(status);
}

void brahma::POSIXDFTracer::_exit(int status) {
  BRAHMA_MAP_OR_FAIL(_exit);
  // Finalizing in a vfork child would stop tracing in the parent.
  if (in_vfork_child()) __real__exit(status);
  DFTRACER_LOG_INFO("Calling finalize from _exit");
  dft_finalize(true);
  __real__exit(status);
}

// --- New overrides mirroring brahma's expanded POSIX interception surface ---

ssize_t brahma::POSIXDFTracer::readv(int fd, const struct iovec* iov,
                                     int iovcnt) {
  BRAHMA_MAP_OR_FAIL(readv);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  ssize_t ret = __real_readv(fd, iov, iovcnt);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::writev(int fd, const struct iovec* iov,
                                      int iovcnt) {
  BRAHMA_MAP_OR_FAIL(writev);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  ssize_t ret = __real_writev(fd, iov, iovcnt);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::preadv(int fd, const struct iovec* iov,
                                      int iovcnt, off_t offset) {
  BRAHMA_MAP_OR_FAIL(preadv);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real_preadv(fd, iov, iovcnt, offset);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::preadv64(int fd, const struct iovec* iov,
                                        int iovcnt, off64_t offset) {
  BRAHMA_MAP_OR_FAIL(preadv64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real_preadv64(fd, iov, iovcnt, offset);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::pwritev(int fd, const struct iovec* iov,
                                       int iovcnt, off_t offset) {
  BRAHMA_MAP_OR_FAIL(pwritev);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real_pwritev(fd, iov, iovcnt, offset);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::pwritev64(int fd, const struct iovec* iov,
                                         int iovcnt, off64_t offset) {
  BRAHMA_MAP_OR_FAIL(pwritev64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real_pwritev64(fd, iov, iovcnt, offset);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::statvfs(const char* path, struct statvfs* buf) {
  BRAHMA_MAP_OR_FAIL(statvfs);
  DFT_LOGGER_START(path);
  int ret = __real_statvfs(path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::statvfs64(const char* path, struct statvfs64* buf) {
  BRAHMA_MAP_OR_FAIL(statvfs64);
  DFT_LOGGER_START(path);
  int ret = __real_statvfs64(path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fstatvfs(int fd, struct statvfs* buf) {
  BRAHMA_MAP_OR_FAIL(fstatvfs);
  DFT_LOGGER_START(fd);
  int ret = __real_fstatvfs(fd, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fstatvfs64(int fd, struct statvfs64* buf) {
  BRAHMA_MAP_OR_FAIL(fstatvfs64);
  DFT_LOGGER_START(fd);
  int ret = __real_fstatvfs64(fd, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::flock(int fd, int operation) {
  BRAHMA_MAP_OR_FAIL(flock);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(operation, MetadataType::MT_VALUE);
  int ret = __real_flock(fd, operation);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::execve(const char* pathname, char* const argv[],
                                  char* const envp[]) {
  BRAHMA_MAP_OR_FAIL(execve);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_HASH(pathname);
  const char* val = argv[0];
  int i = 0;
  while (val != NULL) {
    if (i == 0) {
      const char* arg0 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg0);
    } else if (i == 1) {
      const char* arg1 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg1);
    } else if (i == 2) {
      const char* arg2 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg2);
    } else if (i == 3) {
      const char* arg3 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg3);
    } else if (i == 4) {
      const char* arg4 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg4);
    } else if (i == 5) {
      const char* arg5 = argv[i];
      DFT_LOGGER_UPDATE_HASH(arg5);
    } else {
      break;
    }
    i++;
    val = argv[i];
  }
  // exec replaces this image and does not return when it succeeds: record
  // the call and write out everything buffered first, or it is lost. A
  // flush, not a finalize: if exec fails, tracing goes on. Not in a vfork
  // child, which shares the parent's buffers while the parent's other
  // threads may still be writing to them.
  DFT_LOGGER_END();
  if (!in_vfork_child()) dft_flush();
  return __real_execve(pathname, argv, envp);
}

pid_t brahma::POSIXDFTracer::wait(int* wstatus) {
  BRAHMA_MAP_OR_FAIL(wait);
  DFT_LOGGER_START_ALWAYS();
  pid_t ret = __real_wait(wstatus);
  DFT_LOGGER_UPDATE(ret);
  DFT_LOGGER_END();
  return ret;
}

pid_t brahma::POSIXDFTracer::waitpid(pid_t pid, int* wstatus, int options) {
  BRAHMA_MAP_OR_FAIL(waitpid);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE(pid);
  pid_t ret = __real_waitpid(pid, wstatus, options);
  DFT_LOGGER_UPDATE(ret);
  DFT_LOGGER_END();
  return ret;
}

char* brahma::POSIXDFTracer::realpath(const char* path, char* resolved_path) {
  BRAHMA_MAP_OR_FAIL(realpath);
  DFT_LOGGER_START(path);
  char* ret = __real_realpath(path, resolved_path);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::dirfd(DIR* dir) {
  BRAHMA_MAP_OR_FAIL(dirfd);
  DFT_LOGGER_START_ALWAYS();
  int ret = __real_dirfd(dir);
  DFT_LOGGER_UPDATE(ret);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::sendfile(int out_fd, int in_fd, off_t* offset,
                                        size_t count) {
  BRAHMA_MAP_OR_FAIL(sendfile);
  DFT_LOGGER_START(out_fd);
  DFT_LOGGER_UPDATE(in_fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  ssize_t ret = __real_sendfile(out_fd, in_fd, offset, count);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::sendfile64(int out_fd, int in_fd,
                                          off64_t* offset, size_t count) {
  BRAHMA_MAP_OR_FAIL(sendfile64);
  DFT_LOGGER_START(out_fd);
  DFT_LOGGER_UPDATE(in_fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  ssize_t ret = __real_sendfile64(out_fd, in_fd, offset, count);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::copy_file_range(int fd_in, off64_t* off_in,
                                               int fd_out, off64_t* off_out,
                                               size_t len, unsigned int flags) {
  BRAHMA_MAP_OR_FAIL(copy_file_range);
  DFT_LOGGER_START(fd_in);
  DFT_LOGGER_UPDATE(fd_out);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  ssize_t ret =
      __real_copy_file_range(fd_in, off_in, fd_out, off_out, len, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mknod(const char* pathname, mode_t mode, dev_t dev) {
  BRAHMA_MAP_OR_FAIL(mknod);
  DFT_LOGGER_START(pathname);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_mknod(pathname, mode, dev);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::stat(const char* path, struct stat* buf) {
  BRAHMA_MAP_OR_FAIL(stat);
  DFT_LOGGER_START(path);
  int ret = __real_stat(path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::stat64(const char* path, struct stat64* buf) {
  BRAHMA_MAP_OR_FAIL(stat64);
  DFT_LOGGER_START(path);
  int ret = __real_stat64(path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lstat(const char* path, struct stat* buf) {
  BRAHMA_MAP_OR_FAIL(lstat);
  DFT_LOGGER_START(path);
  int ret = __real_lstat(path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lstat64(const char* path, struct stat64* buf) {
  BRAHMA_MAP_OR_FAIL(lstat64);
  DFT_LOGGER_START(path);
  int ret = __real_lstat64(path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fstat(int fd, struct stat* buf) {
  BRAHMA_MAP_OR_FAIL(fstat);
  DFT_LOGGER_START(fd);
  int ret = __real_fstat(fd, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fstat64(int fd, struct stat64* buf) {
  BRAHMA_MAP_OR_FAIL(fstat64);
  DFT_LOGGER_START(fd);
  int ret = __real_fstat64(fd, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fstatat(int dirfd, const char* path,
                                   struct stat* buf, int flags) {
  BRAHMA_MAP_OR_FAIL(fstatat);
  int ret;
  if (dirfd != AT_FDCWD) {
    DFT_LOGGER_START(dirfd);
    DFT_LOGGER_UPDATE_HASH(path);
    DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
    ret = __real_fstatat(dirfd, path, buf, flags);
    DFT_LOGGER_END();
  } else {
    DFT_LOGGER_START(path);
    DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
    ret = __real_fstatat(dirfd, path, buf, flags);
    DFT_LOGGER_END();
  }
  return ret;
}

int brahma::POSIXDFTracer::fstatat64(int dirfd, const char* path,
                                     struct stat64* buf, int flags) {
  BRAHMA_MAP_OR_FAIL(fstatat64);
  int ret;
  if (dirfd != AT_FDCWD) {
    DFT_LOGGER_START(dirfd);
    DFT_LOGGER_UPDATE_HASH(path);
    DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
    ret = __real_fstatat64(dirfd, path, buf, flags);
    DFT_LOGGER_END();
  } else {
    DFT_LOGGER_START(path);
    DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
    ret = __real_fstatat64(dirfd, path, buf, flags);
    DFT_LOGGER_END();
  }
  return ret;
}

int brahma::POSIXDFTracer::posix_fadvise(int fd, off_t offset, off_t len,
                                         int advice) {
  BRAHMA_MAP_OR_FAIL(posix_fadvise);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_posix_fadvise(fd, offset, len, advice);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::posix_fadvise64(int fd, off64_t offset, off64_t len,
                                           int advice) {
  BRAHMA_MAP_OR_FAIL(posix_fadvise64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_posix_fadvise64(fd, offset, len, advice);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::posix_fallocate(int fd, off_t offset, off_t len) {
  BRAHMA_MAP_OR_FAIL(posix_fallocate);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_posix_fallocate(fd, offset, len);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::posix_fallocate64(int fd, off64_t offset,
                                             off64_t len) {
  BRAHMA_MAP_OR_FAIL(posix_fallocate64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_posix_fallocate64(fd, offset, len);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::closedir(DIR* dir) {
  BRAHMA_MAP_OR_FAIL(closedir);
  int fd = this->dir_fd(dir);
  DFT_LOGGER_START(fd);
  int ret = __real_closedir(dir);
  DFT_LOGGER_END();
  if (trace) this->remove_trace(fd);
  return ret;
}

dirent* brahma::POSIXDFTracer::readdir(DIR* dir) {
  BRAHMA_MAP_OR_FAIL(readdir);
  DFT_LOGGER_START(this->dir_fd(dir));
  dirent* ret = __real_readdir(dir);
  DFT_LOGGER_END();
  return ret;
}

dirent64* brahma::POSIXDFTracer::readdir64(DIR* dir) {
  BRAHMA_MAP_OR_FAIL(readdir64);
  DFT_LOGGER_START(this->dir_fd(dir));
  dirent64* ret = __real_readdir64(dir);
  DFT_LOGGER_END();
  return ret;
}

void brahma::POSIXDFTracer::rewinddir(DIR* dir) {
  BRAHMA_MAP_OR_FAIL(rewinddir);
  DFT_LOGGER_START(this->dir_fd(dir));
  __real_rewinddir(dir);
  DFT_LOGGER_END();
}

int brahma::POSIXDFTracer::mkdirat(int dirfd, const char* pathname,
                                   mode_t mode) {
  BRAHMA_MAP_OR_FAIL(mkdirat);
  DFT_LOGGER_START(AtRef(dirfd, pathname));
  DFT_LOGGER_UPDATE_HASH(pathname);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_mkdirat(dirfd, pathname, mode);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::unlinkat(int dirfd, const char* pathname,
                                    int flags) {
  BRAHMA_MAP_OR_FAIL(unlinkat);
  DFT_LOGGER_START(AtRef(dirfd, pathname));
  DFT_LOGGER_UPDATE_HASH(pathname);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_unlinkat(dirfd, pathname, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::renameat(int olddirfd, const char* oldpath,
                                    int newdirfd, const char* newpath) {
  BRAHMA_MAP_OR_FAIL(renameat);
  DFT_LOGGER_START(AtRef(olddirfd, oldpath));
  DFT_LOGGER_UPDATE_HASH(oldpath);
  DFT_LOGGER_UPDATE_HASH(newpath);
  int ret = __real_renameat(olddirfd, oldpath, newdirfd, newpath);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fchmod(int fd, mode_t mode) {
  BRAHMA_MAP_OR_FAIL(fchmod);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_fchmod(fd, mode);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fchmodat(int dirfd, const char* pathname,
                                    mode_t mode, int flags) {
  BRAHMA_MAP_OR_FAIL(fchmodat);
  DFT_LOGGER_START(AtRef(dirfd, pathname));
  DFT_LOGGER_UPDATE_HASH(pathname);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_fchmodat(dirfd, pathname, mode, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fchown(int fd, uid_t owner, gid_t group) {
  BRAHMA_MAP_OR_FAIL(fchown);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(owner, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(group, MetadataType::MT_VALUE);
  int ret = __real_fchown(fd, owner, group);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fchownat(int dirfd, const char* pathname,
                                    uid_t owner, gid_t group, int flags) {
  BRAHMA_MAP_OR_FAIL(fchownat);
  DFT_LOGGER_START(AtRef(dirfd, pathname));
  DFT_LOGGER_UPDATE_HASH(pathname);
  DFT_LOGGER_UPDATE_TYPE(owner, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(group, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_fchownat(dirfd, pathname, owner, group, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::openat64(int dirfd, const char* pathname, int flags,
                                    ...) {
  BRAHMA_MAP_OR_FAIL(openat64);
  DFT_LOGGER_START(AtRef(dirfd, pathname));
  DFT_LOGGER_UPDATE_HASH(pathname);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = -1;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    int mode = va_arg(args, int);
    va_end(args);
    DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
    ret = __real_openat64(dirfd, pathname, flags, mode);
  } else {
    ret = __real_openat64(dirfd, pathname, flags);
  }
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::fcntl64(int fd, int cmd, ...) {
  BRAHMA_MAP_OR_FAIL(fcntl64);
  va_list arg;
  va_start(arg, cmd);
  void* val = va_arg(arg, void*);
  va_end(arg);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(cmd, MetadataType::MT_VALUE);
  int ret = __real_fcntl64(fd, cmd, val);
  DFT_LOGGER_END();
  if (trace && (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC))
    this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::ftruncate64(int fd, off64_t length) {
  BRAHMA_MAP_OR_FAIL(ftruncate64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(length, MetadataType::MT_VALUE);
  int ret = __real_ftruncate64(fd, length);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::truncate64(const char* pathname, off64_t length) {
  BRAHMA_MAP_OR_FAIL(truncate64);
  DFT_LOGGER_START(pathname);
  DFT_LOGGER_UPDATE_TYPE(length, MetadataType::MT_VALUE);
  int ret = __real_truncate64(pathname, length);
  DFT_LOGGER_END();
  return ret;
}

char* brahma::POSIXDFTracer::getcwd(char* buf, size_t size) {
  BRAHMA_MAP_OR_FAIL(getcwd);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  char* ret = __real_getcwd(buf, size);
  if (ret != nullptr) DFT_LOGGER_UPDATE_HASH(ret);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::pipe(int pipefd[2]) {
  BRAHMA_MAP_OR_FAIL(pipe);
  DFT_LOGGER_START_ALWAYS();
  int ret = __real_pipe(pipefd);
  int read_fd = ret == 0 ? pipefd[0] : -1;
  int write_fd = ret == 0 ? pipefd[1] : -1;
  DFT_LOGGER_UPDATE(read_fd);
  DFT_LOGGER_UPDATE(write_fd);
  DFT_LOGGER_END();
  return ret;
}

long brahma::POSIXDFTracer::sysconf(int name) {
  BRAHMA_MAP_OR_FAIL(sysconf);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_TYPE(name, MetadataType::MT_VALUE);
  long ret = __real_sysconf(name);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::munmap(void* addr, size_t len) {
  BRAHMA_MAP_OR_FAIL(munmap);
  DFT_LOGGER_START_ALWAYS();
  size_t address = reinterpret_cast<uintptr_t>(addr);
  DFT_LOGGER_UPDATE_TYPE(address, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_munmap(addr, len);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::msync(void* addr, size_t len, int flags) {
  BRAHMA_MAP_OR_FAIL(msync);
  DFT_LOGGER_START_ALWAYS();
  size_t address = reinterpret_cast<uintptr_t>(addr);
  DFT_LOGGER_UPDATE_TYPE(address, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_msync(addr, len, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::madvise(void* addr, size_t length, int advice) {
  BRAHMA_MAP_OR_FAIL(madvise);
  DFT_LOGGER_START_ALWAYS();
  size_t address = reinterpret_cast<uintptr_t>(addr);
  DFT_LOGGER_UPDATE_TYPE(address, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(length, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(advice, MetadataType::MT_VALUE);
  int ret = __real_madvise(addr, length, advice);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mprotect(void* addr, size_t length, int prot) {
  BRAHMA_MAP_OR_FAIL(mprotect);
  DFT_LOGGER_START_ALWAYS();
  size_t address = reinterpret_cast<uintptr_t>(addr);
  DFT_LOGGER_UPDATE_TYPE(address, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(length, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(prot, MetadataType::MT_VALUE);
  int ret = __real_mprotect(addr, length, prot);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mlock(const void* addr, size_t len) {
  BRAHMA_MAP_OR_FAIL(mlock);
  DFT_LOGGER_START_ALWAYS();
  size_t address = reinterpret_cast<uintptr_t>(addr);
  DFT_LOGGER_UPDATE_TYPE(address, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_mlock(addr, len);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::munlock(const void* addr, size_t len) {
  BRAHMA_MAP_OR_FAIL(munlock);
  DFT_LOGGER_START_ALWAYS();
  size_t address = reinterpret_cast<uintptr_t>(addr);
  DFT_LOGGER_UPDATE_TYPE(address, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_munlock(addr, len);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mlockall(int flags) {
  BRAHMA_MAP_OR_FAIL(mlockall);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_mlockall(flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::munlockall(void) {
  BRAHMA_MAP_OR_FAIL(munlockall);
  DFT_LOGGER_START_ALWAYS();
  int ret = __real_munlockall();
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__fxstatat(int vers, int dirfd, const char* path,
                                      struct stat* buf, int flags) {
  BRAHMA_MAP_OR_FAIL(__fxstatat);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real___fxstatat(vers, dirfd, path, buf, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__fxstatat64(int vers, int dirfd, const char* path,
                                        struct stat64* buf, int flags) {
  BRAHMA_MAP_OR_FAIL(__fxstatat64);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real___fxstatat64(vers, dirfd, path, buf, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__xmknod(int vers, const char* path, mode_t mode,
                                    dev_t* dev) {
  BRAHMA_MAP_OR_FAIL(__xmknod);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real___xmknod(vers, path, mode, dev);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__open_2(const char* path, int flags) {
  BRAHMA_MAP_OR_FAIL(__open_2);
  DFT_LOGGER_START(path);
  int ret = __real___open_2(path, flags);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::__open64_2(const char* path, int flags) {
  BRAHMA_MAP_OR_FAIL(__open64_2);
  DFT_LOGGER_START(path);
  int ret = __real___open64_2(path, flags);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::__openat_2(int dirfd, const char* path, int flags) {
  BRAHMA_MAP_OR_FAIL(__openat_2);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real___openat_2(dirfd, path, flags);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::__openat64_2(int dirfd, const char* path,
                                        int flags) {
  BRAHMA_MAP_OR_FAIL(__openat64_2);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real___openat64_2(dirfd, path, flags);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

ssize_t brahma::POSIXDFTracer::__read_chk(int fd, void* buf, size_t count,
                                          size_t buflen) {
  BRAHMA_MAP_OR_FAIL(__read_chk);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  ssize_t ret = __real___read_chk(fd, buf, count, buflen);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::__pread_chk(int fd, void* buf, size_t count,
                                           off_t offset, size_t buflen) {
  BRAHMA_MAP_OR_FAIL(__pread_chk);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real___pread_chk(fd, buf, count, offset, buflen);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::__pread64_chk(int fd, void* buf, size_t count,
                                             off64_t offset, size_t buflen) {
  BRAHMA_MAP_OR_FAIL(__pread64_chk);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  ssize_t ret = __real___pread64_chk(fd, buf, count, offset, buflen);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::__readlink_chk(const char* path, char* buf,
                                              size_t bufsize, size_t buflen) {
  BRAHMA_MAP_OR_FAIL(__readlink_chk);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(bufsize, MetadataType::MT_VALUE);
  ssize_t ret = __real___readlink_chk(path, buf, bufsize, buflen);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::__readlinkat_chk(int dirfd, const char* path,
                                                char* buf, size_t bufsize,
                                                size_t buflen) {
  BRAHMA_MAP_OR_FAIL(__readlinkat_chk);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(bufsize, MetadataType::MT_VALUE);
  ssize_t ret = __real___readlinkat_chk(dirfd, path, buf, bufsize, buflen);
  DFT_LOGGER_END();
  return ret;
}

char* brahma::POSIXDFTracer::__getcwd_chk(char* buf, size_t size,
                                          size_t buflen) {
  BRAHMA_MAP_OR_FAIL(__getcwd_chk);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  char* ret = __real___getcwd_chk(buf, size, buflen);
  if (ret != nullptr) DFT_LOGGER_UPDATE_HASH(ret);
  DFT_LOGGER_END();
  return ret;
}

char* brahma::POSIXDFTracer::__realpath_chk(const char* path, char* resolved,
                                            size_t resolvedlen) {
  BRAHMA_MAP_OR_FAIL(__realpath_chk);
  DFT_LOGGER_START(path);
  char* ret = __real___realpath_chk(path, resolved, resolvedlen);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::preadv2(int fd, const struct iovec* iov,
                                       int iovcnt, off_t offset, int flags) {
  BRAHMA_MAP_OR_FAIL(preadv2);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  ssize_t ret = __real_preadv2(fd, iov, iovcnt, offset, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::pwritev2(int fd, const struct iovec* iov,
                                        int iovcnt, off_t offset, int flags) {
  BRAHMA_MAP_OR_FAIL(pwritev2);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  ssize_t ret = __real_pwritev2(fd, iov, iovcnt, offset, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::preadv64v2(int fd, const struct iovec* iov,
                                          int iovcnt, off64_t offset,
                                          int flags) {
  BRAHMA_MAP_OR_FAIL(preadv64v2);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  ssize_t ret = __real_preadv64v2(fd, iov, iovcnt, offset, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::pwritev64v2(int fd, const struct iovec* iov,
                                           int iovcnt, off64_t offset,
                                           int flags) {
  BRAHMA_MAP_OR_FAIL(pwritev64v2);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(iovcnt, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  ssize_t ret = __real_pwritev64v2(fd, iov, iovcnt, offset, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::splice(int fd_in, off64_t* off_in, int fd_out,
                                      off64_t* off_out, size_t len,
                                      unsigned int flags) {
  BRAHMA_MAP_OR_FAIL(splice);
  int fd = fd_hash(fd_in) != NO_HASH_DEFAULT ? fd_in : fd_out;
  DFT_LOGGER_START(fd);
  int other_fd = fd == fd_in ? fd_out : fd_in;
  DFT_LOGGER_UPDATE(other_fd);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  ssize_t ret = __real_splice(fd_in, off_in, fd_out, off_out, len, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::tee(int fd_in, int fd_out, size_t len,
                                   unsigned int flags) {
  BRAHMA_MAP_OR_FAIL(tee);
  int fd = fd_hash(fd_in) != NO_HASH_DEFAULT ? fd_in : fd_out;
  DFT_LOGGER_START(fd);
  int other_fd = fd == fd_in ? fd_out : fd_in;
  DFT_LOGGER_UPDATE(other_fd);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  ssize_t ret = __real_tee(fd_in, fd_out, len, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::vmsplice(int fd, const struct iovec* iov,
                                        size_t nr_segs, unsigned int flags) {
  BRAHMA_MAP_OR_FAIL(vmsplice);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(nr_segs, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  ssize_t ret = __real_vmsplice(fd, iov, nr_segs, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fallocate(int fd, int mode, off_t offset,
                                     off_t len) {
  BRAHMA_MAP_OR_FAIL(fallocate);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_fallocate(fd, mode, offset, len);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fallocate64(int fd, int mode, off64_t offset,
                                       off64_t len) {
  BRAHMA_MAP_OR_FAIL(fallocate64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_fallocate64(fd, mode, offset, len);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::readahead(int fd, off64_t offset, size_t count) {
  BRAHMA_MAP_OR_FAIL(readahead);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  ssize_t ret = __real_readahead(fd, offset, count);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

void brahma::POSIXDFTracer::sync(void) {
  BRAHMA_MAP_OR_FAIL(sync);
  DFT_LOGGER_START_ALWAYS();
  __real_sync();
  DFT_LOGGER_END();
}

int brahma::POSIXDFTracer::syncfs(int fd) {
  BRAHMA_MAP_OR_FAIL(syncfs);
  DFT_LOGGER_START(fd);
  int ret = __real_syncfs(fd);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::sync_file_range(int fd, off64_t offset,
                                           off64_t nbytes, unsigned int flags) {
  BRAHMA_MAP_OR_FAIL(sync_file_range);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(nbytes, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_sync_file_range(fd, offset, nbytes, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::statx(int dirfd, const char* path, int flags,
                                 unsigned int mask, struct statx* buf) {
  BRAHMA_MAP_OR_FAIL(statx);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(mask, MetadataType::MT_VALUE);
  int ret = __real_statx(dirfd, path, flags, mask, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::statfs(const char* path, struct statfs* buf) {
  BRAHMA_MAP_OR_FAIL(statfs);
  DFT_LOGGER_START(path);
  int ret = __real_statfs(path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fstatfs(int fd, struct statfs* buf) {
  BRAHMA_MAP_OR_FAIL(fstatfs);
  DFT_LOGGER_START(fd);
  int ret = __real_fstatfs(fd, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::statfs64(const char* path, struct statfs64* buf) {
  BRAHMA_MAP_OR_FAIL(statfs64);
  DFT_LOGGER_START(path);
  int ret = __real_statfs64(path, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fstatfs64(int fd, struct statfs64* buf) {
  BRAHMA_MAP_OR_FAIL(fstatfs64);
  DFT_LOGGER_START(fd);
  int ret = __real_fstatfs64(fd, buf);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mknodat(int dirfd, const char* path, mode_t mode,
                                   dev_t dev) {
  BRAHMA_MAP_OR_FAIL(mknodat);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_mknodat(dirfd, path, mode, dev);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::__xmknodat(int ver, int dirfd, const char* path,
                                      mode_t mode, dev_t* dev) {
  BRAHMA_MAP_OR_FAIL(__xmknodat);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real___xmknodat(ver, dirfd, path, mode, dev);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::euidaccess(const char* path, int mode) {
  BRAHMA_MAP_OR_FAIL(euidaccess);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_euidaccess(path, mode);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::eaccess(const char* path, int mode) {
  BRAHMA_MAP_OR_FAIL(eaccess);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_eaccess(path, mode);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::renameat2(int olddirfd, const char* oldpath,
                                     int newdirfd, const char* newpath,
                                     unsigned int flags) {
  BRAHMA_MAP_OR_FAIL(renameat2);
  DFT_LOGGER_START(AtRef(olddirfd, oldpath));
  DFT_LOGGER_UPDATE_HASH(oldpath);
  DFT_LOGGER_UPDATE_HASH(newpath);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_renameat2(olddirfd, oldpath, newdirfd, newpath, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mkfifoat(int dirfd, const char* path, mode_t mode) {
  BRAHMA_MAP_OR_FAIL(mkfifoat);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_mkfifoat(dirfd, path, mode);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lchmod(const char* path, mode_t mode) {
  BRAHMA_MAP_OR_FAIL(lchmod);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_lchmod(path, mode);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::utimes(const char* path,
                                  const struct timeval times[2]) {
  BRAHMA_MAP_OR_FAIL(utimes);
  DFT_LOGGER_START(path);
  int ret = __real_utimes(path, times);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::futimes(int fd, const struct timeval tv[2]) {
  BRAHMA_MAP_OR_FAIL(futimes);
  DFT_LOGGER_START(fd);
  int ret = __real_futimes(fd, tv);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lutimes(const char* path,
                                   const struct timeval tv[2]) {
  BRAHMA_MAP_OR_FAIL(lutimes);
  DFT_LOGGER_START(path);
  int ret = __real_lutimes(path, tv);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::futimesat(int dirfd, const char* path,
                                     const struct timeval times[2]) {
  BRAHMA_MAP_OR_FAIL(futimesat);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  int ret = __real_futimesat(dirfd, path, times);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::utimensat(int dirfd, const char* path,
                                     const struct timespec times[2],
                                     int flags) {
  BRAHMA_MAP_OR_FAIL(utimensat);
  DFT_LOGGER_START(AtRef(dirfd, path));
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_utimensat(dirfd, path, times, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::futimens(int fd, const struct timespec times[2]) {
  BRAHMA_MAP_OR_FAIL(futimens);
  DFT_LOGGER_START(fd);
  int ret = __real_futimens(fd, times);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fchdir(int fd) {
  BRAHMA_MAP_OR_FAIL(fchdir);
  DFT_LOGGER_START(fd);
  int ret = __real_fchdir(fd);
  DFT_LOGGER_END();
  return ret;
}

DIR* brahma::POSIXDFTracer::fdopendir(int fd) {
  BRAHMA_MAP_OR_FAIL(fdopendir);
  DFT_LOGGER_START(fd);
  DIR* ret = __real_fdopendir(fd);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::readdir_r(DIR* dir, struct dirent* entry,
                                     struct dirent** res) {
  BRAHMA_MAP_OR_FAIL(readdir_r);
  DFT_LOGGER_START(this->dir_fd(dir));
  int ret = __real_readdir_r(dir, entry, res);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::readdir64_r(DIR* dir, struct dirent64* entry,
                                       struct dirent64** res) {
  BRAHMA_MAP_OR_FAIL(readdir64_r);
  DFT_LOGGER_START(this->dir_fd(dir));
  int ret = __real_readdir64_r(dir, entry, res);
  DFT_LOGGER_END();
  return ret;
}

void brahma::POSIXDFTracer::seekdir(DIR* dir, long loc) {
  BRAHMA_MAP_OR_FAIL(seekdir);
  DFT_LOGGER_START(this->dir_fd(dir));
  DFT_LOGGER_UPDATE_TYPE(loc, MetadataType::MT_VALUE);
  __real_seekdir(dir, loc);
  DFT_LOGGER_END();
}

long brahma::POSIXDFTracer::telldir(DIR* dir) {
  BRAHMA_MAP_OR_FAIL(telldir);
  DFT_LOGGER_START(this->dir_fd(dir));
  long ret = __real_telldir(dir);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::scandir(const char* dir, struct dirent*** namelist,
                                   int (*filter)(const struct dirent*),
                                   int (*compar)(const struct dirent**,
                                                 const struct dirent**)) {
  BRAHMA_MAP_OR_FAIL(scandir);
  DFT_LOGGER_START(dir);
  int ret = __real_scandir(dir, namelist, filter, compar);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::scandir64(const char* dir,
                                     struct dirent64*** namelist,
                                     int (*filter)(const struct dirent64*),
                                     int (*compar)(const struct dirent64**,
                                                   const struct dirent64**)) {
  BRAHMA_MAP_OR_FAIL(scandir64);
  DFT_LOGGER_START(dir);
  int ret = __real_scandir64(dir, namelist, filter, compar);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::scandirat(int dirfd, const char* dir,
                                     struct dirent*** namelist,
                                     int (*filter)(const struct dirent*),
                                     int (*compar)(const struct dirent**,
                                                   const struct dirent**)) {
  BRAHMA_MAP_OR_FAIL(scandirat);
  DFT_LOGGER_START(AtRef(dirfd, dir));
  DFT_LOGGER_UPDATE_HASH(dir);
  int ret = __real_scandirat(dirfd, dir, namelist, filter, compar);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::scandirat64(int dirfd, const char* dir,
                                       struct dirent64*** namelist,
                                       int (*filter)(const struct dirent64*),
                                       int (*compar)(const struct dirent64**,
                                                     const struct dirent64**)) {
  BRAHMA_MAP_OR_FAIL(scandirat64);
  DFT_LOGGER_START(AtRef(dirfd, dir));
  DFT_LOGGER_UPDATE_HASH(dir);
  int ret = __real_scandirat64(dirfd, dir, namelist, filter, compar);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::nftw(const char* path,
                                int (*func)(const char*, const struct stat*,
                                            int, struct FTW*),
                                int nopenfd, int flags) {
  BRAHMA_MAP_OR_FAIL(nftw);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(nopenfd, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_nftw(path, func, nopenfd, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::nftw64(const char* path,
                                  int (*func)(const char*, const struct stat64*,
                                              int, struct FTW*),
                                  int nopenfd, int flags) {
  BRAHMA_MAP_OR_FAIL(nftw64);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(nopenfd, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_nftw64(path, func, nopenfd, flags);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::getdents64(int fd, void* buf, size_t count) {
  BRAHMA_MAP_OR_FAIL(getdents64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  ssize_t ret = __real_getdents64(fd, buf, count);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

void* brahma::POSIXDFTracer::mremap(void* old_addr, size_t old_size,
                                    size_t new_size, int flags,
                                    void* new_addr) {
  BRAHMA_MAP_OR_FAIL(mremap);
  DFT_LOGGER_START_ALWAYS();
  size_t address = reinterpret_cast<uintptr_t>(old_addr);
  DFT_LOGGER_UPDATE_TYPE(address, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(old_size, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(new_size, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  void* ret = (flags & MREMAP_FIXED)
                  ? __real_mremap(old_addr, old_size, new_size, flags, new_addr)
                  : __real_mremap(old_addr, old_size, new_size, flags);
  size_t result_address = reinterpret_cast<uintptr_t>(ret);
  DFT_LOGGER_UPDATE_TYPE(result_address, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::ioctl(int fd, unsigned long request, void* arg) {
  BRAHMA_MAP_OR_FAIL(ioctl);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(request, MetadataType::MT_VALUE);
  int ret = __real_ioctl(fd, request, arg);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lockf(int fd, int cmd, off_t len) {
  BRAHMA_MAP_OR_FAIL(lockf);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(cmd, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_lockf(fd, cmd, len);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lockf64(int fd, int cmd, off64_t len) {
  BRAHMA_MAP_OR_FAIL(lockf64);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(cmd, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(len, MetadataType::MT_VALUE);
  int ret = __real_lockf64(fd, cmd, len);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::pipe2(int pipefd[2], int flags) {
  BRAHMA_MAP_OR_FAIL(pipe2);
  DFT_LOGGER_START_ALWAYS();
  int ret = __real_pipe2(pipefd, flags);
  int read_fd = ret == 0 ? pipefd[0] : -1;
  int write_fd = ret == 0 ? pipefd[1] : -1;
  DFT_LOGGER_UPDATE(read_fd);
  DFT_LOGGER_UPDATE(write_fd);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::memfd_create(const char* name, unsigned int flags) {
  BRAHMA_MAP_OR_FAIL(memfd_create);
  DFT_LOGGER_START(name);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_memfd_create(name, flags);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

int brahma::POSIXDFTracer::mkstemp(char* tmpl) {
  BRAHMA_MAP_OR_FAIL(mkstemp);
  DFT_LOGGER_START(tmpl);
  int ret = __real_mkstemp(tmpl);
  if (trace && ret >= 0) {
    fhash = is_traced(tmpl, __FUNCTION__);
    DFT_LOGGER_UPDATE(fhash);
    this->trace(ret, fhash);
  }
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mkstemp64(char* tmpl) {
  BRAHMA_MAP_OR_FAIL(mkstemp64);
  DFT_LOGGER_START(tmpl);
  int ret = __real_mkstemp64(tmpl);
  if (trace && ret >= 0) {
    fhash = is_traced(tmpl, __FUNCTION__);
    DFT_LOGGER_UPDATE(fhash);
    this->trace(ret, fhash);
  }
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mkostemp(char* tmpl, int flags) {
  BRAHMA_MAP_OR_FAIL(mkostemp);
  DFT_LOGGER_START(tmpl);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_mkostemp(tmpl, flags);
  if (trace && ret >= 0) {
    fhash = is_traced(tmpl, __FUNCTION__);
    DFT_LOGGER_UPDATE(fhash);
    this->trace(ret, fhash);
  }
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mkostemp64(char* tmpl, int flags) {
  BRAHMA_MAP_OR_FAIL(mkostemp64);
  DFT_LOGGER_START(tmpl);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_mkostemp64(tmpl, flags);
  if (trace && ret >= 0) {
    fhash = is_traced(tmpl, __FUNCTION__);
    DFT_LOGGER_UPDATE(fhash);
    this->trace(ret, fhash);
  }
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mkstemps(char* tmpl, int suffixlen) {
  BRAHMA_MAP_OR_FAIL(mkstemps);
  DFT_LOGGER_START(tmpl);
  DFT_LOGGER_UPDATE_TYPE(suffixlen, MetadataType::MT_VALUE);
  int ret = __real_mkstemps(tmpl, suffixlen);
  if (trace && ret >= 0) {
    fhash = is_traced(tmpl, __FUNCTION__);
    DFT_LOGGER_UPDATE(fhash);
    this->trace(ret, fhash);
  }
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mkstemps64(char* tmpl, int suffixlen) {
  BRAHMA_MAP_OR_FAIL(mkstemps64);
  DFT_LOGGER_START(tmpl);
  DFT_LOGGER_UPDATE_TYPE(suffixlen, MetadataType::MT_VALUE);
  int ret = __real_mkstemps64(tmpl, suffixlen);
  if (trace && ret >= 0) {
    fhash = is_traced(tmpl, __FUNCTION__);
    DFT_LOGGER_UPDATE(fhash);
    this->trace(ret, fhash);
  }
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mkostemps(char* tmpl, int suffixlen, int flags) {
  BRAHMA_MAP_OR_FAIL(mkostemps);
  DFT_LOGGER_START(tmpl);
  DFT_LOGGER_UPDATE_TYPE(suffixlen, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_mkostemps(tmpl, suffixlen, flags);
  if (trace && ret >= 0) {
    fhash = is_traced(tmpl, __FUNCTION__);
    DFT_LOGGER_UPDATE(fhash);
    this->trace(ret, fhash);
  }
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::mkostemps64(char* tmpl, int suffixlen, int flags) {
  BRAHMA_MAP_OR_FAIL(mkostemps64);
  DFT_LOGGER_START(tmpl);
  DFT_LOGGER_UPDATE_TYPE(suffixlen, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_mkostemps64(tmpl, suffixlen, flags);
  if (trace && ret >= 0) {
    fhash = is_traced(tmpl, __FUNCTION__);
    DFT_LOGGER_UPDATE(fhash);
    this->trace(ret, fhash);
  }
  DFT_LOGGER_END();
  return ret;
}

char* brahma::POSIXDFTracer::mkdtemp(char* tmpl) {
  BRAHMA_MAP_OR_FAIL(mkdtemp);
  DFT_LOGGER_START(tmpl);
  char* ret = __real_mkdtemp(tmpl);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::getxattr(const char* path, const char* name,
                                        void* value, size_t size) {
  BRAHMA_MAP_OR_FAIL(getxattr);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  ssize_t ret = __real_getxattr(path, name, value, size);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::lgetxattr(const char* path, const char* name,
                                         void* value, size_t size) {
  BRAHMA_MAP_OR_FAIL(lgetxattr);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  ssize_t ret = __real_lgetxattr(path, name, value, size);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::fgetxattr(int fd, const char* name, void* value,
                                         size_t size) {
  BRAHMA_MAP_OR_FAIL(fgetxattr);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  ssize_t ret = __real_fgetxattr(fd, name, value, size);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::setxattr(const char* path, const char* name,
                                    const void* value, size_t size, int flags) {
  BRAHMA_MAP_OR_FAIL(setxattr);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_setxattr(path, name, value, size, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lsetxattr(const char* path, const char* name,
                                     const void* value, size_t size,
                                     int flags) {
  BRAHMA_MAP_OR_FAIL(lsetxattr);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_lsetxattr(path, name, value, size, flags);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fsetxattr(int fd, const char* name,
                                     const void* value, size_t size,
                                     int flags) {
  BRAHMA_MAP_OR_FAIL(fsetxattr);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(flags, MetadataType::MT_VALUE);
  int ret = __real_fsetxattr(fd, name, value, size, flags);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::listxattr(const char* path, char* list,
                                         size_t size) {
  BRAHMA_MAP_OR_FAIL(listxattr);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  ssize_t ret = __real_listxattr(path, list, size);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::llistxattr(const char* path, char* list,
                                          size_t size) {
  BRAHMA_MAP_OR_FAIL(llistxattr);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  ssize_t ret = __real_llistxattr(path, list, size);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::flistxattr(int fd, char* list, size_t size) {
  BRAHMA_MAP_OR_FAIL(flistxattr);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  ssize_t ret = __real_flistxattr(fd, list, size);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::removexattr(const char* path, const char* name) {
  BRAHMA_MAP_OR_FAIL(removexattr);
  DFT_LOGGER_START(path);
  int ret = __real_removexattr(path, name);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lremovexattr(const char* path, const char* name) {
  BRAHMA_MAP_OR_FAIL(lremovexattr);
  DFT_LOGGER_START(path);
  int ret = __real_lremovexattr(path, name);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::fremovexattr(int fd, const char* name) {
  BRAHMA_MAP_OR_FAIL(fremovexattr);
  DFT_LOGGER_START(fd);
  int ret = __real_fremovexattr(fd, name);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_read(struct aiocb* cb) {
  BRAHMA_MAP_OR_FAIL(aio_read);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  size_t aio_nbytes = cb != nullptr ? cb->aio_nbytes : 0;
  DFT_LOGGER_UPDATE_TYPE(aio_nbytes, MetadataType::MT_VALUE);
  off_t aio_offset = cb != nullptr ? cb->aio_offset : 0;
  DFT_LOGGER_UPDATE_TYPE(aio_offset, MetadataType::MT_VALUE);
  int ret = __real_aio_read(cb);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_write(struct aiocb* cb) {
  BRAHMA_MAP_OR_FAIL(aio_write);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  size_t aio_nbytes = cb != nullptr ? cb->aio_nbytes : 0;
  DFT_LOGGER_UPDATE_TYPE(aio_nbytes, MetadataType::MT_VALUE);
  off_t aio_offset = cb != nullptr ? cb->aio_offset : 0;
  DFT_LOGGER_UPDATE_TYPE(aio_offset, MetadataType::MT_VALUE);
  int ret = __real_aio_write(cb);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_error(const struct aiocb* cb) {
  BRAHMA_MAP_OR_FAIL(aio_error);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  int ret = __real_aio_error(cb);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::aio_return(struct aiocb* cb) {
  BRAHMA_MAP_OR_FAIL(aio_return);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  ssize_t ret = __real_aio_return(cb);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_fsync(int op, struct aiocb* cb) {
  BRAHMA_MAP_OR_FAIL(aio_fsync);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(op, MetadataType::MT_VALUE);
  int ret = __real_aio_fsync(op, cb);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_cancel(int fd, struct aiocb* cb) {
  BRAHMA_MAP_OR_FAIL(aio_cancel);
  DFT_LOGGER_START(fd);
  int ret = __real_aio_cancel(fd, cb);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_suspend(const struct aiocb* const list[],
                                       int nent,
                                       const struct timespec* timeout) {
  BRAHMA_MAP_OR_FAIL(aio_suspend);
  int fd = this->traced_aio_fd(list, nent, __FUNCTION__);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(nent, MetadataType::MT_VALUE);
  int ret = __real_aio_suspend(list, nent, timeout);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lio_listio(int mode, struct aiocb* const list[],
                                      int nent, struct sigevent* sig) {
  BRAHMA_MAP_OR_FAIL(lio_listio);
  int fd = this->traced_aio_fd(list, nent, __FUNCTION__);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(nent, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_lio_listio(mode, list, nent, sig);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_read64(struct aiocb64* cb) {
  BRAHMA_MAP_OR_FAIL(aio_read64);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  size_t aio_nbytes = cb != nullptr ? cb->aio_nbytes : 0;
  DFT_LOGGER_UPDATE_TYPE(aio_nbytes, MetadataType::MT_VALUE);
  off64_t aio_offset = cb != nullptr ? cb->aio_offset : 0;
  DFT_LOGGER_UPDATE_TYPE(aio_offset, MetadataType::MT_VALUE);
  int ret = __real_aio_read64(cb);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_write64(struct aiocb64* cb) {
  BRAHMA_MAP_OR_FAIL(aio_write64);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  size_t aio_nbytes = cb != nullptr ? cb->aio_nbytes : 0;
  DFT_LOGGER_UPDATE_TYPE(aio_nbytes, MetadataType::MT_VALUE);
  off64_t aio_offset = cb != nullptr ? cb->aio_offset : 0;
  DFT_LOGGER_UPDATE_TYPE(aio_offset, MetadataType::MT_VALUE);
  int ret = __real_aio_write64(cb);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_error64(const struct aiocb64* cb) {
  BRAHMA_MAP_OR_FAIL(aio_error64);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  int ret = __real_aio_error64(cb);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::POSIXDFTracer::aio_return64(struct aiocb64* cb) {
  BRAHMA_MAP_OR_FAIL(aio_return64);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  ssize_t ret = __real_aio_return64(cb);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_fsync64(int op, struct aiocb64* cb) {
  BRAHMA_MAP_OR_FAIL(aio_fsync64);
  int fd = cb != nullptr ? cb->aio_fildes : -1;
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(op, MetadataType::MT_VALUE);
  int ret = __real_aio_fsync64(op, cb);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_cancel64(int fd, struct aiocb64* cb) {
  BRAHMA_MAP_OR_FAIL(aio_cancel64);
  DFT_LOGGER_START(fd);
  int ret = __real_aio_cancel64(fd, cb);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::aio_suspend64(const struct aiocb64* const list[],
                                         int nent,
                                         const struct timespec* timeout) {
  BRAHMA_MAP_OR_FAIL(aio_suspend64);
  int fd = this->traced_aio_fd(list, nent, __FUNCTION__);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(nent, MetadataType::MT_VALUE);
  int ret = __real_aio_suspend64(list, nent, timeout);
  DFT_LOGGER_END();
  return ret;
}

int brahma::POSIXDFTracer::lio_listio64(int mode, struct aiocb64* const list[],
                                        int nent, struct sigevent* sig) {
  BRAHMA_MAP_OR_FAIL(lio_listio64);
  int fd = this->traced_aio_fd(list, nent, __FUNCTION__);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(nent, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  int ret = __real_lio_listio64(mode, list, nent, sig);
  DFT_LOGGER_END();
  return ret;
}
