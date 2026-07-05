//
// Created by haridev on 7/2/26.
//

#ifndef DFTRACER_POSIX_BYPASS_H
#define DFTRACER_POSIX_BYPASS_H

#include <dftracer/core/common/typedef.h>
#include <sys/types.h>
#include <unistd.h>

namespace dftracer {

// Real libc POSIX I/O function pointers for the functions POSIXDFTracer
// actually intercepts (open/close/read/write/unlink/fsync/readlink),
// resolved once by initialize() via dlopen(DFTRACER_LIBC_SO_PATH,
// RTLD_NOLOAD) + dlsym(handle, ...) -- the same mechanism as, and for the
// same reasons as, dftracer::STDIOBypass (see stdio_bypass.h): GOTCHA's
// gotcha_get_wrappee()/__real_* cannot be trusted to resolve to true libc
// for symbols this library intercepts when loaded via LD_PRELOAD, and a
// plain `&::function` doesn't help either since it's just this library's
// own PLT stub, which still indirects through this library's GOT that
// GOTCHA rewrites.
//
// Used by dftracer's own internal STDIOWriter so its I/O is never
// traced/intercepted, and by POSIXDFTracer's own interceptor bodies for
// these symbols as their "call the real function after logging" step, in
// place of GOTCHA's gotcha_get_wrappee()/__real_*.
class POSIXBypass {
 public:
  static POSIXBypass& get_instance();

  // Idempotent: only the first call actually resolves symbols. Must be
  // called exactly once, eagerly, from DFTracerCore::initialize() -- not
  // lazily from get_instance(), which is a plain Meyers singleton accessor
  // with no extra checks. See STDIOBypass::initialize() for the full
  // rationale (same reasoning applies here).
  void initialize();

  int open(const char* pathname, int flags, mode_t mode = 0);
  int close(int fd);
  ssize_t read(int fd, void* buf, size_t count);
  ssize_t write(int fd, const void* buf, size_t count);
  int unlink(const char* pathname);
  int fsync(int fd);
  ssize_t readlink(const char* path, char* buf, size_t bufsize);

 private:
  POSIXBypass() = default;
  POSIXBypass(const POSIXBypass&) = delete;
  POSIXBypass& operator=(const POSIXBypass&) = delete;

  bool initialized_ = false;

  using open_fn = int (*)(const char*, int, mode_t);
  using close_fn = int (*)(int);
  using read_fn = ssize_t (*)(int, void*, size_t);
  using write_fn = ssize_t (*)(int, const void*, size_t);
  using unlink_fn = int (*)(const char*);
  using fsync_fn = int (*)(int);
  using readlink_fn = ssize_t (*)(const char*, char*, size_t);

  open_fn real_open_ = nullptr;
  close_fn real_close_ = nullptr;
  read_fn real_read_ = nullptr;
  write_fn real_write_ = nullptr;
  unlink_fn real_unlink_ = nullptr;
  fsync_fn real_fsync_ = nullptr;
  readlink_fn real_readlink_ = nullptr;
};

}  // namespace dftracer

// Raw-syscall helpers for getpid/gettid: NOT part of the bypass mechanism
// above, and intentionally kept as direct syscalls rather than
// dlopen+dlsym-resolved libc calls. getpid/gettid are not intercepted by
// POSIXDFTracer at all (no GOTCHA corruption risk either way), and these
// are called on the hot logging path (once per trace event in
// df_logger.h), where a raw syscall is cheaper than an indirect call
// through a dlsym-resolved pointer.
ThreadID df_gettid();

ProcessID df_getpid();

#endif  // DFTRACER_POSIX_BYPASS_H
