//
// Created by haridev on 7/2/26.
//

#ifndef DFTRACER_STDIO_BYPASS_H
#define DFTRACER_STDIO_BYPASS_H

#include <cstdio>

namespace dftracer {

// Real libc STDIO function pointers, resolved once by initialize() via
// dlopen(DFTRACER_LIBC_SO_PATH, RTLD_NOLOAD) + dlsym(handle, ...) -- NOT via
// GOTCHA/brahma's gotcha_get_wrappee(), and NOT via a plain `&::function`
// address-of expression.
//
// Why those two more obvious approaches don't work: STDIODFTracer
// intercepts these same symbol names via GOTCHA. When this library is
// loaded via LD_PRELOAD, gotcha_get_wrappee() was observed (via a live gdb
// backtrace on a hung process) to resolve back to GOTCHA's own generated
// wrapper trampoline for at least one of these symbols (flockfile), not to
// libc -- so caching "the real function" that way just caches the
// interceptor, causing infinite recursion the first time it's called.
// Capturing a plain `&::function` before GOTCHA binds anything doesn't help
// either: that's just the address of this library's own PLT stub for the
// symbol, which still indirects through this library's GOT *at call time*;
// GOTCHA's later bind<>() rewrites that GOT entry regardless of when the
// stub address was captured.
//
// dlopen()ing libc directly (RTLD_NOLOAD: it's already loaded in every
// process, this doesn't trigger a fresh load) and resolving symbols from
// THAT library's own symbol table via dlsym() sidesteps this library's
// PLT/GOT entirely, and is immune to GOTCHA's bind()/unbind() state.
//
// Used by dftracer's own internal STDIOWriter (so its I/O is never
// traced/intercepted, regardless of GOTCHA's binding state) AND by
// STDIODFTracer's flockfile/funlockfile/ftrylockfile/fflush interceptors
// themselves, as the "call the real function after logging" step, in place
// of GOTCHA's gotcha_get_wrappee()/__real_* -- see the comment above for
// why. Routing the interceptor bodies through here too means these four
// symbols can safely stay traced for application code, without reproducing
// the corruption.
class STDIOBypass {
 public:
  static STDIOBypass& get_instance();

  // Idempotent: only the first call actually resolves symbols. Must be
  // called exactly once, eagerly, from DFTracerCore::initialize() (see
  // dftracer_main.cpp) -- not lazily from get_instance(), which is a plain
  // Meyers singleton accessor with no extra checks, so every bypass_* call
  // site stays a direct function-pointer call. dlopen/dlsym are not
  // async-signal-safe, so resolving here, well before any caller might need
  // to invoke the bypass_* methods from a signal handler (e.g. a
  // SIGTERM-driven forced finalize/flush), is what makes those calls safe.
  void initialize();

  FILE* fopen(const char* path, const char* mode);
  int setvbuf(FILE* fp, char* buf, int mode, size_t size);
  void flockfile(FILE* fp);
  void funlockfile(FILE* fp);
  int ftrylockfile(FILE* fp);
  int fflush(FILE* fp);
  size_t fwrite(const void* ptr, size_t size, size_t count, FILE* fp);
  int fseek(FILE* fp, long offset, int whence);
  long ftell(FILE* fp);
  int fclose(FILE* fp);

 private:
  STDIOBypass() = default;
  STDIOBypass(const STDIOBypass&) = delete;
  STDIOBypass& operator=(const STDIOBypass&) = delete;

  bool initialized_ = false;

  using fopen_fn = FILE* (*)(const char*, const char*);
  using setvbuf_fn = int (*)(FILE*, char*, int, size_t);
  using flockfile_fn = void (*)(FILE*);
  using funlockfile_fn = void (*)(FILE*);
  using ftrylockfile_fn = int (*)(FILE*);
  using fflush_fn = int (*)(FILE*);
  using fwrite_fn = size_t (*)(const void*, size_t, size_t, FILE*);
  using fseek_fn = int (*)(FILE*, long, int);
  using ftell_fn = long (*)(FILE*);
  using fclose_fn = int (*)(FILE*);

  fopen_fn real_fopen_ = nullptr;
  setvbuf_fn real_setvbuf_ = nullptr;
  flockfile_fn real_flockfile_ = nullptr;
  funlockfile_fn real_funlockfile_ = nullptr;
  ftrylockfile_fn real_ftrylockfile_ = nullptr;
  fflush_fn real_fflush_ = nullptr;
  fwrite_fn real_fwrite_ = nullptr;
  fseek_fn real_fseek_ = nullptr;
  ftell_fn real_ftell_ = nullptr;
  fclose_fn real_fclose_ = nullptr;
};

}  // namespace dftracer

#endif  // DFTRACER_STDIO_BYPASS_H
