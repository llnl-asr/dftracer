#include <dftracer/core/buffer/local_buffer_manager.h>
#include <dftracer/core/common/event_draft.h>
#include <dftracer/core/common/event_types.h>
#include <dftracer/core/common/profiler.h>
#include <dftracer/core/common/tracer_scope.h>
#include <dftracer/core/ipc/event_codec.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <new>
#include <vector>
template <>
std::shared_ptr<dftracer::LocalBufferManager>
    dftracer::Singleton<dftracer::LocalBufferManager>::instance = nullptr;
template <>
bool
    dftracer::Singleton<dftracer::LocalBufferManager>::stop_creating_instances =
        false;
namespace dftracer {

namespace {

std::atomic<LocalBufferManager*> fork_owner{nullptr};

constexpr size_t kMaxThreadChunk = 1024 * 1024;
// Blocks waiting for the writer before a full thread compresses its own.
constexpr size_t kMaxQueued = 2;
constexpr size_t kMaxFreeBlocks = 8;
// A record's size and kind, ahead of its payload.
constexpr size_t kRecordHead = sizeof(uint16_t) + sizeof(uint8_t);
static_assert(DFTRACER_SERIALIZATION_EVENT_MAX <= 0xffff,
              "a record's size is a u16");
// Room past a block's chunk_size for the record that crosses it.
constexpr size_t kBlockSlack = DFTRACER_SERIALIZATION_EVENT_MAX + kRecordHead;
// Threads that render the deferred blocks at exit. With many ranks on a node
// the cores are busy already, and each one holds a compressor and a block.
constexpr size_t kMaxRenderThreads = 8;

size_t render_threads(size_t blocks) {
  cpu_set_t set;
  size_t cpus = 1;
  if (sched_getaffinity(0, sizeof(set), &set) == 0)
    cpus = static_cast<size_t>(CPU_COUNT(&set));
  return std::max<size_t>(1, std::min({blocks, cpus, kMaxRenderThreads}));
}

// The calling thread's buffer, and the epoch it was handed out in. Trivially
// destructible, so a lookup is one TLS read with no init guard;
// SlotReaper releases the buffer at thread exit.
struct ThreadSlot {
  LocalBufferManager* owner;
  uint64_t epoch;
  LocalBufferManager::ThreadBuffer* tb;
};
thread_local ThreadSlot thread_slot{};

struct SlotReaper {
  ~SlotReaper() {
    ThreadSlot& slot = thread_slot;
    if (slot.owner != nullptr && slot.tb != nullptr &&
        fork_owner.load(std::memory_order_acquire) == slot.owner)
      slot.owner->release_thread_buffer(slot.tb, slot.epoch);
    slot = ThreadSlot{};
  }
};
void arm_slot_reaper() {
  thread_local SlotReaper reaper;
  (void)reaper;
}

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

// Rendering holds a thread's render_mtx while it takes write_mtx and
// free_mtx, and appending holds its mtx while it takes queue_mtx and free_mtx.
void LocalBufferManager::lock_for_fork() {
  mtx.lock();
  registry_mtx.lock();
  for (auto& tb : thread_buffers) tb->render_mtx.lock();
  write_mtx.lock();
  for (auto& tb : thread_buffers) tb->mtx.lock();
  queue_mtx.lock();
  free_mtx.lock();
  pool_mtx.lock();
}

void LocalBufferManager::unlock_in_parent() {
  pool_mtx.unlock();
  free_mtx.unlock();
  queue_mtx.unlock();
  for (auto& tb : thread_buffers) tb->mtx.unlock();
  write_mtx.unlock();
  for (auto& tb : thread_buffers) tb->render_mtx.unlock();
  registry_mtx.unlock();
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
  for (auto& tb : thread_buffers) {
    tb->mtx.reset();
    new (&tb->render_mtx) std::mutex();
  }
  writer_started = false;
  writer_stop = false;
  busy = 0;
  deferred.clear();
  writer_compressor.reset();
  discard_thread_buffers();
}

// One complete gzip member per block, so blocks of different threads may
// reach the file in any order.
void LocalBufferManager::compress_and_store(GzipCompression& compressor,
                                            char* block, size_t size) {
  size = compress_member(compressor, block, size);
  if (size > 0) store_member(block, size);
}

size_t LocalBufferManager::compress_member(GzipCompression& compressor,
                                           char* block, size_t size) {
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(COMPRESSION));
  return compressor.compress(block, size);
}

void LocalBufferManager::store_member(const char* member, size_t size) {
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(STORE));
  std::lock_guard<std::mutex> write_lock(write_mtx);
  this->writer->write(member, size, true);
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
  arm_slot_reaper();
  slot.owner = this;
  slot.epoch = current;
  slot.tb = tb;
  return tb;
}

