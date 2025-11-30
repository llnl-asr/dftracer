#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/utils/utils.h>

#include <cstring>
#include <memory>
#include <mutex>
namespace dftracer {
template <>
std::shared_ptr<JsonLines> Singleton<JsonLines>::instance = nullptr;
template <>
bool Singleton<JsonLines>::stop_creating_instances = false;
JsonLines::JsonLines() : include_metadata(false) {
  auto conf = Singleton<ConfigurationManager>::get_instance();
  include_metadata = conf->metadata;
}

size_t JsonLines::initialize(char *buffer, HashType hostname_hash) {
  this->hostname_hash = hostname_hash;
  buffer[0] = '[';
  buffer[1] = '\n';
  return 2;
}

bool JsonLines::convert_metadata(Metadata *metadata,
                                 std::stringstream &meta_stream) {
  auto meta_size = metadata->size();
  long unsigned int i = 0;
  bool has_meta = false;
  for (const auto &item : *metadata) {
    has_meta = true;
    DFTRACER_FOR_EACH_NUMERIC_TYPE(
        DFTRACER_ANY_CAST_MACRO, item.second.second, {
          meta_stream << "\"" << item.first << "\":" << res.value();
          if (i < meta_size - 1) meta_stream << ",";
          i++;
          continue;
        });
    DFTRACER_FOR_EACH_STRING_TYPE(DFTRACER_ANY_CAST_MACRO, item.second.second, {
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
  if (has_meta && metadata && meta_size > 0) delete (metadata);
  return has_meta;
}

size_t JsonLines::data(char *buffer, int index, ConstEventNameType event_name,
                       ConstEventNameType category, TimeResolution start_time,
                       TimeResolution duration, dftracer::Metadata *metadata,
                       ProcessID process_id, ThreadID thread_id) {
  size_t written_size = 0;
  if (include_metadata && metadata != nullptr) {
    std::stringstream all_stream;
    std::stringstream meta_stream;
    bool has_meta = convert_metadata(metadata, meta_stream);

    if (has_meta) {
      all_stream << "," << meta_stream.str();
    }
    written_size = sprintf(
        buffer,
        R"({"id":%d,"name":"%s","cat":"%s","pid":%d,"tid":%lu,"ts":%llu,"dur":%llu,"ph":"X","args":{"hhash":"%s"%s}})",
        index, event_name, category, process_id, thread_id, start_time,
        duration, this->hostname_hash, all_stream.str().c_str());
  } else {
    written_size = sprintf(
        buffer,
        R"({"id":%d,"name":"%s","cat":"%s","pid":%d,"tid":%lu,"ts":%llu,"dur":%llu,"ph":"X"})",
        index, event_name, category, process_id, thread_id, start_time,
        duration);
  }
  if (written_size > 0) {
    buffer[written_size++] = '\n';
    buffer[written_size] = '\0';
  }
  DFTRACER_LOG_DEBUG("JsonLines.serialize %s", buffer);
  return written_size;
}

size_t JsonLines::counter(char *buffer, int index,
                          ConstEventNameType event_name,
                          ConstEventNameType category,
                          TimeResolution start_time, ProcessID process_id,
                          ThreadID thread_id, dftracer::Metadata *metadata) {
  size_t written_size = 0;
  if (metadata != nullptr && !metadata->empty()) {
    std::stringstream all_stream;
    std::stringstream meta_stream;
    bool has_meta = convert_metadata(metadata, meta_stream);
    if (has_meta) {
      all_stream << "," << meta_stream.str();
    }
    written_size = sprintf(
        buffer,
        R"({"name":"%s","cat":"%s","ts":%llu,"ph":"C","pid":%d,"tid":%lu,"args":{"hhash":"%s"%s}})",
        event_name, category, start_time, process_id, thread_id,
        this->hostname_hash, all_stream.str().c_str());
  } else {
    written_size = sprintf(
        buffer,
        R"({"name":"%s","cat":"%s","ts":%llu,"ph":"C","pid":%d,"tid":%lu})",
        event_name, category, start_time, process_id, thread_id);
  }
  if (written_size > 0) {
    buffer[written_size++] = '\n';
    buffer[written_size] = '\0';
  }
  DFTRACER_LOG_DEBUG("JsonLines.serialize %s", buffer);
  return written_size;
}

size_t JsonLines::metadata(char *buffer, ConstEventNameType name,
                           ConstEventNameType value, ConstEventNameType ph,
                           ProcessID process_id, ThreadID thread_id,
                           bool is_string) {
  size_t written_size = 0;
  if (is_string) {
    written_size = sprintf(
        buffer,
        R"({"name":"%s","cat":"dftracer","pid":%d,"tid":%lu,"ph":"M","args":{"hhash":"%s","name":"%s","value":"%s"}})",
        ph, process_id, thread_id, this->hostname_hash, name, value);
  } else {
    written_size = sprintf(
        buffer,
        R"({"name":"%s","cat":"dftracer","pid":%d,"tid":%lu,"ph":"M","args":{"hhash":"%s","name":"%s","value":%s}})",
        ph, process_id, thread_id, this->hostname_hash, name, value);
  }
  buffer[written_size++] = '\n';
  buffer[written_size] = '\0';
  DFTRACER_LOG_DEBUG("ChromeWriter.convert_json_metadata %s", buffer);
  return written_size;
}

#define BASE_ANY_ID_MACRO(TYPE, VALUE, BLOCK) \
  if (_id == typeid(TYPE)) {                  \
    BLOCK;                                    \
  }

size_t JsonLines::aggregated(char *buffer, int index, ProcessID process_id,
                             dftracer::AggregatedDataType &data) {
  size_t total_written = 0;

  DFTRACER_LOG_INFO("Writing %d intervals", data.size());
  for (const auto &interval_entry : data) {
    const TimeResolution &interval = interval_entry.first;
    const auto &event_map = interval_entry.second;
    DFTRACER_LOG_INFO("Writing %d events for %llu", event_map.size(), interval);
    for (const auto &event_entry : event_map) {
      AggregatedValues *event_values = event_entry.second;
      auto key = event_entry.first;
      auto metadata = key.additional_keys;
      for (const auto &value_entry : event_values->values) {
        const std::string &base_key = value_entry.first;
        BaseAggregatedValue *base_value = value_entry.second;
        if (!base_value) continue;
        // metadata->erase(base_key);
        auto id = base_value->_id;
        DFTRACER_FOR_EACH_NUMERIC_TYPE(DFTRACER_ANY_NUM_AGGREGATE_MACRO,
                                       base_value, { continue; });
        DFTRACER_FOR_EACH_STRING_TYPE(DFTRACER_ANY_GENERAL_AGGREGATE_MACRO,
                                      base_value, { continue; });
      }
      total_written += counter(buffer + total_written, index,
                               key.event_name.c_str(), key.category.c_str(),
                               interval, process_id, key.thread_id, metadata);
    }
  }
  return total_written;
}

}  // namespace dftracer