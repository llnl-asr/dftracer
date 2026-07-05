#pragma once
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/utils/posix_bypass.h>
#include <dftracer/core/utils/stdio_bypass.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace dftracer {
class STDIOWriter {
 public:
  STDIOWriter() : max_size_(0), fh_(nullptr) {}
  void initialize(const char* filename) {
    auto& posix_bypass = POSIXBypass::get_instance();
    posix_bypass.initialize();
    auto& stdio_bypass = STDIOBypass::get_instance();
    stdio_bypass.initialize();

    if (fh_ != nullptr) {
      // Re-invoked after fork: close the inherited fd via POSIXBypass (never
      // traced/intercepted) to avoid fflush/flockfile on a FILE* whose
      // internal mutex state was copied from the parent and may be
      // inconsistent in this child process.
      int fd = fileno(fh_);
      fh_ = nullptr;
      if (fd >= 0) posix_bypass.close(fd);
    }
    filename_ = (filename != nullptr) ? filename : "";
    auto conf =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    max_size_ = conf->write_buffer_size;
    fh_ = stdio_bypass.fopen(filename_.c_str(), "ab+");
    if (fh_ == nullptr) {
      DFTRACER_LOG_ERROR("unable to create log file %s: errno=%d (%s)",
                         filename_.c_str(), errno,
                         strerror(errno));  // GCOVR_EXCL_LINE
    } else {
      stdio_bypass.setvbuf(fh_, NULL, _IOLBF, max_size_ + 16 * 1024);
      DFTRACER_LOG_INFO("created log file %s", filename_.c_str());
    }
  }

  void initialize() {}

  ~STDIOWriter() {}
  void finalize(int index) {
    if (fh_ != nullptr) {
      DFTRACER_LOG_INFO("Finalizing STDIOWriter");
      auto& stdio_bypass = STDIOBypass::get_instance();
      stdio_bypass.fflush(fh_);
      long file_size = 0;
      if (fh_ != nullptr) {
        stdio_bypass.fseek(fh_, 0, SEEK_END);
        file_size = stdio_bypass.ftell(fh_);
        stdio_bypass.fseek(fh_, 0, SEEK_SET);
      }
      int status = stdio_bypass.fclose(fh_);
      if ((index < 5 || file_size == 0) && !filename_.empty()) {
        auto& posix_bypass = POSIXBypass::get_instance();
        posix_bypass.initialize();
        posix_bypass.unlink(filename_.c_str());
      }
      if (status != 0) {
        DFTRACER_LOG_ERROR("unable to close log file %s",
                           filename_.c_str());  // GCOVR_EXCL_LINE
      }
      fh_ = nullptr;
    }
  }

  // Write data to buffer, flush if necessary
  size_t write(const char* data, size_t len, bool force = false) {
    if (fh_ != nullptr && (force || len >= max_size_)) {
      // Use stdio file locking (flockfile/funlockfile) for FILE*
      // needed for fork and spawn cases to maintain consistency
      // Note this may not work with nfs and should typically either create a
      // new file per fork or use a local filesystem which supports flockfile.
      auto& stdio_bypass = STDIOBypass::get_instance();
      stdio_bypass.flockfile(fh_);
      auto written = stdio_bypass.fwrite(data, 1, len, fh_);
      stdio_bypass.funlockfile(fh_);
      if (written != len) {
        DFTRACER_LOG_ERROR("unable to write log file %s",
                           filename_.c_str());  // GCOVR_EXCL_LINE
      }
    }
    return len;
  }

 private:
  std::string filename_;
  size_t max_size_;
  FILE* fh_;
};
}  // namespace dftracer
