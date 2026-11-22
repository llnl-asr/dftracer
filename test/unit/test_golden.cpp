// The trace lines of the current serializer and aggregator are the lines the
// Metadata-based ones wrote (golden_lines.h), byte for byte, for the events of
// golden_events.h, through both of the service's event codecs.
#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/buffer/compact_event.h>
#include <dftracer/core/common/event_draft.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/ipc/event_codec.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "event_feed.h"
#include "golden_events.h"
#include "golden_lines.h"

using namespace dftracer;

namespace {

uint8_t flags_of(bool include, const EventDraft* args) {
  return (include ? SHM_EVENT_INC_METADATA : 0) |
         (args != nullptr ? SHM_EVENT_HAS_METADATA : 0);
}

EventDraft draft_of(const golden::Event& e) {
  EventDraft draft;
  for (const auto& f : e.fields) draft.set(f.key, f.value, f.type);
  return draft;
}

Metadata* metadata_of(const golden::Event& e) {
  if (!e.has_metadata) return nullptr;
  auto* md = new Metadata();
  for (const auto& f : e.fields) md->insert_or_assign(f.key, f.value, f.type);
  return md;
}

// The ring's record of an event, decoded as the service reads it.
void decode_ring(const golden::Event& e, bool include,
                 std::vector<uint8_t>& bytes, ipc::DecodedEvent& out) {
  EventDraft draft = draft_of(e);
  const EventDraft* args = e.has_metadata ? &draft : nullptr;
  ring_decode(bytes, out, golden::kIndex, e.name, e.category, e.type, e.ts,
              e.dur, args, golden::kPid, e.tid, flags_of(include, args));
}

// A block of compact records. The encoder keeps pointers to the strings it
// wrote, so the bytes must not move.
struct CompactBlock {
  CompactBlock() { bytes.reserve(1 << 20); }
  void decode(const golden::Event& e, bool include, ipc::DecodedEvent& out) {
    EventDraft draft = draft_of(e);
    const EventDraft* args = e.has_metadata ? &draft : nullptr;
    const size_t bound = compact::Encoder::bound(e.name, e.category, args);
    const size_t at = bytes.size();
    DFT_CHECK(at + bound <= bytes.capacity());
    bytes.resize(at + bound);
    const size_t n = encoder.event(
        bytes.data() + at, golden::kIndex, e.name, e.category, e.type, e.ts,
        e.dur, args, golden::kPid, e.tid, flags_of(include, args));
    bytes.resize(at + n);
    DFT_CHECK(decoder.event(bytes.data() + at, n, out));
  }
  std::vector<uint8_t> bytes;
  compact::Encoder encoder;
  compact::Decoder decoder;
};

void check_line(const char* what, const std::string& got, const char* want) {
  if (got != want) {
    std::cerr << what << "\nwant " << want << "\ngot  " << got << "\n";
    DFT_CHECK(got == want);
  }
}

void check_data() {
  JsonLines js;
  js.initialize(nullptr, golden::kHostname);
  static char out[DFTRACER_SERIALIZATION_EVENT_MAX];
  const auto events = golden::line_events();
  for (int include : {0, 1}) {
    CompactBlock block;
    for (size_t i = 0; i < events.size(); ++i) {
      const golden::Event& e = events[i];
      std::vector<uint8_t> bytes;
      ipc::DecodedEvent decoded;
      decode_ring(e, include, bytes, decoded);
      size_t n = js.data(out, decoded);
      check_line("ring data", golden::normalized(std::string(out, n)),
                 golden::kData[include][i]);
      block.decode(e, include, decoded);
      n = js.data(out, decoded);
      check_line("compact data", golden::normalized(std::string(out, n)),
                 golden::kData[include][i]);
    }
  }
  std::cout << "data lines match the golden lines" << std::endl;
}

void check_counter() {
  JsonLines js;
  js.initialize(nullptr, golden::kHostname);
  static char out[DFTRACER_SERIALIZATION_EVENT_MAX];
  const auto events = golden::line_events();
  for (int include : {0, 1}) {
    for (size_t i = 0; i < events.size(); ++i) {
      const golden::Event& e = events[i];
      size_t n =
          js.counter(out, golden::kIndex, e.name.c_str(), e.category.c_str(),
                     e.type, e.ts, golden::kPid, e.tid, metadata_of(e));
      check_line("metadata counter", golden::normalized(std::string(out, n)),
                 golden::kCounter[include][i]);
      Metadata* md = metadata_of(e);
      std::vector<uint8_t> bytes;
      ipc::ByteWriter w(bytes);
      ipc::encode_event(w, golden::kIndex, e.name.c_str(), e.category.c_str(),
                        e.type, e.ts, e.dur, md, golden::kPid, e.tid,
                        include ? SHM_EVENT_INC_METADATA : 0);
      delete md;
      ipc::DecodedEvent decoded;
      DFT_CHECK(ipc::decode_event(bytes.data(), bytes.size(), decoded));
      n = js.counter(out, decoded);
      check_line("decoded counter", golden::normalized(std::string(out, n)),
                 golden::kCounter[include][i]);
    }
  }
  std::cout << "counter lines match the golden lines" << std::endl;
}

void check_rows(const char* what, Aggregator& aggregator) {
  Rows rows;
  aggregator.drain(true, golden::kPid, golden::kHostname, rows);
  const auto got = golden::lines_of(rows.buffer.data(), rows.used);
  const std::vector<std::string> want(std::begin(golden::kAggregated),
                                      std::end(golden::kAggregated));
  if (want != got) {
    std::cerr << what << "\n";
    for (const auto& l : want) std::cerr << "want " << l << "\n";
    for (const auto& l : got) std::cerr << "got  " << l << "\n";
  }
  DFT_CHECK(want == got);
}

void check_aggregation() {
  auto config = Singleton<ConfigurationManager>::get_instance();
  const auto events = golden::aggregated_events();
  Aggregator ring_rows(config), compact_rows(config);
  CompactBlock block;
  for (const golden::Event& e : events) {
    std::vector<uint8_t> bytes;
    ipc::DecodedEvent decoded;
    decode_ring(e, true, bytes, decoded);
    ring_rows.aggregate(decoded);
    block.decode(e, true, decoded);
    compact_rows.aggregate(decoded);
  }
  check_rows("ring aggregation", ring_rows);
  check_rows("compact aggregation", compact_rows);
  std::cout << "aggregated rows match the golden lines ("
            << std::size(golden::kAggregated) << " rows)" << std::endl;
}

// SHM_EVENT_INTERNED: records written into one block refer to the strings
// the block already has, and decode back to the same strings.
static void check_interning() {
  std::vector<uint8_t> block(1 << 16), fallback(4096);
  size_t used = 0;
  ipc::Interner intern;
  ipc::InternTable table;
  // One record into the "block" at `used`, as the producer writes it.
  auto put = [&](const char* name, Metadata* m, bool keep = true) {
    const uint16_t mark = intern.mark();
    ipc::ByteWriter w(block.data() + used, block.size() - used, fallback,
                      &intern);
    ipc::encode_event(w, 1, name, "POSIX", TraceEventType::TRACE_TYPE_LIBC_IO,
                      10, 2, m, 7, 8, SHM_EVENT_INC_METADATA);
    DFT_CHECK(!w.moved());
    const size_t at = used, n = w.size();
    if (keep)
      used += n;
    else
      intern.rollback(mark);
    return std::make_pair(at, n);
  };
  auto get = [&](std::pair<size_t, size_t> rec, ipc::DecodedEvent& e) {
    return ipc::decode_event(block.data() + rec.first, rec.second, e, &table);
  };

  intern.begin(1);
  Metadata m;
  m.insert_or_assign("fhash", std::string("abc"));
  auto first = put("read", &m);
  auto second = put("read", &m);
  DFT_CHECK(second.second < first.second);  // name, category, key by id
  char name[8] = "write";  // the same address with other text later
  auto third = put(name, nullptr);
  std::snprintf(name, sizeof(name), "%s", "close");
  auto fourth = put(name, nullptr);
  // A record that is not kept gives its ids back.
  auto dropped = put("open", nullptr, false);
  (void)dropped;
  auto fifth = put("stat", nullptr);

  ipc::DecodedEvent e;
  DFT_CHECK(get(first, e) && e.name == "read" && e.category == "POSIX");
  DFT_CHECK(e.values.size() == 1 && e.values[0].key == "fhash" &&
            e.values[0].s == "abc");
  DFT_CHECK((e.head.flags & SHM_EVENT_INTERNED) != 0);
  DFT_CHECK(get(second, e) && e.name == "read" && e.category == "POSIX" &&
            e.values.size() == 1 && e.values[0].key == "fhash");
  DFT_CHECK(get(third, e) && e.name == "write");
  DFT_CHECK(get(fourth, e) && e.name == "close");
  DFT_CHECK(get(fifth, e) && e.name == "stat" && e.category == "POSIX");

  // A new block starts afresh: everything inline again.
  intern.begin(2);
  table.clear();
  used = 0;
  auto again = put("read", &m);
  DFT_CHECK(again.second == first.second);
  DFT_CHECK(get(again, e) && e.name == "read" && e.values[0].key == "fhash");
  // Without the block's table an interned record cannot be read.
  DFT_CHECK(!ipc::decode_event(block.data() + again.first, again.second, e));
  std::cout << "interned records decode to their strings" << std::endl;
}

}  // namespace

int main() {
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_ENABLE_AGGREGATION", "1", 1);
  setenv("DFTRACER_AGGREGATION_TYPE", "FULL", 1);
  setenv("DFTRACER_TRACE_INTERVAL_MS", "1000", 1);
  check_data();
  check_counter();
  check_aggregation();
  check_interning();
  return 0;
}
