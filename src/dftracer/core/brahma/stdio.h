//
// Created by hariharan on 8/16/22.
//

#ifndef DFTRACER_STDIO_H
#define DFTRACER_STDIO_H

#include <brahma/brahma.h>
#include <dftracer/core/brahma/posix.h>
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/stdio_bypass.h>
#include <dftracer/core/utils/utils.h>
#include <fcntl.h>
#include <features.h>

#include <cstdarg>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace brahma {
class STDIODFTracer : public STDIO {
 private:
  static bool stop_trace;
  static std::shared_ptr<STDIODFTracer> instance;
  static std::shared_ptr<STDIODFTracer>& instance_ref() {
    static auto* leaked_instance = new std::shared_ptr<STDIODFTracer>();
    return *leaked_instance;
  }
  std::unordered_map<FILE*, HashType> tracked_fh;
  std::shared_ptr<DFTLogger> logger;
  bool trace_all_files;

  inline HashType is_traced(FILE* fh, const char* func) {
    DFTRACER_LOG_DEBUG("Calling STDIODFTracer.is_traced for %s", func);
    if (stop_trace) return NO_HASH_DEFAULT;
    if (fh == NULL) return NO_HASH_DEFAULT;
    auto iter = tracked_fh.find(fh);
    if (iter != tracked_fh.end()) {
      return iter->second;
    }
    return NO_HASH_DEFAULT;
  }

  inline HashType is_traced(int fd, const char* func) {
    DFTRACER_LOG_DEBUG("Calling STDIODFTracer.is_traced with fd for %s", func);
    if (stop_trace) return NO_HASH_DEFAULT;
    auto posix = POSIXDFTracer::get_instance();
    return posix == nullptr ? NO_HASH_DEFAULT : posix->fd_hash(fd);
  }

  inline HashType is_traced(const char* filename, const char* func) {
    DFTRACER_LOG_DEBUG("Calling STDIODFTracer.is_traced with filename for %s",
                       func);
    if (stop_trace) return NO_HASH_DEFAULT;
    if (trace_all_files)
      return logger->hash_and_store(filename, METADATA_NAME_FILE_HASH);
    else {
      const char* trace_file = is_traced_common(filename, func);
      return logger->hash_and_store(trace_file, METADATA_NAME_FILE_HASH);
    }
  }

  // libc opens and closes the fd of a FILE* without the POSIX wrappers, so
  // the stream's fd is tracked here for write(fileno(fp)) and the like.
  inline void trace_stream_fd(FILE* fh, HashType hash) {
    auto posix = POSIXDFTracer::get_instance();
    if (posix == nullptr) return;
    posix->set_fd_hash(dftracer::STDIOBypass::get_instance().fileno(fh), hash);
  }

  inline void trace(FILE* fh, HashType hash) {
    DFTRACER_LOG_DEBUG("Calling STDIODFTracer.trace with hash %s", hash);
    if (fh == nullptr) return;
    tracked_fh.insert_or_assign(fh, hash);
    trace_stream_fd(fh, hash);
  }

  inline void remove_trace(FILE* fh) {
    DFTRACER_LOG_DEBUG("Calling STDIODFTracer.remove_trace with filename");
    tracked_fh.erase(fh);
    trace_stream_fd(fh, NO_HASH_DEFAULT);
  }

  // freopen with a null path keeps the file and only changes the mode.
  inline void trace_reopened(FILE* fh, const char* path, HashType old_hash) {
    HashType hash = path == nullptr ? old_hash : is_traced(path, "freopen");
    if (hash != NO_HASH_DEFAULT) trace(fh, hash);
  }

 public:
  STDIODFTracer(bool trace_all)
      : STDIO(), tracked_fh(), trace_all_files(trace_all) {
    DFTRACER_LOG_DEBUG("STDIO class intercepted");
    logger = DFT_LOGGER_INIT();
  }
  void finalize() {
    if (stop_trace) return;
    DFTRACER_LOG_DEBUG("Finalizing STDIODFTracer");
    stop_trace = true;
  }
  ~STDIODFTracer() {};

  static std::shared_ptr<STDIODFTracer> get_instance(bool trace_all = false) {
    DFTRACER_LOG_DEBUG("STDIO class get_instance");
    auto& instance = instance_ref();
    if (!stop_trace && instance == nullptr) {
      instance = std::make_shared<STDIODFTracer>(trace_all);
      STDIO::set_instance(instance);
    }
    return instance;
  }

  FILE* fopen(const char* path, const char* mode) override;

  FILE* fopen64(const char* path, const char* mode) override;

  int fclose(FILE* fp) override;

  int fileno(FILE* stream) override;

  size_t __fread_chk(void* ptr, size_t ptrlen, size_t size, size_t count,
                     FILE* fp) override;

