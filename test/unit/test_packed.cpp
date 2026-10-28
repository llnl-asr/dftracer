// The service's packed path (packed_json, PackedAggregator) writes the lines
// the Metadata path (JsonLines, Aggregator) writes for the same events. Args
// are compared as sets: JSON does not order them, and JsonLines follows its
// unordered_map.
#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/aggregator/packed_aggregator.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/ipc/event_codec.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/serialization/packed_json.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <algorithm>
#include <any>
#include <cstdlib>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "check.h"

using namespace dftracer;

static char kHostname[] = "hosthash";

struct EventField {
  std::string key;
  std::any value;
  MetadataType type;
};

struct Event {
  const char* name;
  const char* category;
  TimeResolution ts, dur;
  ThreadID tid;
  bool has_metadata;
  std::vector<EventField> fields;
};

static Metadata* metadata_of(const Event& e) {
  if (!e.has_metadata) return nullptr;
  auto* md = new Metadata();
  for (const auto& f : e.fields) md->insert_or_assign(f.key, f.value, f.type);
  return md;
}

// The event as the service decodes it from the ring.
static void packed_of(const Event& e, bool include, std::vector<uint8_t>& bytes,
                      ipc::DecodedEvent& out) {
  Metadata* md = metadata_of(e);
  ipc::ByteWriter w(bytes);
  ipc::encode_event(w, 7, e.name, e.category,
                    TraceEventType::TRACE_TYPE_LIBC_IO, e.ts, e.dur, md, 42,
                    e.tid, include ? SHM_EVENT_INC_METADATA : 0);
  delete md;
  DFT_CHECK(ipc::decode_event(bytes.data(), bytes.size(), out));
}

