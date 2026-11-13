#ifndef DFTRACER_SERVICE_PIPELINE_RING_SINK_H
#define DFTRACER_SERVICE_PIPELINE_RING_SINK_H

#include <dftracer/core/buffer/lane_buffer.h>
#include <dftracer/core/buffer/service_buffer_manager.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/ipc/event_codec.h>
#include <dftracer/core/ipc/ring_producer.h>
#include <dftracer/core/ipc/shm_region.h>
#include <dftracer/service/common/datastructure.h>
#include <dftracer/service/pipeline/block_tracker.h>
#include <dftracer/service/pipeline/client_table.h>
#include <dftracer/service/pipeline/consumer_scaler.h>
#include <dftracer/service/pipeline/cpu_placer.h>
#include <dftracer/service/pipeline/node_trace_file.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace dftracer {

/**
 * The service side of producer/consumer mode: owns the ring segment, drains
 * it on N threads, and writes the node's trace file.
 *
 * Each drain thread works a block from start to finish: claim it (or take it
 * over from a producer that holds it unpublished), replay its records into the
 * LaneBuffer of the producer thread that wrote them (the same JSON and
 * aggregation that process would have produced) into the drain thread's
 * own chunk, release it, and, when the chunk is full or old, gzip it into one
 * member and write that at its own offset of the node file. Blocks are
 * independent, so threads share only the producer's lane while replaying it.
 *
 * N follows the load. All `max` threads exist from the start, but only the
 * first `active` drain; the rest sleep on a condition variable. Once per tick
 * drain thread 0 -- not an extra thread -- samples the outstanding work
 * (complete blocks not yet claimed) and the arrival rate, and ConsumerScaler
 * sets `active` from them: up at once, down one at a time. Busy time includes
 * the JSON, aggregation and compression, so the count covers the whole job.
 *
 * Thread 0 also takes over blocks held unpublished for longer than
 * `flush_ns`, and finishes client processes that said GOODBYE or died.
 */
class RingSink {
 public:
  using Stats = DrainStats;
  using Scaling = DrainScaling;

  /** Create the segment. Returns false (and logs) if it cannot. */
  bool create(const std::string& name, uint32_t block_count,
              uint32_t block_size) {
    if (!ipc::RingView::valid_geometry(block_count, block_size)) {
      std::fprintf(stderr,
                   "[DFTRACER] invalid ring geometry: %u blocks of %u bytes\n",
                   block_count, block_size);
      return false;
    }
    // Fit the ring in the shared memory there is (90% of it). If even the
    // smallest ring does not fit, create() below reports the shortage.
    const uint64_t room = ipc::ShmRegion::available_bytes() / 10 * 9;
    const uint32_t asked_count = block_count, asked_size = block_size;
    ipc::RingView::fit(block_count, block_size, room);
    if (block_count != asked_count || block_size != asked_size) {
      std::fprintf(stderr,
                   "[DFTRACER] WARNING: /dev/shm has %llu MiB free: ring "
                   "reduced from %u x %u KiB to %u x %u KiB. Producers drop "
                   "events sooner when the service falls behind; give the "
                   "node or container more shared memory (e.g. docker "
                   "--shm-size).\n",
                   static_cast<unsigned long long>(room / 9 * 10 >> 20),
                   asked_count, asked_size >> 10, block_count,
                   block_size >> 10);
    }
    try {
      region_ = ipc::ShmRegion::create(name, block_count, block_size,
                                       first_blocks(block_size));
    } catch (const std::exception& error) {
      std::fprintf(stderr, "[DFTRACER] cannot create ring %s: %s\n",
                   name.c_str(), error.what());
      return false;
    }
    return true;
  }

