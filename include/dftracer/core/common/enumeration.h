//
// Created by haridev on 3/28/23.
//

#ifndef DFTRACER_ENUMERATION_H
#define DFTRACER_ENUMERATION_H
#include <cpp-logger/logger.h>
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
inline void convert(const std::string& s, cpplogger::LoggerType& type) {
  if (s == "DEBUG") {
    type = cpplogger::LoggerType::CPP_LOGGER_DEBUG;
  } else if (s == "INFO") {
    type = cpplogger::LoggerType::CPP_LOGGER_INFO;
  } else if (s == "WARN") {
    type = cpplogger::LoggerType::CPP_LOGGER_WARN;
  } else {
    type = cpplogger::LoggerType::CPP_LOGGER_ERROR;
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
#define CUSTOM_METADATA "CM"
#endif  // DFTRACER_ENUMERATION_H
