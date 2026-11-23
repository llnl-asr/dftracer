// compact::Encoder and compact::Decoder give back every field of every event
// in a block, with names, keys and values that repeat, change or clash.
#include <dftracer/core/buffer/compact_event.h>
#include <dftracer/core/common/event_draft.h>
#include <dftracer/core/serialization/json_line.h>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <random>
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

// `got` has the args of `draft` encoded alone in full for `e`.
void check_values(const ipc::DecodedEvent& got, const Event& e,
                  const EventDraft& draft) {
  compact::Encoder full;
  Block one;
  add(one, full, e, draft);
  compact::Decoder alone;
  ipc::DecodedEvent want;
  DFT_CHECK(alone.event(one.bytes.data(), one.ends[0], want));
  DFT_CHECK(got.values.size() == want.values.size());
  DFT_CHECK(got.head.meta_count == want.head.meta_count);
  for (size_t k = 0; k < want.values.size(); ++k) {
    const ipc::DecodedValue& a = got.values[k];
    const ipc::DecodedValue& b = want.values[k];
    DFT_CHECK(a.key == b.key && a.tag == b.tag && a.mtype == b.mtype);
    DFT_CHECK(a.i == b.i && a.u == b.u && a.s == b.s);
    DFT_CHECK(a.d == b.d);
  }
}