  /** Start draining and open the ring to producers. */
  bool start(const DrainOptions& options) {
    if (region_ == nullptr || !threads_.empty()) return false;
    options_ = options;
    if (!options_.node_file.empty()) {
      if (!file_.open(options_.node_file, options_.rollover_bytes))
        return false;
      clients_ = std::make_unique<ClientTable>(options_.hostname_hash);
    }
    auto* header = region_->ring().header();
    header->service_pid = static_cast<int32_t>(getpid());
    header->heartbeat_ns.store(ipc::monotonic_ns(), std::memory_order_relaxed);
    stop_.store(false, std::memory_order_relaxed);
    claimer_ = std::make_unique<ipc::ParallelBlockClaimer>(region_->ring());
    scaler_ = std::make_unique<ipc::ConsumerScaler>(
        options_.min_threads, options_.max_threads, DFT_SHM_CONSUMER_CATCHUP_NS,
        DFT_SHM_CONSUMER_DOWN_TICKS);
    const uint32_t threads = scaler_->max_threads();
    tick_ns_ =
        static_cast<uint64_t>(options_.tick_ms == 0 ? 1 : options_.tick_ms) *
        1000000ULL;
    active_.store(scaler_->min_threads(), std::memory_order_relaxed);
    scaling_ = Scaling();
    scaling_.peak = scaler_->min_threads();
    per_thread_.assign(threads, Stats());
    load_.reset(new Load[threads]);
    scratch_.assign(threads, DrainScratch());
    writers_.clear();
    if (clients_ != nullptr) {
      for (uint32_t i = 0; i < threads; ++i)
        writers_.push_back(std::make_unique<ChunkWriter>(
            file_, options_.chunk_size, options_.compression_level));
    }
    // Before the threads start: they inherit a fixed CPU list, and drain
    // thread 0 then owns the placer.
    placer_.configure(options_.service_cpus);
    for (uint32_t i = 0; i < threads; ++i)
      threads_.emplace_back([this, i]() { drain(i); });
    if (region_->reserved_blocks() < region_->ring().block_count())
      reserver_ = std::thread([this]() { reserve_rest(); });
    header->state.store(SHM_SEGMENT_ACCEPTING, std::memory_order_release);
    return true;
  }

  /** Decode and count only, on between `min_threads` and `max_threads`. */
  bool start(uint32_t min_threads, uint32_t max_threads,
             uint32_t tick_ms = DFT_SHM_DEFAULT_CONSUMER_TICK_MS) {
    DrainOptions options;
    options.min_threads = min_threads;
    options.max_threads = max_threads;
    options.tick_ms = tick_ms;
    return start(options);
  }

  /** Drain threads currently allowed to drain. */
  uint32_t active_threads() const {
    return active_.load(std::memory_order_relaxed);
  }
  /** Complete blocks waiting for a drain thread. */
  uint64_t ready_blocks() {
    return claimer_ == nullptr ? 0 : claimer_->ready_blocks();
  }

