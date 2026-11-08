#include <dftracer/core/buffer/local_buffer_manager.h>
#include <dftracer/core/common/event_types.h>
#include <dftracer/core/common/profiler.h>
#include <dftracer/core/serialization/packed_json.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <new>
template <>
std::shared_ptr<dftracer::LocalBufferManager>
    dftracer::Singleton<dftracer::LocalBufferManager>::instance = nullptr;
template <>
bool
    dftracer::Singleton<dftracer::LocalBufferManager>::stop_creating_instances =
        false;
namespace dftracer {

namespace {

thread_local char line_scratch[DFTRACER_SERIALIZATION_EVENT_MAX];

std::atomic<LocalBufferManager*> fork_owner{nullptr};

constexpr size_t kMaxThreadChunk = 1024 * 1024;
// Blocks waiting for the writer before a full thread compresses its own.
constexpr size_t kMaxQueued = 2;
constexpr size_t kMaxFreeBlocks = 8;

// The calling thread's buffer, and the epoch it was handed out in.
struct ThreadSlot {
  LocalBufferManager* owner = nullptr;
  uint64_t epoch = 0;
  LocalBufferManager::ThreadBuffer* tb = nullptr;
  ~ThreadSlot() {
    if (owner != nullptr && tb != nullptr &&
        fork_owner.load(std::memory_order_acquire) == owner)
      owner->release_thread_buffer(tb, epoch);
  }
};
thread_local ThreadSlot thread_slot;

// Registered at load so the child handler releases the locks before
// DFTracerCore's child handler logs again.
__attribute__((constructor)) void register_fork_handlers() {
  pthread_atfork(
      [] {
        auto* m = fork_owner.load(std::memory_order_acquire);
        if (m != nullptr) m->lock_for_fork();
      },
      [] {
        auto* m = fork_owner.load(std::memory_order_acquire);
        if (m != nullptr) m->unlock_in_parent();
      },
      [] {
        auto* m = fork_owner.load(std::memory_order_acquire);
        if (m != nullptr) m->reset_in_child();
      });
}

}  // namespace

LocalBufferManager::~LocalBufferManager() {
  LocalBufferManager* self = this;
  fork_owner.compare_exchange_strong(self, nullptr);
  drain_writer(true);
  for (char* block : free_blocks) free(block);
}

void LocalBufferManager::lock_for_fork() {
  mtx.lock();
  write_mtx.lock();
  registry_mtx.lock();
  queue_mtx.lock();
  free_mtx.lock();
  pool_mtx.lock();
  this->aggregator->lock_for_fork();
}

void LocalBufferManager::unlock_in_parent() {
  this->aggregator->unlock_in_parent();
  pool_mtx.unlock();
  free_mtx.unlock();
  queue_mtx.unlock();
  registry_mtx.unlock();
  write_mtx.unlock();
  mtx.unlock();
}

// pthread_rwlock_unlock matches the writer by tid, which is new in the child.
// The writer thread is not in the child, and what the parent had not written
// yet is the parent's.
void LocalBufferManager::reset_in_child() {
  new (&mtx) std::shared_mutex();
  new (&write_mtx) std::mutex();
  new (&registry_mtx) std::mutex();
  new (&queue_mtx) std::mutex();
  new (&free_mtx) std::mutex();
  new (&pool_mtx) std::mutex();
  new (&queue_cv) std::condition_variable();
  new (&idle_cv) std::condition_variable();
  for (auto& tb : thread_buffers) new (&tb->mtx) std::mutex();
  writer_started = false;
  writer_stop = false;
  busy = 0;
  writer_compressor.reset();
  discard_thread_buffers();
  this->aggregator->reset_in_child();
}

void LocalBufferManager::write_block(char* block, size_t size) {
  // Always one complete gzip member per write.
  {
    DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(COMPRESSION));
    size = this->compressor->compress(block, size);
  }
  if (size > 0) {
    DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(STORE));
    this->writer->write(block, size, true);
  }
}

