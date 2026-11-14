#ifndef DFTRACER_LOCAL_BUFFER_MANAGER_H
#define DFTRACER_LOCAL_BUFFER_MANAGER_H
#include <dftracer/core/buffer/buffer_manager.h>
#include <dftracer/core/buffer/compact_event.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/compression/gzip_compression.h>
//
#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/event_types.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/writer/stdio_writer.h>
#include <pthread.h>
#include <sched.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
namespace dftracer {
/**
 * A lock for data that one thread takes on almost every call and others
 * take rarely and briefly (a thread buffer): uncontended it is one atomic
 * exchange and one store, where std::mutex calls into pthread twice.
 */
class SpinMutex {
 public:
  void lock() {
    for (int spins = 0; locked_.exchange(true, std::memory_order_acquire);) {
      while (locked_.load(std::memory_order_relaxed)) {
        if (++spins < 64)
          pause();
        else
          sched_yield();
      }
    }
  }
  bool try_lock() { return !locked_.exchange(true, std::memory_order_acquire); }
  void unlock() { locked_.store(false, std::memory_order_release); }
  // After fork() the child's only thread owns nothing.
  void reset() { locked_.store(false, std::memory_order_relaxed); }

 private:
  static void pause() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
  }
  std::atomic<bool> locked_{false};
};

// The default BufferManager: this process serializes, aggregates, compresses
// and writes its own trace file. dftracer_service's per-thread state is
// LaneBuffer (lane_buffer.h).
class LocalBufferManager : public BufferManager {
 public:
  LocalBufferManager() : buffer(nullptr), buffer_pos(0), mtx() {}

  ~LocalBufferManager() override;

  int initialize(const char* filename, HashType hostname_hash) override;

  int finalize(int index, ProcessID process_id) override;

  // Compress and write the buffer and every open aggregate, then fflush.
  void flush() override;

  void log_data_event(int index, std::string_view event_name,
                      std::string_view category, TraceEventType type,
                      TimeResolution start_time, TimeResolution duration,
                      const dftracer::EventDraft* args, ProcessID process_id,
                      ThreadID tid) override;
  bool log_site_event(const dftracer::SiteEvent& e) override;

  // Metadata record whose args are the fields (see JsonLines::record).
  void log_record(ConstEventNameType record_name,
                  const dftracer::Metadata* fields, TraceEventType type,
                  ProcessID process_id, ThreadID tid) override;
  // The same with the fields already JSON (records from the ring).
  void log_record(ConstEventNameType record_name, const char* fields,
                  TraceEventType type, ProcessID process_id, ThreadID tid);
  void log_metadata_event(ConstEventNameType name, ConstEventNameType value,
                          ConstEventNameType record_name, TraceEventType type,
                          ProcessID process_id, ThreadID tid,
                          bool is_string = true) override;

  void log_counter_event(int index, ConstEventNameType name,
                         ConstEventNameType category, TraceEventType type,
                         TimeResolution start_time, ProcessID process_id,
                         ThreadID thread_id,
                         dftracer::Metadata* metadata) override;

  /** pthread_atfork handlers: no lock may be held across fork. */
  void lock_for_fork();
  void unlock_in_parent();
  void reset_in_child();

  struct Block {
    char* data;
    size_t size;
  };
  /**
   * The records of one thread, until they fill a block. Only its thread and
   * the flushes take `mtx`, so appending does not contend with other threads.
   *
   * A record is [u16 size | u8 RecordKind | payload]: a JSON line, or an
   * event (compact_event.h, or as the ring encodes it), which the writer turns
   * into its line or folds into `aggregator`. Full blocks wait in `pending`
   * and are rendered oldest first under `render_mtx`, since aggregation needs
   * the thread's events in order.
   */
  struct ThreadBuffer {
    SpinMutex mtx;
    char* data = nullptr;
    size_t pos = 0;
    bool in_use = false;        // registry_mtx
    std::deque<Block> pending;  // mtx
    compact::Encoder encoder;   // mtx; starts over with each block
    std::mutex render_mtx;
    compact::Decoder decoder;                          // render_mtx
    ShapeLines lines;                                  // render_mtx
    std::unique_ptr<dftracer::Aggregator> aggregator;  // render_mtx
  };
  /** At thread exit: queues what `tb` holds and frees it for a new thread.
   * Ignored when `epoch` is not the current one. */
  void release_thread_buffer(ThreadBuffer* tb, uint64_t epoch);