  /**
   * Stop accepting, drain everything producers published, take over every
   * block still held by an idle or dead producer, finish every client
   * process, and close the node file. A block a live producer is writing into
   * at this very moment is left behind (counted as held).
   */
  void stop() {
    if (region_ == nullptr) return;
    auto* header = region_->ring().header();
    header->state.store(SHM_SEGMENT_DRAINING, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lock(park_mtx_);
      stop_.store(true, std::memory_order_release);
    }
    park_cv_.notify_all();
    if (reserver_.joinable()) reserver_.join();
    for (auto& thread : threads_) thread.join();
    threads_.clear();
    // The drain threads ran below the application's priority, and a thread
    // cannot raise its own back. What is left is worked off by new threads,
    // which take this thread's priority.
    const uint32_t threads = static_cast<uint32_t>(per_thread_.size());
    for (uint32_t i = 0; i < threads; ++i)
      threads_.emplace_back([this, i]() { drain_rest(i); });
    for (auto& thread : threads_) thread.join();
    threads_.clear();

    // Single-threaded from here: what is still held, then the processes.
    if (claimer_ != nullptr) {
      uint64_t seq;
      uint32_t pid;
      while (claimer_->claim(seq)) process_block(0, seq, false);
      while (claimer_->take_over(seq, ipc::monotonic_ns(), pid)) {
        ++per_thread_[0].taken;
        process_block(0, seq, true);
      }
      if (clients_ != nullptr) {
        for (auto& process : clients_->take_all()) finish(0, *process);
      }
      for (size_t i = 0; i < writers_.size(); ++i) {
        writers_[i]->flush();
        per_thread_[i].chunks = writers_[i]->chunks();
        per_thread_[i].json_bytes = writers_[i]->json_bytes();
        per_thread_[i].file_bytes = writers_[i]->file_bytes();
      }
      if (file_.is_open()) append_spill_files();
    }
    const uint64_t held =
        claimer_ == nullptr
            ? 0
            : header->head.load(std::memory_order_acquire) - claimer_->tail();
    file_.close();
    for (const auto& stats : per_thread_) stats_.add(stats);
    header->state.store(SHM_SEGMENT_CLOSED, std::memory_order_release);
    std::fprintf(
        stderr,
        "[DFTRACER] ring %s drained %llu blocks (%llu taken over, %llu left "
        "held) on %u..%zu threads (mean %.2f, peak %u, %.2f cpu s): "
        "data=%llu (compact %llu) counter=%llu metadata=%llu record=%llu "
        "hello=%llu "
        "goodbye=%llu malformed=%llu uncommitted=%llu; wrote %llu chunks, "
        "%.1f MB json -> %.1f MB to %s\n",
        region_->name().c_str(), static_cast<unsigned long long>(stats_.blocks),
        static_cast<unsigned long long>(stats_.taken),
        static_cast<unsigned long long>(held),
        scaler_ ? scaler_->min_threads() : 0u, per_thread_.size(),
        scaling_.span_ns == 0 ? 0.0 : scaling_.thread_ns / scaling_.span_ns,
        scaling_.peak, stats_.cpu_ns * 1e-9,
        static_cast<unsigned long long>(stats_.records[SHM_KIND_DATA] +
                                        stats_.records[SHM_KIND_COMPACT]),
        static_cast<unsigned long long>(stats_.records[SHM_KIND_COMPACT]),
        static_cast<unsigned long long>(stats_.records[SHM_KIND_COUNTER]),
        static_cast<unsigned long long>(stats_.records[SHM_KIND_METADATA]),
        static_cast<unsigned long long>(stats_.records[SHM_KIND_RECORD]),
        static_cast<unsigned long long>(stats_.records[SHM_KIND_HELLO]),
        static_cast<unsigned long long>(stats_.records[SHM_KIND_GOODBYE]),
        static_cast<unsigned long long>(stats_.malformed),
        static_cast<unsigned long long>(stats_.uncommitted),
        static_cast<unsigned long long>(stats_.chunks),
        stats_.json_bytes * 1e-6, stats_.file_bytes * 1e-6,
        file_.path().empty() ? "(none)" : file_.path().c_str());
    // Events the producers could not fit in the ring are not lost: each
    // producer spilled them to a file of its own, appended above.
    const uint64_t spilled = header->spilled.load(std::memory_order_relaxed);
    if (spilled != 0 || spill_appended_ != 0 || spill_left_ != 0) {
      std::fprintf(
          stderr,
          "[DFTRACER] ring %s: %llu events did not fit in the ring; %u spill "
          "files (%.1f MB) appended to the node trace, %u left in place (still "
          "being written)%s\n",
          region_->name().c_str(), static_cast<unsigned long long>(spilled),
          spill_appended_, spill_bytes_ * 1e-6, spill_left_,
          spilled != 0 ? "; the service fell behind (more "
                         "DFTRACER_PC_CONSUMER_THREADS, a larger ring or "
                         "aggregation help)"
                       : "");
    }
    if (file_.paths().size() > 1)
      std::fprintf(stderr, "[DFTRACER] node trace rolled over into %zu files\n",
                   file_.paths().size());
    // Every way an event can miss the trace, once, at the end of the log.
    const uint64_t dropped = header->dropped.load(std::memory_order_relaxed);
    if (dropped != 0 || stats_.uncommitted != 0 || held != 0 ||
        stats_.malformed != 0) {
      std::fprintf(
          stderr,
          "[DFTRACER] WARNING: ring %s: the trace is missing events: %llu "
          "dropped by producers (the ring was full and their spill file could "
          "not be written), "
          "%llu left uncommitted by producers that died, %llu malformed, %llu "
          "blocks never published\n",
          region_->name().c_str(), static_cast<unsigned long long>(dropped),
          static_cast<unsigned long long>(stats_.uncommitted),
          static_cast<unsigned long long>(stats_.malformed),
          static_cast<unsigned long long>(held));
    }
    writers_.clear();
    clients_.reset();
    region_.reset();
  }