// One complete gzip member per block, so blocks of different threads may
// reach the file in any order.
void LocalBufferManager::compress_and_store(GzipCompression& compressor,
                                            char* block, size_t size) {
  {
    DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(COMPRESSION));
    size = compressor.compress(block, size);
  }
  if (size > 0) {
    DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(STORE));
    std::lock_guard<std::mutex> write_lock(write_mtx);
    this->writer->write(block, size, true);
  }
}

LocalBufferManager::ThreadBuffer* LocalBufferManager::thread_buffer() {
  ThreadSlot& slot = thread_slot;
  const uint64_t current = epoch.load(std::memory_order_acquire);
  if (slot.owner == this && slot.epoch == current) return slot.tb;
  std::lock_guard<std::mutex> lock(registry_mtx);
  ThreadBuffer* tb = nullptr;
  for (auto& candidate : thread_buffers) {
    if (!candidate->in_use) {
      tb = candidate.get();
      break;
    }
  }
  if (tb == nullptr) {
    thread_buffers.push_back(std::make_unique<ThreadBuffer>());
    tb = thread_buffers.back().get();
  }
  tb->in_use = true;
  slot.owner = this;
  slot.epoch = current;
  slot.tb = tb;
  return tb;
}

void LocalBufferManager::release_thread_buffer(ThreadBuffer* tb,
                                               uint64_t tb_epoch) {
  if (tb_epoch != epoch.load(std::memory_order_acquire)) return;
  char* data;
  size_t pos;
  {
    std::lock_guard<std::mutex> lock(tb->mtx);
    data = tb->data;
    pos = tb->pos;
    tb->data = nullptr;
    tb->pos = 0;
    if (pos > 0) reserve_block();
  }
  if (data != nullptr) {
    if (pos > 0)
      submit(data, pos);
    else
      give_block(data);
  }
  std::lock_guard<std::mutex> lock(registry_mtx);
  tb->in_use = false;
}

void LocalBufferManager::collect_thread_buffers() {
  std::vector<Block> blocks;
  {
    std::lock_guard<std::mutex> lock(registry_mtx);
    for (auto& tb : thread_buffers) {
      std::lock_guard<std::mutex> tb_lock(tb->mtx);
      if (tb->data == nullptr) continue;
      blocks.push_back({tb->data, tb->pos});
      tb->data = nullptr;
      if (tb->pos > 0) reserve_block();
      tb->pos = 0;
    }
  }
  for (const Block& block : blocks) {
    if (block.size > 0)
      submit(block.data, block.size);
    else
      give_block(block.data);
  }
}

void LocalBufferManager::discard_thread_buffers() {
  std::vector<char*> blocks;
  {
    std::lock_guard<std::mutex> lock(registry_mtx);
    epoch.fetch_add(1, std::memory_order_acq_rel);
    for (auto& tb : thread_buffers) {
      std::lock_guard<std::mutex> tb_lock(tb->mtx);
      if (tb->data != nullptr) blocks.push_back(tb->data);
      tb->data = nullptr;
      tb->pos = 0;
      tb->in_use = false;
    }
  }
  {
    std::lock_guard<std::mutex> lock(queue_mtx);
    for (const Block& block : queue) blocks.push_back(block.data);
    queue.clear();
    if (writer_block != nullptr) blocks.push_back(writer_block);
    writer_block = nullptr;
  }
  for (char* block : blocks) give_block(block);
}

char* LocalBufferManager::take_block() {
  {
    std::lock_guard<std::mutex> lock(free_mtx);
    if (!free_blocks.empty()) {
      char* block = free_blocks.back();
      free_blocks.pop_back();
      return block;
    }
  }
  return static_cast<char*>(
      malloc(chunk_size + DFTRACER_SERIALIZATION_EVENT_MAX));
}

void LocalBufferManager::give_block(char* block) {
  {
    std::lock_guard<std::mutex> lock(free_mtx);
    if (free_blocks.size() < kMaxFreeBlocks) {
      free_blocks.push_back(block);
      return;
    }
  }
  free(block);
}

void LocalBufferManager::reserve_block() {
  std::lock_guard<std::mutex> lock(queue_mtx);
  ++busy;
}

