#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/profiler.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/serialization/metadata_json.h>
#include <dftracer/core/serialization/packed_json.h>
#include <dftracer/core/utils/utils.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <typeinfo>

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
  metadata_json::Stream out(meta_stream);
  bool has_meta = metadata_json::fields(out, *metadata, false) > 0;
  Metadata::release(metadata);
  return has_meta;
}

// The fixed parts of every line, split into pieces at compile time
// (line_format.h). A line's args follow its head, written by metadata_json
// straight into the same buffer.
namespace {
DFTRACER_LINE_FORMAT(
    SeriesHead,
    R"({"name":"{}","cat":"{}","type":{},"ts":{},"ph":{},"pid":{},"tid":{})");
DFTRACER_LINE_FORMAT(
    RecordHead,
    R"({"name":"{}","cat":"dftracer","type":{},"pid":{},"tid":{},"ph":{},"args":{"hhash":"{}")");
DFTRACER_LINE_FORMAT(ArgsHead, R"(,"args":{"hhash":"{}")");
DFTRACER_LINE_FORMAT(StringValue, R"(,"name":"{}","value":"{}"}})");
DFTRACER_LINE_FORMAT(RawValue, R"(,"name":"{}","value":{}}})");
DFTRACER_LINE_FORMAT(RawFields, R"(,{}}})");

constexpr unsigned phase(TracePhaseType p) { return static_cast<unsigned>(p); }

// ,"args":{"hhash":"..",<fields>} after a line's head, then the line's end.
// Takes ownership of metadata.
size_t finish_with_args(line_format::Line& w, HashType hostname_hash,
                        Metadata* metadata, const char* what, int index) {
  if (metadata != nullptr) {
    line_format::append<ArgsHead>(w, hostname_hash);
    metadata_json::fields(w, *metadata, true);
    w.raw("}");
    Metadata::release(metadata);
  }
  w.raw("}");
  return w.finish(what, index);
}
}  // namespace

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
                         TracePhaseType phase_type, TimeResolution start_time,
                         ProcessID process_id, ThreadID thread_id,
                         dftracer::Metadata* metadata) {
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(SERIALIZATION));
  // An empty map has no args at all.
  if (metadata != nullptr && metadata->empty()) {
    Metadata::release(metadata);
    metadata = nullptr;
  }
  line_format::Line w(buffer);
  line_format::append<SeriesHead>(w, event_name, category,
                                  static_cast<unsigned>(type), start_time,
                                  phase(phase_type), process_id, thread_id);
  return finish_with_args(w, this->hostname_hash, metadata, "JsonLines.series",
                          index);
}

size_t JsonLines::record(char* buffer, ConstEventNameType record_name,
                         const char* fields, TraceEventType type,
                         ProcessID process_id, ThreadID thread_id) {
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(SERIALIZATION));
  line_format::Line w(buffer);
  line_format::append<RecordHead>(
      w, record_name, static_cast<unsigned>(type), process_id, thread_id,
      phase(TracePhaseType::TRACE_PHASE_METADATA), this->hostname_hash);
  line_format::append<RawFields>(w, fields);
  return w.finish("JsonLines.record", 0);
}

size_t JsonLines::record(char* buffer, ConstEventNameType record_name,
                         const Metadata* fields, TraceEventType type,
                         ProcessID process_id, ThreadID thread_id) {
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(SERIALIZATION));
  line_format::Line w(buffer);
  line_format::append<RecordHead>(
      w, record_name, static_cast<unsigned>(type), process_id, thread_id,
      phase(TracePhaseType::TRACE_PHASE_METADATA), this->hostname_hash);
  if (fields != nullptr) metadata_json::fields(w, *fields, true);
  w.raw("}}");
  return w.finish("JsonLines.record", 0);
}

size_t JsonLines::metadata(char* buffer, ConstEventNameType name,
                           ConstEventNameType value,
                           ConstEventNameType record_name, TraceEventType type,
                           ProcessID process_id, ThreadID thread_id,
                           bool is_string) {
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(SERIALIZATION));
  line_format::Line w(buffer);
  line_format::append<RecordHead>(
      w, record_name, static_cast<unsigned>(type), process_id, thread_id,
      phase(TracePhaseType::TRACE_PHASE_METADATA), this->hostname_hash);
  if (is_string)
    line_format::append<StringValue>(w, name, value);
  else
    line_format::append<RawValue>(w, name, value);
  return w.finish("JsonLines.metadata", 0);
}

}  // namespace dftracer