  size_t __fread_unlocked_chk(void* ptr, size_t ptrlen, size_t size,
                              size_t count, FILE* fp) override;

  char* __fgets_chk(char* str, size_t buflen, int num, FILE* fp) override;

  char* __fgets_unlocked_chk(char* str, size_t buflen, int num,
                             FILE* fp) override;

  FILE* fdopen(int fd, const char* mode) override;

  FILE* tmpfile(void) override;

  FILE* tmpfile64(void) override;

  int fseeko(FILE* stream, off_t offset, int whence) override;

  int fseeko64(FILE* stream, off64_t offset, int whence) override;

  off_t ftello(FILE* stream) override;

  off64_t ftello64(FILE* stream) override;

  int fgetpos64(FILE* stream, fpos64_t* pos) override;

  int fsetpos64(FILE* stream, const fpos64_t* pos) override;

  size_t fread(void* ptr, size_t size, size_t count, FILE* fp) override;

  size_t fwrite(const void* ptr, size_t size, size_t count, FILE* fp) override;

  long ftell(FILE* fp) override;

  int fseek(FILE* fp, long offset, int whence) override;

  void clearerr(FILE*) override;

  int feof(FILE*) override;

  int ferror(FILE*) override;

  int fgetc(FILE*) override;

  int fgetpos(FILE*, fpos_t*) override;

  char* fgets(char*, int, FILE*) override;

  // flockfile/funlockfile/ftrylockfile/fflush: GOTCHA's generated
  // flockfile_wrapper (brahma/interface/stdio.h) was observed, via a live
  // gdb backtrace on a hung process, to resolve
  // gotcha_get_wrappee(flockfile_brahma_handle) back to itself instead of
  // libc when this library is loaded via LD_PRELOAD -- so the usual
  // BRAHMA_MAP_OR_FAIL/__real_* pattern used by every other override in
  // this class would cause unbounded recursive self-invocation for these
  // four. Their out-of-line definitions in stdio.cpp instead call
  // dftracer::STDIOBypass::get_instance() (stdio_bypass.h) for the "real
  // function" step, which resolves libc's actual functions via
  // dlopen(libc)+dlsym, independent of GOTCHA entirely -- so these stay
  // traced for application code without reproducing the corruption.
  void flockfile(FILE*) override;
  void funlockfile(FILE*) override;
  int ftrylockfile(FILE*) override;
  int fflush(FILE*) override;

  int fputc(int, FILE*) override;

  int fputs(const char*, FILE*) override;

  FILE* freopen(const char*, const char*, FILE*) override;

  int fsetpos(FILE*, const fpos_t*) override;

  int getc(FILE*) override;

  int getc_unlocked(FILE*) override;

  int getw(FILE*) override;

  int pclose(FILE*) override;

  int putw(int, FILE*) override;

  void rewind(FILE*) override;

  int setvbuf(FILE*, char*, int, size_t) override;

  int ungetc(int, FILE*) override;

  // --- New overrides mirroring brahma's expanded STDIO interception surface
  // ---

  FILE* freopen64(const char*, const char*, FILE*) override;

