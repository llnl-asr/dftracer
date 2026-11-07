#ifndef DFTRACER_SERIALIZATION_LINE_FORMAT_H
#define DFTRACER_SERIALIZATION_LINE_FORMAT_H

#include <dftracer/core/common/logging.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// The longest line any serializer writes, newline included.
static constexpr size_t DFTRACER_SERIALIZATION_EVENT_MAX = 16 * 1024;

/**
 * Trace lines with the format parsed at compile time, after the line_format
 * of dftracer-utils (utilities/fileio/line_format.h), in C++17.
 *
 * A format is a named type holding a string_view `value`:
 *
 *   DFTRACER_LINE_FORMAT(EventHead, R"({"id":{},"name":"{}")");
 *   line_format::append<EventHead>(line, index, name);
 *
 * Only the two characters `{}` are a placeholder and take the next argument;
 * every other brace is literal, so JSON needs no doubling, and `{{}}` is a
 * literal `{}`. The format must not hold a newline (the line adds its own).
 * Splitting the format into literal pieces, counting the placeholders and
 * checking every argument's type all happen at compile time: a wrong count or
 * an unsupported type does not compile. What is left at run time is copying
 * each piece (a memcpy of constant size) and converting each argument --
 * integers with std::to_chars, strings copied as they are (no escaping, as
 * the snprintf("%s") this replaces did). Nothing allocates.
 *
 * Arguments: integers (not bool or character types; cast enums), const
 * char* / char* (null writes nothing), std::string_view, std::string.
 */
namespace dftracer {
namespace line_format {

/**
 * Bounded writer for one line of at most DFTRACER_SERIALIZATION_EVENT_MAX
 * bytes, newline included; a longer one is cut. It is also the writer
 * metadata_json writes an event's args into, so a line and its args go
 * straight into the trace buffer.
 */
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
  void raw(const char* s) {
    if (s != nullptr) raw(std::string_view(s));
  }
  void i64(int64_t v) { number(v); }
  void u64(uint64_t v) { number(v); }
  void f64(double v) {
    // ostream's default for double: %g with precision 6.
    char tmp[32];
    const int n = std::snprintf(tmp, sizeof(tmp), "%g", v);
    raw(std::string_view(tmp, n > 0 ? static_cast<size_t>(n) : 0));
  }
  template <typename T>
  void number(T v) {
    // 20 digits and a sign hold any 64-bit integer.
    if (static_cast<size_t>(end_ - p_) >= 24) {
      p_ = std::to_chars(p_, end_, v).ptr;
      return;
    }
    char tmp[24];
    auto r = std::to_chars(tmp, tmp + sizeof(tmp), v);
    raw(std::string_view(tmp, static_cast<size_t>(r.ptr - tmp)));
  }
  bool cut() const { return cut_; }
  size_t size() const { return static_cast<size_t>(p_ - begin_); }
  /** Ends the line (newline, then a NUL not counted); returns its size. */
  size_t finish(const char* what, int index) {
    if (cut_) {
      DFTRACER_LOG_WARN("%s truncated event %d to %zu bytes", what, index,
                        DFTRACER_SERIALIZATION_EVENT_MAX - 1);
    }
    *p_++ = '\n';
    *p_ = '\0';
    return static_cast<size_t>(p_ - begin_);
  }

