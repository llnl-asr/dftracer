#include <dftracer/core/buffer/local_buffer_manager.h>
#include <dftracer/core/common/event_types.h>
#include <dftracer/core/common/profiler.h>
#include <dftracer/core/serialization/packed_json.h>
#include <pthread.h>
#include <unistd.h>

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

void LocalBufferManager::lock_for_fork() {
  mtx.lock();
  write_mtx.lock();
  this->aggregator->lock_for_fork();
}

void LocalBufferManager::unlock_in_parent() {
  this->aggregator->unlock_in_parent();
  write_mtx.unlock();
  mtx.unlock();
}

// pthread_rwlock_unlock matches the writer by tid, which is new in the child.
void LocalBufferManager::reset_in_child() {
  new (&mtx) std::shared_mutex();
  new (&write_mtx) std::mutex();
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

void LocalBufferManager::append(const char* line, size_t size) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  if (buffer == nullptr) return;
  std::memcpy(buffer + buffer_pos, line, size);
  buffer_pos += size;
  if (buffer_pos <= this->config->write_buffer_size) return;
  std::lock_guard<std::mutex> write_lock(write_mtx);
  char* full = buffer;
  const size_t full_size = buffer_pos;
  buffer = spare;
  spare = full;
  buffer_pos = 0;
  lock.unlock();
  write_block(full, full_size);
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
  if (spare == nullptr) spare = (char*)malloc(buffer_bytes);
  buffer_pos = 0;
  if (buffer == nullptr || spare == nullptr) {
    DFTRACER_LOG_ERROR("BufferManager.BufferManager Failed to allocate buffer");
    free(buffer);
    free(spare);
    buffer = nullptr;
    spare = nullptr;
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
    if (this->config->aggregation_enable) {
      auto data = dftracer::AggregatedDataType();
      DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(AGGREGATION));
      this->aggregator->get_previous_aggregations(data, true);
      write_aggregations(data, index, process_id);
      this->aggregator->finalize();
    }
    compress_and_write_if_needed(size, true);

    this->compressor->finalize();
    {
      DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(STORE));
      this->writer->finalize(index);
    }
    free(buffer);
    free(spare);
    buffer = nullptr;
    spare = nullptr;
    buffer_pos = 0;
  }
  return 0;
}

void LocalBufferManager::flush() {
  if (!is_ready() || attached_mode) return;
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
