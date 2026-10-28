//
// Created by haridev on 10/5/23.
//

#ifndef DFTRACER_CONSTANTS_H
#define DFTRACER_CONSTANTS_H

#include <stddef.h>
#include <stdint.h>

#define DFTRACER_CONFIGURATION "DFTRACER_CONFIGURATION"
#define DFTRACER_ENABLE "DFTRACER_ENABLE"
#define DFTRACER_GOTCHA_PRIORITY "DFTRACER_GOTCHA_PRIORITY"
#define DFTRACER_LOG_LEVEL "DFTRACER_LOG_LEVEL"
#define DFTRACER_TIME_METRIC "DFTRACER_TIME_METRIC"
#define DFTRACER_LOG_FILE "DFTRACER_LOG_FILE"
#define DFTRACER_INIT "DFTRACER_INIT"
#define DFTRACER_INIT_COUNT "DFTRACER_INIT_COUNT"
#define DFTRACER_DATA_DIR "DFTRACER_DATA_DIR"
#define DFTRACER_DATA_DIR_DELIMITER ':'
#define DFTRACER_SET_CORE_AFFINITY "DFTRACER_SET_CORE_AFFINITY"
#define DFTRACER_ERROR "DFTRACER_ERROR"
#define DFTRACER_INC_METADATA "DFTRACER_INC_METADATA"
#define DFTRACER_DISABLE_POSIX "DFTRACER_DISABLE_POSIX"
#define DFTRACER_DISABLE_STDIO "DFTRACER_DISABLE_STDIO"
#define DFTRACER_DISABLE_IO "DFTRACER_DISABLE_IO"
#define DFTRACER_TRACE_COMPRESSION "DFTRACER_TRACE_COMPRESSION"
#define DFTRACER_DISABLE_TIDS "DFTRACER_DISABLE_TIDS"
#define DFTRACER_ENABLE_AGGREGATION "DFTRACER_ENABLE_AGGREGATION"
#define DFTRACER_AGGREGATION_TYPE "DFTRACER_AGGREGATION_TYPE"
#define DFTRACER_AGGREGATION_FILE "DFTRACER_AGGREGATION_FILE"
#define DFTRACER_BIND_SIGNALS "DFTRACER_BIND_SIGNALS"
#define DFTRACER_WRITE_BUFFER_SIZE "DFTRACER_WRITE_BUFFER_SIZE"
#define DFTRACER_TRACE_INTERVAL_MS "DFTRACER_TRACE_INTERVAL_MS"
#define DFTRACER_LIBUV_THREADS "DFTRACER_LIBUV_THREADS"
#define DFTRACER_ENABLE_PAPI_TRACING "DFTRACER_ENABLE_PAPI_TRACING"
#define DFTRACER_PAPI_EVENTS "DFTRACER_PAPI_EVENTS"
#define DFTRACER_PAPI_MULTIPLEX "DFTRACER_PAPI_MULTIPLEX"
#define DFTRACER_PAPI_SAMPLE_INTERVAL_MS "DFTRACER_PAPI_SAMPLE_INTERVAL_MS"
#define DFTRACER_DISABLE_VARIORUM_POWER "DFTRACER_DISABLE_VARIORUM_POWER"
#define DFTRACER_ENABLE_PC_MODE "DFTRACER_ENABLE_PC_MODE"
#define DFTRACER_SERVICE_SHM "DFTRACER_SERVICE_SHM"
#define DFTRACER_PC_BLOCK_SIZE "DFTRACER_PC_BLOCK_SIZE"
#define DFTRACER_PC_BLOCKS "DFTRACER_PC_BLOCKS"
#define DFTRACER_PC_FULL_WAIT_US "DFTRACER_PC_FULL_WAIT_US"
#define DFTRACER_PC_CONSUMER_THREADS "DFTRACER_PC_CONSUMER_THREADS"
#define DFTRACER_PC_CONSUMER_MIN_THREADS "DFTRACER_PC_CONSUMER_MIN_THREADS"
#define DFTRACER_PC_CONSUMER_TICK_MS "DFTRACER_PC_CONSUMER_TICK_MS"
#define DFTRACER_PC_FLUSH_MS "DFTRACER_PC_FLUSH_MS"
#define DFTRACER_PC_COMPRESSION_LEVEL "DFTRACER_PC_COMPRESSION_LEVEL"
#define DFTRACER_PC_ROLLOVER_MB "DFTRACER_PC_ROLLOVER_MB"
#define DFTRACER_ALL_FILES "all"
#define DFTRACER_DEFAULT_LOG_FILE "./trace"
#define DFTRACER_DEFAULT_AGGREGATION_RULE "dur < 100"
#define DFTRACER_AGGREGATOR_COUNT "dft_cnt"