 private:
  void compress_and_write_if_needed(size_t size, bool force = false);
  // Serializes into thread-local scratch, then appends to the thread's buffer.
  template <class Serialize>
  void log_line(Serialize serialize);
  enum class RecordKind : uint8_t { LINE, EVENT, COMPACT };
  // Appends a record whose payload `fill(out, tb)` writes (at most
  // DFTRACER_SERIALIZATION_EVENT_MAX bytes) and returns the size of; 0 drops
  // it.
  template <class Fill>
  void append(RecordKind kind, Fill fill);
  // Appends an EVENT record that `encode(ByteWriter&)` writes; false when it
  // does not fit one record, which is then dropped.
  template <class Encode>
  bool append_ring_event(Encode encode);
  uint8_t event_flags(TraceEventType type) const {
    return static_cast<uint8_t>(
        include_metadata_flag |
        (event_type_aggregatable(type) ? 0 : SHM_EVENT_NO_AGGREGATE));
  }
  // Queues `tb`'s block for rendering. Called with tb->mtx held.
  void detach_locked(ThreadBuffer* tb);
  // Called with tb->mtx held: queues the open block when it holds records
  // (true), else takes it out of `tb` into `empty` (null when none).
  bool close_block_locked(ThreadBuffer* tb, char** empty);
  // Renders the oldest pending block of `tb` into JSON lines and writes them.
  void render_and_store(GzipCompression& compressor, ThreadBuffer& tb);
  // Renders `in` into JSON lines and hands each full block of them to
  // `flush(char*& data, size_t size)`. Takes `in.data` back to the free list.
  template <class Flush>
  void render_block(ThreadBuffer& tb, compact::Decoder& decoder, Block in,
                    Flush& flush);
  // Compresses `size` bytes of `block` in place; returns the member's size.
  static size_t compress_member(GzipCompression& compressor, char* block,
                                size_t size);
  void store_member(const char* member, size_t size);
  // Writes every thread's open aggregation intervals into the buffer.
  void drain_thread_aggregators(bool all, ProcessID process_id);
  void compress_and_store(GzipCompression& compressor, char* block,
                          size_t size);
  ThreadBuffer* thread_buffer();
  // Hands every thread's lines to the writer.
  void collect_thread_buffers();
  // Drops what the thread buffers and the queue hold and starts a new epoch.
  void discard_thread_buffers();
  char* take_block();
  void give_block(char* block);
  // Counts a block as in flight, before it leaves the lock that guards it, so
  // drain_writer() waits for it. Every reserve_block() is followed by submit().
  void reserve_block();
  // Queues the pending block of `tb` for the writer thread, or renders it on
  // the calling thread when the writer is behind.
  void submit(ThreadBuffer* tb);
  void write_inline(ThreadBuffer* tb);
  // Waits until every submitted block is written; with `stop`, ends the
  // writer thread too.
  void drain_writer(bool stop);
  void start_writer_locked();
  void run_writer();
  static void* writer_main(void* self);
  // Writes the complete packed intervals (or all) into the buffer.
  void drain_packed(Aggregator& aggregator, bool all, ProcessID process_id);
  char* buffer;
  size_t buffer_pos;
  std::shared_mutex mtx;
  // Held while a block is written (and compressed, for the shared buffer).
  std::mutex write_mtx;

  // Size of a thread's block: write_buffer_size, at most 1 MiB, since every
  // thread that logs holds one.
  size_t chunk_size = 0;
  std::atomic<uint64_t> epoch{1};
  std::mutex registry_mtx;
  std::vector<std::unique_ptr<ThreadBuffer>> thread_buffers;
  std::mutex free_mtx;
  std::vector<char*> free_blocks;
  std::mutex pool_mtx;
  std::vector<std::unique_ptr<GzipCompression>> compressor_pool;
  std::mutex queue_mtx;
  std::condition_variable queue_cv;
  std::condition_variable idle_cv;
  // The writer's jobs: the threads whose next pending block it renders.
  std::deque<ThreadBuffer*> queue;
  size_t busy = 0;  // reserved, or being rendered and written
  // The writer thread's compressor. A member, not a local of the thread, so a
  // fork child (where the thread is gone) can still free it.
  std::unique_ptr<GzipCompression> writer_compressor;
  bool writer_started = false;
  bool writer_stop = false;
  pthread_t writer_tid{};
  // The process the writer thread belongs to: a vfork child shares this
  // object and must not start or feed one.
  pid_t writer_pid = 0;

  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::shared_ptr<dftracer::JsonLines> serializer;
  std::shared_ptr<dftracer::GzipCompression> compressor;
  std::shared_ptr<dftracer::STDIOWriter> writer;
  uint8_t include_metadata_flag = 0;
};
}  // namespace dftracer
#endif  // DFTRACER_LOCAL_BUFFER_MANAGER_H