#ifndef DFTRACER_SERVER_COMMON_DATASTRUCTURE
#define DFTRACER_SERVER_COMMON_DATASTRUCTURE

#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dftracer {
class LocalBufferManager;
class JsonLines;

struct CpuMetrics {
  unsigned long long user = 0;
  unsigned long long nice = 0;
  unsigned long long system = 0;
  unsigned long long idle = 0;
  unsigned long long iowait = 0;
  unsigned long long irq = 0;
  unsigned long long softirq = 0;
  unsigned long long steal = 0;
  unsigned long long guest = 0;
  unsigned long long guest_nice = 0;
};

struct MemMetrics {
  // Explicitly declare all required metrics as members
  unsigned long long MemAvailable = 0;
  unsigned long long Buffers = 0;
  unsigned long long Cached = 0;
  unsigned long long SwapCached = 0;
  unsigned long long Active = 0;
  unsigned long long Inactive = 0;
  unsigned long long Active_anon = 0;
  unsigned long long Inactive_anon = 0;
  unsigned long long Active_file = 0;
  unsigned long long Inactive_file = 0;
  unsigned long long Unevictable = 0;
  unsigned long long Mlocked = 0;
  unsigned long long SwapTotal = 0;
  unsigned long long SwapFree = 0;
  unsigned long long Dirty = 0;
  unsigned long long Writeback = 0;
  unsigned long long AnonPages = 0;
  unsigned long long Mapped = 0;
  unsigned long long Shmem = 0;
  unsigned long long KReclaimable = 0;
  unsigned long long Slab = 0;
  unsigned long long SReclaimable = 0;
  unsigned long long SUnreclaim = 0;
  unsigned long long KernelStack = 0;
  unsigned long long PageTables = 0;
  unsigned long long NFS_Unstable = 0;
  unsigned long long Bounce = 0;
  unsigned long long WritebackTmp = 0;
  unsigned long long CommitLimit = 0;
  unsigned long long Committed_AS = 0;
  unsigned long long VmallocTotal = 0;
  unsigned long long VmallocUsed = 0;
  unsigned long long VmallocChunk = 0;
  unsigned long long Percpu = 0;
  unsigned long long HardwareCorrupted = 0;
  unsigned long long AnonHugePages = 0;
  unsigned long long ShmemHugePages = 0;
  unsigned long long ShmemPmdMapped = 0;
  unsigned long long FileHugePages = 0;
  unsigned long long FilePmdMapped = 0;
  unsigned long long HugePages_Total = 0;
  unsigned long long HugePages_Free = 0;
  unsigned long long HugePages_Rsvd = 0;
  unsigned long long HugePages_Surp = 0;
  unsigned long long Hugepagesize = 0;
  unsigned long long Hugetlb = 0;
  unsigned long long DirectMap4k = 0;
  unsigned long long DirectMap2M = 0;
  unsigned long long DirectMap1G = 0;
};

struct IOMetrics {
  unsigned long long read_count = 0;      // Number of reads
  unsigned long long read_bytes = 0;      // Bytes read
  unsigned long long read_time_ms = 0;    // Time spent reading (ms)
  unsigned long long write_count = 0;     // Number of writes
  unsigned long long write_bytes = 0;     // Bytes written
  unsigned long long write_time_ms = 0;   // Time spent writing (ms)
  unsigned long long io_in_progress = 0;  // I/O operations in progress
};

struct NetworkMetrics {
  unsigned long long rx_bytes = 0;    // Bytes received
  unsigned long long rx_packets = 0;  // Packets received
  unsigned long long rx_errors = 0;   // Receive errors
  unsigned long long rx_dropped = 0;  // Packets dropped on RX
  unsigned long long tx_bytes = 0;    // Bytes transmitted
  unsigned long long tx_packets = 0;  // Packets transmitted
  unsigned long long tx_errors = 0;   // Transmit errors
  unsigned long long tx_dropped = 0;  // Packets dropped on TX
  unsigned long long collisions = 0;  // Collision count
};

/*
 * Producer/consumer mode: the service's drain of the shared-memory ring
 * (service/pipeline).
 */

/** What one drain thread has drained. Updated by that thread only. */
struct alignas(DFT_SHM_CACHE_LINE) DrainStats {
  uint64_t blocks = 0;
  uint64_t bytes = 0;
  uint64_t records[SHM_KIND_GOODBYE + 1] = {};
  uint64_t malformed = 0;
  uint64_t taken = 0;  // blocks taken over from their producers
  uint64_t uncommitted =
      0;                    // reservations their dead producers never committed
  uint64_t chunks = 0;      // chunks written to the node file
  uint64_t json_bytes = 0;  // before compression
  uint64_t file_bytes = 0;  // as written
  uint64_t cpu_ns = 0;      // thread CPU time, sampled when the thread exits

