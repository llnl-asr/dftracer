// Regression tests for the per-event cost work on the producer path, aimed at
// what each optimization could break:
//   * Metadata as a flat vector (insert/overwrite/erase/[]/at/copy, order),
//     the per-thread map of acquire()/release(), deferred key hashes;
//   * dftracer::same_type, which must agree with type_info::operator==;
//   * ByteWriter's three forms (exact, buffer, direct into a reservation),
//     which must write the same bytes, also for records larger than the
//     buffer or the reservation;
//   * interned records (SHM_EVENT_INTERNED): table collisions, rollback of
//     more ids than the producer tracks one by one, a block running out of
//     ids, strings longer than an interned length can say.
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/type_match.h>
#include <dftracer/core/ipc/event_codec.h>

#include <any>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

#include "check.h"

using namespace dftracer;

namespace {

void test_metadata_semantics() {
  Metadata m;
  DFT_CHECK(m.empty() && m.find("a") == m.end());
  DFT_CHECK(m.insert_or_assign("a", 1).second);
  DFT_CHECK(m.insert_or_assign(std::string("b"), std::string("x")).second);
  DFT_CHECK(m.insert_or_assign("c", 3.5, MetadataType::MT_VALUE).second);
  // Overwrite keeps the entry (and its place), changes value and type.
  auto r =
      m.insert_or_assign("a", static_cast<int64_t>(7), MetadataType::MT_VALUE);
  DFT_CHECK(!r.second && m.size() == 3);
  DFT_CHECK(std::any_cast<int64_t>(std::get<1>(r.first->second)) == 7);
  DFT_CHECK(std::get<0>(m.at("a")) == MetadataType::MT_VALUE);
  // insert does not overwrite.
  DFT_CHECK(!m.insert("b", std::string("y")).second);
  DFT_CHECK(std::any_cast<std::string>(std::get<1>(m.at("b"))) == "x");
  // Insertion order.
  std::vector<std::string> order;
  for (const auto& e : m) order.push_back(e.first);
  DFT_CHECK((order == std::vector<std::string>{"a", "b", "c"}));
  // operator[] adds a default entry once.
  m["d"] = Metadata::Value(MetadataType::MT_KEY, std::any(4), 0);
  m["d"];
  DFT_CHECK(m.size() == 4 && m.contains("d"));
  // at() of a missing key throws, as unordered_map::at did.
  bool threw = false;
  try {
    (void)m.at("missing");
  } catch (const std::out_of_range&) {
    threw = true;
  }
  DFT_CHECK(threw);
  // Erase while iterating, as JsonLines::data filters optional args.
  for (auto it = m.begin(); it != m.end();)
    it = (it->first == "b" || it->first == "d") ? m.erase(it) : std::next(it);
  DFT_CHECK(m.size() == 2 && !m.contains("b") && m.contains("c"));
  DFT_CHECK(m.erase("c") == 1 && m.erase("c") == 0);
  // Copies are independent.
  Metadata copy = m;
  copy.insert_or_assign("z", 1);
  DFT_CHECK(copy.size() == 2 && m.size() == 1);
  // Many keys: lookups stay correct past the reserved capacity.
  Metadata big;
  for (int i = 0; i < 100; ++i)
    big.insert_or_assign("key" + std::to_string(i), i);
  for (int i = 0; i < 100; ++i)
    DFT_CHECK(std::any_cast<int>(
                  std::get<1>(big.at("key" + std::to_string(i)))) == i);
  // Equal maps in different orders still key aggregation equally.
  Metadata x, y;
  x.insert_or_assign("p", 1);
  x.insert_or_assign("q", std::string("s"));
  y.insert_or_assign("q", std::string("s"));
  y.insert_or_assign("p", 1);
  AggregatedKey kx("c", "n", TraceEventType::TRACE_TYPE_LIBC_IO, 1, 1, 1, &x,
                   nullptr, nullptr);
  AggregatedKey ky("c", "n", TraceEventType::TRACE_TYPE_LIBC_IO, 1, 1, 1, &y,
                   nullptr, nullptr);
  DFT_CHECK(kx == ky);
  DFT_CHECK(std::hash<AggregatedKey>()(kx) == std::hash<AggregatedKey>()(ky));
  std::cout << "✓ Metadata semantics" << std::endl;
}

void test_metadata_reuse_threads() {
  // Many threads acquiring, filling and releasing at once: no map is shared,
  // and a reused map is always empty.
  std::vector<std::thread> threads;
  bool ok[8] = {};
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([t, &ok]() {
      bool good = true;
      for (int i = 0; i < 20000; ++i) {
        Metadata* m = Metadata::acquire();
        good = good && m->empty();
        m->insert_or_assign("thread", t);
        m->insert_or_assign("i", i);
        good = good && std::any_cast<int>(std::get<1>(m->at("thread"))) == t;
        if (i % 3 == 0)
          delete m;  // deleting a pooled map is fine
        else
          Metadata::release(m);
      }
      ok[t] = good;
    });
  }
  for (auto& th : threads) th.join();
  for (bool b : ok) DFT_CHECK(b);
  std::cout << "✓ Metadata reuse across 8 threads" << std::endl;
}