void LocalBufferManager::submit(char* block, size_t size) {
  {
    std::lock_guard<std::mutex> lock(queue_mtx);
    if (writer_stop) {
      if (--busy == 0 && queue.empty()) idle_cv.notify_all();
      give_block(block);
      return;
    }
    if (queue.size() < kMaxQueued && getpid() == writer_pid) {
      if (!writer_started && is_ready()) start_writer_locked();
      if (writer_started) {
        queue.push_back({block, size});
        --busy;
        queue_cv.notify_one();
        return;
      }
    }
  }
  write_inline(block, size);
  std::lock_guard<std::mutex> lock(queue_mtx);
  if (--busy == 0 && queue.empty()) idle_cv.notify_all();
}

void LocalBufferManager::write_inline(char* block, size_t size) {
  std::unique_ptr<GzipCompression> compressor_;
  {
    std::lock_guard<std::mutex> lock(pool_mtx);
    if (!compressor_pool.empty()) {
      compressor_ = std::move(compressor_pool.back());
      compressor_pool.pop_back();
    }
  }
  if (compressor_ == nullptr) {
    compressor_ = std::make_unique<GzipCompression>();
    compressor_->initialize(chunk_size + DFTRACER_SERIALIZATION_EVENT_MAX);
  }
  compress_and_store(*compressor_, block, size);
  give_block(block);
  std::lock_guard<std::mutex> lock(pool_mtx);
  compressor_pool.push_back(std::move(compressor_));
}

void LocalBufferManager::start_writer_locked() {
  // The writer takes no signals: they stay with the application's threads.
  if (writer_compressor == nullptr) {
    writer_compressor = std::make_unique<GzipCompression>();
    writer_compressor->initialize(chunk_size +
                                  DFTRACER_SERIALIZATION_EVENT_MAX);
  }
  sigset_t all, old;
  sigfillset(&all);
  pthread_sigmask(SIG_SETMASK, &all, &old);
  writer_started =
      pthread_create(&writer_tid, nullptr, &writer_main, this) == 0;
  pthread_sigmask(SIG_SETMASK, &old, nullptr);
}

void* LocalBufferManager::writer_main(void* self) {
  static_cast<LocalBufferManager*>(self)->run_writer();
  return nullptr;
}

void LocalBufferManager::run_writer() {
  std::unique_lock<std::mutex> lock(queue_mtx);
  GzipCompression& own = *writer_compressor;
  for (;;) {
    queue_cv.wait(lock, [this] { return writer_stop || !queue.empty(); });
    if (queue.empty()) return;
    const Block block = queue.front();
    queue.pop_front();
    ++busy;
    writer_block = block.data;
    lock.unlock();
    compress_and_store(own, block.data, block.size);
    lock.lock();
    writer_block = nullptr;
    lock.unlock();
    give_block(block.data);
    lock.lock();
    if (--busy == 0 && queue.empty()) idle_cv.notify_all();
  }
}

void LocalBufferManager::drain_writer(bool stop) {
  std::unique_lock<std::mutex> lock(queue_mtx);
  idle_cv.wait(lock, [this] { return queue.empty() && busy == 0; });
  if (!stop) return;
  writer_stop = true;
  if (!writer_started) return;
  writer_started = false;
  const pthread_t tid = writer_tid;
  queue_cv.notify_all();
  lock.unlock();
  pthread_join(tid, nullptr);
  writer_compressor.reset();
}

void LocalBufferManager::append(const char* line, size_t size) {
  ThreadBuffer* tb = thread_buffer();
  char* full;
  size_t full_size;
  {
    std::lock_guard<std::mutex> lock(tb->mtx);
    if (tb->data == nullptr) {
      if (!is_ready()) return;
      tb->data = take_block();
      if (tb->data == nullptr) return;
    }
    std::memcpy(tb->data + tb->pos, line, size);
    tb->pos += size;
    if (tb->pos <= chunk_size) return;
    full = tb->data;
    full_size = tb->pos;
    tb->data = nullptr;
    tb->pos = 0;
    reserve_block();
  }
  submit(full, full_size);
}

