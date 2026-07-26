#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/utils/utils.h>

#include <cstring>
#include <memory>
#include <mutex>

static constexpr size_t DFTRACER_SERIALIZATION_EVENT_MAX = 16 * 1024;
namespace dftracer {
template <>
std::shared_ptr<JsonLines> Singleton<JsonLines>::instance = nullptr;
template <>
bool Singleton<JsonLines>::stop_creating_instances = false;
JsonLines::JsonLines() : include_metadata(false) {
  auto conf = Singleton<ConfigurationManager>::get_instance();
  include_metadata = conf->metadata;
}

size_t JsonLines::initialize(char* buffer, HashType hostname_hash) {
  this->hostname_hash = hostname_hash;
  return 0;
}

bool JsonLines::convert_metadata(Metadata* metadata,
                                 std::stringstream& meta_stream) {
  if (metadata == nullptr) return false;
  auto meta_size = metadata->size();
  long unsigned int i = 0;
  bool has_meta = false;
  for (const auto& item : *metadata) {
    has_meta = true;
    if (std::get<1>(item.second).type() == typeid(dftracer::RawJson)) {
      const auto& raw =
          std::any_cast<const dftracer::RawJson&>(std::get<1>(item.second));
      meta_stream << "\"" << item.first << "\":" << raw.value;
      if (i < meta_size - 1) meta_stream << ",";
      i++;
      continue;
    }
    DFTRACER_FOR_EACH_NUMERIC_TYPE(
        DFTRACER_ANY_CAST_MACRO, std::get<1>(item.second), {
          meta_stream << "\"" << item.first << "\":" << res.value();
          if (i < meta_size - 1) meta_stream << ",";
          i++;
          continue;
        });
    DFTRACER_FOR_EACH_STRING_TYPE(
        DFTRACER_ANY_CAST_MACRO, std::get<1>(item.second), {
          meta_stream << "\"" << item.first << "\":\"" << res.value() << "\"";
          if (i < meta_size - 1) meta_stream << ",";
          i++;
          continue;
        });
    i++;
  }
  if (meta_stream.str().size() > 0 && meta_stream.str().back() == ',') {
    std::string temp = meta_stream.str();
    temp.pop_back();
    meta_stream.str("");
    meta_stream.clear();
    meta_stream << temp;
  }
  delete metadata;
  return has_meta;
}

size_t JsonLines::data(char* buffer, int index, ConstEventNameType event_name,
                       ConstEventNameType category, TraceEventType type,
                       TimeResolution start_time, TimeResolution duration,
                       dftracer::Metadata* metadata, ProcessID process_id,
                       ThreadID thread_id) {
  size_t written_size = 0;
  int n = 0;
  if (include_metadata && metadata != nullptr) {
    std::stringstream all_stream;
    std::stringstream meta_stream;
    bool has_meta = convert_metadata(metadata, meta_stream);

    if (has_meta) {
      all_stream << "," << meta_stream.str();
    }
    n = dftracer_logging_real_snprintf()(
        buffer, DFTRACER_SERIALIZATION_EVENT_MAX,
        R"({"id":%d,"name":"%s","cat":"%s","type":%u,"pid":%d,"tid":%lu,"ts":%llu,"dur":%llu,"ph":%u,"args":{"hhash":"%s"%s}})",
        index, event_name, category, static_cast<unsigned>(type), process_id,
        thread_id, start_time, duration,
        static_cast<unsigned>(TracePhaseType::TRACE_PHASE_COMPLETE),
        this->hostname_hash, all_stream.str().c_str());
  } else {
    n = dftracer_logging_real_snprintf()(
        buffer, DFTRACER_SERIALIZATION_EVENT_MAX,
        R"({"id":%d,"name":"%s","cat":"%s","type":%u,"pid":%d,"tid":%lu,"ts":%llu,"dur":%llu,"ph":%u})",
        index, event_name, category, static_cast<unsigned>(type), process_id,
        thread_id, start_time, duration,
        static_cast<unsigned>(TracePhaseType::TRACE_PHASE_COMPLETE));
    delete metadata;
  }
  if (n < 0) {
    return 0;
  }
  written_size = (static_cast<size_t>(n) >= DFTRACER_SERIALIZATION_EVENT_MAX)
                     ? DFTRACER_SERIALIZATION_EVENT_MAX - 1
                     : static_cast<size_t>(n);
  if (static_cast<size_t>(n) >= DFTRACER_SERIALIZATION_EVENT_MAX) {
    DFTRACER_LOG_WARN("JsonLines.data truncated event %d to %zu bytes", index,
                      DFTRACER_SERIALIZATION_EVENT_MAX - 1);
  }
  if (written_size + 1 < DFTRACER_SERIALIZATION_EVENT_MAX) {
    buffer[written_size++] = '\n';
    buffer[written_size] = '\0';
  } else {
    buffer[DFTRACER_SERIALIZATION_EVENT_MAX - 1] = '\0';
  }
  DFTRACER_LOG_DEBUG("JsonLines.serialize %s", buffer);
  return written_size;
}

size_t JsonLines::counter(char* buffer, int index,
                          ConstEventNameType event_name,
                          ConstEventNameType category, TraceEventType type,
                          TimeResolution start_time, ProcessID process_id,
                          ThreadID thread_id, dftracer::Metadata* metadata) {
  return series(buffer, index, event_name, category, type,
                TracePhaseType::TRACE_PHASE_COUNTER, start_time, process_id,
                thread_id, metadata);
}

size_t JsonLines::series(char* buffer, int index, ConstEventNameType event_name,
                         ConstEventNameType category, TraceEventType type,
                         TracePhaseType phase, TimeResolution start_time,
                         ProcessID process_id, ThreadID thread_id,
                         dftracer::Metadata* metadata) {
  size_t written_size = 0;
  int n = 0;
  if (metadata != nullptr && !metadata->empty()) {
    std::stringstream all_stream;
    std::stringstream meta_stream;
    bool has_meta = convert_metadata(metadata, meta_stream);
    if (has_meta) {
      all_stream << "," << meta_stream.str();
    }
    n = dftracer_logging_real_snprintf()(
        buffer, DFTRACER_SERIALIZATION_EVENT_MAX,
        R"({"name":"%s","cat":"%s","type":%u,"ts":%llu,"ph":%u,"pid":%d,"tid":%lu,"args":{"hhash":"%s"%s}})",
        event_name, category, static_cast<unsigned>(type), start_time,
        static_cast<unsigned>(phase), process_id, thread_id,
        this->hostname_hash, all_stream.str().c_str());
  } else {
    n = dftracer_logging_real_snprintf()(
        buffer, DFTRACER_SERIALIZATION_EVENT_MAX,
        R"({"name":"%s","cat":"%s","type":%u,"ts":%llu,"ph":%u,"pid":%d,"tid":%lu})",
        event_name, category, static_cast<unsigned>(type), start_time,
        static_cast<unsigned>(phase), process_id, thread_id);
  }
  if (n < 0) {
    return 0;
  }
  written_size = (static_cast<size_t>(n) >= DFTRACER_SERIALIZATION_EVENT_MAX)
                     ? DFTRACER_SERIALIZATION_EVENT_MAX - 1
                     : static_cast<size_t>(n);
  if (static_cast<size_t>(n) >= DFTRACER_SERIALIZATION_EVENT_MAX) {
    DFTRACER_LOG_WARN("JsonLines.series truncated event %d to %zu bytes", index,
                      DFTRACER_SERIALIZATION_EVENT_MAX - 1);
  }
  if (written_size + 1 < DFTRACER_SERIALIZATION_EVENT_MAX) {
    buffer[written_size++] = '\n';
    buffer[written_size] = '\0';
  } else {
    buffer[DFTRACER_SERIALIZATION_EVENT_MAX - 1] = '\0';
  }
  DFTRACER_LOG_DEBUG("JsonLines.serialize %s", buffer);
  return written_size;
}

size_t JsonLines::metadata(char* buffer, ConstEventNameType name,
                           ConstEventNameType value,
                           ConstEventNameType record_name, TraceEventType type,
                           ProcessID process_id, ThreadID thread_id,
                           bool is_string) {
  size_t written_size = 0;
  int n = 0;
  if (is_string) {
    n = dftracer_logging_real_snprintf()(
        buffer, DFTRACER_SERIALIZATION_EVENT_MAX,
        R"({"name":"%s","cat":"dftracer","type":%u,"pid":%d,"tid":%lu,"ph":%u,"args":{"hhash":"%s","name":"%s","value":"%s"}})",
        record_name, static_cast<unsigned>(type), process_id, thread_id,
        static_cast<unsigned>(TracePhaseType::TRACE_PHASE_METADATA),
        this->hostname_hash, name, value);
  } else {
    n = dftracer_logging_real_snprintf()(
        buffer, DFTRACER_SERIALIZATION_EVENT_MAX,
        R"({"name":"%s","cat":"dftracer","type":%u,"pid":%d,"tid":%lu,"ph":%u,"args":{"hhash":"%s","name":"%s","value":%s}})",
        record_name, static_cast<unsigned>(type), process_id, thread_id,
        static_cast<unsigned>(TracePhaseType::TRACE_PHASE_METADATA),
        this->hostname_hash, name, value);
  }
  if (n < 0) {
    return 0;
  }
  written_size = (static_cast<size_t>(n) >= DFTRACER_SERIALIZATION_EVENT_MAX)
                     ? DFTRACER_SERIALIZATION_EVENT_MAX - 1
                     : static_cast<size_t>(n);
  if (static_cast<size_t>(n) >= DFTRACER_SERIALIZATION_EVENT_MAX) {
    DFTRACER_LOG_WARN(
        "JsonLines.metadata truncated metadata event to %zu bytes",
        DFTRACER_SERIALIZATION_EVENT_MAX - 1);
  }
  if (written_size + 1 < DFTRACER_SERIALIZATION_EVENT_MAX) {
    buffer[written_size++] = '\n';
    buffer[written_size] = '\0';
  } else {
    buffer[DFTRACER_SERIALIZATION_EVENT_MAX - 1] = '\0';
  }
  DFTRACER_LOG_DEBUG("ChromeWriter.convert_json_metadata %s", buffer);
  return written_size;
}

#define BASE_ANY_ID_MACRO(TYPE, VALUE, BLOCK) \
  if (_id == typeid(TYPE)) {                  \
    BLOCK;                                    \
  }

size_t JsonLines::aggregated(char* buffer, int index, ProcessID process_id,
                             dftracer::AggregatedDataType& data) {
  size_t total_written = 0;

  DFTRACER_LOG_INFO("Writing %d intervals", data.size());
  for (const auto& interval_entry : data) {
    const TimeResolution& interval = interval_entry.first;
    const auto& event_map = interval_entry.second;
    DFTRACER_LOG_INFO("Writing %d events for %llu", event_map.size(), interval);
    for (const auto& event_entry : event_map) {
      AggregatedValues* event_values = event_entry.second;
      auto key = event_entry.first;
      // The aggregator nulls additional_keys on the key it stores, so the
      // aggregated values below have nowhere to be written unless we supply a
      // map here. series() takes ownership and frees it.
      auto metadata = key.additional_keys;
      if (metadata == nullptr) metadata = new dftracer::Metadata();
      for (const auto& value_entry : event_values->values) {
        const std::string& base_key = value_entry.first;
        BaseAggregatedValue* base_value = value_entry.second;
        if (!base_value) continue;
        // metadata->erase(base_key);
        auto id = base_value->_id;
        DFTRACER_FOR_EACH_NUMERIC_TYPE(DFTRACER_ANY_NUM_AGGREGATE_MACRO,
                                       base_value, { continue; });
        DFTRACER_FOR_EACH_STRING_TYPE(DFTRACER_ANY_GENERAL_AGGREGATE_MACRO,
                                      base_value, { continue; });
      }
      total_written += series(buffer + total_written, index,
                              key.event_name.c_str(), key.category.c_str(),
                              key.type, TracePhaseType::TRACE_PHASE_AGGREGATED,
                              interval, process_id, key.thread_id, metadata);
    }
  }
  Aggregator::release_aggregated_data(data);
  return total_written;
}

}  // namespace dftracer