// same_type(a, b) must be exactly a == b for every pair of the candidate
// types the metadata code matches against.
template <typename... T>
struct Types {};
using Candidates =
    Types<unsigned long long, long long, unsigned int, unsigned long, double,
          float, int, size_t, long, uint16_t, ssize_t, off_t, mode_t, off64_t,
          const char*, char*, std::string, RawJson, ArgList, bool, char, int8_t,
          uint8_t, int16_t>;

template <typename... T>
void check_same_type(Types<T...>) {
  const std::type_info* ids[] = {&typeid(T)...};
  size_t n = 0;
  for (const auto* a : ids)
    for (const auto* b : ids) {
      DFT_CHECK(dftracer::same_type(*a, *b) == (*a == *b));
      DFT_CHECK(dftracer::same_type(std::type_index(*a), *b) == (*a == *b));
      ++n;
    }
  std::cout << "✓ same_type agrees with == on " << n << " pairs" << std::endl;
}

Metadata event_metadata(size_t value_bytes, int keys) {
  Metadata m;
  m.insert_or_assign("fhash", std::string(16, 'f'));
  m.insert_or_assign("size", static_cast<uint64_t>(value_bytes));
  m.insert_or_assign("offset", static_cast<int64_t>(-5));
  m.insert_or_assign("ratio", 0.25);
  m.insert_or_assign("big", std::string(value_bytes, 'v'));
  m.insert_or_assign("raw", RawJson("{\"a\":[1,2]}"));
  for (int i = 0; i < keys; ++i) m.insert_or_assign("k" + std::to_string(i), i);
  return m;
}

void encode(ipc::ByteWriter& w, const Metadata& m, const char* name) {
  ipc::encode_event(w, 42, name, "POSIX", TraceEventType::TRACE_TYPE_LIBC_IO,
                    100, 7, &m, 9, 11, SHM_EVENT_INC_METADATA);
}

// The three ByteWriter forms write the same record; records larger than the
// 4 KB buffer and than the direct reservation included.
void test_writer_forms() {
  for (size_t value : {0u, 100u, 4000u, 5000u, 70000u}) {
    Metadata m = event_metadata(value, 10);
    std::vector<uint8_t> exact;
    {
      ipc::ByteWriter w(exact);
      encode(w, m, "pwrite");
    }
    std::vector<uint8_t> buffer(4096);
    size_t buffer_n;
    {
      ipc::ByteWriter w(buffer, ipc::ByteWriter::Buffer{});
      encode(w, m, "pwrite");
      buffer_n = w.size();
    }
    DFT_CHECK(buffer_n == exact.size());
    DFT_CHECK(std::equal(exact.begin(), exact.end(), buffer.begin()));
    for (size_t room : {64u, 512u, 1u << 20}) {
      std::vector<uint8_t> direct(room), fallback(4096);
      ipc::ByteWriter w(direct.data(), room, fallback);
      encode(w, m, "pwrite");
      DFT_CHECK(w.size() == exact.size());
      DFT_CHECK(w.moved() == (exact.size() > room));
      const uint8_t* got = w.moved() ? fallback.data() : direct.data();
      DFT_CHECK(std::equal(exact.begin(), exact.end(), got));
    }
    ipc::DecodedEvent e;
    DFT_CHECK(ipc::decode_event(exact.data(), exact.size(), e));
    DFT_CHECK(e.name == "pwrite" && e.values.size() == m.size());
  }
  std::cout << "✓ ByteWriter forms write identical records" << std::endl;
}