template <class Serialize>
void LocalBufferManager::log_line(Serialize serialize) {
  if (attached_mode) {
    std::unique_lock<std::shared_mutex> lock(mtx);
    compress_and_write_if_needed(serialize(buffer + buffer_pos));
    return;
  }
  append(line_scratch, serialize(line_scratch));
}

void LocalBufferManager::attach(char* chunk, size_t used, size_t capacity_,
                                ChunkSink* sink_) {
  buffer = chunk;
  buffer_pos = used;
  capacity = capacity_;
  sink = sink_;
}

size_t LocalBufferManager::detach() {
  const size_t used = buffer_pos;
  buffer = nullptr;
  buffer_pos = 0;
  sink = nullptr;
  return used;
}

void LocalBufferManager::compress_and_write_if_needed(size_t size, bool force) {
  if (attached_mode) {
    if (buffer_pos + size > capacity && sink != nullptr) {
      sink->flush(buffer, buffer_pos + size);
      buffer_pos = 0;
    } else {
      buffer_pos += size;
    }
    return;
  }
  if (force || buffer_pos + size > this->config->write_buffer_size) {
    // On forced flush, serialize any remaining aggregated data
    if (force && this->config->aggregation_enable) {
      buffer_pos += size;
      size = 0;
      auto data = dftracer::AggregatedDataType();
      DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(AGGREGATION));
      if (this->aggregator->get_previous_aggregations(data, false))
        write_aggregations(data, 0, rank);
    }

    {
      std::lock_guard<std::mutex> write_lock(write_mtx);
      write_block(buffer, buffer_pos + size);
    }
    buffer_pos = 0;
  } else {
    buffer_pos += size;
    DFTRACER_LOG_DEBUG(
        "BufferManager.compress_and_write_if_needed buffer_pos %zu not writing",
        buffer_pos);
  }
}
int LocalBufferManager::initialize(const char* filename,
                                   HashType hostname_hash) {
  DFTRACER_LOG_DEBUG("BufferManager.initialize %s %s", filename, hostname_hash);
  this->config =
      dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
  if (attached_mode) {
    this->serializer->initialize(nullptr, hostname_hash);
    if (this->config->aggregation_enable)
      packed_aggregator = std::make_unique<PackedAggregator>(this->config);
    ready.store(true, std::memory_order_release);
    return 0;
  }
  const size_t buffer_bytes =
      this->config->write_buffer_size + DFTRACER_SERIALIZATION_EVENT_MAX;
  if (buffer == nullptr) buffer = (char*)malloc(buffer_bytes);
  buffer_pos = 0;
  if (buffer == nullptr)
    DFTRACER_LOG_ERROR("BufferManager.BufferManager Failed to allocate buffer");
  discard_thread_buffers();
  chunk_size = std::min(this->config->write_buffer_size, kMaxThreadChunk);
  {
    std::lock_guard<std::mutex> lock(queue_mtx);
    writer_stop = false;
    writer_pid = getpid();
  }
  this->writer = dftracer::Singleton<dftracer::STDIOWriter>::get_instance();
  this->writer->initialize(filename);
  this->serializer = dftracer::Singleton<dftracer::JsonLines>::get_instance();
  this->aggregator = dftracer::Singleton<dftracer::Aggregator>::get_instance();
  this->compressor =
      dftracer::Singleton<dftracer::GzipCompression>::get_instance();
  this->compressor->initialize(this->config->write_buffer_size);
  size_t size = this->serializer->initialize(buffer, hostname_hash);
  compress_and_write_if_needed(size);
  fork_owner.store(this, std::memory_order_release);
  // Everything this object needs (config, buffer, compressor, serializer) is
  // now wired. Only from here is it safe for an asynchronous producer to log.
  ready.store(true, std::memory_order_release);
  return 0;
}

