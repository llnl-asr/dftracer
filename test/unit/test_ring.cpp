// Correctness of the shared-memory event ring (core/ipc): block claiming,
// reservation and commit inside a private block, publishing with padding,
// flush of a quiet producer's block, full-ring backpressure, and exactly-once,
// per-producer-ordered delivery under many producer threads and processes.

#include <dftracer/core/ipc/ring_producer.h>
#include <dftracer/core/ipc/shm_region.h>
#include <dftracer/service/pipeline/block_tracker.h>
#include <dftracer/service/pipeline/consumer_scaler.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "check.h"

using namespace dftracer::ipc;

namespace {

// DFT_CHECK exits without running destructors, which would leave the
// segments of a failed run in /dev/shm. Remove them at exit instead; forked
// producers leave through _exit and so never remove the parent's segments.
std::vector<std::string>& created_segments() {
  static std::vector<std::string> names;
  return names;
}

void remove_created_segments() {
  for (const auto& name : created_segments())
    boost::interprocess::shared_memory_object::remove(name.c_str());
}

std::string segment_name(const char* what) {
  // Construct the list before registering the handler: exit-time cleanup runs
  // in reverse order, so the list must outlive the handler that reads it.
  static bool registered =
      (created_segments(), std::atexit(remove_created_segments), true);
  (void)registered;
  std::string name =
      "/dftracer-test-ring-" + std::to_string(getpid()) + "-" + what;
  created_segments().push_back(name);
  return name;
}

// Payload layout written by producers and verified by the consumer.
struct TestPayload {
  uint32_t producer;
  uint32_t bytes;  // total payload bytes, this struct included
  uint64_t seq;
};

uint8_t fill_byte(uint32_t producer, uint64_t seq, size_t i) {
  return static_cast<uint8_t>(seq * 131 + producer * 17 + i);
}

void produce(const RingView& ring, uint32_t producer, uint64_t count,
             uint32_t max_bytes, uint32_t seed) {
  RingProducer writer(ring, producer + 1);
  std::mt19937 rng(seed);
  std::uniform_int_distribution<uint32_t> size(sizeof(TestPayload), max_bytes);
  for (uint64_t seq = 0; seq < count; ++seq) {
    const uint32_t bytes = size(rng);
    Reservation r;
    RingProducer::Result result;
    while ((result = writer.reserve(bytes, SHM_KIND_RAW, r, 1000000000ULL)) ==
           RingProducer::Result::FULL) {
    }
    DFT_CHECK(result == RingProducer::Result::OK);
    TestPayload p{producer, bytes, seq};
    std::memcpy(r.payload, &p, sizeof(p));
    for (size_t i = sizeof(p); i < bytes; ++i)
      r.payload[i] = fill_byte(producer, seq, i);
    writer.commit(r);
  }
  // The destructor publishes the last, partly filled block.
}

// Drains until every producer's `count` records have been seen, checking each
// one. Returns the number of records received.
uint64_t consume_and_verify(const RingView& ring, uint32_t producers,
                            uint64_t count) {
  BlockTracker tracker(ring, /*verify=*/true);
  std::vector<uint64_t> next(producers, 0);
  uint64_t received = 0;
  const uint64_t expected = static_cast<uint64_t>(producers) * count;
  while (received < expected) {
    uint64_t seq;
    if (!tracker.poll(seq)) {
      if (tracker.corrupt() != 0) {
        const uint64_t w = tracker.bad_word();
        std::cerr << "corrupt header: seq=" << tracker.bad_seq()
                  << " epoch=" << ring.epoch(tracker.bad_seq())
                  << " offset=" << tracker.bad_offset()
                  << " word.epoch=" << record_epoch(w)
                  << " word.length=" << record_length(w) << " word.state=0x"
                  << std::hex << int(record_state(w)) << std::dec
                  << " head=" << ring.header()->head.load()
                  << " armed=" << ring.block(tracker.bad_seq()).armed.load()
                  << std::endl;
      }
      DFT_CHECK(tracker.corrupt() == 0);
      cpu_relax();
      continue;
    }
    for_each_record(
        ring, seq,
        [&](const RecordHeader& hdr, const uint8_t* payload, uint32_t room) {
          DFT_CHECK(hdr.kind == SHM_KIND_RAW);
          TestPayload p;
          std::memcpy(&p, payload, sizeof(p));
          DFT_CHECK(p.producer < producers);
          DFT_CHECK(hdr.pid == p.producer + 1);
          DFT_CHECK(p.bytes <= room && room < p.bytes + DFT_SHM_RECORD_ALIGN);
          // Exactly once and in the producer's own order.
          DFT_CHECK(p.seq == next[p.producer]);
          next[p.producer]++;
          for (size_t i = sizeof(p); i < p.bytes; ++i)
            DFT_CHECK(payload[i] == fill_byte(p.producer, p.seq, i));
          ++received;
        });
    tracker.release(seq);
  }
  for (uint32_t i = 0; i < producers; ++i) DFT_CHECK(next[i] == count);
  return received;
}

void test_layout_and_open() {
  DFT_CHECK(RingView::valid_geometry(4, 4096));
  DFT_CHECK(!RingView::valid_geometry(3, 4096));
  DFT_CHECK(!RingView::valid_geometry(4, 1000));
  DFT_CHECK(RingView::data_offset(4) % DFT_SHM_DATA_ALIGN == 0);

  const std::string name = segment_name("open");
  auto owner = ShmRegion::create(name, 8, 4096);
  DFT_CHECK(owner != nullptr);
  DFT_CHECK(owner->ring().header()->magic == DFT_SHM_SEGMENT_MAGIC);
  auto client = ShmRegion::open(name);
  DFT_CHECK(client != nullptr);
  DFT_CHECK(client->ring().block_count() == 8);
  DFT_CHECK(client->ring().block_size() == 4096);

  // A segment from another ABI is refused rather than misread.
  owner->ring().header()->abi_version = DFT_SHM_ABI_VERSION + 1;
  DFT_CHECK(ShmRegion::open(name) == nullptr);
  owner->ring().header()->abi_version = DFT_SHM_ABI_VERSION;

  DFT_CHECK(ShmRegion::open(segment_name("missing")) == nullptr);
}

void test_single_record_and_flush() {
  auto region = ShmRegion::create(segment_name("single"), 4, 4096);
  const RingView& ring = region->ring();
  RingProducer writer(ring, 42);
  BlockTracker tracker(ring, /*verify=*/true);

  // Nothing is claimed until there is something to write.
  writer.flush();
  DFT_CHECK(!writer.holding());
  DFT_CHECK(ring.header()->head.load() == 0);

  Reservation r;
  DFT_CHECK(writer.try_reserve(5, SHM_KIND_RAW, r) == RingProducer::Result::OK);
  DFT_CHECK(r.length == 32);  // 16-byte header + 5 bytes, rounded to 16
  // The first record claims block 0.
  DFT_CHECK(writer.holding());
  DFT_CHECK(ring.header()->head.load() == 1);
  DFT_CHECK(ring.block(0).claim_ns.load() != 0);
  std::memcpy(r.payload, "hello", 5);

  uint64_t seq;
  DFT_CHECK(!tracker.poll(seq));
  writer.commit(r);
  // Committed, but the block is still private to the producer.
  DFT_CHECK(!tracker.poll(seq));
  // Not old enough yet; then old enough.
  DFT_CHECK(!writer.flush_if_older(monotonic_ns(), 60000000000ULL));
  DFT_CHECK(writer.flush_if_older(monotonic_ns(), 0));
  DFT_CHECK(!writer.holding());
  DFT_CHECK(tracker.poll(seq));
  DFT_CHECK(seq == 0);
  int seen = 0;
  for_each_record(ring, seq,
                  [&](const RecordHeader& hdr, const uint8_t* p, uint32_t) {
                    DFT_CHECK(hdr.pid == 42);
                    DFT_CHECK(std::memcmp(p, "hello", 5) == 0);
                    ++seen;
                  });
  DFT_CHECK(seen == 1);
  tracker.release(seq);
  // Released block 0 is re-armed for its second lap.
  DFT_CHECK(ring.epoch(0) == 1 && ring.epoch(ring.block_count()) == 2);
  DFT_CHECK(ring.block(0).armed.load() == 2);
  DFT_CHECK(ring.block(0).claim_ns.load() == 0);

  // A reservation that is never committed is published as padding.
  DFT_CHECK(writer.try_reserve(5, SHM_KIND_RAW, r) == RingProducer::Result::OK);
  writer.flush();
  DFT_CHECK(tracker.poll(seq) && seq == 1);
  seen = 0;
  for_each_record(
      ring, seq,
      [&](const RecordHeader&, const uint8_t*, uint32_t) { ++seen; });
  DFT_CHECK(seen == 0);
  tracker.release(seq);
}

void test_too_large_and_exact_fit() {
  auto region = ShmRegion::create(segment_name("large"), 4, 4096);
  const RingView& ring = region->ring();
  RingProducer writer(ring, 1);
  Reservation r;
  DFT_CHECK(writer.try_reserve(4096, SHM_KIND_RAW, r) ==
            RingProducer::Result::TOO_LARGE);
  DFT_CHECK(!writer.holding());
  // A record exactly one block long fits.
  DFT_CHECK(writer.try_reserve(4096 - sizeof(RecordHeader), SHM_KIND_RAW, r) ==
            RingProducer::Result::OK);
  writer.commit(r);
  BlockTracker tracker(ring, /*verify=*/true);
  uint64_t seq;
  DFT_CHECK(!tracker.poll(seq));
  // The next reservation publishes block 0 and claims block 1.
  DFT_CHECK(writer.try_reserve(8, SHM_KIND_RAW, r) == RingProducer::Result::OK);
  writer.commit(r);
  DFT_CHECK(ring.header()->head.load() == 2);
  DFT_CHECK(tracker.poll(seq) && seq == 0);
}

void test_full_ring() {
  auto region = ShmRegion::create(segment_name("full"), 2, 4096);
  const RingView& ring = region->ring();
  RingProducer writer(ring, 1);
  BlockTracker tracker(ring, /*verify=*/true);
  Reservation r;
  uint64_t accepted = 0;
  RingProducer::Result result;
  while ((result = writer.try_reserve(1000, SHM_KIND_RAW, r)) ==
         RingProducer::Result::OK) {
    writer.commit(r);
    ++accepted;
    DFT_CHECK(accepted < 100);
  }
  DFT_CHECK(result == RingProducer::Result::FULL);
  // 1000 bytes + 16-byte header round to 1024, so four records fill each
  // 4 KiB block exactly: eight in all before the ring is full. The failed
  // attempt published the second block before trying to claim a third.
  DFT_CHECK(accepted == 8);
  DFT_CHECK(!writer.holding());
  // Repeated attempts on a full ring claim nothing.
  for (int i = 0; i < 1000; ++i)
    DFT_CHECK(writer.try_reserve(1000, SHM_KIND_RAW, r) ==
              RingProducer::Result::FULL);
  DFT_CHECK(ring.header()->head.load() == 2);
  // A bounded wait gives up.
  DFT_CHECK(writer.reserve(1000, SHM_KIND_RAW, r, 1000000) ==
            RingProducer::Result::FULL);

  uint64_t seq;
  DFT_CHECK(tracker.poll(seq) && seq == 0);
  tracker.release(seq);
  DFT_CHECK(tracker.poll(seq) && seq == 1);
  DFT_CHECK(writer.try_reserve(1000, SHM_KIND_RAW, r) ==
            RingProducer::Result::OK);
  writer.commit(r);
  DFT_CHECK(ring.header()->head.load() == 3);
}

void test_many_threads() {
  auto region = ShmRegion::create(segment_name("threads"), 16, 4096);
  const RingView& ring = region->ring();
  const uint32_t producers = 8;
  const uint64_t count = 20000;
  std::vector<std::thread> threads;
  for (uint32_t p = 0; p < producers; ++p)
    threads.emplace_back(produce, ring, p, count, 600, 1234 + p);
  DFT_CHECK(consume_and_verify(ring, producers, count) == producers * count);
  for (auto& t : threads) t.join();
}

// Several consumer threads take blocks through ParallelBlockClaimer while
// several producers fill them. Blocks are processed and released out of
// order, so per-producer order only holds within a block: check every record
// arrives exactly once, intact.
void test_parallel_consumers() {
  auto region = ShmRegion::create(segment_name("pconsume"), 16, 4096);
  const RingView& ring = region->ring();
  const uint32_t producers = 8;
  const uint32_t consumers = 4;
  const uint64_t count = 20000;
  std::vector<std::atomic<uint8_t>> seen(producers * count);
  for (auto& s : seen) s.store(0);
  std::atomic<uint64_t> received{0};
  ParallelBlockClaimer claimer(ring);
  std::vector<std::thread> threads;
  for (uint32_t p = 0; p < producers; ++p)
    threads.emplace_back(produce, ring, p, count, 600, 777 + p);
  for (uint32_t c = 0; c < consumers; ++c) {
    threads.emplace_back([&]() {
      while (received.load() < producers * count) {
        uint64_t seq;
        if (!claimer.claim(seq)) {
          cpu_relax();
          continue;
        }
        for_each_record(
            ring, seq,
            [&](const RecordHeader& hdr, const uint8_t* payload,
                uint32_t room) {
              TestPayload p;
              std::memcpy(&p, payload, sizeof(p));
              DFT_CHECK(p.producer < producers && p.seq < count);
              DFT_CHECK(hdr.pid == p.producer + 1);
              DFT_CHECK(p.bytes <= room &&
                        room < p.bytes + DFT_SHM_RECORD_ALIGN);
              for (size_t i = sizeof(p); i < p.bytes; ++i)
                DFT_CHECK(payload[i] == fill_byte(p.producer, p.seq, i));
              DFT_CHECK(seen[p.producer * count + p.seq].exchange(1) == 0);
              received.fetch_add(1);
            });
        claimer.release(seq);
      }
    });
  }
  for (auto& t : threads) t.join();
  DFT_CHECK(received.load() == producers * count);
  for (auto& s : seen) DFT_CHECK(s.load() == 1);
  // Every block was released, out of order or not, so the tail caught up.
  if (claimer.backlog_blocks() != 0)
    std::cerr << "head=" << ring.header()->head.load()
              << " next=" << claimer.next() << " tail=" << claimer.tail()
              << std::endl;
  DFT_CHECK(claimer.backlog_blocks() == 0);
}

void test_many_processes() {
  const std::string name = segment_name("procs");
  auto region = ShmRegion::create(name, 16, 4096);
  const uint32_t processes = 4;
  const uint32_t threads_each = 2;
  const uint64_t count = 10000;
  std::vector<pid_t> children;
  for (uint32_t c = 0; c < processes; ++c) {
    pid_t pid = fork();
    DFT_CHECK(pid >= 0);
    if (pid == 0) {
      // Attach by name, as a traced process would, not through the mapping
      // inherited from the parent.
      auto client = ShmRegion::open(name);
      if (!client) _exit(2);
      std::vector<std::thread> threads;
      for (uint32_t t = 0; t < threads_each; ++t) {
        uint32_t producer = c * threads_each + t;
        threads.emplace_back(produce, client->ring(), producer, count, 1500,
                             99 + producer);
      }
      for (auto& t : threads) t.join();
      _exit(0);
    }
    children.push_back(pid);
  }
  DFT_CHECK(consume_and_verify(region->ring(), processes * threads_each,
                               count) == processes * threads_each * count);
  for (pid_t pid : children) {
    int status = 0;
    DFT_CHECK(waitpid(pid, &status, 0) == pid);
    DFT_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
}

}  // namespace

// Thread-count controller: up at once to cover arrivals plus backlog, down one
// thread per `down_ticks` quiet ticks, never outside [min, max].
void test_consumer_scaler() {
  const uint64_t ms = 1000000;
  ConsumerScaler scaler(1, 8, 100 * ms, 3, 1.0);
  ConsumerScaler::Sample s{};
  uint32_t active = 1;
  active = scaler.update(s, active);  // baseline only
  DFT_CHECK(active == 1);

  // A thread drains 1 block per ms of busy time. 4 blocks/ms arrive: 4 threads.
  auto tick = [&](uint64_t arrived, uint64_t drained, uint64_t ready) {
    s.now_ns += 10 * ms;
    s.head += arrived;
    s.drained += drained;
    s.busy_ns += drained * ms;
    s.ready = ready;
    active = scaler.update(s, active);
  };
  tick(40, 10, 0);
  DFT_CHECK(active == 4);
  // Same rate, plus 100 blocks behind: 1 more block/ms to clear in 100 ms.
  tick(40, 40, 100);
  DFT_CHECK(active == 5);
  // Never above max.
  tick(400, 50, 1000);
  DFT_CHECK(active == 8);
  // Load gone: hold for down_ticks - 1 ticks, then one thread per 3 ticks.
  tick(0, 10, 0);
  tick(0, 10, 0);
  DFT_CHECK(active == 8);
  tick(0, 10, 0);
  DFT_CHECK(active == 7);
  for (int i = 0; i < 3 * 10; ++i) tick(0, 10, 0);
  DFT_CHECK(active == 1);
  // A burst between quiet ticks resets the step-down count.
  tick(0, 10, 0);
  tick(0, 10, 0);
  tick(20, 10, 0);
  DFT_CHECK(active == 2);
  tick(0, 10, 0);
  tick(0, 10, 0);
  DFT_CHECK(active == 2);

  // Before any block has been drained there is no rate: one thread at a time.
  ConsumerScaler cold(2, 8, 100 * ms, 3);
  ConsumerScaler::Sample c{};
  uint32_t n = cold.update(c, 1);
  DFT_CHECK(n == 2);  // raised to min
  c.now_ns += 10 * ms;
  c.head = 50;
  c.ready = 50;
  n = cold.update(c, n);
  DFT_CHECK(n == 3);
}

// Write one committed record of `value` through `writer`.
void put(RingProducer& writer, uint32_t value) {
  Reservation r;
  DFT_CHECK(writer.try_reserve(sizeof(value), SHM_KIND_RAW, r) ==
            RingProducer::Result::OK);
  std::memcpy(r.payload, &value, sizeof(value));
  writer.commit(r);
}

// Records of block `seq` as values, walked as the service walks it.
std::vector<uint32_t> values_of(const RingView& ring, uint64_t seq,
                                bool salvage, uint32_t* uncommitted = nullptr) {
  std::vector<uint32_t> values;
  uint32_t n = for_each_record(
      ring, seq,
      [&](const RecordHeader&, const uint8_t* payload, uint32_t) {
        uint32_t v;
        std::memcpy(&v, payload, sizeof(v));
        values.push_back(v);
      },
      salvage);
  if (uncommitted != nullptr) *uncommitted = n;
  return values;
}

// A block its producer keeps without publishing neither holds back the blocks
// after it nor stays stuck: the service passes over it, then takes it over.
void test_take_over() {
  auto region = ShmRegion::create(segment_name("takeover"), 8, 4096);
  const RingView& ring = region->ring();
  const uint32_t pid = static_cast<uint32_t>(getpid());
  ParallelBlockClaimer claimer(ring);
  uint64_t seq;
  uint32_t owner;

  RingProducer quiet(ring, pid, 11);
  for (uint32_t v = 1; v <= 3; ++v) put(quiet, v);  // block 0, held
  RingProducer busy(ring, pid, 12);
  put(busy, 100);
  busy.flush();  // block 1, published
  DFT_CHECK(claimer.claim(seq) && seq == 1);
  DFT_CHECK(ring.block(seq).owner_tid.load() == 12);
  DFT_CHECK(values_of(ring, seq, false) == std::vector<uint32_t>{100});
  claimer.release(seq);
  DFT_CHECK(!claimer.claim(seq));

  // Too young to take; then taken, with every committed record.
  const uint64_t claimed = ring.block(0).claim_ns.load();
  DFT_CHECK(!claimer.take_over(seq, claimed - 1, owner));
  DFT_CHECK(claimer.take_over(seq, monotonic_ns(), owner));
  DFT_CHECK(seq == 0 && owner == pid);
  DFT_CHECK(ring.block(seq).owner_tid.load() == 11);
  uint32_t uncommitted = 1;
  DFT_CHECK((values_of(ring, seq, true, &uncommitted) ==
             std::vector<uint32_t>{1, 2, 3}));
  DFT_CHECK(uncommitted == 0);
  claimer.release(seq);
  DFT_CHECK(!claimer.take_over(seq, monotonic_ns(), owner));

  // The producer notices, moves to a new block, and loses nothing after.
  put(quiet, 4);
  DFT_CHECK(quiet.taken_blocks() == 1);
  quiet.flush();
  DFT_CHECK(claimer.claim(seq) && seq == 2);
  DFT_CHECK(values_of(ring, seq, false) == std::vector<uint32_t>{4});
  claimer.release(seq);

  // A record being written is never taken from a live producer...
  RingProducer writing(ring, pid, 13);
  put(writing, 5);
  Reservation open;
  DFT_CHECK(writing.try_reserve(4, SHM_KIND_RAW, open) ==
            RingProducer::Result::OK);
  DFT_CHECK(!claimer.take_over(seq, monotonic_ns(), owner));
  writing.commit(open);
  writing.flush();
  DFT_CHECK(claimer.claim(seq));
  claimer.release(seq);

  // ...but is from a dead one: committed records kept, the open one skipped.
  pid_t child = fork();
  if (child == 0) {
    RingProducer dying(ring, static_cast<uint32_t>(getpid()), 14);
    put(dying, 6);
    put(dying, 7);
    Reservation never;
    if (dying.try_reserve(4, SHM_KIND_RAW, never) != RingProducer::Result::OK)
      _exit(1);
    _exit(0);  // no destructor: the block stays WRITING
  }
  int status = 0;
  waitpid(child, &status, 0);
  DFT_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  DFT_CHECK(claimer.take_over(seq, monotonic_ns(), owner));
  DFT_CHECK(owner == static_cast<uint32_t>(child));
  DFT_CHECK((values_of(ring, seq, true, &uncommitted) ==
             std::vector<uint32_t>{6, 7}));
  DFT_CHECK(uncommitted == 1);
  claimer.release(seq);
  DFT_CHECK(claimer.backlog_blocks() == 0);
}

// The service shrinks its ring to the shared memory there is: fewer blocks,
// then smaller ones, never below the minimum.
void test_fit_geometry() {
  const uint64_t mib = 1 << 20;
  uint32_t count = 1024, size = 1 << 20;
  DFT_CHECK(RingView::fit(count, size, 4096 * mib));  // fits as asked
  DFT_CHECK(count == 1024 && size == (1u << 20));
  DFT_CHECK(RingView::fit(count, size, 60 * mib));  // a 64 MB container
  DFT_CHECK(count == 32 && size == (1u << 20));
  DFT_CHECK(RingView::bytes_for(count, size) <= 60 * mib);
  count = 1024, size = 1 << 20;
  DFT_CHECK(RingView::fit(count, size, 2 * mib));  // blocks shrink last
  DFT_CHECK(count == DFT_SHM_MIN_BLOCKS && size == (64u << 10));
  count = 1024, size = 1 << 20;
  DFT_CHECK(!RingView::fit(count, size, 64 * 1024));  // nothing fits
  DFT_CHECK(count == DFT_SHM_MIN_BLOCKS && size == DFT_SHM_MIN_BLOCK_SIZE);
  DFT_CHECK(RingView::valid_geometry(count, size));
}

int main() {
  test_layout_and_open();
  test_single_record_and_flush();
  test_too_large_and_exact_fit();
  test_full_ring();
  test_many_threads();
  test_parallel_consumers();
  test_consumer_scaler();
  test_take_over();
  test_fit_geometry();
#if !defined(__SANITIZE_THREAD__)
  // ThreadSanitizer does not see synchronization that goes through another
  // process (here: producer commit -> consumer release -> producer reuse), so
  // it reports reuse of a released block as a race. Covered without TSan.
  test_many_processes();
#endif
  std::cout << "test_ring: all checks passed" << std::endl;
  return 0;
}