// One block's worth of interned records, written and read as the producer
// and the service do.
struct Block {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(64 << 20);
  std::vector<uint8_t> fallback = std::vector<uint8_t>(4096);
  std::vector<std::pair<size_t, size_t>> records;
  size_t used = 0;
  ipc::Interner intern;
  ipc::InternTable table;
  uint64_t serial = 1;

  Block() { intern.begin(serial); }
  void next_block() {
    intern.begin(++serial);
    table.clear();
    used = 0;
    records.clear();
  }
  // Encodes one event; `keep` false is a record that is cancelled.
  void put(const char* name, const Metadata* m, bool keep = true) {
    intern.begin(serial);
    const uint16_t mark = intern.mark();
    ipc::ByteWriter w(bytes.data() + used, bytes.size() - used, fallback,
                      &intern);
    ipc::encode_event(w, 1, name, "C_APP", TraceEventType::TRACE_TYPE_C_APP, 1,
                      1, m, 1, 1, SHM_EVENT_INC_METADATA);
    DFT_CHECK(!w.moved());
    if (keep) {
      records.emplace_back(used, w.size());
      used += w.size();
    } else {
      intern.rollback(mark);
    }
  }
  bool get(size_t i, ipc::DecodedEvent& e) {
    return ipc::decode_event(bytes.data() + records[i].first, records[i].second,
                             e, &table);
  }
};

void test_interning_collisions() {
  // 2000 distinct names at 2000 addresses: far more than the producer's 256
  // slots, so slots are overwritten all the time. Each name twice.
  Block b;
  std::vector<std::string> names;
  for (int i = 0; i < 2000; ++i) names.push_back("name" + std::to_string(i));
  for (int round = 0; round < 2; ++round)
    for (const auto& n : names) b.put(n.c_str(), nullptr);
  ipc::DecodedEvent e;
  for (size_t i = 0; i < b.records.size(); ++i) {
    DFT_CHECK(b.get(i, e));
    DFT_CHECK(e.name == names[i % names.size()] && e.category == "C_APP");
  }
  std::cout << "✓ interning with slot collisions" << std::endl;
}

void test_interning_rollback() {
  // A cancelled record with more new strings than the producer undoes one
  // by one (32), between records that refer to earlier strings.
  Block b;
  Metadata few;
  few.insert_or_assign("a", 1);
  few.insert_or_assign("b", 2);
  b.put("first", &few);
  Metadata many;
  for (int i = 0; i < 40; ++i)
    many.insert_or_assign("m" + std::to_string(i), i);
  b.put("cancelled", &many, false);
  b.put("first", &few);
  b.put("after", &many);
  b.put("first", &few);
  // And a cancelled record with a few new strings (undone one by one).
  Metadata two;
  two.insert_or_assign("x", 1);
  two.insert_or_assign("y", 2);
  b.put("cancelled2", &two, false);
  b.put("after2", &two);
  ipc::DecodedEvent e;
  const char* want[] = {"first", "first", "after", "first", "after2"};
  for (size_t i = 0; i < b.records.size(); ++i) {
    DFT_CHECK(b.get(i, e));
    DFT_CHECK(e.name == want[i]);
  }
  DFT_CHECK(b.get(2, e) && e.values.size() == 40);
  for (int i = 0; i < 40; ++i)
    DFT_CHECK(e.values[i].key == "m" + std::to_string(i) && e.values[i].i == i);
  DFT_CHECK(b.get(4, e) && e.values.size() == 2 && e.values[0].key == "x");
  std::cout << "✓ interning rollback keeps producer and service in step"
            << std::endl;
}