int LocalBufferManager::finalize(int index, ProcessID process_id) {
  // Stop accepting events BEFORE tearing anything down, so a producer thread
  // dftracer does not own (rocprofiler's flush pool, the PAPI sampler) cannot
  // enter a half-dismantled manager.
  ready.store(false, std::memory_order_release);
  std::unique_lock<std::shared_mutex> lock(mtx);
  if (attached_mode) {
    // Only the aggregates still open; the chunk belongs to the caller.
    if (buffer != nullptr && this->config->aggregation_enable) {
      auto data = dftracer::AggregatedDataType();
      DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(AGGREGATION));
      this->aggregator->get_previous_aggregations(data, true);
      write_aggregations(data, index, process_id);
      this->aggregator->finalize();
      drain_packed(true, process_id);
    }
    return 0;
  }
  if (buffer) {
    size_t size = 0;
    collect_thread_buffers();
    if (this->config->aggregation_enable) {
      auto data = dftracer::AggregatedDataType();
      DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(AGGREGATION));
      this->aggregator->get_previous_aggregations(data, true);
      write_aggregations(data, index, process_id);
      this->aggregator->finalize();
    }
    compress_and_write_if_needed(size, true);
    drain_writer(true);
    discard_thread_buffers();

    this->compressor->finalize();
    {
      DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(STORE));
      this->writer->finalize(index);
    }
    free(buffer);
    buffer = nullptr;
    buffer_pos = 0;
    {
      std::lock_guard<std::mutex> lock(free_mtx);
      for (char* block : free_blocks) free(block);
      free_blocks.clear();
    }
    std::lock_guard<std::mutex> lock(pool_mtx);
    compressor_pool.clear();
  }
  return 0;
}

void LocalBufferManager::flush() {
  if (!is_ready() || attached_mode) return;
  collect_thread_buffers();
  std::unique_lock<std::shared_mutex> lock(mtx);
  if (buffer == nullptr) return;
  size_t size = 0;
  if (this->config->aggregation_enable) {
    // All intervals, the current one too: after a successful exec nothing
    // would write them. If exec fails, later events open fresh buckets.
    auto data = dftracer::AggregatedDataType();
    DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(AGGREGATION));
    if (this->aggregator->get_previous_aggregations(data, true) > 0)
      write_aggregations(data, 0, static_cast<ProcessID>(getpid()));
  }
  compress_and_write_if_needed(size, true);
  drain_writer(false);
  DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(STORE));
  this->writer->flush();
}

void LocalBufferManager::log_data_event(
    int index, ConstEventNameType event_name, ConstEventNameType category,
    TraceEventType type, TimeResolution start_time, TimeResolution duration,
    dftracer::Metadata* metadata, ProcessID process_id, ThreadID tid) {
  // Reject events outside [initialize(), finalize()) -- see is_ready().
  if (!is_ready()) return;
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(BUFFER));
  DFTRACER_LOG_DEBUG("BufferManager.log_data_event %d", index);
  auto trace_in_full = [&] {
    log_line([&](char* out) {
      return this->serializer->data(out, index, event_name, category, type,
                                    start_time, duration, metadata, process_id,
                                    tid);
    });
  };
  // An event type that is not aggregatable is always traced in full.
  if (!this->config->aggregation_enable || strcmp(category, "dftracer") == 0 ||
      !dftracer::event_type_aggregatable(type)) {
    trace_in_full();
    return;
  }
  // Counted once per event that goes through the aggregator; writing a
  // finished interval out (serialization, compression, store) nests in it.
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(AGGREGATION));
  auto aggregated_key =
      AggregatedKey{category, event_name, type,           start_time, duration,
                    tid,      metadata,   get_app_name(), &rank};
  if (this->config->aggregation_type ==
          AggregationType::AGGREGATION_TYPE_SELECTIVE &&
      !this->aggregator->should_aggregate(&aggregated_key)) {
    trace_in_full();
    return;
  }
  const bool interval_changed = this->aggregator->aggregate(aggregated_key);
  // aggregate() only reads additional_keys; it never stores or frees them.
  Metadata::release(metadata);
  if (!interval_changed) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  if (buffer == nullptr) return;
  auto data = dftracer::AggregatedDataType();
  this->aggregator->get_previous_aggregations(data, false);
  if (!data.empty()) write_aggregations(data, index, process_id);
}

