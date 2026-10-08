//
// Created by haridev on 10/27/23.
//

#ifndef DFTRACER_CONFIGURATION_MANAGER_H
#define DFTRACER_CONFIGURATION_MANAGER_H
#include <cpp-logger/logger.h>
#include <dftracer/core/common/enumeration.h>

#include <string>
#include <vector>
namespace dftracer {
class Metadata;
class ConfigurationManager {
 private:
  void derive_configurations();
  std::string aggregation_file;

 public:
  bool enable;
  ProfileInitType init_type;
  std::string log_file;
  std::string data_dirs;
  bool metadata;
  bool core_affinity;
  int gotcha_priority;
  cpplogger::LoggerType logger_level;
  TimeMetricType time_metric;
  bool io;
  bool posix;
  bool stdio;
  bool compression;
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
  std::vector<std::string> aggregation_inclusion_rules;
  std::vector<std::string> aggregation_exclusion_rules;
  ConfigurationManager();
  void finalize() {}

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
