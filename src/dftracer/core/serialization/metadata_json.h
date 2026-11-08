#ifndef DFTRACER_SERIALIZATION_METADATA_JSON_H
#define DFTRACER_SERIALIZATION_METADATA_JSON_H
#include <dftracer/core/common/datastructure.h>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <string_view>
#include <type_traits>
#include <variant>

/**
 * The args of an event or the fields of a record, written as "key":value,...
 * straight from its Metadata into whatever holds the line: the trace buffer
 * (packed_json::Line), the ring (ipc::encode_metadata) or a stream. A writer
 * has raw(std::string_view), i64(int64_t), u64(uint64_t) and f64(double).
 *
 * Values print as ostream prints them (double as %g); strings are not escaped,
 * except in lists, whose strings come from callers as they are.
 */
namespace dftracer {
namespace metadata_json {

/** Writer over a std::ostream. */
class Stream {
 public:
  explicit Stream(std::ostream& out) : out_(out) {}
  void raw(std::string_view s) { out_.write(s.data(), s.size()); }
  void i64(int64_t v) { out_ << v; }
  void u64(uint64_t v) { out_ << v; }
  void f64(double v) { out_ << v; }

 private:
  std::ostream& out_;
};

/** A JSON string literal: quotes, backslashes and control characters
 * escaped. */
template <typename W>
inline void quoted(W& w, const char* s) {
  w.raw("\"");
  const char* run = s == nullptr ? "" : s;
  const char* p = run;
  for (; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c != '"' && c != '\\' && c >= 0x20) continue;
    w.raw(std::string_view(run, static_cast<size_t>(p - run)));
    char esc[8];
    if (c < 0x20)
      std::snprintf(esc, sizeof(esc), "\\u%04x", c);
    else
      std::snprintf(esc, sizeof(esc), "\\%c", c);
    w.raw(esc);
    run = p + 1;
  }
  w.raw(std::string_view(run, static_cast<size_t>(p - run)));
  w.raw("\"");
}

template <typename W>
inline void list_values(W& w, const ArgList::Sub& sub) {
  w.raw("[");
  for (size_t i = 0; i < sub.values.size(); ++i) {
    if (i) w.raw(",");
    const ArgList::Value& v = sub.values[i];
    if (v.kind == ArgList::Value::STRING) {
      quoted(w, v.s);
    } else if (v.kind == ArgList::Value::INT) {
      w.i64(v.i);
    } else {
      static const char digits[] = "0123456789abcdef";
      char hex[18];
      hex[0] = hex[17] = '"';
      uint64_t id = v.u;
      for (int d = 16; d >= 1; --d, id >>= 4) hex[d] = digits[id & 0xfu];
      w.raw(std::string_view(hex, sizeof(hex)));
    }
  }
  w.raw("]");
}

/** [...] or {"sub":[...],...}. */
template <typename W>
inline void list(W& w, const ArgList& l) {
  if (!l.nested) {
    if (l.subs.empty())
      w.raw("[]");
    else
      list_values(w, l.subs.front());
    return;
  }
  w.raw("{");
  for (size_t i = 0; i < l.subs.size(); ++i) {
    if (i) w.raw(",");
    quoted(w, l.subs[i].name);
    w.raw(":");
    list_values(w, l.subs[i]);
  }
  w.raw("}");
}

/** A value. */
template <typename W>
inline void value(W& w, const MetadataValue& v) {
  std::visit(
      [&w](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, RawJson>) {
          w.raw(x.value);
        } else if constexpr (std::is_same_v<T, ArgList>) {
          list(w, x);
        } else if constexpr (std::is_same_v<T, std::string>) {
          w.raw("\"");
          w.raw(x);
          w.raw("\"");
        } else if constexpr (std::is_same_v<T, const char*>) {
          w.raw("\"");
          if (x != nullptr) w.raw(std::string_view(x));
          w.raw("\"");
        } else if constexpr (std::is_same_v<T, double>) {
          w.f64(x);
        } else if constexpr (std::is_same_v<T, int64_t>) {
          w.i64(x);
        } else {
          w.u64(x);
        }
      },
      v);
}

/** "key":value,... (a comma before each when `leading_comma`). Returns how
 * many were written. */
template <typename W>
inline size_t fields(W& w, const Metadata& metadata, bool leading_comma) {
  size_t n = 0;
  for (const auto& item : metadata) {
    if (n > 0 || leading_comma) w.raw(",");
    w.raw("\"");
    w.raw(item.first);
    w.raw("\":");
    value(w, std::get<1>(item.second));
    ++n;
  }
  return n;
}

}  // namespace metadata_json
}  // namespace dftracer
#endif  // DFTRACER_SERIALIZATION_METADATA_JSON_H
