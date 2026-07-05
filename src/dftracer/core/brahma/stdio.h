//
// Created by hariharan on 8/16/22.
//

#ifndef DFTRACER_STDIO_H
#define DFTRACER_STDIO_H

#include <brahma/brahma.h>
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/utils.h>
#include <fcntl.h>

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

  inline void trace(FILE* fh, HashType hash) {
    DFTRACER_LOG_DEBUG("Calling STDIODFTracer.trace with hash %s", hash);
    tracked_fh.insert_or_assign(fh, hash);
  }

  inline void remove_trace(FILE* fh) {
    DFTRACER_LOG_DEBUG("Calling STDIODFTracer.remove_trace with filename");
    tracked_fh.erase(fh);
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
};

}  // namespace brahma
#endif  // DFTRACER_STDIO_H