void test_interning_id_overflow() {
  // More distinct strings than a block has ids: the rest stay inline, and
  // everything still reads back.
  Block b;
  const int n = DFT_SHM_INTERN_MAX + 3000;
  std::vector<std::string> names;
  names.reserve(n);
  for (int i = 0; i < n; ++i) names.push_back("u" + std::to_string(i));
  for (int i = 0; i < n; ++i) b.put(names[i].c_str(), nullptr);
  // Early names still refer by id, late ones are inline.
  b.put(names[5].c_str(), nullptr);
  b.put(names[n - 1].c_str(), nullptr);
  ipc::DecodedEvent e;
  for (int i = 0; i < n; ++i) {
    DFT_CHECK(b.get(i, e));
    DFT_CHECK(e.name == names[i]);
  }
  DFT_CHECK(b.get(n, e) && e.name == names[5]);
  DFT_CHECK(b.get(n + 1, e) && e.name == names[n - 1]);
  // A new block has all its ids again.
  b.next_block();
  b.put("fresh", nullptr);
  b.put("fresh", nullptr);
  DFT_CHECK(b.records[1].second < b.records[0].second);
  // The service reads a block's records in order: the first defines.
  DFT_CHECK(b.get(0, e) && e.name == "fresh");
  DFT_CHECK(b.get(1, e) && e.name == "fresh");
  std::cout << "✓ interning past " << DFT_SHM_INTERN_MAX << " ids in a block"
            << std::endl;
}

void test_interning_long_strings() {
  // An interned length says at most DFT_SHM_INTERN_MAX bytes: longer names
  // and keys are cut there (the JSON line limit, 16 KB, cuts them earlier
  // in any mode). Same name twice: the second by reference.
  Block b;
  std::string name(DFT_SHM_INTERN_MAX + 500, 'n');
  std::string key(DFT_SHM_INTERN_MAX + 10, 'k');
  Metadata m;
  m.insert_or_assign(key, 1);
  b.put(name.c_str(), &m);
  b.put(name.c_str(), &m);
  ipc::DecodedEvent e;
  for (int i = 0; i < 2; ++i) {
    DFT_CHECK(b.get(i, e));
    DFT_CHECK(e.name.size() == DFT_SHM_INTERN_MAX);
    DFT_CHECK(e.name == std::string_view(name).substr(0, DFT_SHM_INTERN_MAX));
    DFT_CHECK(e.values.size() == 1 &&
              e.values[0].key.size() == DFT_SHM_INTERN_MAX);
  }
  DFT_CHECK(b.records[1].second < b.records[0].second);
  std::cout << "✓ interning of strings longer than an interned length"
            << std::endl;
}

void test_interning_matches_plain() {
  // Every event decodes to the same fields interned or plain.
  Block b;
  std::vector<Metadata> ms;
  for (int i = 0; i < 50; ++i) ms.push_back(event_metadata(i * 37, i % 7));
  for (int i = 0; i < 50; ++i) b.put(i % 2 ? "read" : "write", &ms[i]);
  ipc::DecodedEvent interned, plain;
  for (int i = 0; i < 50; ++i) {
    std::vector<uint8_t> bytes;
    ipc::ByteWriter w(bytes);
    ipc::encode_event(w, 1, i % 2 ? "read" : "write", "C_APP",
                      TraceEventType::TRACE_TYPE_C_APP, 1, 1, &ms[i], 1, 1,
                      SHM_EVENT_INC_METADATA);
    DFT_CHECK(ipc::decode_event(bytes.data(), bytes.size(), plain));
    DFT_CHECK(b.get(i, interned));
    DFT_CHECK(interned.name == plain.name &&
              interned.category == plain.category);
    DFT_CHECK(interned.values.size() == plain.values.size());
    for (size_t j = 0; j < plain.values.size(); ++j) {
      const auto& x = interned.values[j];
      const auto& y = plain.values[j];
      DFT_CHECK(x.key == y.key && x.tag == y.tag && x.mtype == y.mtype);
      DFT_CHECK(x.s == y.s && x.u == y.u);
    }
    DFT_CHECK((interned.head.flags & ~SHM_EVENT_INTERNED) == plain.head.flags);
  }
  std::cout << "✓ interned and plain records decode alike" << std::endl;
}

}  // namespace

int main() {
  test_metadata_semantics();
  test_metadata_reuse_threads();
  check_same_type(Candidates{});
  test_writer_forms();
  test_interning_collisions();
  test_interning_rollback();
  test_interning_id_overflow();
  test_interning_long_strings();
  test_interning_matches_plain();
  std::cout << "test_regression: all checks passed" << std::endl;
  return 0;
}
