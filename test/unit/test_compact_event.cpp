// compact::Encoder and compact::Decoder give back every field of every event
// in a block, with names, keys and values that repeat, change or clash.
#include <dftracer/core/buffer/compact_event.h>
#include <dftracer/core/common/event_draft.h>

#include <cstdint>
#include <string>
#include <vector>

#include "check.h"

using namespace dftracer;

namespace {

struct Event {
  int index;
  std::string name;
  std::string category;
  TraceEventType type;
  TimeResolution start;
  TimeResolution duration;
  ProcessID pid;
  ThreadID tid;
  uint8_t flags;
};

// The encoder keeps pointers to the strings it wrote, so the bytes must not
// move, as a thread's block does not.
struct Block {
  Block() { bytes.reserve(64 * 1024); }
  std::vector<uint8_t> bytes;
  std::vector<size_t> ends;
};

void add(Block& block, compact::Encoder& enc, const Event& e,
         const EventDraft& args) {
  const size_t bound = compact::Encoder::bound(e.name, e.category, &args);
  const size_t at = block.bytes.size();
  block.bytes.resize(at + bound);
  const size_t n =
      enc.event(block.bytes.data() + at, e.index, e.name, e.category, e.type,
                e.start, e.duration, &args, e.pid, e.tid, e.flags);
  DFT_CHECK(n > 0 && n <= bound);
  block.bytes.resize(at + n);
  block.ends.push_back(at + n);
}

void check_head(const ipc::DecodedEvent& d, const Event& e) {
  DFT_CHECK(d.head.index == e.index);
  DFT_CHECK(d.name == e.name);
  DFT_CHECK(d.category == e.category);
  DFT_CHECK(d.head.name_len == e.name.size());
  DFT_CHECK(d.head.category_len == e.category.size());
  DFT_CHECK(d.head.type == static_cast<uint8_t>(e.type));
  DFT_CHECK(d.head.start_time == e.start);
  DFT_CHECK(d.head.duration == e.duration);
  DFT_CHECK(d.head.process_id == e.pid);
  DFT_CHECK(d.head.thread_id == e.tid);
  DFT_CHECK(d.head.flags == e.flags);
}

}  // namespace

int main() {
  compact::Encoder enc;
  Block block;
  std::vector<Event> events;
  std::vector<EventDraft> drafts(6);
  const TraceEventType data = static_cast<TraceEventType>(3);

  // pread-like events: offset grows, the rest repeats.
  for (int i = 0; i < 3; ++i) {
    EventDraft& d = drafts[i];
    d.reset();
    d.set("fhash", std::string("b0b77bd72d504aee"));
    d.set("count", size_t{4096}, MetadataType::MT_VALUE);
    d.set("offset", int64_t{4096} * i, MetadataType::MT_VALUE);
    d.set("ret", int64_t{4096}, MetadataType::MT_VALUE);
    events.push_back({100 + i, "pread", "POSIX", data,
                      1794419931659004ull + 2 * i, 1, 845579, 845580, 0});
    add(block, enc, events.back(), d);
  }
  // Times and ids that go back, a negative value, a double, strings, an
  // empty name and another thread.
  drafts[3].reset();
  drafts[3].set("ret", int64_t{-1}, MetadataType::MT_VALUE);
  drafts[3].set("ratio", 0.25);
  drafts[3].set("fname", std::string("/data/x.pfw.gz"));
  drafts[3].set("json", RawJson{"[1,2]"});
  events.push_back({7, "", "POSIX", data, 1794419931650000ull, 90, 845579,
                    845590, SHM_EVENT_NO_AGGREGATE});
  add(block, enc, events.back(), drafts[3]);
  // More distinct strings than hash slots, so some clash.
  drafts[4].reset();
  for (int i = 0; i < 100; ++i)
    drafts[4].set("key" + std::to_string(i), uint64_t(i) * 3);
  events.push_back({8, "many", "APP", data, 1794419931660000ull, 0, 1, 2, 0});
  add(block, enc, events.back(), drafts[4]);
  drafts[5].reset();
  events.push_back(
      {9, "pread", "POSIX", data, 1794419931660001ull, 3, 845579, 845580, 0});
  add(block, enc, events.back(), drafts[5]);

  // A repeated pread takes about a sixth of its 134-byte ring form.
  DFT_CHECK(block.ends[2] - block.ends[1] <= 24);

  compact::Decoder dec;
  ipc::DecodedEvent out;
  size_t at = 0;
  for (size_t i = 0; i < events.size(); ++i) {
    DFT_CHECK(dec.event(block.bytes.data() + at, block.ends[i] - at, out));
    check_head(out, events[i]);
    at = block.ends[i];
    if (i < 3) {
      DFT_CHECK(out.values.size() == 4);
      DFT_CHECK(out.values[0].key == "fhash" &&
                out.values[0].s == "b0b77bd72d504aee");
      DFT_CHECK(out.values[1].key == "count" && out.values[1].u == 4096 &&
                out.values[1].mtype == MetadataType::MT_VALUE);
      DFT_CHECK(out.values[2].key == "offset" &&
                out.values[2].i == int64_t{4096} * static_cast<int64_t>(i));
      DFT_CHECK(out.values[3].key == "ret" && out.values[3].i == 4096);
    } else if (i == 3) {
      DFT_CHECK(out.values.size() == 4);
      DFT_CHECK(out.values[0].key == "ret" && out.values[0].i == -1);
      DFT_CHECK(out.values[1].tag == SHM_VALUE_DOUBLE &&
                out.values[1].d == 0.25);
      DFT_CHECK(out.values[2].tag == SHM_VALUE_STRING &&
                out.values[2].s == "/data/x.pfw.gz");
      DFT_CHECK(out.values[3].tag == SHM_VALUE_RAW_JSON &&
                out.values[3].s == "[1,2]");
    } else if (i == 4) {
      DFT_CHECK(out.values.size() == 100);
      for (int k = 0; k < 100; ++k) {
        DFT_CHECK(out.values[k].key == "key" + std::to_string(k));
        DFT_CHECK(out.values[k].u == uint64_t(k) * 3);
      }
    } else {
      DFT_CHECK(out.values.empty());
    }
  }

  // A cut record fails, and a new block decodes alone after reset.
  dec.reset();
  DFT_CHECK(!dec.event(block.bytes.data(), block.ends[0] - 1, out));
  enc.reset();
  dec.reset();
  Block next;
  add(next, enc, events[2], drafts[2]);
  DFT_CHECK(dec.event(next.bytes.data(), next.ends[0], out));
  check_head(out, events[2]);
  DFT_CHECK(out.values[2].i == 8192);
  return 0;
}
