//
// Created by haridev on 3/28/23.
//

#ifndef DFTRACER_ENUMERATION_H
#define DFTRACER_ENUMERATION_H
#include <cstdint>
#include <string>

enum WriterType : uint8_t { CHROME = 0 };
enum ProfilerStage : uint8_t {
  PROFILER_INIT = 0,
  PROFILER_FINI = 1,
  PROFILER_OTHER = 2
};
enum ProfileType : uint8_t {
  PROFILER_PRELOAD = 0,
  PROFILER_PY_APP = 1,
  PROFILER_CPP_APP = 2,
  PROFILER_C_APP = 3,
  PROFILER_ANY = 4
};
enum ProfileInitType : uint8_t {
  PROFILER_INIT_NONE = 0,
  PROFILER_INIT_LD_PRELOAD = 1,
  PROFILER_INIT_FUNCTION = 2
};
enum ValueType : uint8_t { VALUE_TYPE_NUMBER = 0, VALUE_TYPE_STRING = 1 };
enum MetadataType : uint8_t { MT_KEY = 0, MT_VALUE = 1, MT_IGNORE = 2 };
enum AggregationType : uint8_t {
  AGGREGATION_TYPE_FULL = 0,
  AGGREGATION_TYPE_SELECTIVE = 1
};
enum TimeMetricType : uint8_t {
  TIME_METRIC_US = 0,
  TIME_METRIC_NS = 1,
  TIME_METRIC_MS = 2,
  TIME_METRIC_SEC = 3
};
enum class RuleOp { AND, OR, NOT, EQ, NEQ, GT, LT, GTE, LTE, IN, LIKE };

// Instrumentation layer that produced an event, written as the integer "type"
// column. "cat" stays free-form and holds the sub-category within the layer
// (e.g. TRACE_TYPE_LIBC_IO with cat "POSIX" or "STDIO").
// On-disk format: append-only, never renumber or reuse a value.
enum TraceEventType : uint8_t {
  TRACE_TYPE_UNKNOWN = 0,
  TRACE_TYPE_DFTRACER = 1,
  TRACE_TYPE_C_APP = 2,
  TRACE_TYPE_LIBC_IO = 3,
  TRACE_TYPE_HIP = 4,
  TRACE_TYPE_HDF5 = 5,
  TRACE_TYPE_PYTHON = 6,
  TRACE_TYPE_PSUTIL = 7,
  TRACE_TYPE_FINSTRUMENT = 8,
  TRACE_TYPE_CPP_APP = 9,
  TRACE_TYPE_MPI = 10,
  TRACE_TYPE_PAPI = 11,
  TRACE_TYPE_OMNISTAT = 12,
  TRACE_TYPE_VARIORUM = 13,
  TRACE_TYPE_CUDA = 14,
  // Append new types above. Sentinel only, never serialized.
  TRACE_TYPE_MAX
};

inline const char* to_string(const TraceEventType& type) {
  switch (type) {
    case TraceEventType::TRACE_TYPE_DFTRACER:
      return "DFTRACER";
    case TraceEventType::TRACE_TYPE_C_APP:
      return "C_APP";
    case TraceEventType::TRACE_TYPE_LIBC_IO:
      return "LIBC_IO";
    case TraceEventType::TRACE_TYPE_HIP:
      return "HIP";
    case TraceEventType::TRACE_TYPE_HDF5:
      return "HDF5";
    case TraceEventType::TRACE_TYPE_PYTHON:
      return "PYTHON";
    case TraceEventType::TRACE_TYPE_PSUTIL:
      return "PSUTIL";
    case TraceEventType::TRACE_TYPE_FINSTRUMENT:
      return "FINSTRUMENT";
    case TraceEventType::TRACE_TYPE_CPP_APP:
      return "CPP_APP";
    case TraceEventType::TRACE_TYPE_MPI:
      return "MPI";
    case TraceEventType::TRACE_TYPE_PAPI:
      return "PAPI";
    case TraceEventType::TRACE_TYPE_OMNISTAT:
      return "OMNISTAT";
    case TraceEventType::TRACE_TYPE_VARIORUM:
      return "VARIORUM";
    case TraceEventType::TRACE_TYPE_CUDA:
      return "CUDA";
    default:
      return "UNKNOWN";
  }
}

inline void convert(const int& s, TraceEventType& type) {
  if (s >= 0 && s < static_cast<int>(TraceEventType::TRACE_TYPE_MAX)) {
    type = static_cast<TraceEventType>(s);
  } else {
    type = TraceEventType::TRACE_TYPE_UNKNOWN;
  }
}

