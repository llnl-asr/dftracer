#ifndef DFTRACER_SERIALIZATION_METADATA_JSON_H
#define DFTRACER_SERIALIZATION_METADATA_JSON_H
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/serialization/line_format.h>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <variant>

/**
 * The args of an event or the fields of a record, written as "key":value,...
 * straight from its Metadata into whatever holds the line: the trace buffer
 * (packed_json::Line) or the ring (ipc::encode_metadata). A writer
 * has raw(std::string_view), escaped(std::string_view), i64(int64_t),
 * u64(uint64_t) and f64(double).
 *
 * Values print as ostream prints them (double as %g). Keys and strings are
 * escaped; RawJson is written as it is.
 */
namespace dftracer {
namespace metadata_json {

/** A JSON string literal. */
template <typename W>
inline void quoted(W& w, std::string_view s) {
  w.raw("\"");
  w.escaped(s);
  w.raw("\"");
}
template <typename W>
inline void quoted(W& w, const char* s) {
  quoted(w, std::string_view(s == nullptr ? "" : s));
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
          quoted(w, std::string_view(x));
        } else if constexpr (std::is_same_v<T, const char*>) {
          quoted(w, x);
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
    quoted(w, item.first.view());
    w.raw(":");
    value(w, std::get<1>(item.second));
    ++n;
  }
  return n;
}

}  // namespace metadata_json
}  // namespace dftracer
#endif  // DFTRACER_SERIALIZATION_METADATA_JSON_H