// Events sent by their shape decode to the same fields as events sent in
// full: shapes that alternate, share keys, clash in the table, change a
// key's type, have no call site or are broken by a dynamic or replaced key.
void test_shapes() {
  const TraceEventType data = static_cast<TraceEventType>(3);
  compact::Encoder enc;
  Block block;
  std::vector<Event> events;
  std::deque<EventDraft> drafts;
  const auto add_event = [&](const Event& e) {
    events.push_back(e);
    add(block, enc, e, drafts.back());
  };
  char dynamic_name[16];
  static char seek_site, read_site, write_site, region_site, long_site,
      op_sites[100];
  for (int i = 0; i < 200; ++i) {
    // Crosses a multiple of 10^8 halfway, where the shaped line's cached
    // high digits of ts change.
    const TimeResolution t = 1794677399999000 + 10 * i;
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
    drafts.back().set(StaticKey{"fname"},
                      std::string(i == 1 ? "/tmp/\"f0" : "/tmp/f0"));
    drafts.back().set(StaticKey{"level"}, i + 10, MetadataType::MT_VALUE);
    add_event({620 + i, "region0", "APP", data, 6000, 1, 7, 8, 0});
  }
  // Long strings: one that fits however it is escaped, one that fits though
  // the shaped line's bound does not, and one that is cut once escaped.
  const std::string longs[] = {std::string(2500, 'a'),
                               std::string(3000, '"') + std::string(9000, 'b'),
                               std::string(9000, '"')};
  for (int i = 0; i < 6; ++i) {
    drafts.emplace_back();
    drafts.back().set_site(&long_site);
    drafts.back().set(StaticKey{"fname"}, longs[i % 3]);
    add_event({640 + i, "open", "POSIX", data, 6100, 1, 7, 8, 0});
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
  JsonLines js;
  char hostname[] = "6c6f63616c686f73";
  js.initialize(nullptr, hostname);
  ShapeLines lines;
  static char line[DFTRACER_SERIALIZATION_EVENT_MAX];
  static char plain[DFTRACER_SERIALIZATION_EVENT_MAX];
  size_t shaped_lines = 0;
  size_t at = 0;
  for (size_t i = 0; i < events.size(); ++i) {
    DFT_CHECK(dec.event(block.bytes.data() + at, block.ends[i] - at, out));
    check_head(out, events[i]);
    // A line from the shape's stored text is the line written in full.
    for (uint8_t flags :
         {uint8_t{0}, uint8_t{SHM_EVENT_HAS_METADATA},
          uint8_t{SHM_EVENT_HAS_METADATA | SHM_EVENT_INC_METADATA}}) {
      ipc::DecodedEvent e = out;
      e.head.flags = flags;
      const size_t n = js.data(line, e, lines, dec.shape());
      const size_t m = js.data(plain, e);
      DFT_CHECK(std::string(line, n) == std::string(plain, m));
    }
    shaped_lines += dec.shape() != compact::Decoder::kNoShape;
    at = block.ends[i];
    check_values(out, events[i], drafts[i]);
  }

  DFT_CHECK(shaped_lines > events.size() / 2);

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

// The 8-byte scan escapes exactly what a byte-by-byte scan does.
void test_escape() {
  struct Text {
    std::string& text;
    void raw(std::string_view v) { text.append(v.data(), v.size()); }
  };
  const auto plain = [](std::string_view s) {
    std::string t;
    for (unsigned char c : s) {
      char esc[8];
      if (c < 0x20)
        std::snprintf(esc, sizeof(esc), "\\u%04x", c);
      else if (c == '"' || c == '\\')
        std::snprintf(esc, sizeof(esc), "\\%c", c);
      else
        esc[0] = static_cast<char>(c), esc[1] = 0;
      t += esc;
    }
    return t;
  };
  const unsigned char pool[] = {'a',  '/',  '0',  '"',  '\\', 0x00, 0x01,
                                0x1f, 0x20, 0x21, 0x7f, 0x80, 0xc3, 0xff};
  std::mt19937 rng(7);
  for (int round = 0; round < 20000; ++round) {
    std::string s(rng() % 40, 'x');
    const unsigned rare = rng() % 4;
    for (char& c : s)
      c = static_cast<char>(rng() % 8 < rare ? pool[rng() % sizeof(pool)]
                                             : pool[rng() % 3]);
    std::string got;
    Text w{got};
    line_format::escaped(w, s);
    DFT_CHECK(got == plain(s));
    DFT_CHECK(line_format::plain(s) == (got == s));
  }
}

// Line::number() writes what std::to_chars writes, at the edges of each
// digit count and for random values of every width.
template <typename T>
void check_number(T v) {
  std::vector<char> buf(DFTRACER_SERIALIZATION_EVENT_MAX);
  line_format::Line w(buf.data());
  w.number(v);
  char want[32];
  const auto r = std::to_chars(want, want + sizeof(want), v);
  DFT_CHECK(std::string(buf.data(), w.size()) ==
            std::string(want, static_cast<size_t>(r.ptr - want)));
}

void test_number() {
  uint64_t p = 1;
  for (int d = 0; d < 20; ++d) {
    for (uint64_t v : {p - 1, p, p + 1}) {
      check_number(v);
      check_number(static_cast<int64_t>(v));
      check_number(-static_cast<int64_t>(v));
    }
    p *= 10;
  }
  check_number(std::numeric_limits<uint64_t>::max());
  check_number(std::numeric_limits<int64_t>::max());
  check_number(std::numeric_limits<int64_t>::min());
  check_number(std::numeric_limits<int32_t>::min());
  check_number(std::numeric_limits<uint32_t>::max());
  for (int64_t v = -300; v <= 300; ++v) {
    check_number(v);
    check_number(static_cast<int32_t>(v));
    if (v >= 0) check_number(static_cast<uint64_t>(v));
  }
  // Every length of a piece, after every offset, copies only that piece.
  std::string text;
  for (int i = 0; i < 64; ++i) text += static_cast<char>('A' + i % 26);
  for (size_t at = 0; at < 8; ++at) {
    for (size_t n = 0; at + n <= 64; ++n) {
      std::vector<char> buf(DFTRACER_SERIALIZATION_EVENT_MAX, '#');
      line_format::Line w(buf.data());
      w.raw(std::string_view(text).substr(0, at));
      w.raw(std::string_view(text).substr(at, n));
      DFT_CHECK(w.size() == at + n);
      DFT_CHECK(std::string(buf.data(), at + n) ==
                text.substr(0, at) + text.substr(at, n));
      DFT_CHECK(buf[at + n] == '#');
    }
  }
  std::mt19937_64 rng(7);
  for (int i = 0; i < 100000; ++i) {
    const uint64_t v = rng() >> (rng() % 64);
    check_number(v);
    check_number(static_cast<int64_t>(v));
    check_number(static_cast<int32_t>(v));
    check_number(static_cast<uint32_t>(v));
  }
}

// A DFT_SITE_END interceptor's args (EventDraft::entry() and
// Encoder::site_event()) give the same bytes as the EventDraft of the same
// calls. Event i comes from site site_of(i); more sites than the encoder keeps
// shapes for evict them.
template <typename SiteOf>
void test_site_event(int sites, SiteOf site_of) {
  static char site_storage[100];
  static const char kFhash[] = "fhash";
  static const char kCount[] = "count";
  static const char kOffset[] = "offset";
  static const char kRet[] = "ret";
  const std::string_view hash = "0123456789abcdef";
  compact::Encoder plain_enc;
  compact::Encoder site_enc;
  Block plain;
  Block by_site;
  int fused_hits = 0;
  int defined = 0;
  std::vector<SiteInfo> infos;
  std::vector<Event> events;
  std::deque<EventDraft> drafts;
  for (int i = 0; i < 200; ++i) {
    const char& site = site_storage[site_of(i)];
    const size_t count = 4096 + (i % 3);
    const off_t offset = static_cast<off_t>(i) * 4096 - 100;
    const ssize_t ret = i % 7 == 0 ? -1 : static_cast<ssize_t>(count);
    const Event e{i + 1,
                  "pread",
                  "POSIX",
                  TraceEventType::TRACE_TYPE_LIBC_IO,
                  static_cast<TimeResolution>(1000 + 37 * i),
                  static_cast<TimeResolution>(5 + i % 11),
                  42,
                  static_cast<ThreadID>(i % 50 == 0 ? 8 : 7),
                  static_cast<uint8_t>(SHM_EVENT_HAS_METADATA)};
    events.push_back(e);
    drafts.emplace_back();
    EventDraft& d = drafts.back();
    d.set_site(&site);
    d.set(StaticKey{kFhash}, hash);
    d.set(StaticKey{kCount}, count, MetadataType::MT_VALUE);
    d.set(StaticKey{kOffset}, offset, MetadataType::MT_VALUE);
    d.set(StaticKey{kRet}, ret, MetadataType::MT_VALUE);
    add(plain, plain_enc, e, d);

    const EventDraft::Entry args[] = {
        EventDraft::entry(StaticKey{kFhash}, hash, MetadataType::MT_KEY),
        EventDraft::entry(StaticKey{kCount}, count, MetadataType::MT_VALUE),
        EventDraft::entry(StaticKey{kOffset}, offset, MetadataType::MT_VALUE),
        EventDraft::entry(StaticKey{kRet}, ret, MetadataType::MT_VALUE)};
    uint64_t sig = 0;
    size_t size = 0;
    for (const EventDraft::Entry& a : args) {
      sig = EventDraft::mix(sig, a);
      size += EventDraft::wire_size(a);
    }
    DFT_CHECK(sig == d.sig() && size == d.size());
    const size_t bound = compact::Encoder::bound(e.name, e.category, size, 4);
    DFT_CHECK(bound == compact::Encoder::bound(e.name, e.category, &d));
    const size_t at = by_site.bytes.size();
    by_site.bytes.resize(at + bound);
    if (infos.empty())
      for (int k = 0; k < sites; ++k)
        infos.push_back(
            SiteInfo::of(&site_storage[k], e.name, e.category, e.type, args));
    const SiteInfo& info = infos[site_of(i)];
    DFT_CHECK(info.sig == sig && info.args_size == size && info.count == 4);
    DFT_CHECK(info.bound == bound);
    const SiteEvent se{&info, e.index, e.start, e.duration, args, e.pid, e.tid};
    size_t n = site_enc.site_event(by_site.bytes.data() + at, se, e.flags);
    if (n != 0) {
      ++fused_hits;
    } else {
      ++defined;
      EventDraft back;
      back.set_site(&site);
      for (const EventDraft::Entry& a : args) back.append(a);
      DFT_CHECK(back.sig() == d.sig() && back.shaped());
      n = site_enc.event(by_site.bytes.data() + at, e.index, e.name, e.category,
                         e.type, e.start, e.duration, &back, e.pid, e.tid,
                         e.flags);
    }
    DFT_CHECK(n > 0 && n <= bound);
    by_site.bytes.resize(at + n);
    by_site.ends.push_back(at + n);
    if (i == 99) {
      plain_enc.reset();
      site_enc.reset();
    }
  }
  if (sites == 1) DFT_CHECK(fused_hits == 198);
  DFT_CHECK(fused_hits + defined == 200 && fused_hits > 0);
  DFT_CHECK(by_site.ends == plain.ends);
  DFT_CHECK(by_site.bytes == plain.bytes);

  compact::Decoder dec;
  ipc::DecodedEvent out;
  size_t at = 0;
  for (size_t i = 0; i < events.size(); ++i) {
    if (i == 100) dec.reset();
    DFT_CHECK(dec.event(by_site.bytes.data() + at, by_site.ends[i] - at, out));
    check_head(out, events[i]);
    check_values(out, events[i], drafts[i]);
    at = by_site.ends[i];
  }
}

int main() {
  test_escape();
  test_number();
  test_shapes();
  test_site_event(1, [](int) { return 0; });
  test_site_event(3, [](int i) { return i % 3; });
  test_site_event(40, [](int i) { return i % 40; });
  // Site 0 three times, then 80 others that take its shape's slot, then site
  // 0 again; then the same in the next block.
  test_site_event(81, [](int i) {
    const int k = i % 100;
    return k < 3 || k >= 83 ? 0 : k - 2;
  });
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
