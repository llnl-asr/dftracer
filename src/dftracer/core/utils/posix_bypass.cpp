//
// Created by haridev on 7/2/26.
//

#include <dftracer/core/common/logging.h>
#include <dftracer/core/utils/posix_bypass.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <syscall.h>

#include <cassert>
#include <dftracer/core/dftracer_config.hpp>

#ifndef DFTRACER_LIBC_SO_PATH
#define DFTRACER_LIBC_SO_PATH "libc.so.6"
#endif

namespace dftracer {

POSIXBypass& POSIXBypass::get_instance() {
  // No initialize() call here: initialization happens exactly once, eagerly,
  // from DFTracerCore::initialize() (see dftracer_main.cpp). Every call site
  // (the read/write/open/close/unlink hot paths) is then just this
  // static-local access and a direct function-pointer call below.
  static POSIXBypass instance;
  return instance;
}

void POSIXBypass::initialize() {
  if (initialized_) return;
  DFTRACER_LOG_DEBUG("POSIXBypass::initialize");
  void* libc = dlopen(DFTRACER_LIBC_SO_PATH, RTLD_LAZY | RTLD_NOLOAD);
  assert(libc != nullptr);
  real_open_ = reinterpret_cast<open_fn>(dlsym(libc, "open"));
  real_close_ = reinterpret_cast<close_fn>(dlsym(libc, "close"));
  real_read_ = reinterpret_cast<read_fn>(dlsym(libc, "read"));
  real_write_ = reinterpret_cast<write_fn>(dlsym(libc, "write"));
  real_unlink_ = reinterpret_cast<unlink_fn>(dlsym(libc, "unlink"));
  real_fsync_ = reinterpret_cast<fsync_fn>(dlsym(libc, "fsync"));
  real_readlink_ = reinterpret_cast<readlink_fn>(dlsym(libc, "readlink"));
  assert(real_open_ != nullptr);
  assert(real_close_ != nullptr);
  assert(real_read_ != nullptr);
  assert(real_write_ != nullptr);
  assert(real_unlink_ != nullptr);
  assert(real_fsync_ != nullptr);
  assert(real_readlink_ != nullptr);
  initialized_ = true;
}

int POSIXBypass::open(const char* pathname, int flags, mode_t mode) {
  return real_open_(pathname, flags, mode);
}

int POSIXBypass::close(int fd) { return real_close_(fd); }

ssize_t POSIXBypass::read(int fd, void* buf, size_t count) {
  return real_read_(fd, buf, count);
}

ssize_t POSIXBypass::write(int fd, const void* buf, size_t count) {
  return real_write_(fd, buf, count);
}

int POSIXBypass::unlink(const char* pathname) { return real_unlink_(pathname); }

int POSIXBypass::fsync(int fd) { return real_fsync_(fd); }

ssize_t POSIXBypass::readlink(const char* path, char* buf, size_t bufsize) {
  return real_readlink_(path, buf, bufsize);
}

}  // namespace dftracer

// getpid/gettid intentionally stay as raw syscalls -- see the comment in
// posix_bypass.h for why.
ThreadID df_gettid() { return syscall(SYS_gettid); }

ProcessID df_getpid() { return syscall(SYS_getpid); }
