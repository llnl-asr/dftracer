#ifndef DFTRACER_SERVICE_PIPELINE_NODE_TRACE_FILE_H
#define DFTRACER_SERVICE_PIPELINE_NODE_TRACE_FILE_H

#include <dftracer/core/buffer/chunk_sink.h>
#include <dftracer/core/compression/gzip_compression.h>
#include <fcntl.h>
#include <libdeflate.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dftracer {

/**
 * The node's trace, written by every drain thread at once. Each write reserves
 * its range under a short lock (every write is a whole chunk) and then
 * pwrite()s into it without one, so writers never wait on each other's I/O.
 * Every chunk is a complete gzip member, and members concatenated in any order
 * are a valid .pfw.gz.
 *
 * Once a file holds `rollover_bytes` the next write opens the next file,
 * <base>-1.pfw.gz, -2, ... next to the first (<base>.pfw.gz), so no member
 * ever spans two files and a reader on a parallel filesystem can split a
 * node's trace across files.
 */
class NodeTraceFile {
 public:
  ~NodeTraceFile() { close(); }

  bool open(const std::string& path, uint64_t rollover_bytes = 0) {
    rollover_ = rollover_bytes;
    base_ = path;
    const std::string ext = ".pfw.gz";
    if (base_.size() > ext.size() &&
        base_.compare(base_.size() - ext.size(), ext.size(), ext) == 0)
      base_.resize(base_.size() - ext.size());
    std::lock_guard<std::mutex> lock(mtx_);
    return open_file(path);
  }

  bool is_open() const { return fd_ >= 0; }
  /** The first file; paths() lists every file written. */
  const std::string& path() const { return path_; }
  const std::vector<std::string>& paths() const { return paths_; }

  /** Write `len` bytes as one unit. Thread-safe. */
  bool write(const char* data, size_t len) {
    if (len == 0) return fd_ >= 0;
    int fd;
    off_t offset;
    if (!reserve(len, fd, offset)) return false;
    return write_at(fd, data, len, offset);
  }

  /**
   * Append the complete gzip members at the start of `file` (a producer's
   * spill file), as one unit. A member cut short (its producer died while
   * writing it) and anything after it are left out. Returns the bytes added.
   */
  uint64_t append_members(const std::string& file) {
    int in = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) return 0;
    struct stat st;
    std::vector<char> data;
    if (fstat(in, &st) == 0) data.resize(static_cast<size_t>(st.st_size));
    size_t got = 0;
    while (got < data.size()) {
      const ssize_t n = ::read(in, data.data() + got, data.size() - got);
      if (n <= 0) break;
      got += static_cast<size_t>(n);
    }
    ::close(in);
    const size_t complete = complete_members(data.data(), got);
    if (complete == 0) return 0;
    int fd;
    off_t offset;
    if (!reserve(complete, fd, offset)) return 0;
    return write_at(fd, data.data(), complete, offset) ? complete : 0;
  }

  /** After the last write: make it durable and close. */
  void close() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (int fd : fds_) {
      ::fsync(fd);
      ::close(fd);
    }
    fds_.clear();
    fd_ = -1;
  }

  uint64_t bytes() const { return total_.load(std::memory_order_relaxed); }

 private:
  bool open_file(const std::string& path) {
    const int fd =
        ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
      std::fprintf(stderr, "[DFTRACER] cannot create node trace %s: %s\n",
                   path.c_str(), std::strerror(errno));
      return false;
    }
    // Earlier files stay open: a write reserved in one may still be running.
    fds_.push_back(fd);
    fd_ = fd;
    size_ = 0;
    if (paths_.empty()) path_ = path;
    paths_.push_back(path);
    return true;
  }

  bool reserve(size_t len, int& fd, off_t& offset) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (fd_ < 0) return false;
    if (rollover_ != 0 && size_ != 0 && size_ + len > rollover_ &&
        !open_file(base_ + "-" + std::to_string(paths_.size()) + ".pfw.gz"))
      return false;
    fd = fd_;
    offset = static_cast<off_t>(size_);
    size_ += len;
    total_.fetch_add(len, std::memory_order_relaxed);
    return true;
  }

  bool write_at(int fd, const char* data, size_t len, off_t offset) {
    while (len > 0) {
      const ssize_t n = ::pwrite(fd, data, len, offset);
      if (n < 0) {
        if (errno == EINTR) continue;
        std::fprintf(stderr, "[DFTRACER] write to node trace %s failed: %s\n",
                     path_.c_str(), std::strerror(errno));
        return false;
      }
      data += n;
      len -= static_cast<size_t>(n);
      offset += n;
    }
    return true;
  }

  /** Length of the leading run of complete, valid gzip members in data. */
  static size_t complete_members(const char* data, size_t len) {
    libdeflate_decompressor* d = libdeflate_alloc_decompressor();
    if (d == nullptr) return 0;
    std::vector<char> out(64u << 20);
    size_t done = 0;
    while (done < len) {
      size_t in_used = 0, out_used = 0;
      libdeflate_result r =
          libdeflate_gzip_decompress_ex(d, data + done, len - done, out.data(),
                                        out.size(), &in_used, &out_used);
      if (r == LIBDEFLATE_INSUFFICIENT_SPACE && out.size() < (1ull << 31)) {
        out.resize(out.size() * 2);
        continue;
      }
      if (r != LIBDEFLATE_SUCCESS || in_used == 0) break;
      done += in_used;
    }
    libdeflate_free_decompressor(d);
    return done;
  }

  std::mutex mtx_;  // guards the fields below but total_
  int fd_ = -1;
  std::vector<int> fds_;
  std::string base_;
  std::string path_;
  std::vector<std::string> paths_;
  uint64_t rollover_ = 0;
  uint64_t size_ = 0;  // of the current file
  std::atomic<uint64_t> total_{0};
};