// Kind of record, written as the integer "ph" column. Replaces the single
// letters DFTracer used to borrow from the Chrome tracing format; the letter
// each value supersedes is noted below. AGGREGATED is new: aggregated records
// used to be written as counters and could not be told apart from them.
// On-disk format: append-only, never renumber or reuse a value.
enum TracePhaseType : uint8_t {
  TRACE_PHASE_UNKNOWN = 0,
  TRACE_PHASE_COMPLETE = 1,  // "X" -- individual events
  // "C" -- time series counters. The producer is in the "type" column, e.g.
  // TRACE_TYPE_PSUTIL for node telemetry or TRACE_TYPE_PAPI for hardware
  // counters, so the phase only records the shape: a reading at a timestamp,
  // with no duration.
  TRACE_PHASE_COUNTER = 2,
  TRACE_PHASE_AGGREGATED = 3,  // "A" -- aggregated events
  TRACE_PHASE_METADATA = 4,    // "M" -- metadata records
  // Append new phases above. Sentinel only, never serialized.
  TRACE_PHASE_MAX
};

inline const char* to_string(const TracePhaseType& phase) {
  switch (phase) {
    case TracePhaseType::TRACE_PHASE_COMPLETE:
      return "COMPLETE";
    case TracePhaseType::TRACE_PHASE_COUNTER:
      return "COUNTER";
    case TracePhaseType::TRACE_PHASE_AGGREGATED:
      return "AGGREGATED";
    case TracePhaseType::TRACE_PHASE_METADATA:
      return "METADATA";
    default:
      return "UNKNOWN";
  }
}

inline void convert(const int& s, TracePhaseType& phase) {
  if (s >= 0 && s < static_cast<int>(TracePhaseType::TRACE_PHASE_MAX)) {
    phase = static_cast<TracePhaseType>(s);
  } else {
    phase = TracePhaseType::TRACE_PHASE_UNKNOWN;
  }
}

inline MetadataType convert(const int& s) {
  if (s == 0) {
    return MetadataType::MT_KEY;
  } else if (s == 1) {
    return MetadataType::MT_VALUE;
  } else if (s == 2) {
    return MetadataType::MT_IGNORE;
  } else {
    return MetadataType::MT_KEY;
  }
}

inline void convert(const int& s, MetadataType& type) {
  if (s == 0) {
    type = MetadataType::MT_KEY;
  } else if (s == 1) {
    type = MetadataType::MT_VALUE;
  } else if (s == 2) {
    type = MetadataType::MT_IGNORE;
  } else {
    type = MetadataType::MT_KEY;
  }
}

inline void convert(const std::string& s, ProfileInitType& type) {
  if (s == "PRELOAD") {
    type = ProfileInitType::PROFILER_INIT_LD_PRELOAD;
  } else if (s == "FUNCTION") {
    type = ProfileInitType::PROFILER_INIT_FUNCTION;
  } else {
    type = ProfileInitType::PROFILER_INIT_NONE;
  }
}
inline void convert(const std::string& s, AggregationType& type) {
  if (s == "FULL") {
    type = AggregationType::AGGREGATION_TYPE_FULL;
  } else if (s == "SELECTIVE") {
    type = AggregationType::AGGREGATION_TYPE_SELECTIVE;
  } else {
    type = AggregationType::AGGREGATION_TYPE_FULL;
  }
}
inline std::string to_string(const AggregationType& type) {
  switch (type) {
    case AggregationType::AGGREGATION_TYPE_FULL:
      return "FULL";
    case AggregationType::AGGREGATION_TYPE_SELECTIVE:
      return "SELECTIVE";
    default:
      return "FULL";
  }
}
inline void convert(const std::string& s, TimeMetricType& type) {
  if (s == "NS") {
    type = TimeMetricType::TIME_METRIC_NS;
  } else if (s == "MS") {
    type = TimeMetricType::TIME_METRIC_MS;
  } else if (s == "SEC") {
    type = TimeMetricType::TIME_METRIC_SEC;
  } else if (s == "US") {
    type = TimeMetricType::TIME_METRIC_US;
  } else {
    type = TimeMetricType::TIME_METRIC_US;
  }
}
inline std::string to_string(const TimeMetricType& type) {
  switch (type) {
    case TimeMetricType::TIME_METRIC_NS:
      return "NS";
    case TimeMetricType::TIME_METRIC_MS:
      return "MS";
    case TimeMetricType::TIME_METRIC_SEC:
      return "SEC";
    case TimeMetricType::TIME_METRIC_US:
      return "US";
    default:
      return "US";
  }
}
// Number of TIME_METRIC units in one second. Used by any consumer that needs
// to convert a wall-clock quantity (e.g. a millisecond interval) into the
// same unit that DFTLogger::get_time() is currently returning.
inline double time_metric_units_per_second(const TimeMetricType& type) {
  switch (type) {
    case TimeMetricType::TIME_METRIC_NS:
      return 1e9;
    case TimeMetricType::TIME_METRIC_MS:
      return 1e3;
    case TimeMetricType::TIME_METRIC_SEC:
      return 1.0;
    case TimeMetricType::TIME_METRIC_US:
    default:
      return 1e6;
  }
}

