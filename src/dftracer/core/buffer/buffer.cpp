#include <dftracer/core/buffer/buffer.h>
template <>
std::shared_ptr<dftracer::BufferManager>
    dftracer::Singleton<dftracer::BufferManager>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::BufferManager>::stop_creating_instances =
    false;
namespace dftracer {

void BufferManager::compress_and_write_if_needed(size_t size, bool force) {
  if (force || buffer_pos + size > this->config->write_buffer_size) {
    // On forced flush, serialize any remaining aggregated data
    if (force && this->config->aggregation_enable) {
      auto data = dftracer::AggregatedDataType();
      bool has_data = this->aggregator->get_previous_aggregations(data, false);
      if (has_data) {
        size_t agg_size = this->serializer->aggregated(
            buffer + buffer_pos + size, 0, rank, data);
        size += agg_size;
      }
    }

    if (this->config->compression) {
      size = this->compressor->compress(buffer, buffer_pos + size);
      DFTRACER_LOG_DEBUG(
          "BufferManager.compress_and_write_if_needed compressed size %zu "
          "bytes",
          size);
    } else {
      size = buffer_pos + size;
    }
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
int BufferManager::initialize(const char* filename, HashType hostname_hash) {
  DFTRACER_LOG_DEBUG("BufferManager.initialize %s %s", filename, hostname_hash);
  this->config =
      dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
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
  if (this->config->compression) {
    this->compressor =
        dftracer::Singleton<dftracer::ZlibCompression>::get_instance();
    this->compressor->initialize(this->config->write_buffer_size);
  }
  size_t size = this->serializer->initialize(buffer, hostname_hash);
  compress_and_write_if_needed(size);
  // Everything this object needs (config, buffer, compressor, serializer) is
  // now wired. Only from here is it safe for an asynchronous producer to log.
  ready.store(true, std::memory_order_release);
  return 0;
}

int BufferManager::finalize(int index, ProcessID process_id) {
  // Stop accepting events BEFORE tearing anything down, so a producer thread
  // dftracer does not own (rocprofiler's flush pool, the PAPI sampler) cannot
  // enter a half-dismantled manager.
  ready.store(false, std::memory_order_release);
  std::unique_lock<std::shared_mutex> lock(mtx);
  if (buffer) {
    size_t size = 0;
    if (this->config->aggregation_enable) {
      auto data = dftracer::AggregatedDataType();
      this->aggregator->get_previous_aggregations(data, true);
      size = this->serializer->aggregated(buffer + buffer_pos, index,
                                          process_id, data);
      this->aggregator->finalize();
    }
    compress_and_write_if_needed(size, true);

    if (this->config->compression) this->compressor->finalize();
    this->writer->finalize(index);
    free(buffer);
    buffer = nullptr;
    buffer_pos = 0;
  }
  return 0;
}

void BufferManager::log_data_event(
    int index, ConstEventNameType event_name, ConstEventNameType category,
    TraceEventType type, TimeResolution start_time, TimeResolution duration,
    dftracer::Metadata* metadata, ProcessID process_id, ThreadID tid) {
  // Reject events outside [initialize(), finalize()) -- see is_ready().
  if (!is_ready()) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_data_event %d", index);
  size_t size = 0;
  bool enable_tracing = true;
  if (this->config->aggregation_enable && strcmp(category, "dftracer") != 0) {
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
        if (!data.empty()) {
          size = this->serializer->aggregated(buffer + buffer_pos, index,
                                              process_id, data);

          DFTRACER_LOG_DEBUG(
              "BufferManager.log_data_event serialized aggregated size %zu "
              "bytes",
              size);
        }
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

void BufferManager::log_counter_event(int index, ConstEventNameType name,
                                      ConstEventNameType category,
                                      TraceEventType type,
                                      TimeResolution start_time,
                                      ProcessID process_id, ThreadID thread_id,
                                      dftracer::Metadata* metadata) {
  // Reject events outside [initialize(), finalize()) -- see is_ready().
  if (!is_ready()) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_counter_event %d", index);
  size_t size = this->serializer->counter(buffer + buffer_pos, index, name,
                                          category, type, start_time,
                                          process_id, thread_id, metadata);
  compress_and_write_if_needed(size);
}

void BufferManager::log_record(ConstEventNameType record_name,
                               const char* fields, TraceEventType type,
                               ProcessID process_id, ThreadID tid) {
  if (!is_ready()) return;
  std::unique_lock<std::shared_mutex> lock(mtx);
  size_t size = this->serializer->record(buffer + buffer_pos, record_name,
                                         fields, type, process_id, tid);
  compress_and_write_if_needed(size);
}

void BufferManager::log_metadata_event(ConstEventNameType name,
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
}  // namespace dftracer