void LocalBufferManager::release_thread_buffer(ThreadBuffer* tb,
                                               uint64_t tb_epoch) {
  if (tb_epoch != epoch.load(std::memory_order_acquire)) return;
  char* empty;
  bool queued;
  {
    std::lock_guard<SpinMutex> lock(tb->mtx);
    queued = close_block_locked(tb, &empty);
  }
  if (queued) submit(tb);
  if (empty != nullptr) give_block(empty);
  std::lock_guard<std::mutex> lock(registry_mtx);
  tb->in_use = false;
}

void LocalBufferManager::detach_locked(ThreadBuffer* tb) {
  tb->pending.push_back({tb->data, tb->pos});
  tb->data = nullptr;
  tb->pos = 0;
  reserve_block();
}

bool LocalBufferManager::close_block_locked(ThreadBuffer* tb, char** empty) {
  *empty = nullptr;
  if (tb->data != nullptr && tb->pos > 0) {
    detach_locked(tb);
    return true;
  }
  *empty = tb->data;
  tb->data = nullptr;
  tb->pos = 0;
  return false;
}

void LocalBufferManager::collect_thread_buffers() {
  std::vector<ThreadBuffer*> queued;
  std::vector<char*> empty;
  {
    std::lock_guard<std::mutex> lock(registry_mtx);
    for (auto& tb : thread_buffers) {
      std::lock_guard<SpinMutex> tb_lock(tb->mtx);
      char* block;
      if (close_block_locked(tb.get(), &block))
        queued.push_back(tb.get());
      else if (block != nullptr)
        empty.push_back(block);
    }
  }
  for (ThreadBuffer* tb : queued) submit(tb);
  for (char* block : empty) give_block(block);
}