/**
 * A drain thread's output chunk: the JSON lines of whatever producers it
 * replays, compressed into one gzip member and written to the node file when
 * it reaches `capacity`, gets old, or the thread goes idle. It is the
 * ChunkSink of the LocalBufferManager attached to it.
 */
class ChunkWriter : public ChunkSink {
 public:
  ChunkWriter(NodeTraceFile& file, size_t capacity, int level)
      : file_(file),
        capacity_(capacity),
        // Slack for the record, or the aggregates, that cross `capacity`.
        alloc_(capacity + (capacity < (1u << 20) ? (1u << 20) : capacity)),
        data_(new char[alloc_]) {
    gzip_.initialize(alloc_, level);
  }
  ~ChunkWriter() override { gzip_.finalize(); }

  char* data() { return data_.get(); }
  size_t used() const { return used_; }
  size_t capacity() const { return capacity_; }

  /** Record how much of the chunk is filled after a replay. */
  void set_used(size_t used, uint64_t now_ns) {
    if (used_ == 0 && used != 0) first_ns_ = now_ns;
    used_ = used;
  }

  /** Compress and write the first `len` bytes of `chunk` (== data()). */
  void flush(char* chunk, size_t len) override {
    json_bytes_ += len;
    const size_t out = gzip_.compress(chunk, len);
    if (out > 0 && file_.write(chunk, out)) {
      file_bytes_ += out;
      ++chunks_;
    }
    used_ = 0;
    first_ns_ = 0;
  }

  void flush() {
    if (used_ != 0) flush(data(), used_);
  }

  /** Flush if the oldest line in the chunk is at least `age_ns` old. */
  void flush_if_older(uint64_t now_ns, uint64_t age_ns) {
    if (used_ != 0 && now_ns - first_ns_ >= age_ns) flush();
  }

  uint64_t chunks() const { return chunks_; }
  uint64_t json_bytes() const { return json_bytes_; }
  uint64_t file_bytes() const { return file_bytes_; }

 private:
  NodeTraceFile& file_;
  size_t capacity_;
  size_t alloc_;
  std::unique_ptr<char[]> data_;
  GzipCompression gzip_;
  size_t used_ = 0;
  uint64_t first_ns_ = 0;
  uint64_t chunks_ = 0;
  uint64_t json_bytes_ = 0;
  uint64_t file_bytes_ = 0;
};

}  // namespace dftracer

#endif  // DFTRACER_SERVICE_PIPELINE_NODE_TRACE_FILE_H
