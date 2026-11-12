#ifndef DFTRACER_COMPRESSION_GZIP_COMPRESSION_H
#define DFTRACER_COMPRESSION_GZIP_COMPRESSION_H

#include <dftracer/core/common/logging.h>
#include <libdeflate.h>

#include <cstring>
#include <memory>

namespace dftracer {
/**
 * Compresses a buffer into one gzip member with libdeflate. libdeflate is
 * one-shot (whole buffer in, whole member out), which is exactly how traces
 * are written: every call produces one complete member, so members can be
 * concatenated in any order and decompressed independently.
 */
class GzipCompression {
 public:
  GzipCompression() = default;
  GzipCompression(const GzipCompression&) = delete;
  GzipCompression& operator=(const GzipCompression&) = delete;
  ~GzipCompression() { finalize(); }

  // chunk_size: the largest input compress() is given. level: 0-12, as
  // libdeflate counts; 1-9 mean the same as in zlib.
  int initialize(size_t chunk_size, int level = 6) {
    finalize();
    compressor_ = libdeflate_alloc_compressor(level);
    if (compressor_ == nullptr) {
      DFTRACER_LOG_ERROR("Failed to initialize libdeflate (level %d)", level);
      return -1;
    }
    // Room for the whole member even if the data does not compress.
    reserve(libdeflate_gzip_compress_bound(compressor_, chunk_size));
    return 0;
  }

  // Compresses data_size bytes of buffer in place and returns the compressed
  // size, or 0 on failure (nothing is written then).
  size_t compress(char* buffer, size_t data_size) {
    if (compressor_ == nullptr) return 0;
    const size_t bound = libdeflate_gzip_compress_bound(compressor_, data_size);
    if (bound > out_size_) reserve(bound);
    const size_t compressed_size = libdeflate_gzip_compress(
        compressor_, buffer, data_size, out_.get(), out_size_);
    if (compressed_size == 0) {
      DFTRACER_LOG_ERROR("Compression of %zu bytes failed", data_size);
      return 0;
    }
    std::memcpy(buffer, out_.get(), compressed_size);
    return compressed_size;
  }

  int finalize() {
    if (compressor_ != nullptr) {
      libdeflate_free_compressor(compressor_);
      compressor_ = nullptr;
    }
    return 0;
  }

 private:
  // Not zero-filled, so a page costs memory only once a member reaches it.
  void reserve(size_t size) {
    out_.reset(new char[size]);
    out_size_ = size;
  }

  libdeflate_compressor* compressor_ = nullptr;
  std::unique_ptr<char[]> out_;
  size_t out_size_ = 0;
};
}  // namespace dftracer

#endif  // DFTRACER_COMPRESSION_GZIP_COMPRESSION_H