void LocalBufferManager::log_counter_event(
    int index, ConstEventNameType name, ConstEventNameType category,
    TraceEventType type, TimeResolution start_time, ProcessID process_id,
    ThreadID thread_id, dftracer::Metadata* metadata) {
  // Reject events outside [initialize(), finalize()) -- see is_ready().
  if (!is_ready()) return;
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(BUFFER));
  DFTRACER_LOG_DEBUG("BufferManager.log_counter_event %d", index);
  log_line([&](char* out) {
    return this->serializer->counter(out, index, name, category, type,
                                     start_time, process_id, thread_id,
                                     metadata);
  });
}

void LocalBufferManager::log_record(ConstEventNameType record_name,
                                    const dftracer::Metadata* fields,
                                    TraceEventType type, ProcessID process_id,
                                    ThreadID tid) {
  if (!is_ready()) return;
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(BUFFER));
  log_line([&](char* out) {
    return this->serializer->record(out, record_name, fields, type, process_id,
                                    tid);
  });
}

void LocalBufferManager::log_record(ConstEventNameType record_name,
                                    const char* fields, TraceEventType type,
                                    ProcessID process_id, ThreadID tid) {
  if (!is_ready()) return;
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(BUFFER));
  log_line([&](char* out) {
    return this->serializer->record(out, record_name, fields, type, process_id,
                                    tid);
  });
}

void LocalBufferManager::log_metadata_event(ConstEventNameType name,
                                            ConstEventNameType value,
                                            ConstEventNameType record_name,
                                            TraceEventType type,
                                            ProcessID process_id, ThreadID tid,
                                            bool is_string) {
  // Reject events outside [initialize(), finalize()) -- see is_ready().
  if (!is_ready()) return;
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(BUFFER));
  DFTRACER_LOG_DEBUG("BufferManager.log_metadata_event %s", value);
  log_line([&](char* out) {
    return this->serializer->metadata(out, name, value, record_name, type,
                                      process_id, tid, is_string);
  });
}

void LocalBufferManager::log_packed_event(const ipc::DecodedEvent& event,
                                          bool counter) {
  if (!is_ready()) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  const HashType hostname_hash = this->serializer->hostname();
  if (counter) {
    compress_and_write_if_needed(
        packed_json::counter(buffer + buffer_pos, event, hostname_hash));
    return;
  }
  // As log_data_event: everything but dftracer's own events and kinds that
  // are never aggregated is, or under SELECTIVE what the rules pick.
  if (packed_aggregator != nullptr && event.category != "dftracer" &&
      (event.head.flags & SHM_EVENT_NO_AGGREGATE) == 0 &&
      (this->config->aggregation_type !=
           AggregationType::AGGREGATION_TYPE_SELECTIVE ||
       packed_aggregator->should_aggregate(event, get_app_name(), &rank))) {
    if (packed_aggregator->aggregate(event))
      drain_packed(false, event.head.process_id);
    return;
  }
  compress_and_write_if_needed(
      packed_json::data(buffer + buffer_pos, event, hostname_hash));
}

void LocalBufferManager::write_aggregations(dftracer::AggregatedDataType& data,
                                            int index, ProcessID process_id) {
  // Same shape as drain_packed: each row into the buffer, then the usual
  // fill check, so a long interval moves on to a fresh buffer.
  struct BufferRows : JsonLines::RowSink {
    explicit BufferRows(LocalBufferManager& m) : self(m) {}
    char* row() override { return self.buffer + self.buffer_pos; }
    void wrote(size_t size) override {
      self.compress_and_write_if_needed(size);
    }
    LocalBufferManager& self;
  } rows(*this);
  this->serializer->aggregated(rows, index, process_id, data);
}

void LocalBufferManager::drain_packed(bool all, ProcessID process_id) {
  if (packed_aggregator == nullptr) return;
  // One row at a time, each into the chunk, so a long drain moves on to a
  // fresh chunk instead of running past this one's slack.
  struct ChunkRows : PackedAggregator::RowSink {
    explicit ChunkRows(LocalBufferManager& m) : self(m) {}
    char* row() override { return self.buffer + self.buffer_pos; }
    void wrote(size_t size) override {
      self.compress_and_write_if_needed(size);
    }
    LocalBufferManager& self;
  } rows(*this);
  packed_aggregator->drain(all, process_id, this->serializer->hostname(), rows);
}
}  // namespace dftracer