  const Stats& stats() const { return stats_; }
  const Scaling& scaling() const { return scaling_; }

 private:
  using Load = DrainLoad;

  static uint64_t thread_cpu_ns() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
  }

  /** Wait while this thread is above the active count. False: stopping. */
  bool wait_until_active(uint32_t id) {
    if (id < active_.load(std::memory_order_acquire)) return true;
    // Parking: write out what this thread has collected first.
    if (!writers_.empty()) writers_[id]->flush();
    std::unique_lock<std::mutex> lock(park_mtx_);
    park_cv_.wait(lock, [&]() {
      return stop_.load(std::memory_order_acquire) ||
             id < active_.load(std::memory_order_acquire);
    });
    return !stop_.load(std::memory_order_acquire) &&
           id < active_.load(std::memory_order_acquire);
  }

  /**
   * Append every finished spill file of this node's producers (and the
   * service's own) to the node trace, then delete it. Their events went
   * through a LocalBufferManager in the producer: aggregated and serialized
   * already, so the gzip members go in as they are. A file a producer still
   * holds locked is left in place.
   */
  void append_spill_files() {
    std::set<std::string> files;
    {
      std::lock_guard<std::mutex> lock(spill_mtx_);
      files = spill_files_;
    }
    region_->ring().for_each_spill([&](const char* path, uint32_t length) {
      files.emplace(path, length);
    });
    if (!options_.own_spill_file.empty()) files.insert(options_.own_spill_file);
    for (const std::string& path : files) {
      const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
      if (fd < 0) continue;  // that producer never spilled
      if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ++spill_left_;
        ::close(fd);
        continue;
      }
      spill_bytes_ += file_.append_members(path);
      // A member cut short by a producer that died cannot be recovered;
      // everything complete is in the node trace now.
      ::unlink(path.c_str());
      ::close(fd);
      ++spill_appended_;
    }
  }

  /**
   * Replay block `seq` into the lane of the producer thread that wrote it,
   * then release it. `salvage`: the block was taken over, not published.
   */
  void process_block(uint32_t id, uint64_t seq, bool salvage) {
    Stats& stats = per_thread_[id];
    const ipc::RingView& ring = region_->ring();
    const ipc::BlockControl& block = ring.block(seq);
    // Written by the producer before it published or went IDLE; visible
    // through the acquire that claimed or took the block.
    const uint32_t pid = block.owner_pid.load(std::memory_order_relaxed);
    const uint32_t tid = block.owner_tid.load(std::memory_order_relaxed);
    // Interned ids and compact state are the block's own: start each block
    // afresh.
    scratch_[id].intern.clear();
    scratch_[id].compact.reset();
    scratch_[id].lines.reset();
    scratch_[id].compact_ok = true;
    if (clients_ == nullptr) {
      DrainScratch& s = scratch_[id];
      stats.uncommitted += ipc::for_each_record(
          ring, seq,
          [&](const ipc::RecordHeader& hdr, const uint8_t* payload,
              uint32_t bytes) {
            if (hdr.kind <= SHM_KIND_COMPACT) ++stats.records[hdr.kind];
            if ((hdr.kind == SHM_KIND_DATA || hdr.kind == SHM_KIND_COUNTER) &&
                !ipc::decode_event(payload, bytes, s.event, &s.intern))
              ++stats.malformed;
            if (hdr.kind == SHM_KIND_COMPACT &&
                !decode_compact(s, payload, bytes))
              ++stats.malformed;
          },
          salvage);
    } else {
      const uint64_t now = ipc::monotonic_ns();
      auto process = clients_->process(pid, now);
      auto lane = clients_->lane(*process, tid);
      ChunkWriter& writer = *writers_[id];
      std::lock_guard<std::mutex> lock(lane->mtx);
      ClientTable::sync(*process, *lane);
      LaneBuffer& buffer = *lane->buffer;
      buffer.attach(writer.data(), writer.used(), writer.capacity(), &writer);
      stats.uncommitted += ipc::for_each_record(
          ring, seq,
          [&](const ipc::RecordHeader& hdr, const uint8_t* payload,
              uint32_t bytes) {
            if (hdr.kind <= SHM_KIND_COMPACT) ++stats.records[hdr.kind];
            if (!replay(id, *process, *lane, hdr.kind, payload, bytes, now))
              ++stats.malformed;
          },
          salvage);
      writer.set_used(buffer.detach(), now);
    }
    ++stats.blocks;
    stats.bytes += ring.block_size();
    claimer_->release(seq);
  }

  static bool decode_compact(DrainScratch& s, const uint8_t* payload,
                             uint32_t bytes) {
    if (s.compact_ok && s.compact.event(payload, bytes, s.event)) return true;
    s.compact_ok = false;
    return false;
  }

  /** One record into its lane (attached to this thread's chunk). */
  bool replay(uint32_t id, ClientProcess& process, ClientLane& lane,
              uint16_t kind, const uint8_t* payload, uint32_t bytes,
              uint64_t now) {
    DrainScratch& s = scratch_[id];
    LaneBuffer& buffer = *lane.buffer;
    switch (kind) {
      case SHM_KIND_DATA:
      case SHM_KIND_COUNTER: {
        // Written and aggregated from the decoded record itself: its
        // strings and values stay views into the ring.
        if (!ipc::decode_event(payload, bytes, s.event, &s.intern))
          return false;
        buffer.log_packed_event(s.event, kind == SHM_KIND_COUNTER);
        return true;
      }
      case SHM_KIND_COMPACT: {
        if (!decode_compact(s, payload, bytes)) return false;
        buffer.log_packed_event(s.event, false, &s.lines, s.compact.shape());
        return true;
      }
      case SHM_KIND_METADATA: {
        if (!ipc::decode_metadata_event(payload, bytes, s.metadata))
          return false;
        s.name.assign(s.metadata.name);
        s.value.assign(s.metadata.value);
        s.record_name.assign(s.metadata.record_name);
        buffer.log_metadata_event(
            s.name.c_str(), s.value.c_str(), s.record_name.c_str(),
            static_cast<TraceEventType>(s.metadata.head.type),
            s.metadata.head.process_id,
            static_cast<ThreadID>(s.metadata.head.thread_id),
            s.metadata.head.is_string != 0);
        return true;
      }
      case SHM_KIND_RECORD: {
        if (!ipc::decode_record(payload, bytes, s.record)) return false;
        s.record_name.assign(s.record.record_name);
        s.value.assign(s.record.fields);
        buffer.log_record(s.record_name.c_str(), s.value.c_str(),
                          static_cast<TraceEventType>(s.record.head.type),
                          s.record.head.process_id,
                          static_cast<ThreadID>(s.record.head.thread_id));
        return true;
      }
      case SHM_KIND_HELLO: {
        if (!ipc::decode_hello(payload, bytes, s.hello)) return false;
        {
          // Where this process spills what does not fit in the ring; taken
          // in at stop.
          std::lock_guard<std::mutex> lock(spill_mtx_);
          spill_files_.insert(ServiceBufferManager::spill_file_for(
              std::string(s.hello.log_file)));
        }
        std::lock_guard<std::mutex> lock(process.mtx);
        // A pid seen again after its GOODBYE is a new process.
        process.said_goodbye = false;
        return true;
      }
      case SHM_KIND_SET_APP_NAME: {
        {
          std::lock_guard<std::mutex> lock(process.mtx);
          const char* name = reinterpret_cast<const char*>(payload);
          process.app_name.assign(name, strnlen(name, bytes));
          ++process.version;
        }
        ClientTable::sync(process, lane);
        return true;
      }
      case SHM_KIND_SET_RANK:
      case SHM_KIND_GOODBYE: {
        int32_t value;
        if (bytes < sizeof(value)) return false;
        std::memcpy(&value, payload, sizeof(value));
        {
          std::lock_guard<std::mutex> lock(process.mtx);
          if (kind == SHM_KIND_SET_RANK) {
            process.rank = value;
            ++process.version;
          } else {
            process.said_goodbye = true;
            process.goodbye_ns = now;
          }
        }
        if (kind == SHM_KIND_SET_RANK) ClientTable::sync(process, lane);
        return true;
      }
      default:
        return false;
    }
  }

  /** Write a finished process's open aggregates, on thread `id`'s chunk. */
  void finish(uint32_t id, ClientProcess& process) {
    std::vector<std::shared_ptr<ClientLane>> lanes;
    {
      std::lock_guard<std::mutex> lock(process.mtx);
      for (auto& entry : process.lanes) lanes.push_back(entry.second);
    }
    ChunkWriter& writer = *writers_[id];
    for (auto& lane : lanes) {
      std::lock_guard<std::mutex> lock(lane->mtx);
      ClientTable::sync(process, *lane);
      lane->buffer->attach(writer.data(), writer.used(), writer.capacity(),
                           &writer);
      lane->buffer->finalize(static_cast<ProcessID>(process.pid));
      writer.set_used(lane->buffer->detach(), ipc::monotonic_ns());
    }
  }

  // The processes the service has seen, and those holding a block now: a
  // process is in the table only once one of its blocks is drained.
  std::vector<uint32_t> client_pids() {
    const uint32_t self = static_cast<uint32_t>(getpid());
    std::vector<uint32_t> pids = clients_->pids(self);
    const ipc::RingView& ring = region_->ring();
    for (uint32_t i = 0; i < ring.block_count(); ++i) {
      const ipc::BlockControl& block = ring.block(i);
      if (block.claim_ns.load(std::memory_order_relaxed) == 0) continue;
      const uint32_t pid = block.owner_pid.load(std::memory_order_relaxed);
      if (pid != 0 && pid != self) pids.push_back(pid);
    }
    std::sort(pids.begin(), pids.end());
    pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
    return pids;
  }

  // Events clients could not put in the ring so far.
  uint64_t spilled() const {
    return region_->ring().header()->spilled.load(std::memory_order_relaxed);
  }

  // A quarter of the ring is published and waits for a drain thread.
  bool backlogged() {
    return claimer_->ready_blocks() >=
           std::max<uint64_t>(1, region_->ring().block_count() / 4);
  }

  /**
   * Drain thread 0, between blocks: notices a process by the blocks claimed
   * since the last look, so the service leaves its CPUs at once rather than
   * at the next tick. A pid read before its producer wrote it is seen at
   * the tick.
   */
  void watch_clients(uint64_t now) {
    if (clients_ == nullptr || placer_.mode() != CpuPlacer::Mode::kAuto) return;
    const ipc::RingView& ring = region_->ring();
    const uint64_t head = ring.header()->head.load(std::memory_order_acquire);
    if (head == seen_head_) return;
    const uint64_t count = ring.block_count();
    uint64_t seq = head - seen_head_ > count ? head - count : seen_head_;
    seen_head_ = head;
    const uint32_t self = static_cast<uint32_t>(getpid());
    for (; seq < head; ++seq) {
      const uint32_t pid =
          ring.block(seq).owner_pid.load(std::memory_order_relaxed);
      if (pid == 0 || pid == self ||
          std::binary_search(placed_pids_.begin(), placed_pids_.end(), pid))
        continue;
      placed_pids_ = client_pids();
      placer_.update(placed_pids_, now, spilled(), backlogged());
      return;
    }
  }

  /** Controller, run by drain thread 0 between blocks. */
  void maybe_scale(uint64_t now) {
    if (now < next_tick_ || stop_.load(std::memory_order_relaxed)) return;
    const bool first = next_tick_ == 0;
    next_tick_ = now + tick_ns_;
    housekeeping(now);
    const ipc::RingView& ring = region_->ring();
    ipc::ConsumerScaler::Sample sample{};
    sample.now_ns = now;
    sample.head = ring.header()->head.load(std::memory_order_acquire);
    sample.ready = claimer_->ready_blocks();
    for (size_t i = 0; i < per_thread_.size(); ++i) {
      sample.drained += load_[i].blocks.load(std::memory_order_relaxed);
      sample.busy_ns += load_[i].busy_ns.load(std::memory_order_relaxed);
    }
    const uint32_t active = active_.load(std::memory_order_relaxed);
    if (first) {
      scaler_->reset(sample);
      last_sample_ns_ = now;
      return;
    }
    scaling_.thread_ns += static_cast<double>(active) * (now - last_sample_ns_);
    scaling_.span_ns += now - last_sample_ns_;
    last_sample_ns_ = now;
    ++scaling_.ticks;
    const uint32_t want = scaler_->update(sample, active);
    if (want == active) return;
    if (want > active) {
      ++scaling_.ups;
      if (want > scaling_.peak) scaling_.peak = want;
      {
        std::lock_guard<std::mutex> lock(park_mtx_);
        active_.store(want, std::memory_order_release);
      }
      park_cv_.notify_all();
    } else {
      ++scaling_.downs;
      // Threads above `want` finish the block in hand, then park.
      active_.store(want, std::memory_order_release);
    }
  }

  /**
   * Thread 0, once per tick: take over blocks held unpublished for longer
   * than flush_ns (a quiet producer thread, or a dead process), so they do
   * not delay its events, and blocks that the next lap is about to reach, so
   * they do not stop producers; and, once a second, finish the client
   * processes that are done.
   */
  void housekeeping(uint64_t now) {
    region_->ring().header()->heartbeat_ns.store(now,
                                                 std::memory_order_relaxed);
    {
      // A quarter of the ring is many ticks of claims at any rate the
      // drain threads keep up with.
      const uint64_t margin =
          std::max<uint64_t>(1, region_->ring().block_count() / 4);
      const uint64_t older_than =
          now > options_.flush_ns ? now - options_.flush_ns : 0;
      uint64_t seq;
      uint32_t pid;
      while (claimer_->take_over(seq, older_than, pid, margin)) {
        ++per_thread_[0].taken;
        process_block(0, seq, true);
      }
    }
    if (clients_ != nullptr && placer_.mode() == CpuPlacer::Mode::kAuto) {
      placed_pids_ = client_pids();
      placer_.update(placed_pids_, now, spilled(), backlogged());
    }
    if (clients_ != nullptr && now >= next_reap_) {
      next_reap_ = now + 1000000000ULL;
      for (auto& process : clients_->take_finished(now, 2 * options_.flush_ns))
        finish(0, *process);
    }
  }

  static constexpr uint32_t kIdleSleepMinUs = 200;
  static constexpr uint32_t kIdleSleepMaxUs = 3200;

  /**
   * A drain thread while the application runs. It runs at
   * DFT_SHM_DRAIN_NICE, so it takes the cores the application leaves idle
   * rather than time from it; at stop it leaves the rest to drain_rest().
   */
  void drain(uint32_t id) {
    // On Linux a nice value belongs to the thread, not the process.
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)),
                DFT_SHM_DRAIN_NICE);
    Stats& stats = per_thread_[id];
    Load& load = load_[id];
    ChunkWriter* writer = writers_.empty() ? nullptr : writers_[id].get();
    uint32_t idle = 0;
    uint32_t sleep_us = kIdleSleepMinUs;
    for (;;) {
      if (!wait_until_active(id)) break;
      uint64_t seq;
      if (claimer_->claim(seq)) {
        idle = 0;
        sleep_us = kIdleSleepMinUs;
        const uint64_t t0 = ipc::monotonic_ns();
        process_block(id, seq, false);
        const uint64_t t1 = ipc::monotonic_ns();
        load.busy_ns.store(
            load.busy_ns.load(std::memory_order_relaxed) + (t1 - t0),
            std::memory_order_relaxed);
        load.blocks.store(load.blocks.load(std::memory_order_relaxed) + 1,
                          std::memory_order_relaxed);
        if (writer != nullptr) writer->flush_if_older(t1, options_.flush_ns);
        if (id == 0) {
          watch_clients(t1);
          maybe_scale(t1);
        }
        continue;
      }
      if (stop_.load(std::memory_order_acquire)) break;
      const uint64_t now = ipc::monotonic_ns();
      if (writer != nullptr) writer->flush_if_older(now, options_.flush_ns);
      if (id == 0) {
        watch_clients(now);
        maybe_scale(now);
      }
      // Nothing complete: back off from spinning to sleeps that double, so
      // waiting for the next block wakes the thread a few times, not once
      // per kIdleSleepMinUs on a core the application may be using.
      if (++idle < 1024) {
        ipc::cpu_relax();
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        sleep_us = std::min(2 * sleep_us, kIdleSleepMaxUs);
      }
    }
    if (writer != nullptr) writer->flush();
    stats.cpu_ns = thread_cpu_ns();
  }

  static uint32_t first_blocks(uint32_t block_size) {
    const uint64_t blocks = DFT_SHM_RESERVE_FIRST_BYTES / block_size;
    return blocks == 0 ? 1 : static_cast<uint32_t>(blocks);
  }

  /**
   * Reserves the rest of the ring's shared memory, right after the start.
   * Keeping only ahead of the producers' first lap is not enough: many
   * producers that start at once claim blocks faster than this thread can
   * reserve them on CPUs they keep busy, and they spill.
   */
  void reserve_rest() {
    const ipc::RingView& ring = region_->ring();
    const uint32_t count = ring.block_count();
    uint64_t step = DFT_SHM_RESERVE_STEP_BYTES / ring.block_size();
    if (step == 0) step = 1;
    while (!stop_.load(std::memory_order_acquire) &&
           region_->reserved_blocks() < count) {
      const uint32_t have = region_->reserved_blocks();
      const uint32_t next =
          static_cast<uint32_t>(std::min<uint64_t>(count, have + step));
      if (!region_->reserve(next)) {
        std::fprintf(stderr,
                     "[DFTRACER] WARNING: /dev/shm cannot back more of ring "
                     "%s: it stays at %u of %u blocks, and producers spill "
                     "once those are in use.\n",
                     region_->name().c_str(), have, count);
        return;
      }
    }
  }

  /** At stop: drain every published block, on thread `id`'s chunk. */
  void drain_rest(uint32_t id) {
    uint64_t seq;
    while (claimer_->claim(seq)) process_block(id, seq, false);
    if (!writers_.empty()) writers_[id]->flush();
    per_thread_[id].cpu_ns += thread_cpu_ns();
  }

  DrainOptions options_;
  std::mutex spill_mtx_;
  std::set<std::string> spill_files_;  // of every client that said HELLO
  uint32_t spill_appended_ = 0;
  uint32_t spill_left_ = 0;
  uint64_t spill_bytes_ = 0;
  std::unique_ptr<ipc::ShmRegion> region_;
  std::unique_ptr<ipc::ParallelBlockClaimer> claimer_;
  std::unique_ptr<ipc::ConsumerScaler> scaler_;
  NodeTraceFile file_;
  std::unique_ptr<ClientTable> clients_;
  // Drain thread 0 only, once started: where the service runs, for which
  // clients, and the ring head watch_clients() looked at last.
  CpuPlacer placer_;
  std::vector<uint32_t> placed_pids_;
  uint64_t seen_head_ = 0;
  std::vector<std::thread> threads_;
  std::thread reserver_;
  std::vector<Stats> per_thread_;
  std::unique_ptr<Load[]> load_;
  std::vector<DrainScratch> scratch_;
  std::vector<std::unique_ptr<ChunkWriter>> writers_;
  std::atomic<uint32_t> active_{1};
  std::atomic<bool> stop_{false};
  std::mutex park_mtx_;
  std::condition_variable park_cv_;
  // Controller state: touched by drain thread 0 only, read after the join.
  uint64_t tick_ns_ = 0;
  uint64_t next_tick_ = 0;
  uint64_t next_reap_ = 0;
  uint64_t last_sample_ns_ = 0;
  Scaling scaling_;
  Stats stats_;
};

}  // namespace dftracer

#endif  // DFTRACER_SERVICE_PIPELINE_RING_SINK_H
