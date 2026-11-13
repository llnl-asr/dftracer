#ifndef DFTRACER_COMPRESSION_GZIP_COMPRESSION_H
#define DFTRACER_COMPRESSION_GZIP_COMPRESSION_H

#include <dftracer/core/common/logging.h>
#include <isa-l/igzip_lib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace dftracer {
/**
 * Compresses a buffer into one gzip member with ISA-L (igzip). Every call
 * produces one complete member, so members can be concatenated in any order
 * and decompressed independently.
 */
class GzipCompression {
 public:
  /**
   * A member ends a deflate block every this many input bytes, so a reader's
   * index (bit offset and window at a block end) can start inside a member:
   * a whole member in one block would make it decode up to 4 MiB per seek.
   */
  static constexpr size_t kBlockBytes = 256u << 10;

  GzipCompression() = default;
  GzipCompression(const GzipCompression&) = delete;
  GzipCompression& operator=(const GzipCompression&) = delete;
  ~GzipCompression() { finalize(); }

  // chunk_size: the largest input compress() is usually given. level: as zlib
  // counts it (0-9); ISA-L's level 0 for 0 and its level 1 for every other,
  // since its higher levels are much slower for little gain on trace text.
  int initialize(size_t chunk_size, int level = 1) {
    finalize();
    level_ = level <= 0 ? 0 : 1;
    level_buf_size_ =
        level_ == 0 ? ISAL_DEF_LVL0_DEFAULT : ISAL_DEF_LVL1_DEFAULT;
    level_buf_.reset(new (std::nothrow) uint8_t[level_buf_size_]);
    stream_.reset(new (std::nothrow) isal_zstream);
    if (level_buf_ == nullptr || stream_ == nullptr) {
      DFTRACER_LOG_ERROR("Failed to initialize ISA-L (level %d)", level);
      finalize();
      return -1;
    }
    reserve(bound(chunk_size));
    return 0;
  }

  // Compresses data_size bytes of buffer in place and returns the compressed
  // size, or 0 on failure (nothing is written then). The member is copied
  // back into `buffer`, which must have room for it: bound(data_size) bytes
  // for data that does not compress.
  size_t compress(char* buffer, size_t data_size) {
    if (stream_ == nullptr) return 0;
    const size_t need = bound(data_size);
    if (need > out_size_) reserve(need);
    isal_zstream& s = *stream_;
    isal_deflate_init(&s);
    s.gzip_flag = IGZIP_GZIP;
    s.level = level_;
    s.level_buf = level_buf_.get();
    s.level_buf_size = level_buf_size_;
    s.next_out = out_.get();
    s.avail_out = static_cast<uint32_t>(out_size_);
    size_t at = 0;
    do {
      const size_t n = std::min(kBlockBytes, data_size - at);
      s.next_in = reinterpret_cast<uint8_t*>(buffer + at);
      s.avail_in = static_cast<uint32_t>(n);
      at += n;
      s.end_of_stream = at == data_size;
      s.flush = s.end_of_stream ? NO_FLUSH : SYNC_FLUSH;
      if (isal_deflate(&s) != COMP_OK || s.avail_in != 0) break;
    } while (at < data_size);
    if (s.internal_state.state != ZSTATE_END) {
      DFTRACER_LOG_ERROR("Compression of %zu bytes failed", data_size);
      return 0;
    }
    std::memcpy(buffer, out_.get(), s.total_out);
    return s.total_out;
  }

  int finalize() {
    stream_.reset();
    level_buf_.reset();
    return 0;
  }

 private:
  // Room for a member of data that does not compress: deflate's stored
  // blocks add 5 bytes per 64 KiB, each SYNC_FLUSH at most 5 more, and the
  // gzip header and trailer 18.
  static size_t bound(size_t n) {
    return n + (n / 65535 + n / kBlockBytes + 2) * 5 + 18 + 1024;
  }

  // Not zero-filled, so a page costs memory only once a member reaches it.
  void reserve(size_t size) {
    out_.reset(new uint8_t[size]);
    out_size_ = size;
  }

  std::unique_ptr<isal_zstream> stream_;
  std::unique_ptr<uint8_t[]> level_buf_;
  uint32_t level_buf_size_ = 0;
  uint32_t level_ = 1;
  std::unique_ptr<uint8_t[]> out_;
  size_t out_size_ = 0;
};
}  // namespace dftracer

#endif  // DFTRACER_COMPRESSION_GZIP_COMPRESSION_H
