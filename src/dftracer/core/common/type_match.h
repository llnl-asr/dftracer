#ifndef DFTRACER_CORE_COMMON_TYPE_MATCH_H
#define DFTRACER_CORE_COMMON_TYPE_MATCH_H

#include <typeindex>
#include <typeinfo>

namespace dftracer {
/**
 * Whether a std::any (or an aggregated value) holds `b`: the same answer as
 * `a == b`, without its string compare in the common case.
 *
 * Metadata values are matched against a chain of candidate types (the
 * DFTRACER_FOR_EACH_*_TYPE macros), so most comparisons are mismatches. With
 * libstdc++, type_info::operator== compares the name pointers and, when they
 * differ, the names with strcmp -- one call per candidate, measured at 11% of
 * a producer's user time in producer/consumer mode. Two types whose mangled
 * names differ in the first character are different types, so that check
 * settles nearly every mismatch; only names that share it fall through to
 * the full comparison.
 */
inline bool same_type(const std::type_info& a, const std::type_info& b) {
  const char* x = a.name();
  const char* y = b.name();
  if (x == y) return true;
  if (x[0] != y[0]) return false;
  return a == b;
}

inline bool same_type(const std::type_index& a, const std::type_info& b) {
  const char* x = a.name();
  const char* y = b.name();
  if (x == y) return true;
  if (x[0] != y[0]) return false;
  return a == std::type_index(b);
}
}  // namespace dftracer

#endif  // DFTRACER_CORE_COMMON_TYPE_MATCH_H