void LocalBufferManager::discard_thread_buffers() {
  std::vector<char*> blocks;
  {
    std::lock_guard<std::mutex> lock(registry_mtx);
    epoch.fetch_add(1, std::memory_order_acq_rel);
    for (auto& tb : thread_buffers) {
      std::lock_guard<SpinMutex> tb_lock(tb->mtx);
      if (tb->data != nullptr) blocks.push_back(tb->data);
      for (const Block& block : tb->pending) blocks.push_back(block.data);
      tb->pending.clear();
      tb->data = nullptr;
      tb->pos = 0;
      tb->in_use = false;
      tb->aggregator.reset();
    }
  }
  {
    std::lock_guard<std::mutex> lock(queue_mtx);
    queue.clear();
    busy -= deferred.size();
    deferred.clear();
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
  return static_cast<char*>(malloc(chunk_size + kBlockSlack));
}

void LocalBufferManager::prefault_block() {
  const size_t size = chunk_size + kBlockSlack;
  char* block = static_cast<char*>(malloc(size));
  if (block == nullptr) return;
  // Not sysconf(): it is intercepted and would be traced. A step at or below
  // the page size touches every page.
  constexpr size_t kPage = 4096;
  for (size_t at = 0; at < size; at += kPage)
    reinterpret_cast<volatile char*>(block)[at] = 0;
  give_block(block);
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

void LocalBufferManager::submit(ThreadBuffer* tb) {
  {
    std::lock_guard<std::mutex> lock(queue_mtx);
    if (!writer_stop && max_deferred > 0 && getpid() == writer_pid) {
      deferred.push_back(tb);
      if (deferred.size() <= max_deferred) {
        // The block the thread takes next was never touched; fault it in on
        // the writer thread, which is idle while blocks are deferred.
        if (!writer_started && is_ready()) start_writer_locked();
        if (writer_started) {
          queue.push_back(nullptr);
          queue_cv.notify_one();
        }
        return;
      }
      tb = deferred.front();
      deferred.pop_front();
    }
  }
  submit_now(tb);
}

void LocalBufferManager::release_deferred() {
  if (release_deferred_parallel()) return;
  for (;;) {
    ThreadBuffer* tb;
    {
      std::lock_guard<std::mutex> lock(queue_mtx);
      if (deferred.empty()) return;
      tb = deferred.front();
      deferred.pop_front();
    }
    submit_now(tb);
  }
}

void LocalBufferManager::submit_now(ThreadBuffer* tb) {
  {
    std::lock_guard<std::mutex> lock(queue_mtx);
    if (writer_stop) {
      if (--busy == 0 && queue.empty()) idle_cv.notify_all();
      char* block;
      {
        std::lock_guard<SpinMutex> tb_lock(tb->mtx);
        block = tb->pending.front().data;
        tb->pending.pop_front();
      }
      give_block(block);
      return;
    }
    if (queue.size() < kMaxQueued && getpid() == writer_pid) {
      if (!writer_started && is_ready()) start_writer_locked();
      if (writer_started) {
        queue.push_back(tb);
        --busy;
        queue_cv.notify_one();
        return;
      }
    }
  }
  write_inline(tb);
  std::lock_guard<std::mutex> lock(queue_mtx);
  if (--busy == 0 && queue.empty()) idle_cv.notify_all();
}

bool LocalBufferManager::release_deferred_parallel() {
  if (is_ready() || this->config->aggregation_enable) return false;
  struct Job {
    ThreadBuffer* tb;
    Block in;
    std::vector<Block> members;
  };
  std::vector<Job> jobs;
  {
    std::unique_lock<std::mutex> lock(queue_mtx);
    if (deferred.size() < 2 || writer_stop || getpid() != writer_pid)
      return false;
    // Blocks the writer holds are older than the deferred ones of their
    // thread, which take the next pending block: let it finish first.
    while (!queue.empty() || busy != deferred.size())
      idle_cv.wait_for(lock, std::chrono::milliseconds(1));
    for (ThreadBuffer* tb : deferred) jobs.push_back({tb, {}, {}});
    deferred.clear();
  }
  for (Job& job : jobs) {
    std::lock_guard<SpinMutex> lock(job.tb->mtx);
    job.in = job.tb->pending.front();
    job.tb->pending.pop_front();
  }
  std::atomic<size_t> next{0};
  // Catches everything: the helpers run it against this frame's locals, so
  // it must not unwind past the joins below.
  auto work = [&]() noexcept {
    try {
      GzipCompression compressor;
      compressor.initialize(chunk_size + kBlockSlack);
      compact::Decoder decoder;
      for (size_t i; (i = next.fetch_add(1)) < jobs.size();) {
        Job& job = jobs[i];
        auto keep = [this, &job, &compressor](char*& data, size_t size) {
          size = compress_member(compressor, data, size);
          if (size == 0) return;
          char* member = static_cast<char*>(malloc(size));
          if (member == nullptr) {
            // Out of order rather than lost.
            store_member(data, size);
            return;
          }
          std::memcpy(member, data, size);
          job.members.push_back({member, size});
        };
        render_block(*job.tb, decoder, job.in, keep);
      }
    } catch (const std::exception& e) {
      DFTRACER_LOG_ERROR("Rendering deferred blocks failed: %s", e.what());
    }
  };
  const size_t threads = render_threads(jobs.size());
  DFTRACER_LOG_DEBUG("Rendering %zu deferred blocks on %zu threads",
                     jobs.size(), threads);
  std::vector<pthread_t> helpers;
  {
    // Like the writer, the helpers take no signals.
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, &old);
    while (helpers.size() + 1 < threads) {
      pthread_t tid;
      if (pthread_create(
              &tid, nullptr,
              [](void* fn) -> void* {
                (*static_cast<decltype(work)*>(fn))();
                return nullptr;
              },
              &work) != 0)
        break;
      helpers.push_back(tid);
    }
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
  }
  work();
  for (pthread_t tid : helpers) pthread_join(tid, nullptr);
  for (Job& job : jobs) {
    for (const Block& member : job.members) {
      store_member(member.data, member.size);
      free(member.data);
    }
  }
  std::lock_guard<std::mutex> lock(queue_mtx);
  busy -= jobs.size();
  if (busy == 0 && queue.empty()) idle_cv.notify_all();
  return true;
}

void LocalBufferManager::render_and_store(GzipCompression& compressor,
                                          ThreadBuffer& tb) {
  std::lock_guard<std::mutex> render(tb.render_mtx);
  Block in;
  {
    std::lock_guard<SpinMutex> lock(tb.mtx);
    if (tb.pending.empty()) return;
    in = tb.pending.front();
    tb.pending.pop_front();
  }
  auto flush = [this, &compressor](char*& data, size_t size) {
    compress_and_store(compressor, data, size);
  };
  render_block(tb, tb.decoder, in, flush);
}

template <class Flush>
void LocalBufferManager::render_block(ThreadBuffer& tb,
                                      compact::Decoder& decoder, Block in,
                                      Flush& flush) {
  // JSON lines into a block of their own, flushed each time it fills.
  struct Lines : Aggregator::RowSink {
    Lines(LocalBufferManager& m, Flush& f)
        : self(m), flush_block(f), data(m.take_block()) {}
    char* row() override { return data + pos; }
    void wrote(size_t size) override {
      pos += size;
      if (pos > self.chunk_size) flush();
    }
    void flush() {
      if (pos > 0) flush_block(data, pos);
      pos = 0;
    }
    LocalBufferManager& self;
    Flush& flush_block;
    char* data;
    size_t pos = 0;
  } out(*this, flush);
  if (out.data == nullptr) {
    give_block(in.data);
    return;
  }
  const HashType hostname_hash = this->serializer->hostname();
  const bool aggregate = this->config->aggregation_enable;
  const bool selective = this->config->aggregation_type ==
                         AggregationType::AGGREGATION_TYPE_SELECTIVE;
  ipc::DecodedEvent event;
  decoder.reset();
  for (size_t at = 0; at + kRecordHead <= in.size;) {
    uint16_t size;
    std::memcpy(&size, in.data + at, sizeof(size));
    const auto kind = static_cast<RecordKind>(in.data[at + sizeof(size)]);
    const char* payload = in.data + at + kRecordHead;
    at += kRecordHead + size;
    if (kind == RecordKind::LINE) {
      std::memcpy(out.row(), payload, size);
      out.wrote(size);
      continue;
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(payload);
    if (kind == RecordKind::COMPACT ? !decoder.event(bytes, size, event)
                                    : !ipc::decode_event(bytes, size, event))
      continue;
    if (aggregate && (event.head.flags & SHM_EVENT_NO_AGGREGATE) == 0 &&
        event.category != "dftracer") {
      if (tb.aggregator == nullptr)
        tb.aggregator = std::make_unique<Aggregator>(this->config);
      if (!selective ||
          tb.aggregator->should_aggregate(event, get_app_name(), &rank)) {
        if (tb.aggregator->aggregate(event))
          tb.aggregator->drain(false, event.head.process_id, hostname_hash,
                               out);
        continue;
      }
    }
    out.wrote(this->serializer->data(out.row(), event));
  }
  out.flush();
  give_block(out.data);
  give_block(in.data);
}

void LocalBufferManager::drain_thread_aggregators(bool all,
                                                  ProcessID process_id) {
  std::vector<ThreadBuffer*> tbs;
  {
    std::lock_guard<std::mutex> lock(registry_mtx);
    for (auto& tb : thread_buffers) tbs.push_back(tb.get());
  }
  for (ThreadBuffer* tb : tbs) {
    std::lock_guard<std::mutex> render(tb->render_mtx);
    if (tb->aggregator != nullptr)
      drain_packed(*tb->aggregator, all, process_id);
  }
}

void LocalBufferManager::write_inline(ThreadBuffer* tb) {
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
    compressor_->initialize(chunk_size + kBlockSlack);
  }
  render_and_store(*compressor_, *tb);
  std::lock_guard<std::mutex> lock(pool_mtx);
  compressor_pool.push_back(std::move(compressor_));
}

void LocalBufferManager::start_writer_locked() {
  // The writer takes no signals: they stay with the application's threads.
  if (writer_compressor == nullptr) {
    writer_compressor = std::make_unique<GzipCompression>();
    writer_compressor->initialize(chunk_size + kBlockSlack);
  }
  sigset_t all, old;
  sigfillset(&all);
  pthread_sigmask(SIG_SETMASK, &all, &old);
  writer_started =
      pthread_create(&writer_tid, nullptr, &writer_main, this) == 0;
  pthread_sigmask(SIG_SETMASK, &old, nullptr);
}

void* LocalBufferManager::writer_main(void* self) {
  dftracer::TracerScope scope;
  static_cast<LocalBufferManager*>(self)->run_writer();
  return nullptr;
}

void LocalBufferManager::run_writer() {
  std::unique_lock<std::mutex> lock(queue_mtx);
  GzipCompression& own = *writer_compressor;
  for (;;) {
    queue_cv.wait(lock, [this] { return writer_stop || !queue.empty(); });
    if (queue.empty()) return;
    ThreadBuffer* tb = queue.front();
    queue.pop_front();
    ++busy;
    lock.unlock();
    if (tb != nullptr)
      render_and_store(own, *tb);
    else
      prefault_block();
    lock.lock();
    if (--busy == 0 && queue.empty()) idle_cv.notify_all();
  }
}

void LocalBufferManager::drain_writer(bool stop) {
  release_deferred();
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

template <class Fill>
void LocalBufferManager::append(RecordKind kind, Fill fill) {
  ThreadBuffer* tb = thread_buffer();
  {
    std::lock_guard<SpinMutex> lock(tb->mtx);
    if (tb->data == nullptr) {
      if (!is_ready()) return;
      tb->data = take_block();
      if (tb->data == nullptr) return;
      tb->encoder.reset();
    }
    char* at = tb->data + tb->pos;
    const size_t size = fill(at + kRecordHead, *tb);
    if (size == 0) return;
    const uint16_t n = static_cast<uint16_t>(size);
    std::memcpy(at, &n, sizeof(n));
    at[sizeof(n)] = static_cast<char>(kind);
    tb->pos += kRecordHead + size;
    if (tb->pos <= chunk_size) return;
    detach_locked(tb);
  }
  submit(tb);
}

template <class Encode>
bool LocalBufferManager::append_ring_event(Encode encode) {
  bool fits = true;
  append(RecordKind::EVENT, [&](char* out, ThreadBuffer&) -> size_t {
    thread_local std::vector<uint8_t> overflow;
    ipc::ByteWriter w(reinterpret_cast<uint8_t*>(out),
                      DFTRACER_SERIALIZATION_EVENT_MAX, overflow);
    encode(w);
    fits = !w.moved();
    return fits ? w.size() : 0;
  });
  return fits;
}

template <class Serialize>
void LocalBufferManager::log_line(Serialize serialize) {
  append(RecordKind::LINE,
         [&](char* out, ThreadBuffer&) { return serialize(out); });
}

void LocalBufferManager::compress_and_write_if_needed(size_t size, bool force) {
  if (force || buffer_pos + size > this->config->write_buffer_size) {
    compress_and_store(*this->compressor, buffer, buffer_pos + size);
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
  include_metadata_flag = this->config->metadata ? SHM_EVENT_INC_METADATA : 0;
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
    max_deferred =
        chunk_size == 0 ? 0 : this->config->defer_buffer_size / chunk_size;
    writer_stop = false;
    writer_pid = getpid();
  }
  this->writer = dftracer::Singleton<dftracer::STDIOWriter>::get_instance();
  this->writer->initialize(filename);
  this->serializer = dftracer::Singleton<dftracer::JsonLines>::get_instance();
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
  if (buffer) {
    size_t size = 0;
    collect_thread_buffers();
    drain_writer(false);
    if (this->config->aggregation_enable) {
      DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(AGGREGATION));
      drain_thread_aggregators(true, process_id);
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
  if (!is_ready()) return;
  collect_thread_buffers();
  std::unique_lock<std::shared_mutex> lock(mtx);
  if (buffer == nullptr) return;
  size_t size = 0;
  drain_writer(false);
  if (this->config->aggregation_enable) {
    // All intervals, the current one too: after a successful exec nothing
    // would write them. If exec fails, later events open fresh buckets.
    DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(AGGREGATION));
    drain_thread_aggregators(true, static_cast<ProcessID>(getpid()));
  }
  compress_and_write_if_needed(size, true);
  drain_writer(false);
  DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(STORE));
  this->writer->flush();
}

void LocalBufferManager::log_data_event(
    int index, std::string_view event_name, std::string_view category,
    TraceEventType type, TimeResolution start_time, TimeResolution duration,
    const dftracer::EventDraft* args, ProcessID process_id, ThreadID tid) {
  if (!is_ready()) return;
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(BUFFER));
  const uint8_t flags = static_cast<uint8_t>(
      event_flags(type) | (args != nullptr ? SHM_EVENT_HAS_METADATA : 0));
  if (compact::Encoder::bound(event_name, category, args) >
      DFTRACER_SERIALIZATION_EVENT_MAX) {
    const auto encode = [&](ipc::ByteWriter& w) {
      ipc::encode_event_entries(w, index, event_name, category, type,
                                start_time, duration, args, process_id, tid,
                                flags);
    };
    if (append_ring_event(encode)) return;
    // Too long for one record: the line, cut at the line limit.
    thread_local std::vector<uint8_t> encoded;
    thread_local ipc::DecodedEvent event;
    ipc::ByteWriter w(encoded);
    encode(w);
    if (!ipc::decode_event(encoded.data(), encoded.size(), event)) return;
    log_line([&](char* out) { return this->serializer->data(out, event); });
    return;
  }
  append(RecordKind::COMPACT, [&](char* out, ThreadBuffer& tb) -> size_t {
    return tb.encoder.event(reinterpret_cast<uint8_t*>(out), index, event_name,
                            category, type, start_time, duration, args,
                            process_id, tid, flags);
  });
}

void LocalBufferManager::log_counter_event(
    int index, ConstEventNameType name, ConstEventNameType category,
    TraceEventType type, TimeResolution start_time, ProcessID process_id,
    ThreadID thread_id, dftracer::Metadata* metadata) {
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
  if (!is_ready()) return;
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(BUFFER));
  DFTRACER_LOG_DEBUG("BufferManager.log_metadata_event %s", value);
  log_line([&](char* out) {
    return this->serializer->metadata(out, name, value, record_name, type,
                                      process_id, tid, is_string);
  });
}

void LocalBufferManager::drain_packed(Aggregator& aggregator, bool all,
                                      ProcessID process_id) {
  // One row at a time, so a long drain writes the buffer out instead of
  // running past its slack.
  struct ChunkRows : Aggregator::RowSink {
    explicit ChunkRows(LocalBufferManager& m) : self(m) {}
    char* row() override { return self.buffer + self.buffer_pos; }
    void wrote(size_t size) override {
      self.compress_and_write_if_needed(size);
    }
    LocalBufferManager& self;
  } rows(*this);
  aggregator.drain(all, process_id, this->serializer->hostname(), rows);
}
}  // namespace dftracer
