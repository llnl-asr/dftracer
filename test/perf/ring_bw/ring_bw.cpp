// Bandwidth benchmark for the shared-memory event ring (core/ipc).
//
// Measures how fast N producers (threads or processes) can push records into
// one ring while a single consumer completes and releases blocks, so the
// numbers can be compared with STREAM on the same node (run_ring_bw.sh).
//
// Modes
//   produce   producers write records continuously; the consumer only walks
//             record headers and releases blocks (no payload copy). Measures
//             the producer path: reserve + payload copy + commit.
//   consume   the ring is filled first, then the consumer alone drains it,
//             copying every block out. Measures consumer copy-out.
//   both      producers write continuously and the consumer copies every block
//             out. End-to-end.
//
// Accounting: STREAM Copy counts bytes read + bytes written. Producer and
// consumer are both copy kernels, so "copy_GBps" is 2 x payload bytes/s for the
// producers and "consumer_copy_GBps" 2 x block bytes/s released by the
// consumer -- the figures to compare with STREAM Copy. Payload-only GB/s and
// records/s are reported too.
//
// One CSV row per run on stdout (header with --header).

#include <dftracer/core/ipc/ring_producer.h>
#include <dftracer/core/ipc/shm_region.h>
#include <dftracer/service/pipeline/block_tracker.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(__x86_64__)
#include <immintrin.h>
#endif

using namespace dftracer::ipc;

namespace {

struct Options {
  std::string mode = "produce";    // produce | consume | both
  std::string launch = "threads";  // threads | processes
  uint32_t producers = 1;
  uint32_t record = 1024;  // payload bytes per record
  uint32_t block_kib = 1024;
  uint32_t ring_mib = 1024;
  double seconds = 5.0;
  double warmup = 1.0;
  int first_cpu = 0;  // producers pinned to first_cpu + i*stride
  int cpu_stride = 1;
  int consumer_cpu = -1;     // default: right after the producers
  bool nontemporal = false;  // streaming stores for the payload copy
  bool header = false;
};

// Per-producer counters, one cache line each, in memory shared with forked
// producers.
struct alignas(64) ProducerStats {
  std::atomic<uint64_t> records;
  std::atomic<uint64_t> full_retries;
};

struct Control {
  std::atomic<uint32_t> ready;
  std::atomic<uint32_t> stop;
  std::atomic<uint32_t> fill_done;
  alignas(64) ProducerStats stats[1];  // flexible: sized at allocation
};

void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--mode=produce|consume|both] "
               "[--launch=threads|processes] [--producers=N] [--record=B] "
               "[--block-kib=K] [--ring-mib=M] [--seconds=S] [--warmup=S] "
               "[--first-cpu=C] [--cpu-stride=S] [--consumer-cpu=C] "
               "[--nontemporal] [--header]\n",
               argv0);
}

bool parse(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&](const char* key) -> const char* {
      size_t n = std::strlen(key);
      return a.compare(0, n, key) == 0 ? a.c_str() + n : nullptr;
    };
    const char* v;
    if ((v = val("--mode=")))
      o.mode = v;
    else if ((v = val("--launch=")))
      o.launch = v;
    else if ((v = val("--producers=")))
      o.producers = std::strtoul(v, nullptr, 10);
    else if ((v = val("--record=")))
      o.record = std::strtoul(v, nullptr, 10);
    else if ((v = val("--block-kib=")))
      o.block_kib = std::strtoul(v, nullptr, 10);
    else if ((v = val("--ring-mib=")))
      o.ring_mib = std::strtoul(v, nullptr, 10);
    else if ((v = val("--seconds=")))
      o.seconds = std::atof(v);
    else if ((v = val("--warmup=")))
      o.warmup = std::atof(v);
    else if ((v = val("--first-cpu=")))
      o.first_cpu = std::atoi(v);
    else if ((v = val("--cpu-stride=")))
      o.cpu_stride = std::atoi(v);
    else if ((v = val("--consumer-cpu=")))
      o.consumer_cpu = std::atoi(v);
    else if (a == "--nontemporal")
      o.nontemporal = true;
    else if (a == "--header")
      o.header = true;
    else
      return false;
  }
  if (o.mode != "produce" && o.mode != "consume" && o.mode != "both")
    return false;
  if (o.launch != "threads" && o.launch != "processes") return false;
  return o.producers > 0 && o.record > 0;
}