// Splits a line's args at its top-level commas and sorts them.
static std::string normalized(std::string line) {
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
  bool quoted = false;
  for (char c : body) {
    if (c == '"') quoted = !quoted;
    if (!quoted && (c == '{' || c == '[')) ++depth;
    if (!quoted && (c == '}' || c == ']')) --depth;
    if (!quoted && depth == 0 && c == ',') {
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

static std::vector<std::string> lines_of(const char* buffer, size_t size) {
  std::vector<std::string> lines;
  std::string all(buffer, size), line;
  for (char c : all) {
    if (c == '\n') {
      lines.push_back(normalized(line));
      line.clear();
    } else {
      line += c;
    }
  }
  std::sort(lines.begin(), lines.end());
  return lines;
}

static const std::vector<Event> kEvents = {
    {"open", "POSIX", 100, 5, 1, false, {}},
    {"read", "POSIX", 110, 6, 1, true, {}},
    {"read",
     "POSIX",
     120,
     7,
     1,
     true,
     {{"fhash", std::string("aaa"), MetadataType::MT_KEY},
      {"fd", static_cast<int64_t>(3), MetadataType::MT_KEY},
      {"size", static_cast<int64_t>(-4096), MetadataType::MT_VALUE},
      {"offset", static_cast<uint64_t>(1) << 40, MetadataType::MT_VALUE},
      {"bw", 1.5e9, MetadataType::MT_VALUE},
      {"mode", std::string("r"), MetadataType::MT_VALUE},
      {"note", std::string("x"), MetadataType::MT_IGNORE}}},
    {"step",
     "app",
     130,
     8,
     2,
     true,
     {{"relations", RawJson("[{\"rel\":\"used\",\"id\":\"e1\"}]"),
       MetadataType::MT_KEY},
      {"epoch", static_cast<int64_t>(2), MetadataType::MT_VALUE}}},
};

static void check_data() {
  auto js = std::make_shared<JsonLines>();
  js->initialize(nullptr, kHostname);
  static char expect[DFTRACER_SERIALIZATION_EVENT_MAX];
  static char got[DFTRACER_SERIALIZATION_EVENT_MAX];
  for (bool include : {true, false}) {
    js->set_include_metadata(include);
    for (const Event& e : kEvents) {
      std::vector<uint8_t> bytes;
      ipc::DecodedEvent decoded;
      packed_of(e, include, bytes, decoded);
      const size_t n = js->data(expect, 7, e.name, e.category,
                                TraceEventType::TRACE_TYPE_LIBC_IO, e.ts, e.dur,
                                metadata_of(e), 42, e.tid);
      const size_t m = packed_json::data(got, decoded, kHostname);
      DFT_CHECK(normalized(std::string(expect, n)) ==
                normalized(std::string(got, m)));
      const size_t c = js->counter(expect, 7, e.name, e.category,
                                   TraceEventType::TRACE_TYPE_LIBC_IO, e.ts, 42,
                                   e.tid, metadata_of(e));
      const size_t d = packed_json::counter(got, decoded, kHostname);
      DFT_CHECK(normalized(std::string(expect, c)) ==
                normalized(std::string(got, d)));
    }
  }
  std::cout << "✓ packed data and counter lines match JsonLines" << std::endl;
}

static void check_aggregation() {
  auto config = Singleton<ConfigurationManager>::get_instance();
  // Same bucket (keys and thread), another key value, another thread, a
  // later interval, and events with no keys: the second interval starts more
  // than trace_interval_ms later in any time unit.
  const TimeResolution later = 1000ULL * 1000 * 1000 * 10;
  std::vector<Event> events;
  for (int64_t i = 0; i < 4; ++i) {
    for (const char* fhash : {"aaa", "bbb"}) {
      for (ThreadID tid : {1, 2}) {
        for (TimeResolution base : {TimeResolution(0), later}) {
          events.push_back(
              {"read",
               "POSIX",
               base + 100 + i,
               10 + i,
               tid,
               true,
               {{"fhash", std::string(fhash), MetadataType::MT_KEY},
                {"fd", static_cast<int64_t>(3), MetadataType::MT_KEY},
                {"size", static_cast<int64_t>(100 * (i + 1)),
                 MetadataType::MT_VALUE},
                {"offset", static_cast<uint64_t>(i) << 33,
                 MetadataType::MT_VALUE},
                {"bw", 0.25 * (i + 1), MetadataType::MT_VALUE},
                {"mode", std::string("r"), MetadataType::MT_VALUE},
                {"note", std::string("x"), MetadataType::MT_IGNORE}}});
        }
      }
    }
    events.push_back({"close", "POSIX", 200 + i, 1, 1, false, {}});
  }
  // One event alone in its bucket: its values are written as `key`, not
  // key_sum / key_min / key_max.
  events.push_back({"write",
                    "POSIX",
                    300,
                    9,
                    1,
                    true,
                    {{"size", static_cast<int64_t>(7), MetadataType::MT_VALUE},
                     {"bw", 2.0, MetadataType::MT_VALUE}}});

  Aggregator aggregator;
  PackedAggregator packed(config);
  int rank = -1;
  for (const Event& e : events) {
    Metadata* md = metadata_of(e);
    AggregatedKey key(e.category, e.name, TraceEventType::TRACE_TYPE_LIBC_IO,
                      e.ts, e.dur, e.tid, md, nullptr, &rank);
    aggregator.aggregate(key);
    delete md;
    std::vector<uint8_t> bytes;
    ipc::DecodedEvent decoded;
    packed_of(e, true, bytes, decoded);
    packed.aggregate(decoded);
  }

  AggregatedDataType data;
  aggregator.get_previous_aggregations(data, true);
  auto js = std::make_shared<JsonLines>();
  js->initialize(nullptr, kHostname);
  std::vector<char> expect(1 << 20);
  const size_t n = js->aggregated(expect.data(), 0, 42, data);

  struct Rows : PackedAggregator::RowSink {
    std::vector<char> buffer = std::vector<char>(1 << 20);
    size_t used = 0;
    char* row() override { return buffer.data() + used; }
    void wrote(size_t size) override { used += size; }
  } rows;
  packed.drain(true, 42, kHostname, rows);

  const auto want = lines_of(expect.data(), n);
  const auto got = lines_of(rows.buffer.data(), rows.used);
  if (want != got) {
    for (const auto& l : want) std::cerr << "want " << l << "\n";
    for (const auto& l : got) std::cerr << "got  " << l << "\n";
  }
  DFT_CHECK(want == got);
  // 2 fhash x 2 threads x 2 intervals of read, close, write.
  DFT_CHECK(got.size() == 10);
  std::cout << "✓ PackedAggregator rows match Aggregator (" << got.size()
            << " rows)" << std::endl;
}

int main() {
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_ENABLE_AGGREGATION", "1", 1);
  setenv("DFTRACER_AGGREGATION_TYPE", "FULL", 1);
  setenv("DFTRACER_TRACE_INTERVAL_MS", "1000", 1);
  check_data();
  check_aggregation();
  return 0;
}
