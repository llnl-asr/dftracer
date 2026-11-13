//
// Created by haridev on 7/2/26.
//

#include <dftracer/core/common/logging.h>
#include <dftracer/core/utils/posix_bypass.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <syscall.h>

#include <atomic>
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
  real_getcwd_ = reinterpret_cast<getcwd_fn>(dlsym(libc, "getcwd"));
  real_dirfd_ = reinterpret_cast<dirfd_fn>(dlsym(libc, "dirfd"));
  real_access_ = reinterpret_cast<access_fn>(dlsym(libc, "access"));
  assert(real_open_ != nullptr);
  assert(real_close_ != nullptr);
  assert(real_read_ != nullptr);
  assert(real_write_ != nullptr);
  assert(real_unlink_ != nullptr);
  assert(real_fsync_ != nullptr);
  assert(real_readlink_ != nullptr);
  assert(real_getcwd_ != nullptr);
  assert(real_dirfd_ != nullptr);
  assert(real_access_ != nullptr);
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

char* POSIXBypass::getcwd(char* buf, size_t size) {
  return real_getcwd_(buf, size);
}

int POSIXBypass::dirfd(DIR* dir) { return real_dirfd_(dir); }

int POSIXBypass::access(const char* pathname, int mode) {
  if (!initialized_) initialize();
  return real_access_(pathname, mode);
}

}  // namespace dftracer

// getpid/gettid intentionally stay as raw syscalls -- see the comment in
// posix_bypass.h for why. A thread's id never changes, so gettid is asked
// once per thread and kept; only fork() gives the (one) surviving thread a
// new id, so the child forgets it. The default TLS model, not initial-exec:
// initial-exec would put this library's whole TLS block in the static TLS
// reserve, which a library loaded with dlopen (Python's import dftracer)
// cannot get: "cannot allocate memory in static TLS block".
namespace {
thread_local ThreadID tl_tid = 0;
std::atomic<ProcessID> owner_pid{static_cast<ProcessID>(syscall(SYS_getpid))};
void forget_tid_in_child() {
  tl_tid = 0;
  owner_pid.store(static_cast<ProcessID>(syscall(SYS_getpid)),
                  std::memory_order_relaxed);
}
const int tid_fork_hook = pthread_atfork(nullptr, nullptr, forget_tid_in_child);
}  // namespace

ThreadID df_gettid() {
  ThreadID tid = tl_tid;
  if (__builtin_expect(tid == 0, 0)) {
    tid = static_cast<ThreadID>(syscall(SYS_gettid));
    // A vfork child shares the parent thread's cache.
    if (syscall(SYS_getpid) == owner_pid.load(std::memory_order_relaxed))
      tl_tid = tid;
  }
  return tid;
}

ProcessID df_getpid() { return syscall(SYS_getpid); }
