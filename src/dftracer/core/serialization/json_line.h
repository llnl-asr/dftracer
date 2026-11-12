#ifndef DFTRACER_SERIALIZATION_JSON_LINE_H
#define DFTRACER_SERIALIZATION_JSON_LINE_H

#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/serialization/line_format.h>
#include <dftracer/core/serialization/packed_json.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <typeinfo>
#include <unordered_map>

namespace dftracer {
class JsonLines {
  bool include_metadata;
  HashType hostname_hash;
  // Shared layout for the timestamped, duration-less records: counters and
  // aggregated events differ only in their phase. Takes ownership of metadata
  // (convert_metadata() frees a non-empty map, series() an empty one).
  size_t series(char* buffer, int index, ConstEventNameType event_name,
                ConstEventNameType category, TraceEventType type,
                TracePhaseType phase_type, TimeResolution start_time,
                ProcessID process_id, ThreadID thread_id,
                dftracer::Metadata* metadata);

 public:
  JsonLines();
  // Writes the metadata as "key":value,... into meta_stream and frees it.
  // Returns whether there was any.
  static bool convert_metadata(Metadata* metadata,
                               std::stringstream& meta_stream);
  // The service serializes for many processes, each with its own setting.
  void set_include_metadata(bool include) { include_metadata = include; }
  bool includes_metadata() const { return include_metadata; }
  HashType hostname() const { return hostname_hash; }
  size_t initialize(char* buffer, HashType hostname_hash);
  // A data event, written straight from its decoded record.
  size_t data(char* out, const ipc::DecodedEvent& event) const;
  // record_name is the metadata record kind (PR, FH, HH, SH, CM), not a phase.
  size_t metadata(char* buffer, ConstEventNameType name,
                  ConstEventNameType value, ConstEventNameType record_name,
                  TraceEventType type, ProcessID process_id, ThreadID thread_id,
                  bool is_string = true);
  // A metadata record whose args are named fields. `fields` is the inside of
  // the args object after hhash, e.g. "\"id\":\"8f0d...\",\"store\":3" -- the
  // caller guarantees it is valid JSON (records from the ring are written by
  // metadata_json).
  size_t record(char* buffer, ConstEventNameType record_name,
                const char* fields, TraceEventType type, ProcessID process_id,
                ThreadID thread_id);
  // The same from a record's Metadata, written straight into the line.
  size_t record(char* buffer, ConstEventNameType record_name,
                const Metadata* fields, TraceEventType type,
                ProcessID process_id, ThreadID thread_id);
  size_t counter(char* buffer, int index, ConstEventNameType name,
                 ConstEventNameType category, TraceEventType type,
                 TimeResolution start_time, ProcessID process_id,
                 ThreadID thread_id, dftracer::Metadata* metadata);
  // A COUNTER record from the decoded event: every field, no args when none.
  size_t counter(char* out, const ipc::DecodedEvent& event) const;
};

inline size_t JsonLines::data(char* out, const ipc::DecodedEvent& e) const {
  const ipc::EventHead& h = e.head;
  const bool has_metadata = (h.flags & SHM_EVENT_HAS_METADATA) != 0;
  const bool include = (h.flags & SHM_EVENT_INC_METADATA) != 0;
  // Without DFTRACER_INC_METADATA only the relations survive, and an event
  // left with none has no args at all.
  bool args = has_metadata;
  if (has_metadata && !include) {
    args = false;
    for (const auto& v : e.values) args = args || packed_json::is_relations(v);
  }
  packed_json::Line w(out);
  line_format::append<packed_json::DataHead>(
      w, h.index, e.name, e.category, static_cast<unsigned>(h.type),
      h.process_id, h.thread_id, h.start_time, h.duration,
      static_cast<unsigned>(TracePhaseType::TRACE_PHASE_COMPLETE));
  if (args) {
    line_format::append<packed_json::ArgsHead>(w, hostname_hash);
    for (const auto& v : e.values) {
      if (!include && !packed_json::is_relations(v)) continue;
      w.raw(",");
      packed_json::field(w, v);
    }
    w.raw("}");
  }
  w.raw("}");
  return w.finish("JsonLines.data", h.index);
}

inline size_t JsonLines::counter(char* out, const ipc::DecodedEvent& e) const {
  const ipc::EventHead& h = e.head;
  packed_json::Line w(out);
  packed_json::series_head(w, e.name, e.category, h.type,
                           TracePhaseType::TRACE_PHASE_COUNTER, h.start_time,
                           h.process_id, h.thread_id);
  if ((h.flags & SHM_EVENT_HAS_METADATA) != 0 && !e.values.empty()) {
    line_format::append<packed_json::ArgsHead>(w, hostname_hash);
    for (const auto& v : e.values) {
      w.raw(",");
      packed_json::field(w, v);
    }
    w.raw("}");
  }
  w.raw("}");
  return w.finish("JsonLines.counter", h.index);
}
}  // namespace dftracer

#endif  // DFTRACER_SERIALIZATION_JSON_LINE_H