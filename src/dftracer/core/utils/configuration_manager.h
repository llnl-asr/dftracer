//
// Created by haridev on 10/27/23.
//

#ifndef DFTRACER_CONFIGURATION_MANAGER_H
#define DFTRACER_CONFIGURATION_MANAGER_H
#include <cpp-logger/logger.h>
#include <dftracer/core/common/enumeration.h>

#include <string>
#include <vector>

// Kept out of the public enumeration.h so installed headers do not
// depend on cpp-logger, which is a private dependency.
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

namespace dftracer {
class Metadata;
struct ExecInfo {
  std::string name;
  std::string cmd;
};
class ConfigurationManager {
 private:
  ExecInfo exec_info_;
  bool exec_info_ready_ = false;
  void derive_configurations();
  std::string aggregation_file;

 public:
  bool enable;
  ProfileInitType init_type;
  std::string log_file;
  std::string data_dirs;
  // Internal: always on. DFTRACER_INC_METADATA and features.metadata are
  // ignored (with a warning when they ask to turn it off).
  bool metadata;
  bool core_affinity;
  int gotcha_priority;
  cpplogger::LoggerType logger_level;
  TimeMetricType time_metric;
  bool io;
  bool posix;
  bool stdio;
  bool trace_all_files;
  // Size of the traced-descriptor table, from the open-file soft limit at
  // startup. A descriptor above it is untraced.
  size_t max_fd;
  bool tids;
  bool bind_signals;
  bool throw_error;
  size_t write_buffer_size;
  size_t trace_interval_ms;
  bool trace_interval_explicit;
  size_t libuv_thread_count;
  bool papi_tracing;
  bool papi_multiplex;
  size_t papi_sample_interval_ms;
  std::vector<std::string> papi_events;
  // Node-level power sampling through variorum, in the service. On by default
  // wherever the build found variorum, like the other service collectors: it
  // costs one register read per tick and disables itself on a machine whose
  // power domains variorum cannot reach.
  bool variorum_power;
  bool aggregation_enable;
  AggregationType aggregation_type;
  // Producer/consumer mode: events are encoded and pushed into the shared
  // ring of the node's dftracer_service instead of being written by this
  // process (docs/design_pc_service.rst).
  bool pc_mode;
  // Name of the ring segment; both the service and traced processes read it.
  std::string service_shm;
  // Ring geometry, used by the service when it creates the segment.
  uint32_t pc_block_size;
  uint32_t pc_blocks;
  // How long a producer waits on a full ring before dropping an event.
  uint64_t pc_full_wait_us;
  // Service threads draining the ring: at most pc_consumer_threads, at least
  // pc_consumer_min_threads, resized every pc_consumer_tick_ms.
  uint32_t pc_consumer_threads;
  uint32_t pc_consumer_min_threads;
  uint32_t pc_consumer_tick_ms;
  // Age at which the service takes over a producer's unpublished block and
  // writes out a partial chunk; gzip level of the node trace.
  uint32_t pc_flush_ms;
  int pc_compression_level;
  // Compressed MiB after which the node trace rolls over to a new file
  // (fractions allowed; 0: never).
  double pc_rollover_mb;
  std::vector<std::string> aggregation_inclusion_rules;
  std::vector<std::string> aggregation_exclusion_rules;
  ConfigurationManager();
  void finalize() {}

  // Caller-supplied values replace the configured ones. A null or empty
  // argument keeps the configured value. The path loses a .pfw, .pfw.gz or
  // other extension, so the suffix and extension are added in make_log_file.
  void set_log_file(const char* path);
  // "all" selects tracing of all files. Any other value replaces data_dirs.
  void set_data_dirs(const char* dirs);
  // Falls back to the defaults for a log file or data dirs that are still
  // empty.
  void resolve_defaults();
  // Program name and command line of this process, read from /proc once and
  // cached. Call it after POSIXBypass is initialized, never from a constructor.
  const ExecInfo& exec_info();
  // Parses the NUL separated bytes of a /proc/<pid>/cmdline file. The name is
  // the first argument that is not python, env, a multiprocessing helper or
  // an option.
  static ExecInfo parse_cmdline(const char* data, ssize_t size);
  // <log_file>-<hash>-<suffix>.pfw[.gz]
  std::string make_log_file(const char* hash, const std::string& suffix) const;

  // Writes the effective configuration (and compile-time layer availability)
  // into `meta` as a single batch, so callers fold it into one trace event
  // instead of emitting one metadata event per setting. `bind` and
  // `log_file` are resolved at runtime by DFTracerCore (not stored on
  // ConfigurationManager itself: `bind` depends on how initialize_main vs
  // initialize_no_bind was called, and `log_file` is the final path after
  // the hostname/exec hash and extension are appended to the configured
  // prefix), so the caller supplies them.
  void populate_metadata(Metadata* meta, bool bind,
                         const std::string& log_file) const;
};
}  // namespace dftracer
#endif  // DFTRACER_CONFIGURATION_MANAGER_H