 private:
  char* begin_;
  char* p_;
  char* end_;
  bool cut_ = false;
};

namespace detail {

// Walks `f`: lit(c) for each literal character, hole() for each `{}`.
// Returns false for a format holding a newline or carriage return.
template <typename Lit, typename Hole>
constexpr bool scan(std::string_view f, Lit&& lit, Hole&& hole) {
  for (size_t i = 0; i < f.size(); ++i) {
    const char c = f[i];
    if (c == '\n' || c == '\r') return false;
    if (f.substr(i, 4) == "{{}}") {
      lit('{');
      lit('}');
      i += 3;
    } else if (f.substr(i, 2) == "{}") {
      hole();
      ++i;
    } else {
      lit(c);
    }
  }
  return true;
}

struct Counter {
  size_t holes = 0;
  bool ok = false;
};

constexpr Counter count(std::string_view f) {
  Counter c;
  c.ok = scan(f, [](char) {}, [&c]() { ++c.holes; });
  return c;
}

// The literal text with the placeholders taken out; piece i is
// text[i == 0 ? 0 : end[i - 1], end[i]).
template <size_t H, size_t N>
struct Pieces {
  char text[N + 1] = {};
  size_t end[H + 1] = {};
};

template <size_t H, size_t N>
constexpr Pieces<H, N> split(std::string_view f) {
  Pieces<H, N> p{};
  size_t total = 0, hole = 0;
  scan(
      f, [&](char c) { p.text[total++] = c; },
      [&]() { p.end[hole++] = total; });
  p.end[H] = total;
  return p;
}

template <typename T>
constexpr bool is_int =
    std::is_integral_v<T> && !std::is_same_v<T, bool> &&
    !std::is_same_v<T, char> && !std::is_same_v<T, signed char> &&
    !std::is_same_v<T, unsigned char> && !std::is_same_v<T, wchar_t> &&
    !std::is_same_v<T, char16_t> && !std::is_same_v<T, char32_t>;

template <typename T>
constexpr bool is_str =
    std::is_same_v<T, const char*> || std::is_same_v<T, char*> ||
    std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>;

template <typename T>
constexpr bool is_arg = is_int<T> || is_str<T>;

}  // namespace detail

/** A format, parsed once per type at compile time. */
template <typename F>
struct Compiled {
  static constexpr std::string_view format = F::value;
  static constexpr detail::Counter counted = detail::count(format);
  static_assert(counted.ok,
                "a line format must not contain a newline; the line adds it");
  static constexpr size_t holes = counted.holes;
  static constexpr detail::Pieces<holes, format.size()> pieces =
      detail::split<holes, format.size()>(format);

  template <size_t I>
  static constexpr std::string_view piece() {
    constexpr size_t b = I == 0 ? 0 : pieces.end[I - 1];
    return std::string_view(pieces.text + b, pieces.end[I] - b);
  }
};

namespace detail {

template <typename W, typename T>
inline void put(W& w, const T& v) {
  static_assert(is_arg<T>,
                "unsupported line argument: use an integer (cast enums) or "
                "a string");
  if constexpr (is_int<T>) {
    w.number(v);
  } else if constexpr (std::is_same_v<T, const char*> ||
                       std::is_same_v<T, char*>) {
    w.raw(static_cast<const char*>(v));
  } else {
    w.raw(std::string_view(v));
  }
}

template <typename F, typename W, size_t... I, typename... A>
inline void append(W& w, std::index_sequence<I...>, const A&... a) {
  using C = Compiled<F>;
  ((w.raw(C::template piece<I>()), put(w, a)), ...);
  constexpr std::string_view last = C::template piece<sizeof...(I)>();
  if constexpr (!last.empty()) w.raw(last);
}

}  // namespace detail

namespace detail {
// A string literal as const char*; every other argument by reference.
template <typename T>
constexpr decltype(auto) arg(const T& a) {
  if constexpr (std::is_array_v<T>)
    return static_cast<const char*>(a);
  else
    return (a);
}
}  // namespace detail

/** Appends format F with its placeholders filled by `a`, in order. */
template <typename F, typename W, typename... A>
inline void append(W& w, const A&... a) {
  static_assert(Compiled<F>::holes == sizeof...(A),
                "the number of arguments does not match the format's {}");
  detail::append<F>(w, std::index_sequence_for<A...>{}, detail::arg(a)...);
}

}  // namespace line_format
}  // namespace dftracer

/** Declares the line format `name` (a type) holding `text`. */
#define DFTRACER_LINE_FORMAT(name, text)              \
  struct name {                                       \
    static constexpr std::string_view value = (text); \
  }

#endif  // DFTRACER_SERIALIZATION_LINE_FORMAT_H
