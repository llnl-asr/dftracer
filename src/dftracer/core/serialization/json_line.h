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
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace dftracer {
/**
 * The JSON text that every event of one compact::Decoder shape shares (name,
 * category, type and keys), built once and reused. One per decoder: reset()
 * whenever that decoder resets.
 */
class ShapeLines {
 public:
  static constexpr size_t kNoShape = ~size_t{0};
  void reset() { shapes_.clear(); }

 private:
  friend class JsonLines;
  struct Shape {
    bool built = false;
    uint8_t type = 0;
    // ,"name":"...","cat":"...","type":N,"pid":
    std::string head;
    // Per value: ,"key": and the opening quote of a string, ending at ends[i].
    std::string keys;
    std::vector<uint32_t> ends;
    std::vector<ShmValueTag> tags;
    // Per value: the last string that needed no escaping. The views point
    // into the decoder's block, so the same view is the same text.
    std::vector<std::string_view> plain;
    // The longest line of this shape, apart from ids_, args_head_ and the
    // values at `strings`: those vary by event.
    size_t fixed = 0;
    std::vector<uint32_t> strings;
  };
  std::vector<Shape> shapes_;
  // ,"ph":N,"args":{"hhash":"..."  and  ,"ph":N
  std::string args_head_, no_args_;
  const char* hostname_ = nullptr;
  bool have_ids_ = false;
  int32_t pid_ = 0;
  uint64_t tid_ = 0;
  uint64_t ts_hi_ = 0;
  // pid,"tid":tid,"ts": and the digits of ts above its last 8, if any.
  std::string ids_;
};

class JsonLines {
  HashType hostname_hash{};
  // Shared layout for the timestamped, duration-less records: counters and
  // aggregated events differ only in their phase. Takes ownership of metadata.
  size_t series(char* buffer, int index, ConstEventNameType event_name,
                ConstEventNameType category, TraceEventType type,
                TracePhaseType phase_type, TimeResolution start_time,
                ProcessID process_id, ThreadID thread_id,
                dftracer::Metadata* metadata);

 public:
  JsonLines() = default;
  HashType hostname() const { return hostname_hash; }
  size_t initialize(char* buffer, HashType hostname_hash);
  // A data event, written straight from its decoded record.
  size_t data(char* out, const ipc::DecodedEvent& event) const;
  // The same line, with the text of `shape` (compact::Decoder::shape(), or
  // ShapeLines::kNoShape) taken from `lines`.
  size_t data(char* out, const ipc::DecodedEvent& event, ShapeLines& lines,
              size_t shape) const;
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

inline size_t JsonLines::data(char* out, const ipc::DecodedEvent& e,
                              ShapeLines& lines, size_t shape) const {
  const ipc::EventHead& h = e.head;
  const bool has_metadata = (h.flags & SHM_EVENT_HAS_METADATA) != 0;
  const bool include = (h.flags & SHM_EVENT_INC_METADATA) != 0;
  if (shape == ShapeLines::kNoShape || (has_metadata && !include))
    return data(out, e);
  using line_format::Text;
  if (shape >= lines.shapes_.size()) lines.shapes_.resize(shape + 1);
  ShapeLines::Shape& sh = lines.shapes_[shape];
  if (!sh.built) {
    sh.built = true;
    sh.type = h.type;
    Text head{sh.head};
    head.raw(",\"name\":\"");
    line_format::escaped(head, e.name);
    head.raw("\",\"cat\":\"");
    line_format::escaped(head, e.category);
    head.raw("\",\"type\":");
    head.raw(std::to_string(static_cast<unsigned>(h.type)));
    head.raw(",\"pid\":");
    Text keys{sh.keys};
    for (const auto& v : e.values) {
      keys.raw(",\"");
      line_format::escaped(keys, v.key);
      keys.raw(v.tag == SHM_VALUE_STRING ? "\":\"" : "\":");
      sh.ends.push_back(static_cast<uint32_t>(sh.keys.size()));
      sh.tags.push_back(v.tag);
    }
    sh.plain.resize(sh.tags.size());
    // {"id":, an int32, ,"dur":, a uint64, then }} and the newline.
    sh.fixed = 6 + 11 + sh.head.size() + 7 + 20 + sh.keys.size() + 3;
    for (size_t i = 0; i < sh.tags.size(); ++i) {
      if (sh.tags[i] == SHM_VALUE_STRING || sh.tags[i] == SHM_VALUE_RAW_JSON)
        sh.strings.push_back(static_cast<uint32_t>(i));
      // A number, a %g double, or a string's closing quote.
      sh.fixed += sh.tags[i] == SHM_VALUE_DOUBLE ? 31 : 21;
    }
  }
  // A decoder shape fixes the tag of each value, so only the count is
  // checked.
  if (sh.type != h.type || sh.tags.size() != e.values.size())
    return data(out, e);
  if (lines.hostname_ != hostname_hash || lines.no_args_.empty()) {
    const std::string ph = std::to_string(
        static_cast<unsigned>(TracePhaseType::TRACE_PHASE_COMPLETE));
    lines.hostname_ = hostname_hash;
    lines.no_args_ = ",\"ph\":" + ph;
    lines.args_head_ = lines.no_args_;
    Text args{lines.args_head_};
    line_format::append<packed_json::ArgsHead>(args, hostname_hash);
  }
  constexpr uint64_t kLow = 100000000;
  const uint64_t ts_hi = h.start_time / kLow;
  if (!lines.have_ids_ || lines.pid_ != h.process_id ||
      lines.tid_ != h.thread_id || lines.ts_hi_ != ts_hi) {
    lines.have_ids_ = true;
    lines.pid_ = h.process_id;
    lines.tid_ = h.thread_id;
    lines.ts_hi_ = ts_hi;
    lines.ids_ = std::to_string(h.process_id) +
                 ",\"tid\":" + std::to_string(h.thread_id) + ",\"ts\":";
    if (ts_hi != 0) lines.ids_ += std::to_string(ts_hi);
  }
  const ipc::DecodedValue* values = e.values.data();
  size_t bound = sh.fixed + lines.ids_.size() + 20 + lines.args_head_.size();
  for (uint32_t i : sh.strings) bound += 6 * values[i].s.size();
  // Line cuts text at DFTRACER_SERIALIZATION_EVENT_MAX - 2 bytes; bound
  // also counts the newline. A line that may not fit takes the full path,
  // which writes the same text.
  if (bound > DFTRACER_SERIALIZATION_EVENT_MAX - 2) return data(out, e);
  using line_format::detail::emit;
  char* p = emit(out, "{\"id\":");
  p = emit(p, static_cast<int64_t>(h.index));
  p = emit(p, sh.head);
  p = emit(p, lines.ids_);
  if (ts_hi != 0) {
    line_format::detail::eight_digits(
        p, static_cast<uint32_t>(h.start_time - ts_hi * kLow));
    p += 8;
  } else {
    p = emit(p, static_cast<uint64_t>(h.start_time));
  }
  p = emit(p, ",\"dur\":");
  p = emit(p, static_cast<uint64_t>(h.duration));
  if (!has_metadata) {
    p = emit(p, lines.no_args_);
    *p++ = '}';
  } else {
    p = emit(p, lines.args_head_);
    const char* keys = sh.keys.data();
    const uint32_t* ends = sh.ends.data();
    const size_t count = sh.tags.size();
    uint32_t at = 0;
    for (size_t i = 0; i < count; ++i) {
      const ipc::DecodedValue& v = values[i];
      p = emit(p, std::string_view(keys + at, ends[i] - at));
      at = ends[i];
      switch (v.tag) {
        case SHM_VALUE_INT:
          p = emit(p, static_cast<int64_t>(v.i));
          break;
        case SHM_VALUE_UINT:
          p = emit(p, static_cast<uint64_t>(v.u));
          break;
        case SHM_VALUE_DOUBLE: {
          packed_json::Line w(p);
          w.f64(v.d);
          p += w.size();
          break;
        }
        case SHM_VALUE_STRING: {
          std::string_view& seen = sh.plain[i];
          if (v.s.data() == seen.data() && v.s.size() == seen.size()) {
            p = emit(p, v.s);
          } else if (line_format::plain(v.s)) {
            seen = v.s;
            p = emit(p, v.s);
          } else {
            packed_json::Line w(p);
            w.escaped(v.s);
            p += w.size();
          }
          *p++ = '"';
          break;
        }
        case SHM_VALUE_RAW_JSON:
          p = emit(p, v.s);
          break;
      }
    }
    *p++ = '}';
    *p++ = '}';
  }
  *p++ = '\n';
  *p = '\0';
  return static_cast<size_t>(p - out);
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