  // printf/scanf family: all call through dftracer::STDIOBypass for the
  // real-function step rather than BRAHMA_MAP_OR_FAIL/__real_* -- see the
  // comment on stdio_bypass.h's own printf/scanf declarations for why
  // (GOTCHA's variadic-wrapper dispatch and, for __isoc23_*, a glibc
  // 2.38+ redirect brahma itself needed extern "C" workarounds for, are
  // both exactly the kind of unusual symbol resolution where
  // gotcha_get_wrappee() is at risk of misbehaving, matching what was
  // already observed for flockfile/funlockfile/ftrylockfile/fflush).
  //
  // printf/sprintf/vsprintf/snprintf/vsnprintf, and (for consistency)
  // scanf/sscanf/vscanf/vsscanf and their __isoc23_* aliases, are
  // deliberately NOT overridden here (unlike the rest of this family):
  // they operate on stdin/an in-memory buffer rather than an arbitrary
  // file, and dftracer's own internal diagnostic logger (cpp-logger) calls
  // vsprintf directly to format its own log messages. If STDIODFTracer
  // traced these, every DFTRACER_LOG_DEBUG/ERROR call inside any
  // interceptor body (including this one) would recurse back into
  // cpp_logger_clog -> vsprintf -> this interceptor -> logging -> vsprintf
  // -> ... infinitely (observed as unbounded memory growth/hang in even
  // the simplest traced binary). Leaving these unoverridden means brahma's
  // own STDIO base class handles them (a plain passthrough, see
  // brahma/interface/stdio.cpp) -- GOTCHA still binds the symbols
  // (brahma's bind<>() is unconditional), but no tracing/logging happens
  // for them, so the recursion can't occur. Only the FILE*-targeting
  // members of this family (vfprintf/fscanf/vfscanf, which can point at a
  // real file, not just stdin/stdout/a buffer) stay traced.
  //
  // cpp-logger writes each log line with fprintf. The wrapper that sees it
  // only logs, and DFTracerLogReentryGuard in logging.h drops that nested
  // log, so tracing fprintf does not recurse.
  int fprintf(FILE* stream, const char* format, va_list args) override;
  int vfprintf(FILE* stream, const char* format, va_list args) override;
  int __fprintf_chk(FILE* stream, int flag, const char* format,
                    va_list args) override;
  int __vfprintf_chk(FILE* stream, int flag, const char* format,
                     va_list args) override;
  int dprintf(int fd, const char* format, va_list args) override;
  int vdprintf(int fd, const char* format, va_list args) override;
  int __dprintf_chk(int fd, int flag, const char* format,
                    va_list args) override;
  int __vdprintf_chk(int fd, int flag, const char* format,
                     va_list args) override;
  size_t fwrite_unlocked(const void* ptr, size_t size, size_t n,
                         FILE* stream) override;
  size_t fread_unlocked(void* ptr, size_t size, size_t n,
                        FILE* stream) override;
  int fputs_unlocked(const char* s, FILE* stream) override;
  char* fgets_unlocked(char* s, int n, FILE* stream) override;
  int fputc_unlocked(int c, FILE* stream) override;
  int fgetc_unlocked(FILE* stream) override;
  int fscanf(FILE* stream, const char* format, va_list args) override;
  int vfscanf(FILE* stream, const char* format, va_list args) override;
  int __isoc99_fscanf(FILE* stream, const char* format, va_list args) override;
  int __isoc99_vfscanf(FILE* stream, const char* format, va_list args) override;
  int __isoc23_fscanf(FILE* stream, const char* format, va_list args) override;
  int __isoc23_vfscanf(FILE* stream, const char* format, va_list args) override;

  int puts(const char* s) override;
  int putchar(int c) override;
  int putc(int c, FILE* stream) override;
  int putc_unlocked(int c, FILE* stream) override;
  int getchar(void) override;
  int getchar_unlocked(void) override;
  void perror(const char* s) override;
  void setbuf(FILE* stream, char* buf) override;
  void setbuffer(FILE* stream, char* buf, size_t size) override;
  void setlinebuf(FILE* stream) override;
  ssize_t getline(char** lineptr, size_t* n, FILE* stream) override;
  ssize_t getdelim(char** lineptr, size_t* n, int delim, FILE* stream) override;
  FILE* fmemopen(void* buf, size_t size, const char* mode) override;
  FILE* open_memstream(char** ptr, size_t* sizeloc) override;
  FILE* popen(const char* command, const char* type) override;
  char* tmpnam(char* s) override;

  int fcloseall(void) override;
  FILE* fopencookie(void* cookie, const char* mode,
                    cookie_io_functions_t funcs) override;
  FILE* open_wmemstream(wchar_t** ptr, size_t* sizeloc) override;
  int fflush_unlocked(FILE* stream) override;
  int feof_unlocked(FILE* stream) override;
  int ferror_unlocked(FILE* stream) override;
  int fileno_unlocked(FILE* stream) override;
  void clearerr_unlocked(FILE* stream) override;
  int _IO_getc(FILE* stream) override;
  int _IO_putc(int c, FILE* stream) override;
  wint_t fgetwc(FILE* stream) override;
  wint_t getwc(FILE* stream) override;
  wint_t fputwc(wchar_t wc, FILE* stream) override;
  wint_t putwc(wchar_t wc, FILE* stream) override;
  wint_t ungetwc(wint_t wc, FILE* stream) override;
  wchar_t* fgetws(wchar_t* ws, int n, FILE* stream) override;
  int fputws(const wchar_t* ws, FILE* stream) override;
  int fwprintf(FILE* stream, const wchar_t* format, va_list args) override;
  int vfwprintf(FILE* stream, const wchar_t* format, va_list args) override;
  int __fwprintf_chk(FILE* stream, int flag, const wchar_t* format,
                     va_list args) override;
  int __vfwprintf_chk(FILE* stream, int flag, const wchar_t* format,
                      va_list args) override;
  int fwscanf(FILE* stream, const wchar_t* format, va_list args) override;
  int vfwscanf(FILE* stream, const wchar_t* format, va_list args) override;
  int __isoc99_fwscanf(FILE* stream, const wchar_t* format,
                       va_list args) override;
  int __isoc99_vfwscanf(FILE* stream, const wchar_t* format,
                        va_list args) override;
  int __isoc23_fwscanf(FILE* stream, const wchar_t* format,
                       va_list args) override;
  int __isoc23_vfwscanf(FILE* stream, const wchar_t* format,
                        va_list args) override;
};

}  // namespace brahma
#endif  // DFTRACER_STDIO_H
