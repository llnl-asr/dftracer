#ifndef DFTRACER_SERIALIZATION_PACKED_JSON_H
#define DFTRACER_SERIALIZATION_PACKED_JSON_H
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/serialization/json_line.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

/**
 * JSON lines of the service's packed events (docs/design_pc_service.rst,
 * "Metadata in the service"): the same lines JsonLines writes from a
 * Metadata, written straight from the decoded record -- no Metadata, no
 * stringstream, no allocation. Field order is the record's, which JSON does
 * not fix (JsonLines follows its unordered_map); values print as JsonLines
 * prints the types the decoder yields (64-bit integers, double as ostream
 * does, strings unescaped).
 */
namespace dftracer {
namespace packed_json {

/** The line writer JsonLines uses (line_format.h). */
using Line = line_format::Line;

/** "key":value of a decoded metadata entry. */
inline void field(Line& w, const ipc::DecodedValue& v) {
  w.raw("\"");
  w.raw(v.key);
  w.raw("\":");
  switch (v.tag) {
    case SHM_VALUE_INT:
      w.i64(v.i);
      break;
    case SHM_VALUE_UINT:
      w.u64(v.u);
      break;
    case SHM_VALUE_DOUBLE:
      w.f64(v.d);
      break;
    case SHM_VALUE_STRING:
      w.raw("\"");
      w.raw(v.s);
      w.raw("\"");
      break;
    case SHM_VALUE_RAW_JSON:
      w.raw(v.s);
      break;
  }
}

DFTRACER_LINE_FORMAT(
    DataHead,
    R"({"id":{},"name":"{}","cat":"{}","type":{},"pid":{},"tid":{},"ts":{},"dur":{},"ph":{})");
DFTRACER_LINE_FORMAT(
    SeriesHead,
    R"({"name":"{}","cat":"{}","type":{},"ts":{},"ph":{},"pid":{},"tid":{})");
DFTRACER_LINE_FORMAT(ArgsHead, R"(,"args":{"hhash":"{}")");

/** The relations arg, which JsonLines keeps without DFTRACER_INC_METADATA. */
inline bool is_relations(const ipc::DecodedValue& v) {
  return v.tag == SHM_VALUE_RAW_JSON && v.key == DFT_RELATIONS_ARG;
}

/** A DATA record: JsonLines::data. */
inline size_t data(char* out, const ipc::DecodedEvent& e,
                   HashType hostname_hash) {
  const ipc::EventHead& h = e.head;
  const bool has_metadata = (h.flags & SHM_EVENT_HAS_METADATA) != 0;
  const bool include = (h.flags & SHM_EVENT_INC_METADATA) != 0;
  // Without DFTRACER_INC_METADATA only the relations survive, and an event
  // left with none has no args at all.
  bool args = has_metadata;
  if (has_metadata && !include) {
    args = false;
    for (const auto& v : e.values) args = args || is_relations(v);
  }
  Line w(out);
  line_format::append<DataHead>(
      w, h.index, e.name, e.category, static_cast<unsigned>(h.type),
      h.process_id, h.thread_id, h.start_time, h.duration,
      static_cast<unsigned>(TracePhaseType::TRACE_PHASE_COMPLETE));
  if (args) {
    line_format::append<ArgsHead>(w, hostname_hash);
    for (const auto& v : e.values) {
      if (!include && !is_relations(v)) continue;
      w.raw(",");
      field(w, v);
    }
    w.raw("}");
  }
  w.raw("}");
  return w.finish("packed_json.data", h.index);
}

/** The head shared by counters and aggregated rows: JsonLines::series. */
inline void series_head(Line& w, std::string_view name,
                        std::string_view category, uint8_t type,
                        TracePhaseType phase, uint64_t ts, int32_t pid,
                        uint64_t tid) {
  line_format::append<SeriesHead>(w, name, category,
                                  static_cast<unsigned>(type), ts,
                                  static_cast<unsigned>(phase), pid, tid);
}

/** A COUNTER record: JsonLines::counter (every field, no args when none). */
inline size_t counter(char* out, const ipc::DecodedEvent& e,
                      HashType hostname_hash) {
  const ipc::EventHead& h = e.head;
  Line w(out);
  series_head(w, e.name, e.category, h.type,
              TracePhaseType::TRACE_PHASE_COUNTER, h.start_time, h.process_id,
              h.thread_id);
  if ((h.flags & SHM_EVENT_HAS_METADATA) != 0 && !e.values.empty()) {
    line_format::append<ArgsHead>(w, hostname_hash);
    for (const auto& v : e.values) {
      w.raw(",");
      field(w, v);
    }
    w.raw("}");
  }
  w.raw("}");
  return w.finish("packed_json.counter", h.index);
}

}  // namespace packed_json
}  // namespace dftracer
#endif  // DFTRACER_SERIALIZATION_PACKED_JSON_H
