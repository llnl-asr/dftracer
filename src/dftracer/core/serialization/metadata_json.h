#ifndef DFTRACER_SERIALIZATION_METADATA_JSON_H
#define DFTRACER_SERIALIZATION_METADATA_JSON_H
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/macros.h>
#include <dftracer/core/common/type_match.h>

#include <any>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <string_view>
#include <type_traits>
#include <typeinfo>

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

namespace detail {

// The types value() writes, most common first: an interceptor's hashes and
// descriptors, then sizes and offsets. A Kind is the 1-based position here.
#define DFTRACER_JSON_TYPES(X) \
  X(1, HashType)               \
  X(2, const char*)            \
  X(3, int)                    \
  X(4, long)                   \
  X(5, unsigned long)          \
  X(6, unsigned int)           \
  X(7, long long)              \
  X(8, unsigned long long)     \
  X(9, std::string)            \
  X(10, RawJson)               \
  X(11, ArgList)               \
  X(12, double)                \
  X(13, float)                 \
  X(14, uint16_t)              \
  X(15, size_t)                \
  X(16, ssize_t)               \
  X(17, off_t)                 \
  X(18, off64_t)               \
  X(19, mode_t)

using Kind = int;
constexpr Kind kUnknown = 0;

inline Kind kind_of(const std::type_info& id) {
#define DFTRACER_JSON_KIND(K, TYPE) \
  if (dftracer::same_type(id, typeid(TYPE))) return K;
  DFTRACER_JSON_TYPES(DFTRACER_JSON_KIND)
#undef DFTRACER_JSON_KIND
  return kUnknown;
}

template <typename T, typename W>
inline void put_as(W& w, const std::any& v) {
  const T& x = *std::any_cast<T>(&v);
  if constexpr (std::is_same_v<T, RawJson>) {
    w.raw(x.value);
  } else if constexpr (std::is_same_v<T, ArgList>) {
    list(w, x);
  } else if constexpr (std::is_same_v<T, std::string>) {
    w.raw("\"");
    w.raw(x);
    w.raw("\"");
  } else if constexpr (std::is_pointer_v<T>) {
    w.raw("\"");
    if (x != nullptr) w.raw(std::string_view(x));
    w.raw("\"");
  } else if constexpr (std::is_floating_point_v<T>) {
    w.f64(static_cast<double>(x));
  } else if constexpr (std::is_signed_v<T>) {
    w.i64(static_cast<int64_t>(x));
  } else {
    w.u64(static_cast<uint64_t>(x));
  }
}

template <typename W>
inline void put(W& w, Kind kind, const std::any& v) {
  switch (kind) {
#define DFTRACER_JSON_PUT(K, TYPE) \
  case K:                          \
    put_as<TYPE>(w, v);            \
    return;
    DFTRACER_JSON_TYPES(DFTRACER_JSON_PUT)
#undef DFTRACER_JSON_PUT
  }
}

#undef DFTRACER_JSON_TYPES

}  // namespace detail

/** A value; false, writing nothing, for a type it does not know. */
template <typename W>
inline bool value(W& w, const std::any& v) {
  const detail::Kind kind = detail::kind_of(v.type());
  if (kind == detail::kUnknown) return false;
  detail::put(w, kind, v);
  return true;
}

/** Whether value() knows the type. */
inline bool known(const std::any& v) {
  return detail::kind_of(v.type()) != detail::kUnknown;
}

/** "key":value,... (a comma before each when `leading_comma`); entries of
 * unknown types are left out. Returns how many were written. */
template <typename W>
inline size_t fields(W& w, const Metadata& metadata, bool leading_comma) {
  size_t n = 0;
  for (const auto& item : metadata) {
    const std::any& v = std::get<1>(item.second);
    const detail::Kind kind = detail::kind_of(v.type());
    if (kind == detail::kUnknown) continue;
    if (n > 0 || leading_comma) w.raw(",");
    w.raw("\"");
    w.raw(item.first);
    w.raw("\":");
    detail::put(w, kind, v);
    ++n;
  }
  return n;
}

}  // namespace metadata_json
}  // namespace dftracer
#endif  // DFTRACER_SERIALIZATION_METADATA_JSON_H