#define METADATA_NAME_PROCESS "PR"
#define METADATA_NAME_PROCESS_NAME "process_name"
#define METADATA_NAME_THREAD_NAME "thread_name"
#define METADATA_NAME_FILE_HASH "FH"
#define METADATA_NAME_HOSTNAME_HASH "HH"
#define METADATA_NAME_STRING_HASH "SH"
// Entities (see dftracer/core/common/entity.h):
//   EH  args: {"id":"<16-hex>","type":"<type>","store":<int>,"uri":"<uri>"}
//   ET  args: {"type":"<type>","role":<int>,"description":"<text>"}
//   ER  args: {"relation":<int>,"subject":"<16-hex>","object":"<16-hex>"}
#define METADATA_NAME_ENTITY_HASH "EH"
#define METADATA_NAME_ENTITY_TYPE "ET"
#define METADATA_NAME_ENTITY_RELATION "ER"
#define CUSTOM_METADATA "CM"
// Shared-memory event ring between traced processes and dftracer_service
// (core/ipc/shm_layout.h). These values live in the shared segment, so they
// are ABI: append-only, never renumber or reuse a value, and bump
// DFT_SHM_ABI_VERSION when the meaning of one changes.
enum ShmSegmentState : uint32_t {
  SHM_SEGMENT_INITIALIZING = 0,
  SHM_SEGMENT_ACCEPTING = 1,
  SHM_SEGMENT_DRAINING = 2,
  SHM_SEGMENT_CLOSED = 3
};
// Low byte of a record header word. The values are deliberately not 0/1/2: the
// consumer only ever inspects header words at record boundaries of the current
// lap, but a boundary whose header has not been written yet still holds bytes
// from an earlier lap -- or, in the first lap, zeros. Matching such leftovers
// requires the exact epoch of the current lap (never 0, see RingView::epoch)
// *and* one of these byte values *and* a plausible length, so a stale word is
// never mistaken for a record.
enum ShmRecordState : uint8_t {
  SHM_RECORD_RESERVED = 0x5E,
  SHM_RECORD_COMMITTED = 0xC5,
  SHM_RECORD_PAD = 0xA7
};
enum ShmRecordKind : uint16_t {
  SHM_KIND_PAD = 0,
  SHM_KIND_HELLO = 1,
  SHM_KIND_SET_APP_NAME = 2,
  SHM_KIND_SET_RANK = 3,
  SHM_KIND_DATA = 4,
  SHM_KIND_COUNTER = 5,
  SHM_KIND_METADATA = 6,
  SHM_KIND_RECORD = 7,
  SHM_KIND_GOODBYE = 8,
  // Opaque payload, used by tests and benchmarks of the ring itself.
  SHM_KIND_RAW = 0x7fff
};
// Type tag of one metadata value in an encoded event (core/ipc/event_codec.h).
// Part of the ring ABI: append only, never renumber.
enum ShmValueTag : uint8_t {
  SHM_VALUE_INT = 1,      // int64_t
  SHM_VALUE_UINT = 2,     // uint64_t
  SHM_VALUE_DOUBLE = 3,   // IEEE-754 double
  SHM_VALUE_STRING = 4,   // uint32_t length + bytes
  SHM_VALUE_RAW_JSON = 5  // uint32_t length + bytes, embedded unquoted
};
// Who may write a claimed block (BlockControl::owner, low byte; the epoch of
// the lap is above it). The producer holds a block IDLE between records and
// WRITING while a record is reserved but not committed. The service may take a
// block over (TAKEN) only from IDLE -- or from WRITING once the producer is
// dead -- so it never reads a record that is still being written. A producer
// that finds its block TAKEN drops it and claims another.
enum ShmBlockOwner : uint8_t {
  SHM_OWNER_NONE = 0,
  SHM_OWNER_IDLE = 1,
  SHM_OWNER_WRITING = 2,
  SHM_OWNER_PUBLISHED = 3,
  SHM_OWNER_TAKEN = 4
};
// EventHead::flags: how the producing process serializes its events, which
// the service then does on its behalf.
enum ShmEventFlags : uint8_t {
  SHM_EVENT_INC_METADATA = 1,  // DFTRACER_INC_METADATA: keep optional args
  // The event was logged with a Metadata, possibly empty; without one its
  // args carry no hhash either.
  SHM_EVENT_HAS_METADATA = 2,
  // The event's type is not aggregatable (dftracer::event_type_aggregatable):
  // it is always traced in full.
  SHM_EVENT_NO_AGGREGATE = 4
};
// Result of reserving room for a record in the ring (RingProducer).
enum class ShmReserveResult {
  OK = 0,
  FULL = 1,      // no free block could be claimed in time
  TOO_LARGE = 2  // the record can never fit in one block
};
// Who may touch a producer thread's slot (ServiceBufferManager): its thread
// while logging (BUSY), a flush timer while it is not (IDLE), nobody once the
// thread has exited or the slot was inherited across fork() (DEAD).
enum ShmSlotState : uint32_t {
  SHM_SLOT_IDLE = 0,
  SHM_SLOT_BUSY = 1,
  SHM_SLOT_DEAD = 2
};

#endif  // DFTRACER_ENUMERATION_H
