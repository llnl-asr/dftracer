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

/** Bounded writer for one line of at most DFTRACER_SERIALIZATION_EVENT_MAX
 * bytes, newline included; a longer one is cut, as JsonLines cuts it. */
class Line {
 public:
  explicit Line(char* out)
      : begin_(out),
        p_(out),
        end_(out + DFTRACER_SERIALIZATION_EVENT_MAX - 2) {}

  void raw(std::string_view s) {
    size_t n = s.size();
    if (n > static_cast<size_t>(end_ - p_)) {
      n = static_cast<size_t>(end_ - p_);
      cut_ = true;
    }
    std::memcpy(p_, s.data(), n);
    p_ += n;
  }
  void i64(int64_t v) { number(v); }
  void u64(uint64_t v) { number(v); }
  void f64(double v) {
    // ostream's default for double: %g with precision 6.
    char tmp[32];
    const int n = std::snprintf(tmp, sizeof(tmp), "%g", v);
    raw(std::string_view(tmp, n > 0 ? static_cast<size_t>(n) : 0));
  }
  /** "key":value of a decoded metadata entry. */
  void field(const ipc::DecodedValue& v) {
    raw("\"");
    raw(v.key);
    raw("\":");
    switch (v.tag) {
      case SHM_VALUE_INT:
        i64(v.i);
        break;
      case SHM_VALUE_UINT:
        u64(v.u);
        break;
      case SHM_VALUE_DOUBLE:
        f64(v.d);
        break;
      case SHM_VALUE_STRING:
        raw("\"");
        raw(v.s);
        raw("\"");
        break;
      case SHM_VALUE_RAW_JSON:
        raw(v.s);
        break;
    }
  }
  /** Ends the line; returns its size. */
  size_t finish(const char* what, int index) {
    if (cut_) {
      DFTRACER_LOG_WARN("packed_json.%s truncated event %d to %zu bytes", what,
                        index, DFTRACER_SERIALIZATION_EVENT_MAX - 1);
    }
    *p_++ = '\n';
    *p_ = '\0';
    return static_cast<size_t>(p_ - begin_);
  }

 private:
  template <typename T>
  void number(T v) {
    char tmp[24];
    auto r = std::to_chars(tmp, tmp + sizeof(tmp), v);
    raw(std::string_view(tmp, static_cast<size_t>(r.ptr - tmp)));
  }
  char* begin_;
  char* p_;
  char* end_;
  bool cut_ = false;
};

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
  w.raw("{\"id\":");
  w.i64(h.index);
  w.raw(",\"name\":\"");
  w.raw(e.name);
  w.raw("\",\"cat\":\"");
  w.raw(e.category);
  w.raw("\",\"type\":");
  w.u64(h.type);
  w.raw(",\"pid\":");
  w.i64(h.process_id);
  w.raw(",\"tid\":");
  w.u64(h.thread_id);
  w.raw(",\"ts\":");
  w.u64(h.start_time);
  w.raw(",\"dur\":");
  w.u64(h.duration);
  w.raw(",\"ph\":");
  w.u64(static_cast<unsigned>(TracePhaseType::TRACE_PHASE_COMPLETE));
  if (args) {
    w.raw(",\"args\":{\"hhash\":\"");
    w.raw(hostname_hash);
    w.raw("\"");
    for (const auto& v : e.values) {
      if (!include && !is_relations(v)) continue;
      w.raw(",");
      w.field(v);
    }
    w.raw("}");
  }
  w.raw("}");
  return w.finish("data", h.index);
}

/** The head shared by counters and aggregated rows: JsonLines::series. */
inline void series_head(Line& w, std::string_view name,
                        std::string_view category, uint8_t type,
                        TracePhaseType phase, uint64_t ts, int32_t pid,
                        uint64_t tid) {
  w.raw("{\"name\":\"");
  w.raw(name);
  w.raw("\",\"cat\":\"");
  w.raw(category);
  w.raw("\",\"type\":");
  w.u64(type);
  w.raw(",\"ts\":");
  w.u64(ts);
  w.raw(",\"ph\":");
  w.u64(static_cast<unsigned>(phase));
  w.raw(",\"pid\":");
  w.i64(pid);
  w.raw(",\"tid\":");
  w.u64(tid);
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
    w.raw(",\"args\":{\"hhash\":\"");
    w.raw(hostname_hash);
    w.raw("\"");
    for (const auto& v : e.values) {
      w.raw(",");
      w.field(v);
    }
    w.raw("}");
  }
  w.raw("}");
  return w.finish("counter", h.index);
}

}  // namespace packed_json
}  // namespace dftracer
#endif  // DFTRACER_SERIALIZATION_PACKED_JSON_H
