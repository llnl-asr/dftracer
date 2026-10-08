//
// Created by haridev on 7/2/26.
//

#include <dftracer/core/common/logging.h>
#include <dftracer/core/utils/stdio_bypass.h>
#include <dlfcn.h>

#include <cassert>
#include <dftracer/core/dftracer_config.hpp>

#ifndef DFTRACER_LIBC_SO_PATH
#define DFTRACER_LIBC_SO_PATH "libc.so.6"
#endif

namespace dftracer {

STDIOBypass& STDIOBypass::get_instance() {
  // No initialize() call here: initialization happens exactly once, eagerly,
  // from DFTracerCore::initialize() (see dftracer_main.cpp). This keeps
  // every call site (every bypass_* invocation on the write hot path) to
  // just this static-local access and a direct function-pointer call below,
  // with no redundant re-check.
  static STDIOBypass instance;
  return instance;
}

void STDIOBypass::initialize() {
  if (initialized_) return;
  DFTRACER_LOG_DEBUG("STDIOBypass::initialize");
  // libc is already loaded in every process; RTLD_NOLOAD means this just
  // returns a handle to the existing mapping, not a fresh load.
  void* libc = dlopen(DFTRACER_LIBC_SO_PATH, RTLD_LAZY | RTLD_NOLOAD);
  assert(libc != nullptr);
  real_fopen_ = reinterpret_cast<fopen_fn>(dlsym(libc, "fopen"));
  real_setvbuf_ = reinterpret_cast<setvbuf_fn>(dlsym(libc, "setvbuf"));
  real_flockfile_ = reinterpret_cast<flockfile_fn>(dlsym(libc, "flockfile"));
  real_funlockfile_ =
      reinterpret_cast<funlockfile_fn>(dlsym(libc, "funlockfile"));
  real_ftrylockfile_ =
      reinterpret_cast<ftrylockfile_fn>(dlsym(libc, "ftrylockfile"));
  real_fflush_ = reinterpret_cast<fflush_fn>(dlsym(libc, "fflush"));
  real_fwrite_ = reinterpret_cast<fwrite_fn>(dlsym(libc, "fwrite"));
  real_fseek_ = reinterpret_cast<fseek_fn>(dlsym(libc, "fseek"));
  real_ftell_ = reinterpret_cast<ftell_fn>(dlsym(libc, "ftell"));
  real_fclose_ = reinterpret_cast<fclose_fn>(dlsym(libc, "fclose"));
  real_fileno_ = reinterpret_cast<fileno_fn>(dlsym(libc, "fileno"));
  real_vfprintf_ = reinterpret_cast<vfprintf_fn>(dlsym(libc, "vfprintf"));
  real_vprintf_ = reinterpret_cast<vprintf_fn>(dlsym(libc, "vprintf"));
  real_vsprintf_ = reinterpret_cast<vsprintf_fn>(dlsym(libc, "vsprintf"));
  real_vsnprintf_ = reinterpret_cast<vsnprintf_fn>(dlsym(libc, "vsnprintf"));
  real_vfscanf_ = reinterpret_cast<vfscanf_fn>(dlsym(libc, "vfscanf"));
  real_vscanf_ = reinterpret_cast<vscanf_fn>(dlsym(libc, "vscanf"));
  real_vsscanf_ = reinterpret_cast<vsscanf_fn>(dlsym(libc, "vsscanf"));
  real_isoc99_vfscanf_ =
      reinterpret_cast<vfscanf_fn>(dlsym(libc, "__isoc99_vfscanf"));
  real_isoc23_vfscanf_ =
      reinterpret_cast<vfscanf_fn>(dlsym(libc, "__isoc23_vfscanf"));
  real_isoc23_vscanf_ =
      reinterpret_cast<vscanf_fn>(dlsym(libc, "__isoc23_vscanf"));
  real_isoc23_vsscanf_ =
      reinterpret_cast<vsscanf_fn>(dlsym(libc, "__isoc23_vsscanf"));
  assert(real_fopen_ != nullptr);
  assert(real_setvbuf_ != nullptr);
  assert(real_flockfile_ != nullptr);
  assert(real_funlockfile_ != nullptr);
  assert(real_ftrylockfile_ != nullptr);
  assert(real_fflush_ != nullptr);
  assert(real_fwrite_ != nullptr);
  assert(real_fseek_ != nullptr);
  assert(real_ftell_ != nullptr);
  assert(real_fclose_ != nullptr);
  assert(real_fileno_ != nullptr);
  assert(real_vfprintf_ != nullptr);
  assert(real_vprintf_ != nullptr);
  assert(real_vsprintf_ != nullptr);
  assert(real_vsnprintf_ != nullptr);
  assert(real_vfscanf_ != nullptr);
  assert(real_vscanf_ != nullptr);
  assert(real_vsscanf_ != nullptr);
  if (real_isoc99_vfscanf_ == nullptr) real_isoc99_vfscanf_ = real_vfscanf_;
  if (real_isoc23_vfscanf_ == nullptr) real_isoc23_vfscanf_ = real_vfscanf_;
  if (real_isoc23_vscanf_ == nullptr) real_isoc23_vscanf_ = real_vscanf_;
  if (real_isoc23_vsscanf_ == nullptr) real_isoc23_vsscanf_ = real_vsscanf_;
  initialized_ = true;
}

FILE* STDIOBypass::fopen(const char* path, const char* mode) {
  return real_fopen_(path, mode);
}

int STDIOBypass::setvbuf(FILE* fp, char* buf, int mode, size_t size) {
  return real_setvbuf_(fp, buf, mode, size);
}

void STDIOBypass::flockfile(FILE* fp) { real_flockfile_(fp); }

void STDIOBypass::funlockfile(FILE* fp) { real_funlockfile_(fp); }

int STDIOBypass::ftrylockfile(FILE* fp) { return real_ftrylockfile_(fp); }

int STDIOBypass::fflush(FILE* fp) { return real_fflush_(fp); }

size_t STDIOBypass::fwrite(const void* ptr, size_t size, size_t count,
                           FILE* fp) {
  return real_fwrite_(ptr, size, count, fp);
}

int STDIOBypass::fseek(FILE* fp, long offset, int whence) {
  return real_fseek_(fp, offset, whence);
}

long STDIOBypass::ftell(FILE* fp) { return real_ftell_(fp); }

int STDIOBypass::fclose(FILE* fp) { return real_fclose_(fp); }

int STDIOBypass::fileno(FILE* fp) { return real_fileno_(fp); }

int STDIOBypass::vfprintf(FILE* stream, const char* format, va_list args) {
  return real_vfprintf_(stream, format, args);
}

int STDIOBypass::vprintf(const char* format, va_list args) {
  return real_vprintf_(format, args);
}

int STDIOBypass::vsprintf(char* str, const char* format, va_list args) {
  return real_vsprintf_(str, format, args);
}

int STDIOBypass::vsnprintf(char* str, size_t size, const char* format,
                           va_list args) {
  return real_vsnprintf_(str, size, format, args);
}

int STDIOBypass::vfscanf(FILE* stream, const char* format, va_list args) {
  return real_vfscanf_(stream, format, args);
}

int STDIOBypass::vscanf(const char* format, va_list args) {
  return real_vscanf_(format, args);
}

int STDIOBypass::vsscanf(const char* str, const char* format, va_list args) {
  return real_vsscanf_(str, format, args);
}

int STDIOBypass::__isoc99_vfscanf(FILE* stream, const char* format,
                                  va_list args) {
  return real_isoc99_vfscanf_(stream, format, args);
}

int STDIOBypass::__isoc23_vfscanf(FILE* stream, const char* format,
                                  va_list args) {
  return real_isoc23_vfscanf_(stream, format, args);
}

int STDIOBypass::__isoc23_vscanf(const char* format, va_list args) {
  return real_isoc23_vscanf_(format, args);
}

int STDIOBypass::__isoc23_vsscanf(const char* str, const char* format,
                                  va_list args) {
  return real_isoc23_vsscanf_(str, format, args);
}

}  // namespace dftracer