void pin(int cpu) {
  if (cpu < 0) return;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

// Payload copy into the ring. Both pointers are 16-byte aligned (records are).
inline void copy_payload(uint8_t* dst, const uint8_t* src, size_t n,
                         bool nontemporal) {
#if defined(__x86_64__)
  if (nontemporal) {
    size_t i = 0;
    for (; i + 64 <= n; i += 64) {
      __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(src + i));
      __m128i b =
          _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 16));
      __m128i c =
          _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 32));
      __m128i d =
          _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 48));
      _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i), a);
      _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 16), b);
      _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 32), c);
      _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 48), d);
    }
    if (i < n) std::memcpy(dst + i, src + i, n - i);
    // Streaming stores are weakly ordered: they must be globally visible
    // before the commit store publishes the record.
    _mm_sfence();
    return;
  }
#endif
  (void)nontemporal;
  std::memcpy(dst, src, n);
}

// Producer loop. In fill mode it stops at the first FULL (ring filled);
// otherwise it runs until told to stop, retrying while the ring is full.
void producer_loop(const RingView& ring, Control* ctl, uint32_t id,
                   const Options& o, bool fill_only) {
  pin(o.first_cpu + static_cast<int>(id) * o.cpu_stride);
  RingProducer writer(ring, id + 1);
  alignas(64) uint8_t src[65536];
  for (size_t i = 0; i < sizeof(src); ++i)
    src[i] = static_cast<uint8_t>(i * 7 + id);
  ProducerStats& st = ctl->stats[id];
  uint64_t records = 0, full = 0;
  ctl->ready.fetch_add(1);
  while (!ctl->stop.load(std::memory_order_relaxed)) {
    Reservation r;
    RingProducer::Result res = writer.try_reserve(o.record, SHM_KIND_RAW, r);
    if (res != RingProducer::Result::OK) {
      if (res == RingProducer::Result::TOO_LARGE) break;
      if (fill_only) break;
      ++full;
      if ((full & 63) == 0)
        st.full_retries.store(full, std::memory_order_relaxed);
      cpu_relax();
      continue;
    }
    copy_payload(r.payload, src, o.record, o.nontemporal);
    writer.commit(r);
    ++records;
    // A relaxed store to this producer's own line: no contention, and the
    // main thread can sample it at the window edges.
    st.records.store(records, std::memory_order_relaxed);
  }
  st.records.store(records, std::memory_order_relaxed);
  st.full_retries.store(full, std::memory_order_relaxed);
  if (fill_only) ctl->fill_done.fetch_add(1);
}

uint64_t total_records(Control* ctl, uint32_t n) {
  uint64_t sum = 0;
  for (uint32_t i = 0; i < n; ++i)
    sum += ctl->stats[i].records.load(std::memory_order_relaxed);
  return sum;
}

uint64_t total_full(Control* ctl, uint32_t n) {
  uint64_t sum = 0;
  for (uint32_t i = 0; i < n; ++i)
    sum += ctl->stats[i].full_retries.load(std::memory_order_relaxed);
  return sum;
}

double now_s() { return monotonic_ns() * 1e-9; }

// Consumer: complete and release blocks in order; optionally copy each block
// out first. Counts whole blocks: record headers and padding included, since
// that is what a consumer moves. It never walks the records -- that is a chain
// of dependent loads and would make the consumer, not the ring, the limit.
struct ConsumerStats {
  std::atomic<uint64_t> blocks{0};
  std::atomic<uint64_t> bytes{0};
};

void consumer_loop(const RingView& ring, std::atomic<uint32_t>& stop, bool copy,
                   int cpu, ConsumerStats& stats) {
  pin(cpu);
  BlockTracker tracker(ring);
  std::vector<uint8_t> sink(ring.block_size());
  uint64_t blocks = 0;
  while (!stop.load(std::memory_order_relaxed)) {
    uint64_t seq;
    if (!tracker.poll(seq)) {
      cpu_relax();
      continue;
    }
    if (copy) std::memcpy(sink.data(), ring.block_data(seq), ring.block_size());
    tracker.release(seq);
    ++blocks;
    stats.blocks.store(blocks, std::memory_order_relaxed);
    stats.bytes.store(blocks * ring.block_size(), std::memory_order_relaxed);
  }
}

