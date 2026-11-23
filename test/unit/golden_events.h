// The events that golden_lines.h holds the trace lines of. The lines were
// written by the Metadata serializer and aggregator that the packed ones
// replaced, so a test that runs these events through the current code and
// gets the same lines shows the trace output did not change.
#ifndef DFTRACER_TEST_GOLDEN_EVENTS_H
#define DFTRACER_TEST_GOLDEN_EVENTS_H
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/typedef.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace golden {

using dftracer::ArgList;
using dftracer::MetadataValue;
using dftracer::RawJson;

static char kHostname[] = "hosthash";
constexpr int kIndex = 7;
constexpr ProcessID kPid = 42;

struct Field {
  std::string key;
  MetadataValue value;
  MetadataType type;
};

struct Event {
  std::string name;
  std::string category;
  TraceEventType type;
  TimeResolution ts, dur;
  ThreadID tid;
  bool has_metadata;
  std::vector<Field> fields;
};

inline ArgList::Value str_value(const char* s) {
  ArgList::Value v{ArgList::Value::STRING, {}};
  v.s = s;
  return v;
}
inline ArgList::Value int_value(int64_t i) {
  ArgList::Value v{ArgList::Value::INT, {}};
  v.i = i;
  return v;
}
inline ArgList::Value id_value(uint64_t u) {
  ArgList::Value v{ArgList::Value::ID, {}};
  v.u = u;
  return v;
}

/** One line each, as a data event and as a counter. */
inline std::vector<Event> line_events() {
  const auto io = TraceEventType::TRACE_TYPE_LIBC_IO;
  const auto py = TraceEventType::TRACE_TYPE_PYTHON;
  const auto K = MetadataType::MT_KEY;
  const auto V = MetadataType::MT_VALUE;
  const auto I = MetadataType::MT_IGNORE;
  ArgList list{
      false,
      {{nullptr,
        {str_value("a"), int_value(-3), id_value(0x0123456789abcdefULL)}}}};
  ArgList nested{true,
                 {{"used", {id_value(1), id_value(0xffffffffffffffffULL)}},
                  {"made", {str_value("x\"y")}}}};
  return {
      {"open", "POSIX", io, 100, 5, 1, false, {}},
      {"read", "POSIX", io, 110, 6, 1, true, {}},
      {"read",
       "POSIX",
       io,
       120,
       7,
       1,
       true,
       {{"fhash", std::string("aaa"), K},
        {"fd", int64_t{3}, K},
        {"size", int64_t{-4096}, V},
        {"offset", uint64_t{1} << 40, V},
        {"bw", 1.5e9, V},
        {"mode", std::string("r"), V},
        {"note", std::string("x"), I}}},
      {"step",
       "app",
       py,
       130,
       8,
       2,
       true,
       {{"relations", RawJson("[{\"rel\":\"used\",\"id\":\"e1\"}]"), K},
        {"epoch", int64_t{2}, V}}},
      {"numbers",
       "app",
       py,
       140,
       0,
       3,
       true,
       {{"i64_min", std::numeric_limits<int64_t>::min(), K},
        {"i64_max", std::numeric_limits<int64_t>::max(), K},
        {"u64_max", std::numeric_limits<uint64_t>::max(), K},
        {"zero", int64_t{0}, K},
        {"tenth", 0.1, K},
        {"tiny", 1e-7, K},
        {"big", 1e21, K},
        {"frac", 123456789.125, K},
        {"neg", -2.5, K},
        {"whole", 3.0, K}}},
      {"strings",
       "app",
       py,
       150,
       1,
       3,
       true,
       {{"empty", std::string(""), K},
        {"quote", std::string("say \"hi\""), K},
        {"slash", std::string("a\\b/c"), K},
        {"control", std::string("tab\tline\nret\r\x01"), K},
        {"utf8", std::string("caf\xc3\xa9"), K},
        {"ptr", static_cast<const char*>("pointer"), K},
        {"raw", RawJson("{\"a\":[1,2],\"b\":null}"), K}}},
      {"lists",
       "app",
       py,
       160,
       2,
       4,
       true,
       {{"items", list, K}, {"relations", nested, K}}},
      {"only_ignored", "POSIX", io, 170, 3, 1, true, {{"note", int64_t{1}, I}}},
      {"say \"x\"",
       "c\\at\n",
       py,
       180,
       2,
       5,
       true,
       {{"k\"ey", std::string("v\tal"), K}, {"n\\m", int64_t{1}, V}}},
  };
}

/** Events aggregated together, as one run of a thread set would. */
inline std::vector<Event> aggregated_events() {
  const auto io = TraceEventType::TRACE_TYPE_LIBC_IO;
  const auto K = MetadataType::MT_KEY;
  const auto V = MetadataType::MT_VALUE;
  const auto I = MetadataType::MT_IGNORE;
  // The second interval starts more than trace_interval_ms later in any time
  // unit.
  const TimeResolution later = 1000ULL * 1000 * 1000 * 10;
  std::vector<Event> events;
  for (TimeResolution i = 0; i < 4; ++i) {
    for (const char* fhash : {"aaa", "bbb"}) {
      for (ThreadID tid : {1, 2}) {
        for (TimeResolution base : {TimeResolution(0), later}) {
          events.push_back({"read",
                            "POSIX",
                            io,
                            base + 100 + i,
                            10 + i,
                            tid,
                            true,
                            {{"fhash", std::string(fhash), K},
                             {"fd", int64_t{3}, K},
                             {"size", static_cast<int64_t>(100 * (i + 1)), V},
                             {"offset", static_cast<uint64_t>(i) << 33, V},
                             {"bw", 0.25 * (i + 1), V},
                             {"mode", std::string(i % 2 ? "r" : "w"), V},
                             {"note", std::string("x"), I}}});
        }
      }
    }
    events.push_back({"close", "POSIX", io, 200 + i, 1, 1, false, {}});
  }
  // A bucket of one event: its values are written as `key`, not key_sum,
  // key_min and key_max.
  events.push_back({"write",
                    "POSIX",
                    io,
                    300,
                    9,
                    1,
                    true,
                    {{"size", int64_t{7}, V}, {"bw", 2.0, V}}});
  // Negative and mixed values, and an MT_KEY that splits one name in two.
  for (int64_t v : {-5, 3, -1}) {
    events.push_back({"seek",
                      "POSIX",
                      io,
                      400 + static_cast<TimeResolution>(v + 10),
                      2,
                      3,
                      true,
                      {{"whence", std::string(v < 0 ? "cur" : "set"), K},
                       {"delta", v, V},
                       {"frac", static_cast<double>(v) / 4, V}}});
  }
  // Fields that are only ignored, and a key without values.
  events.push_back(
      {"stat", "POSIX", io, 500, 4, 1, true, {{"note", std::string("x"), I}}});
  events.push_back({"stat",
                    "POSIX",
                    io,
                    501,
                    6,
                    1,
                    true,
                    {{"fhash", std::string("ccc"), K}}});
  // Escapes in a name and a key.
  events.push_back(
      {"we\"ird",
       "POSIX",
       io,
       600,
       4,
       1,
       true,
       {{"f\"h", std::string("a\nb"), K}, {"s\\z", int64_t{5}, V}}});
  return events;
}

/**
 * `line` with its args sorted: the old serializer wrote them in hash order,
 * which JSON readers do not see.
 */
inline std::string normalized(std::string line) {
  while (!line.empty() && (line.back() == '\n' || line.back() == '\0'))
    line.pop_back();
  const std::string marker = ",\"args\":{";
  const size_t at = line.find(marker);
  if (at == std::string::npos) return line;
  const std::string body =
      line.substr(at + marker.size(), line.size() - at - marker.size() - 2);
  std::vector<std::string> parts;
  std::string part;
  int depth = 0;
  bool quoted = false, escaped = false;
  for (char c : body) {
    if (escaped) {
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      quoted = !quoted;
    } else if (!quoted && (c == '{' || c == '[')) {
      ++depth;
    } else if (!quoted && (c == '}' || c == ']')) {
      --depth;
    } else if (!quoted && depth == 0 && c == ',') {
      parts.push_back(part);
      part.clear();
      continue;
    }
    part += c;
  }
  parts.push_back(part);
  std::sort(parts.begin(), parts.end());
  std::string out = line.substr(0, at) + " |";
  for (const auto& p : parts) out += " " + p;
  return out;
}

/** The lines of `size` bytes at `buffer`, each normalized, sorted. */
inline std::vector<std::string> lines_of(const char* buffer, size_t size) {
  std::vector<std::string> lines;
  std::string line;
  for (size_t i = 0; i < size; ++i) {
    if (buffer[i] == '\n') {
      lines.push_back(normalized(line));
      line.clear();
    } else {
      line += buffer[i];
    }
  }
  std::sort(lines.begin(), lines.end());
  return lines;
}

}  // namespace golden
#endif  // DFTRACER_TEST_GOLDEN_EVENTS_H