  void add(const DrainStats& other) {
    blocks += other.blocks;
    bytes += other.bytes;
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i)
      records[i] += other.records[i];
    malformed += other.malformed;
    taken += other.taken;
    uncommitted += other.uncommitted;
    chunks += other.chunks;
    json_bytes += other.json_bytes;
    file_bytes += other.file_bytes;
    cpu_ns += other.cpu_ns;
  }
};

/** One drain thread's work so far, read by the controller while it runs. */
struct alignas(DFT_SHM_CACHE_LINE) DrainLoad {
  std::atomic<uint64_t> blocks{0};
  std::atomic<uint64_t> busy_ns{0};
};

/** How the drain thread count moved, kept by the controller. */
struct DrainScaling {
  uint64_t ticks = 0;
  uint64_t ups = 0;
  uint64_t downs = 0;
  uint32_t peak = 0;
  double thread_ns = 0;  // integral of active threads over time
  uint64_t span_ns = 0;  // time covered by thread_ns
};

/** One controller sample for ConsumerScaler. */
struct ScalerSample {
  uint64_t now_ns;   // monotonic time of the sample
  uint64_t head;     // ring head: blocks ever claimed by producers
  uint64_t ready;    // complete blocks not yet claimed by a drain thread
  uint64_t drained;  // blocks ever drained, all threads
  uint64_t busy_ns;  // time ever spent draining them, all threads
};

/** How RingSink drains the ring and where the trace goes. */
struct DrainOptions {
  uint32_t min_threads = DFT_SHM_DEFAULT_CONSUMER_MIN_THREADS;
  uint32_t max_threads = DFT_SHM_DEFAULT_CONSUMER_THREADS;
  uint32_t tick_ms = DFT_SHM_DEFAULT_CONSUMER_TICK_MS;
  // A producer's unpublished block is taken over, and a drain thread's chunk
  // written, once this old.
  uint64_t flush_ns = DFT_SHM_DEFAULT_FLUSH_MS * 1000000ULL;
  // The node's trace file. Empty: decode and count only (benchmarks).
  std::string node_file;
  std::string hostname_hash;
  size_t chunk_size = DFT_SHM_DEFAULT_CHUNK_SIZE;
  int compression_level = DFT_SHM_DEFAULT_COMPRESSION_LEVEL;
  // Compressed bytes after which the node trace rolls over (0: never).
  uint64_t rollover_bytes = DFT_SHM_DEFAULT_ROLLOVER_MB << 20;
  // The service's own spill file, appended at stop with its clients'.
  std::string own_spill_file;
};

/** A drain thread's reusable decode state. */
struct DrainScratch {
  ipc::DecodedEvent event;
  ipc::DecodedMetadata metadata;
  ipc::DecodedRecord record;
  ipc::DecodedHello hello;
  // NUL-terminated copies of the METADATA and RECORD strings the serializer
  // takes as char*. DATA and COUNTER records need none (log_packed_event).
  std::string name, value, record_name;
};

/**
 * One producer thread of a client process. Its LocalBufferManager holds that
 * thread's aggregation state; aggregation keys carry the thread id, so lanes
 * aggregate exactly as the process would have. A drain thread holds `mtx`
 * while it replays one of the thread's blocks.
 */
struct ClientLane {
  std::mutex mtx;
  std::shared_ptr<LocalBufferManager> buffer;
  std::shared_ptr<JsonLines> serializer;  // the one inside `buffer`
  uint64_t synced = 0;  // ClientProcess::version last applied to `buffer`
};

/** One client process. Fields below `mtx` are guarded by it. */
struct ClientProcess {
  uint32_t pid = 0;
  std::atomic<uint64_t> last_seen_ns{0};
  std::mutex mtx;
  std::string app_name;
  int rank = -1;
  uint64_t version = 0;  // bumped by SET_APP_NAME and SET_RANK
  bool said_goodbye = false;
  int goodbye_index = 0;
  uint64_t goodbye_ns = 0;
  std::unordered_map<uint32_t, std::shared_ptr<ClientLane>> lanes;
};
}  // namespace dftracer
#endif  // DFTRACER_SERVER_COMMON_DATASTRUCTURE