void sleep_s(double s) {
  std::this_thread::sleep_for(std::chrono::duration<double>(s));
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  if (!parse(argc, argv, o)) {
    usage(argv[0]);
    return 2;
  }
  if (o.header) {
    std::printf(
        "mode,launch,producers,record_bytes,block_bytes,ring_bytes,"
        "nontemporal,seconds,records,payload_GBps,copy_GBps,Mrec_per_s,"
        "full_retries,consumer_GBps,consumer_copy_GBps\n");
  }
  const uint32_t block_size = o.block_kib * 1024u;
  const uint64_t ring_bytes = static_cast<uint64_t>(o.ring_mib) << 20;
  const uint32_t block_count = static_cast<uint32_t>(ring_bytes / block_size);
  if (!RingView::valid_geometry(block_count, block_size)) {
    std::fprintf(stderr, "invalid geometry: %u blocks of %u bytes\n",
                 block_count, block_size);
    return 2;
  }
  // Every producer holds one block while filling it; with too few blocks the
  // held ones, not the consumer, decide when the ring is full.
  if (block_count < 4 * o.producers) {
    std::fprintf(stderr,
                 "warning: %u blocks for %u producers; want at least 4 per "
                 "producer\n",
                 block_count, o.producers);
  }
  const std::string name = "/dftracer-ringbw-" + std::to_string(getpid());
  auto region = ShmRegion::create(name, block_count, block_size);
  const RingView& ring = region->ring();

  // Pre-fault the data pages so first-touch page faults are not measured.
  // Done from the first producer's CPU, so the pages land on that NUMA node.
  pin(o.first_cpu);
  std::memset(ring.block_data(0), 0,
              static_cast<size_t>(block_count) * block_size);

  const size_t ctl_bytes =
      sizeof(Control) + sizeof(ProducerStats) * (o.producers);
  auto* ctl =
      static_cast<Control*>(mmap(nullptr, ctl_bytes, PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  if (ctl == MAP_FAILED) {
    std::perror("mmap");
    return 1;
  }
  std::memset(static_cast<void*>(ctl), 0, ctl_bytes);

  const int consumer_cpu =
      o.consumer_cpu >= 0
          ? o.consumer_cpu
          : o.first_cpu + static_cast<int>(o.producers) * o.cpu_stride;
  const bool fill_only = (o.mode == "consume");
  const bool consumer_copies = (o.mode != "produce");

  // Launch producers.
  std::vector<std::thread> threads;
  std::vector<pid_t> children;
  for (uint32_t i = 0; i < o.producers; ++i) {
    if (o.launch == "threads") {
      threads.emplace_back(producer_loop, ring, ctl, i, o, fill_only);
    } else {
      pid_t pid = fork();
      if (pid < 0) {
        std::perror("fork");
        return 1;
      }
      if (pid == 0) {
        producer_loop(ring, ctl, i, o, fill_only);
        _exit(0);
      }
      children.push_back(pid);
    }
  }
  auto join_producers = [&]() {
    for (auto& t : threads) t.join();
    threads.clear();
    for (pid_t pid : children) waitpid(pid, nullptr, 0);
    children.clear();
  };

  double seconds = 0;
  uint64_t records = 0, full = 0, consumer_payload = 0;
  std::atomic<uint32_t> consumer_stop{0};
  ConsumerStats cstats;

  if (fill_only) {
    // Fill the ring (producers stop at FULL), then time the consumer alone.
    join_producers();
    records = total_records(ctl, o.producers);
    std::thread consumer(consumer_loop, ring, std::ref(consumer_stop), true,
                         consumer_cpu, std::ref(cstats));
    const double t0 = now_s();
    // Every block but the open head block is complete once producers stop.
    const uint64_t target = ring.header()->head.load();
    while (cstats.blocks.load(std::memory_order_relaxed) < target) cpu_relax();
    seconds = now_s() - t0;
    consumer_stop.store(1);
    consumer.join();
    consumer_payload = cstats.bytes.load();
  } else {
    std::thread consumer(consumer_loop, ring, std::ref(consumer_stop),
                         consumer_copies, consumer_cpu, std::ref(cstats));
    while (ctl->ready.load() < o.producers) cpu_relax();
    sleep_s(o.warmup);
    const uint64_t r0 = total_records(ctl, o.producers);
    const uint64_t c0 = cstats.bytes.load();
    const double t0 = now_s();
    sleep_s(o.seconds);
    const uint64_t r1 = total_records(ctl, o.producers);
    const uint64_t c1 = cstats.bytes.load();
    seconds = now_s() - t0;
    ctl->stop.store(1);
    join_producers();
    consumer_stop.store(1);
    consumer.join();
    records = r1 - r0;
    consumer_payload = c1 - c0;
    full = total_full(ctl, o.producers);
  }

  const double payload_bps =
      fill_only ? 0.0 : static_cast<double>(records) * o.record / seconds;
  const double consumer_bps = static_cast<double>(consumer_payload) / seconds;
  std::printf(
      "%s,%s,%u,%u,%u,%llu,%d,%.3f,%llu,%.3f,%.3f,%.3f,%llu,%.3f,%.3f\n",
      o.mode.c_str(), o.launch.c_str(), o.producers, o.record, block_size,
      static_cast<unsigned long long>(ring_bytes), o.nontemporal ? 1 : 0,
      seconds, static_cast<unsigned long long>(records), payload_bps / 1e9,
      2 * payload_bps / 1e9, fill_only ? 0.0 : records / seconds / 1e6,
      static_cast<unsigned long long>(full), consumer_bps / 1e9,
      2 * consumer_bps / 1e9);
  munmap(ctl, ctl_bytes);
  return 0;
}
