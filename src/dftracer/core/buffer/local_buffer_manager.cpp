#include <dftracer/core/buffer/local_buffer_manager.h>
#include <dftracer/core/common/event_types.h>
#include <dftracer/core/serialization/packed_json.h>
#include <unistd.h>
template <>
std::shared_ptr<dftracer::LocalBufferManager>
    dftracer::Singleton<dftracer::LocalBufferManager>::instance = nullptr;
template <>
bool
    dftracer::Singleton<dftracer::LocalBufferManager>::stop_creating_instances =
        false;
namespace dftracer {

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
      if (this->aggregator->get_previous_aggregations(data, false))
        write_aggregations(data, 0, rank);
    }

    // Always one complete gzip member per write.
    size = this->compressor->compress(buffer, buffer_pos + size);
    DFTRACER_LOG_DEBUG(
        "BufferManager.compress_and_write_if_needed compressed size %zu "
        "bytes",
        size);
    if (size > 0) {
      size = this->writer->write(buffer, size, true);
      DFTRACER_LOG_DEBUG(
          "BufferManager.compress_and_write_if_needed wrote %zu bytes", size);
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
  if (buffer == nullptr) {
    buffer = (char*)malloc(this->config->write_buffer_size + 16 * 1024);
  }
  buffer_pos = 0;
  if (!buffer) {
    DFTRACER_LOG_ERROR("BufferManager.BufferManager Failed to allocate buffer");
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
      this->aggregator->get_previous_aggregations(data, true);
      write_aggregations(data, index, process_id);
      this->aggregator->finalize();
    }
    compress_and_write_if_needed(size, true);

    this->compressor->finalize();
    this->writer->finalize(index);
    free(buffer);
    buffer = nullptr;
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
    if (this->aggregator->get_previous_aggregations(data, true) > 0)
      write_aggregations(data, 0, static_cast<ProcessID>(getpid()));
  }
  compress_and_write_if_needed(size, true);
  this->writer->flush();
}

void LocalBufferManager::log_data_event(
    int index, ConstEventNameType event_name, ConstEventNameType category,
    TraceEventType type, TimeResolution start_time, TimeResolution duration,
    dftracer::Metadata* metadata, ProcessID process_id, ThreadID tid) {
  // Reject events outside [initialize(), finalize()) -- see is_ready().
  if (!is_ready()) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_data_event %d", index);
  size_t size = 0;
  bool enable_tracing = true;
  // An event type that is not aggregatable is always traced in full.
  if (this->config->aggregation_enable && strcmp(category, "dftracer") != 0 &&
      dftracer::event_type_aggregatable(type)) {
    enable_tracing = false;
    auto aggregated_key =
        AggregatedKey{category, event_name,     type, start_time, duration, tid,
                      metadata, get_app_name(), &rank};
    if (this->config->aggregation_type ==
        AggregationType::AGGREGATION_TYPE_SELECTIVE) {
      enable_tracing = !this->aggregator->should_aggregate(&aggregated_key);
    }
    if (!enable_tracing) {
      // Accumulate data; returns true when time interval changes
      bool interval_changed = this->aggregator->aggregate(aggregated_key);
      // aggregate() only reads additional_keys to fold values into the
      // aggregated buckets; it never stores or frees the original metadata,
      // so we must release it here (the non-aggregated path below frees its
      // copy inside serializer->data()).
      delete metadata;
      // Serialize aggregated data when moving to new time interval
      if (interval_changed) {
        auto data = dftracer::AggregatedDataType();
        this->aggregator->get_previous_aggregations(data, false);
        if (!data.empty()) write_aggregations(data, index, process_id);
      }
    }
  }
  if (enable_tracing) {
    size = this->serializer->data(buffer + buffer_pos, index, event_name,
                                  category, type, start_time, duration,
                                  metadata, process_id, tid);
    DFTRACER_LOG_DEBUG(
        "BufferManager.log_data_event serialized tracing size %zu bytes", size);
  }
  compress_and_write_if_needed(size);
}

void LocalBufferManager::log_counter_event(
    int index, ConstEventNameType name, ConstEventNameType category,
    TraceEventType type, TimeResolution start_time, ProcessID process_id,
    ThreadID thread_id, dftracer::Metadata* metadata) {
  // Reject events outside [initialize(), finalize()) -- see is_ready().
  if (!is_ready()) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_counter_event %d", index);
  size_t size = this->serializer->counter(buffer + buffer_pos, index, name,
                                          category, type, start_time,
                                          process_id, thread_id, metadata);
  compress_and_write_if_needed(size);
}

void LocalBufferManager::log_record(ConstEventNameType record_name,
                                    const dftracer::Metadata* fields,
                                    TraceEventType type, ProcessID process_id,
                                    ThreadID tid) {
  if (!is_ready()) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  size_t size = this->serializer->record(buffer + buffer_pos, record_name,
                                         fields, type, process_id, tid);
  compress_and_write_if_needed(size);
}

void LocalBufferManager::log_record(ConstEventNameType record_name,
                                    const char* fields, TraceEventType type,
                                    ProcessID process_id, ThreadID tid) {
  if (!is_ready()) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  size_t size = this->serializer->record(buffer + buffer_pos, record_name,
                                         fields, type, process_id, tid);
  compress_and_write_if_needed(size);
}

void LocalBufferManager::log_metadata_event(ConstEventNameType name,
                                            ConstEventNameType value,
                                            ConstEventNameType record_name,
                                            TraceEventType type,
                                            ProcessID process_id, ThreadID tid,
                                            bool is_string) {
  // Reject events outside [initialize(), finalize()) -- see is_ready().
  if (!is_ready()) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_metadata_event %s", value);
  size_t size =
      this->serializer->metadata(buffer + buffer_pos, name, value, record_name,
                                 type, process_id, tid, is_string);
  compress_and_write_if_needed(size);
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
