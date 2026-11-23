#ifndef DFTRACER_SERIALIZATION_PACKED_JSON_H
#define DFTRACER_SERIALIZATION_PACKED_JSON_H
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/ipc/event_codec.h>
#include <dftracer/core/serialization/line_format.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

/**
 * The pieces of a JSON line that JsonLines::data, JsonLines::counter and the
 * Aggregator's rows share. Lines are written straight from the decoded record,
 * with no Metadata, no stringstream and no allocation. Field order is the
 * record's; values print as the types the decoder yields (64-bit integers,
 * double as ostream does, strings escaped).
 */
namespace dftracer {
namespace packed_json {

/** The line writer JsonLines uses (line_format.h). */
using Line = line_format::Line;

/** A number of one of the three numeric tags. */
union Number {
  int64_t i;
  uint64_t u;
  double d;
};

/** The numeric value of `v` in `n`; false, and `n` untouched, for text. */
inline bool to_number(const ipc::DecodedValue& v, Number& n) {
  switch (v.tag) {
    case SHM_VALUE_INT:
      n.i = v.i;
      return true;
    case SHM_VALUE_UINT:
      n.u = v.u;
      return true;
    case SHM_VALUE_DOUBLE:
      n.d = v.d;
      return true;
    default:
      return false;
  }
}

/** `n` as the type `tag` names. */
inline void number(Line& w, uint8_t tag, Number n) {
  if (tag == SHM_VALUE_INT)
    w.i64(n.i);
  else if (tag == SHM_VALUE_UINT)
    w.u64(n.u);
  else
    w.f64(n.d);
}

/** "key":value of a decoded metadata entry. */
inline void field(Line& w, const ipc::DecodedValue& v) {
  w.raw("\"");
  w.escaped(v.key);
  w.raw("\":");
  Number n;
  if (to_number(v, n)) {
    number(w, v.tag, n);
  } else if (v.tag == SHM_VALUE_STRING) {
    w.raw("\"");
    w.escaped(v.s);
    w.raw("\"");
  } else {
    w.raw(v.s);
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

/** The head shared by counters and aggregated rows: JsonLines::series. */
inline void series_head(Line& w, std::string_view name,
                        std::string_view category, uint8_t type,
                        TracePhaseType phase, uint64_t ts, int32_t pid,
                        uint64_t tid) {
  line_format::append<SeriesHead>(w, name, category,
                                  static_cast<unsigned>(type), ts,
                                  static_cast<unsigned>(phase), pid, tid);
}

}  // namespace packed_json
}  // namespace dftracer
#endif  // DFTRACER_SERIALIZATION_PACKED_JSON_H
