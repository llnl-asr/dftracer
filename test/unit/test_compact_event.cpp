// compact::Encoder and compact::Decoder give back every field of every event
// in a block, with names, keys and values that repeat, change or clash.
#include <dftracer/core/buffer/compact_event.h>
#include <dftracer/core/common/event_draft.h>

#include <cstdint>
#include <cstdio>
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

// Events sent by their shape decode to the same fields as events sent in
// full: shapes that alternate, share keys, clash in the table, change a
// key's type, have no call site or are broken by a dynamic or replaced key.
void test_shapes() {
  const TraceEventType data = static_cast<TraceEventType>(3);
  compact::Encoder enc;
  Block block;
  std::vector<Event> events;
  std::vector<EventDraft> drafts;
  drafts.reserve(1024);
  const auto add_event = [&](const Event& e) {
    events.push_back(e);
    add(block, enc, e, drafts.back());
  };
  char dynamic_name[16];
  static char seek_site, read_site, write_site, region_site, op_sites[100];
  for (int i = 0; i < 200; ++i) {
    const TimeResolution t = 1000 + 10 * i;
    drafts.emplace_back();
    EventDraft& seek = drafts.back();
    seek.set_site(&seek_site);
    seek.set(StaticKey{"fhash"}, uint64_t{0xb0b77bd7});
    seek.set(StaticKey{"offset"}, int64_t{4096} * i, MetadataType::MT_VALUE);
    seek.set(StaticKey{"whence"}, 0, MetadataType::MT_VALUE);
    seek.set(StaticKey{"ret"}, int64_t{4096} * i, MetadataType::MT_VALUE);
    // Every third pair from another thread, so both head forms occur.
    add_event(
        {2 * i, "lseek64", "POSIX", data, t, 1, 7, i % 3 == 0 ? 9u : 8u, 0});
    drafts.emplace_back();
    EventDraft& rw = drafts.back();
    rw.set_site(i % 2 == 0 ? &read_site : &write_site);
    rw.set(StaticKey{"fhash"}, uint64_t{0xb0b77bd7});
    rw.set(StaticKey{"count"}, size_t{4096}, MetadataType::MT_VALUE);
    rw.set(StaticKey{"ret"}, i % 7 == 0 ? int64_t{-1} : int64_t{4096},
           MetadataType::MT_VALUE);
    add_event({2 * i + 1, i % 2 == 0 ? "read" : "write", "POSIX", data, t + 2,
               3, 7, 8, 0});
  }
  // A name built at run time in one buffer, from no call site.
  for (int i = 0; i < 4; ++i) {
    std::snprintf(dynamic_name, sizeof(dynamic_name), "region%d", i % 2);
    drafts.emplace_back();
    drafts.back().set(StaticKey{"level"}, i, MetadataType::MT_VALUE);
    drafts.back().set(StaticKey{"fname"},
                      std::string("/tmp/f") + std::to_string(i % 2));
    add_event({500 + i, dynamic_name, "APP", data, 5000, 1, 7, 8, 0});
  }
  // The same key with another MetadataType, a dynamic key and a replaced
  // key; each must keep its own fields.
  for (int i = 0; i < 3; ++i) {
    drafts.emplace_back();
    drafts.back().set_site(&region_site);
    drafts.back().set(StaticKey{"level"}, i, MetadataType::MT_KEY);
    drafts.back().set(StaticKey{"fname"}, std::string("/tmp/f0"));
    add_event({600 + i, "region0", "APP", data, 6000, 1, 7, 8, 0});
    drafts.emplace_back();
    drafts.back().set_site(&region_site);
    drafts.back().set(StaticKey{"level"}, i, MetadataType::MT_VALUE);
    drafts.back().set(StaticKey{"fname"}, std::string("/tmp/f0"));
    add_event({605 + i, "region0", "APP", data, 6000, 1, 7, 8, 0});
    drafts.emplace_back();
    drafts.back().set_site(&region_site);
    drafts.back().set(StaticKey{"level"}, uint64_t(i), MetadataType::MT_VALUE);
    drafts.back().set(StaticKey{"fname"}, std::string("/tmp/f0"));
    add_event({607 + i, "region0", "APP", data, 6000, 1, 7, 8, 0});
    drafts.emplace_back();
    drafts.back().set_site(&region_site);
    drafts.back().set(StaticKey{"level"}, i, MetadataType::MT_VALUE);
    drafts.back().set(std::string_view("dyn"), 1.5);
    add_event({610 + i, "region0", "APP", data, 6000, 1, 7, 8, 0});
    drafts.emplace_back();
    drafts.back().set_site(&region_site);
    drafts.back().set(StaticKey{"level"}, i, MetadataType::MT_VALUE);
    drafts.back().set(StaticKey{"fname"}, std::string("/tmp/f0"));
    drafts.back().set(StaticKey{"level"}, i + 10, MetadataType::MT_VALUE);
    add_event({620 + i, "region0", "APP", data, 6000, 1, 7, 8, 0});
  }
  // More shapes than the table holds, twice over.
  for (int round = 0; round < 2; ++round) {
    for (int k = 0; k < 100; ++k) {
      drafts.emplace_back();
      drafts.back().set_site(&op_sites[k]);
      drafts.back().set(StaticKey{"fhash"}, uint64_t(k));
      events.push_back(
          {700 + k, "op" + std::to_string(k), "HDF5", data, 7000, 1, 7, 8, 0});
      add(block, enc, events.back(), drafts.back());
    }
  }

  // A repeated read or lseek takes its shape id, head and values only.
  DFT_CHECK(block.ends[5] - block.ends[4] <= 12);

  compact::Decoder dec;
  ipc::DecodedEvent out;
  size_t at = 0;
  for (size_t i = 0; i < events.size(); ++i) {
    DFT_CHECK(dec.event(block.bytes.data() + at, block.ends[i] - at, out));
    check_head(out, events[i]);
    at = block.ends[i];
    // Encode the draft alone in full and compare the args.
    compact::Encoder full;
    Block one;
    EventDraft& d = drafts[i];
    add(one, full, events[i], d);
    compact::Decoder alone;
    ipc::DecodedEvent want;
    DFT_CHECK(alone.event(one.bytes.data(), one.ends[0], want));
    DFT_CHECK(out.values.size() == want.values.size());
    DFT_CHECK(out.head.meta_count == want.head.meta_count);
    for (size_t k = 0; k < want.values.size(); ++k) {
      const ipc::DecodedValue& a = out.values[k];
      const ipc::DecodedValue& b = want.values[k];
      DFT_CHECK(a.key == b.key && a.tag == b.tag && a.mtype == b.mtype);
      DFT_CHECK(a.i == b.i && a.u == b.u && a.s == b.s);
      DFT_CHECK(a.d == b.d);
    }
  }

  // A new block starts with no shapes: a site sent by its shape before is
  // sent in full again and decodes alone.
  enc.reset();
  Block next;
  add(next, enc, events[4], drafts[4]);
  add(next, enc, events[6], drafts[6]);
  DFT_CHECK(next.ends[1] - next.ends[0] <= 16);
  compact::Decoder fresh;
  DFT_CHECK(fresh.event(next.bytes.data(), next.ends[0], out));
  check_head(out, events[4]);
  DFT_CHECK(out.values.size() == 4 && out.values[1].i == 4096 * 2);
  DFT_CHECK(fresh.event(next.bytes.data() + next.ends[0],
                        next.ends[1] - next.ends[0], out));
  check_head(out, events[6]);
  DFT_CHECK(out.values.size() == 4 && out.values[1].i == 4096 * 3);
}

}  // namespace

int main() {
  test_shapes();
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