static const int EVENT_TYPE_SIZE = 128;
static const unsigned int DFT_PATH_MAX = 1024 * 4;
static const char SEPARATOR = ';';
static const int HASH_OUTPUT = 16;
#define NO_HASH_DEFAULT 0
// Size of the traced-descriptor table when no limit is known, and its cap. The
// cap is 32 MB of address space for 8-byte hashes.
static const size_t DFT_DEFAULT_MAX_FD = 1024;
static const size_t DFT_MAX_TRACKED_FD = 4194304;

// Shared-memory event ring (core/ipc/shm_layout.h).
static const uint64_t DFT_SHM_SEGMENT_MAGIC =
    0x314d485352544644ULL;  // "DFTRSHM1"
// Bump on any change to the segment layout, the record framing, or the
// meaning of a ShmSegmentState / ShmRecordState / ShmRecordKind value.
static const uint32_t DFT_SHM_ABI_VERSION = 5;
static const size_t DFT_SHM_CACHE_LINE = 64;
// Records start on, and are sized in multiples of, this many bytes.
static const uint32_t DFT_SHM_RECORD_ALIGN = 16;
// The data area starts on a page boundary.
static const size_t DFT_SHM_DATA_ALIGN = 4096;
// BlockControl::committed is epoch << 32 | block_size once the block is
// published; a drain thread claims it by setting this bit.
static const uint64_t DFT_SHM_BLOCK_CLAIMED = 1ULL << 31;
// Record length is a 24-bit field of the header word.
static const uint32_t DFT_SHM_MAX_RECORD_LENGTH =
    (1u << 24) - DFT_SHM_RECORD_ALIGN;
// The default ring geometry is a build option: DFTRACER_PC_RING_BLOCKS and
// DFTRACER_PC_RING_BLOCK_SIZE in dftracer_config.hpp (CMake).
// The smallest ring the service shrinks to when /dev/shm is short.
static const uint32_t DFT_SHM_MIN_BLOCKS = 16;
static const uint32_t DFT_SHM_MIN_BLOCK_SIZE = 64u << 10;
// How long a producer waits on a full ring before it spills the event to its
// own file. 0: it does not wait -- a full ring means the service is behind,
// and waiting would only hold the application to the service's pace.
static const uint64_t DFT_SHM_DEFAULT_FULL_WAIT_US = 0;
// After a thread found the ring full, it spills straight away (without
// encoding for the ring) for this long, then tries the ring again.
static const uint64_t DFT_SHM_SPILL_RETRY_NS = 1000000ULL;
// How long finalize waits for a thread that is inside a log call to leave
// it; past that, its block is left for the service to take over.
static const uint64_t DFT_SHM_FINALIZE_WAIT_NS = 100000000ULL;
// How long a producer waits for room for a control record (hello, goodbye,
// app name, rank): there is no spill file for those, and they are rare.
static const uint64_t DFT_SHM_CONTROL_WAIT_NS = 5000000000ULL;
// How often a producer checks that the service still drains the ring (the
// segment is accepting and the service process alive). Once it is not, the
// producer's events go to its spill file instead of a ring nobody drains.
static const uint64_t DFT_SHM_LIVENESS_CHECK_NS = 100000000ULL;
// Service threads draining the ring; each takes whole blocks. The service
// runs between MIN and THREADS of them, sized each tick to the arrival rate
// plus the backlog it must work off within CATCHUP; it sheds one thread after
// DOWN_TICKS ticks in a row that wanted fewer.
static const uint32_t DFT_SHM_DEFAULT_CONSUMER_THREADS = 16;
static const uint32_t DFT_SHM_DEFAULT_CONSUMER_MIN_THREADS = 1;
static const uint32_t DFT_SHM_DEFAULT_CONSUMER_TICK_MS = 10;
static const uint64_t DFT_SHM_CONSUMER_CATCHUP_NS = 100000000ULL;
static const uint32_t DFT_SHM_CONSUMER_DOWN_TICKS = 10;
// The service takes over a producer's block held unpublished this long, and
// writes out a drain thread's partial chunk once it is this old.
static const uint32_t DFT_SHM_DEFAULT_FLUSH_MS = 1000;
// JSON a drain thread collects before compressing it into one gzip member.
static const size_t DFT_SHM_DEFAULT_CHUNK_SIZE = 4u << 20;
// gzip level (libdeflate) for those members: the service compresses the whole
// node's trace, so speed over ratio.
static const int DFT_SHM_DEFAULT_COMPRESSION_LEVEL = 1;
// The node trace rolls over to <log_file>_<host>-<n>.pfw.gz once this much
// compressed data is in it (0: never), so a reader on a parallel filesystem
// can split a node's trace across files as well as across members.
static const uint64_t DFT_SHM_DEFAULT_ROLLOVER_MB = 1024;

#endif  // DFTRACER_CONSTANTS_H
