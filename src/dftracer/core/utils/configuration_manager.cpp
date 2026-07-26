//
// Created by haridev on 10/27/23.
//

#include "configuration_manager.h"

#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/datastructure.h>
#include <yaml-cpp/yaml.h>

#include <dftracer/core/dftracer_config.hpp>
#include <filesystem>
#include <sstream>

#include "utils.h"

#define DFT_YAML_ENABLE "enable"
// TRACER
#define DFT_YAML_TRACER "tracer"
#define DFT_YAML_PROFILER "profiler"
#define DFT_YAML_TRACER_INIT "init"
#define DFT_YAML_TRACER_LOG_FILE "log_file"
#define DFT_YAML_TRACER_DATA_DIRS "data_dirs"
#define DFT_YAML_TRACER_LOG_LEVEL "log_level"
#define DFT_YAML_TRACER_TIME_METRIC "time_metric"
#define DFT_YAML_TRACER_COMPRESSION "compression"
#define DFT_YAML_TRACER_INTERVAL "interval"
#define DFT_YAML_TRACER_LIBUV_THREADS "libuv_threads"
// GOTCHA
#define DFT_YAML_GOTCHA "gotcha"
#define DFT_YAML_GOTCHA_PRIORITY "priority"
// Features
#define DFT_YAML_FEATURES "features"
#define DFT_YAML_FEATURES_METADATA "metadata"
#define DFT_YAML_FEATURES_CORE_AFFINITY "core_affinity"
#define DFT_YAML_FEATURES_IO "io"
#define DFT_YAML_FEATURES_IO_ENABLE "enable"
#define DFT_YAML_FEATURES_IO_POSIX "posix"
#define DFT_YAML_FEATURES_IO_STDIO "stdio"
#define DFT_YAML_FEATURES_TID "tid"
#define DFT_YAML_FEATURES_AGGREGATION "aggregation"
#define DFT_YAML_FEATURES_AGGREGATION_ENABLE "enable"
#define DFT_YAML_FEATURES_AGGREGATION_TYPE "type"
#define DFT_YAML_FEATURES_AGGREGATION_FILE "file"
#define DFT_YAML_FEATURES_AGGREGATION_INCLUSION_FILTERS "inclusion"
#define DFT_YAML_FEATURES_AGGREGATION_EXCLUSION_FILTERS "exclusion"

// INTERNAL
#define DFT_YAML_INTERNAL "internal"
#define DFT_YAML_INTERNAL_SIGNALS "bind_signals"
#define DFT_YAML_INTERNAL_THROW_ERROR "throw_error"
#define DFT_YAML_INTERNAL_WRITE_BUFFER_SIZE "write_buffer_size"

template <>
std::shared_ptr<dftracer::ConfigurationManager>
    dftracer::Singleton<dftracer::ConfigurationManager>::instance = nullptr;
template <>
bool dftracer::Singleton<
    dftracer::ConfigurationManager>::stop_creating_instances = false;
dftracer::ConfigurationManager::ConfigurationManager()
    : enable(false),
      init_type(PROFILER_INIT_FUNCTION),
      log_file("./trace"),
      data_dirs("all"),
      metadata(false),
      core_affinity(false),
      gotcha_priority(1),
      logger_level(cpplogger::CPP_LOGGER_ERROR),
      time_metric(TimeMetricType::TIME_METRIC_US),
      io(true),
      posix(true),
      stdio(true),
      compression(true),
      trace_all_files(false),
      tids(true),
      bind_signals(false),
      throw_error(false),
      write_buffer_size(16 * 1024 * 1024),
      trace_interval_ms(1000),
      libuv_thread_count(1),
      aggregation_enable(false),
      aggregation_type(AggregationType::AGGREGATION_TYPE_FULL),
      aggregation_inclusion_rules(),
      aggregation_exclusion_rules() {
  const char* env_conf = getenv(DFTRACER_CONFIGURATION);
  YAML::Node config;
  if (env_conf != nullptr) {
    config = YAML::LoadFile(env_conf);
    if (config[DFT_YAML_TRACER]) {
      if (config[DFT_YAML_TRACER][DFT_YAML_TRACER_LOG_LEVEL]) {
        convert(config[DFT_YAML_TRACER][DFT_YAML_TRACER_LOG_LEVEL]
                    .as<std::string>(),
                this->logger_level);
      }
    }
  }
  const char* env_log_level = getenv(DFTRACER_LOG_LEVEL);
  if (env_log_level != nullptr) {
    convert(env_log_level, this->logger_level);
  }
  DFTRACER_LOGGER_LEVEL(logger_level);
  DFTRACER_LOG_DEBUG("Enabling logging level %d", logger_level);
  if (env_conf != nullptr) {
    this->enable = config[DFT_YAML_ENABLE].as<bool>();
    DFTRACER_LOG_DEBUG("YAML ConfigurationManager.enable %d", this->enable);
    if (config[DFT_YAML_TRACER]) {
      if (config[DFT_YAML_TRACER][DFT_YAML_TRACER_LOG_LEVEL]) {
        convert(config[DFT_YAML_TRACER][DFT_YAML_TRACER_LOG_LEVEL]
                    .as<std::string>(),
                this->logger_level);
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.logger_level %d",
                         this->logger_level);
      if (config[DFT_YAML_TRACER][DFT_YAML_TRACER_TIME_METRIC]) {
        convert(config[DFT_YAML_TRACER][DFT_YAML_TRACER_TIME_METRIC]
                    .as<std::string>(),
                this->time_metric);
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.time_metric %s",
                         to_string(this->time_metric).c_str());
      if (config[DFT_YAML_TRACER][DFT_YAML_TRACER_INIT]) {
        convert(config[DFT_YAML_TRACER][DFT_YAML_TRACER_INIT].as<std::string>(),
                this->init_type);
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.init_type %d",
                         this->init_type);
      if (config[DFT_YAML_TRACER][DFT_YAML_TRACER_LOG_FILE]) {
        this->log_file =
            config[DFT_YAML_TRACER][DFT_YAML_TRACER_LOG_FILE].as<std::string>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.log_file %s",
                         this->log_file.c_str());
      if (config[DFT_YAML_TRACER][DFT_YAML_TRACER_DATA_DIRS]) {
        auto data_dirs_str = config[DFT_YAML_TRACER][DFT_YAML_TRACER_DATA_DIRS]
                                 .as<std::string>();
        if (data_dirs_str == DFTRACER_ALL_FILES) {
          this->trace_all_files = true;
        } else {
          this->data_dirs = data_dirs_str;
        }
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.data_dirs_str %s",
                         this->data_dirs.c_str());
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.trace_all_files %d",
                         this->trace_all_files);

      if (config[DFT_YAML_TRACER][DFT_YAML_TRACER_COMPRESSION]) {
        this->compression =
            config[DFT_YAML_TRACER][DFT_YAML_TRACER_COMPRESSION].as<bool>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.compression %d",
                         this->compression);
      if (config[DFT_YAML_TRACER][DFT_YAML_TRACER_LIBUV_THREADS]) {
        this->libuv_thread_count =
            config[DFT_YAML_TRACER][DFT_YAML_TRACER_LIBUV_THREADS].as<size_t>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.libuv_thread_count %zu",
                         this->libuv_thread_count);
    }
    if (config[DFT_YAML_PROFILER] &&
        config[DFT_YAML_PROFILER][DFT_YAML_TRACER_LIBUV_THREADS]) {
      this->libuv_thread_count =
          config[DFT_YAML_PROFILER][DFT_YAML_TRACER_LIBUV_THREADS].as<size_t>();
      DFTRACER_LOG_DEBUG(
          "YAML ConfigurationManager.libuv_thread_count (profiler) %zu",
          this->libuv_thread_count);
    }
    if (config[DFT_YAML_GOTCHA]) {
      if (config[DFT_YAML_GOTCHA][DFT_YAML_GOTCHA_PRIORITY]) {
        this->gotcha_priority =
            config[DFT_YAML_GOTCHA][DFT_YAML_GOTCHA_PRIORITY].as<int>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.gotcha_priority %d",
                         this->gotcha_priority);
    }
    if (config[DFT_YAML_FEATURES]) {
      if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_METADATA]) {
        this->metadata =
            config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_METADATA].as<bool>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.metadata %d",
                         this->metadata);
      if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_CORE_AFFINITY]) {
        this->core_affinity =
            config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_CORE_AFFINITY]
                .as<bool>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.core_affinity %d",
                         this->core_affinity);
      if (config[DFT_YAML_FEATURES][DFT_YAML_TRACER_INTERVAL]) {
        this->trace_interval_ms =
            config[DFT_YAML_FEATURES][DFT_YAML_TRACER_INTERVAL].as<size_t>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.trace_interval_ms %d",
                         this->trace_interval_ms);
      if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_IO] &&
          config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_IO_ENABLE]) {
        this->io = config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_IO]
                         [DFT_YAML_FEATURES_IO_ENABLE]
                             .as<bool>();
        DFTRACER_LOG_DEBUG("YAML ConfigurationManager.io %d", this->io);
        if (this->io) {
          if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_IO]
                    [DFT_YAML_FEATURES_IO_POSIX]) {
            this->posix = config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_IO]
                                [DFT_YAML_FEATURES_IO_POSIX]
                                    .as<bool>();
          }
          DFTRACER_LOG_DEBUG("YAML ConfigurationManager.posix %d", this->posix);
          if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_IO]
                    [DFT_YAML_FEATURES_IO_STDIO]) {
            this->stdio = config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_IO]
                                [DFT_YAML_FEATURES_IO_STDIO]
                                    .as<bool>();
          }
          DFTRACER_LOG_DEBUG("YAML ConfigurationManager.stdio %d", this->stdio);
        }
      }
      if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_TID]) {
        this->tids =
            config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_TID].as<bool>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.tids %d", this->tids);
      if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_AGGREGATION]) {
        if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_AGGREGATION]
                  [DFT_YAML_FEATURES_AGGREGATION_ENABLE]) {
          this->aggregation_enable =
              config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_AGGREGATION]
                    [DFT_YAML_FEATURES_AGGREGATION_ENABLE]
                        .as<bool>();
          if (this->aggregation_enable) {
            this->aggregation_type = AggregationType::AGGREGATION_TYPE_FULL;
            if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_AGGREGATION]
                      [DFT_YAML_FEATURES_AGGREGATION_TYPE]) {
              convert(config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_AGGREGATION]
                            [DFT_YAML_FEATURES_AGGREGATION_TYPE]
                                .as<std::string>(),
                      this->aggregation_type);
            }
            if (this->aggregation_type ==
                AggregationType::AGGREGATION_TYPE_SELECTIVE) {
              if (config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_AGGREGATION]
                        [DFT_YAML_FEATURES_AGGREGATION_FILE]) {
                this->aggregation_file =
                    config[DFT_YAML_FEATURES][DFT_YAML_FEATURES_AGGREGATION]
                          [DFT_YAML_FEATURES_AGGREGATION_FILE]
                              .as<std::string>();
              }
            }
          }
        }
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.aggregation_enable %d",
                         this->aggregation_enable);
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.aggregation_type %d",
                         this->aggregation_type);
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.aggregation_file %s",
                         this->aggregation_file.c_str());
    }
    if (config[DFT_YAML_INTERNAL]) {
      if (config[DFT_YAML_INTERNAL][DFT_YAML_INTERNAL_SIGNALS]) {
        this->bind_signals =
            config[DFT_YAML_INTERNAL][DFT_YAML_INTERNAL_SIGNALS].as<bool>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.bind_signals %d",
                         this->bind_signals);
      if (config[DFT_YAML_INTERNAL][DFT_YAML_INTERNAL_THROW_ERROR]) {
        this->throw_error =
            config[DFT_YAML_INTERNAL][DFT_YAML_INTERNAL_THROW_ERROR].as<bool>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.throw_error %d",
                         this->throw_error);
      if (config[DFT_YAML_INTERNAL][DFT_YAML_INTERNAL_WRITE_BUFFER_SIZE]) {
        this->write_buffer_size =
            config[DFT_YAML_INTERNAL][DFT_YAML_INTERNAL_WRITE_BUFFER_SIZE]
                .as<size_t>();
      }
      DFTRACER_LOG_DEBUG("YAML ConfigurationManager.write_buffer_size %d",
                         this->write_buffer_size);
    }
  }
  // ENV variables override any YAML configuration, so this must run after
  // the YAML block above has had a chance to set time_metric.
  const char* env_time_metric = getenv(DFTRACER_TIME_METRIC);
  if (env_time_metric != nullptr) {
    convert(env_time_metric, this->time_metric);
  }
  DFTRACER_LOG_DEBUG("ConfigurationManager.time_metric %s",
                     to_string(this->time_metric).c_str());
  const char* env_enable = getenv(DFTRACER_ENABLE);
  if (env_enable != nullptr && strcmp(env_enable, "1") == 0) {
    this->enable = true;
  }
  DFTRACER_LOG_DEBUG("ENV ConfigurationManager.enable %d", this->enable);
  if (this->enable) {
    const char* env_trace_interval = getenv(DFTRACER_TRACE_INTERVAL_MS);
    if (env_trace_interval != nullptr) {
      this->trace_interval_ms = atoi(env_trace_interval);
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.trace_interval_ms %d",
                       this->trace_interval_ms);
    const char* env_libuv_threads = getenv(DFTRACER_LIBUV_THREADS);
    if (env_libuv_threads != nullptr) {
      this->libuv_thread_count = atoi(env_libuv_threads);
    }
    if (this->libuv_thread_count == 0) {
      this->libuv_thread_count = 1;
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.libuv_thread_count %zu",
                       this->libuv_thread_count);
    const char* env_init_type = getenv(DFTRACER_INIT);
    if (env_init_type != nullptr) {
      convert(env_init_type, this->init_type);
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.init_type %d",
                       this->init_type);
    const char* env_bind_signals = getenv(DFTRACER_BIND_SIGNALS);
    if (env_bind_signals != nullptr && strcmp(env_bind_signals, "1") == 0) {
      bind_signals = true;
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.bind_signals %d",
                       this->bind_signals);
    const char* env_meta = getenv(DFTRACER_INC_METADATA);
    if (env_meta != nullptr && strcmp(env_meta, "1") == 0) {
      metadata = true;
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.metadata %d", this->metadata);

    const char* env_core = getenv(DFTRACER_SET_CORE_AFFINITY);
    if (env_core != nullptr && strcmp(env_core, "1") == 0) {
      core_affinity = true;
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.core_affinity %d",
                       this->core_affinity);

    const char* env_gotcha_priority = getenv(DFTRACER_GOTCHA_PRIORITY);
    if (env_gotcha_priority != nullptr) {
      this->gotcha_priority = atoi(env_gotcha_priority);  // GCOV_EXCL_LINE
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.gotcha_priority %d",
                       this->gotcha_priority);
    const char* env_log_file = getenv(DFTRACER_LOG_FILE);
    if (env_log_file != nullptr) {
      this->log_file = env_log_file;
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.log_file %s",
                       this->log_file.c_str());
    const char* env_data_dirs = getenv(DFTRACER_DATA_DIR);
    if (env_data_dirs != nullptr) {
      if (strcmp(env_data_dirs, DFTRACER_ALL_FILES) == 0) {
        this->trace_all_files = true;
      } else {
        this->data_dirs = env_data_dirs;
      }
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.data_dirs %s",
                       this->data_dirs.c_str());
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.trace_all_files %d",
                       this->trace_all_files);
    const char* disable_io = getenv(DFTRACER_DISABLE_IO);
    if (disable_io != nullptr && strcmp(disable_io, "1") == 0) {
      this->io = false;
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.io %d", this->io);
    if (this->io) {
      const char* disable_posix = getenv(DFTRACER_DISABLE_POSIX);
      if (disable_posix != nullptr && strcmp(disable_posix, "1") == 0) {
        this->posix = false;
      }
      DFTRACER_LOG_DEBUG("ENV ConfigurationManager.posix %d", this->posix);
      const char* disable_stdio = getenv(DFTRACER_DISABLE_STDIO);
      if (disable_stdio != nullptr && strcmp(disable_stdio, "1") == 0) {
        this->stdio = false;
      }
      DFTRACER_LOG_DEBUG("ENV ConfigurationManager.stdio %d", this->stdio);
    }
    const char* env_tid = getenv(DFTRACER_DISABLE_TIDS);
    if (env_tid != nullptr && strcmp(env_tid, "0") == 0) {
      this->tids = false;
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.tids %d", this->tids);
    const char* env_enable_aggregation = getenv(DFTRACER_ENABLE_AGGREGATION);
    if (env_enable_aggregation != nullptr &&
        strcmp(env_enable_aggregation, "1") == 0) {
      this->aggregation_enable = true;
      if (this->aggregation_enable) {
        this->aggregation_type = AggregationType::AGGREGATION_TYPE_FULL;
        const char* env_aggregation_type = getenv(DFTRACER_AGGREGATION_TYPE);
        if (env_aggregation_type != nullptr) {
          convert(env_aggregation_type, this->aggregation_type);
        }
        if (this->aggregation_type ==
            AggregationType::AGGREGATION_TYPE_SELECTIVE) {
          const char* env_aggregation_file = getenv(DFTRACER_AGGREGATION_FILE);
          if (env_aggregation_file != nullptr) {
            this->aggregation_file = env_aggregation_file;
          }
        }
      }
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.enable_aggregation %s",
                       this->aggregation_enable ? "true" : "false");
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.aggregation_type %d",
                       to_string(this->aggregation_type).c_str());
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.aggregation_file %s",
                       this->aggregation_file.c_str());
    const char* env_throw_error = getenv(DFTRACER_ERROR);
    if (env_throw_error != nullptr && strcmp(env_throw_error, "1") == 0) {
      this->throw_error = true;  // GCOVR_EXCL_LINE
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.throw_error %d",
                       this->throw_error);
    const char* env_compression = getenv(DFTRACER_TRACE_COMPRESSION);
    if (env_compression != nullptr) {
      if (strcmp(env_compression, "1") == 0)
        this->compression = true;
      else
        this->compression = false;
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.compression %d",
                       this->compression);
    const char* env_write_buf_size = getenv(DFTRACER_WRITE_BUFFER_SIZE);
    if (env_write_buf_size != nullptr) {
      this->write_buffer_size = atoi(env_write_buf_size);
    }
    DFTRACER_LOG_DEBUG("ENV ConfigurationManager.write_buffer_size %d",
                       this->write_buffer_size);
  }
  derive_configurations();
  DFTRACER_LOG_DEBUG("ENV ConfigurationManager finished");
}

void dftracer::ConfigurationManager::derive_configurations() {
  // Derive configurations based on the current settings
  if (this->aggregation_type == AggregationType::AGGREGATION_TYPE_SELECTIVE) {
    if (!this->aggregation_file.empty() &&
        std::filesystem::exists(this->aggregation_file)) {
      // Load aggregation rules from the specified file
      YAML::Node agg_config = YAML::LoadFile(this->aggregation_file);
      if (agg_config[DFT_YAML_FEATURES_AGGREGATION_INCLUSION_FILTERS]) {
        const auto& inclusion =
            agg_config[DFT_YAML_FEATURES_AGGREGATION_INCLUSION_FILTERS];
        if (inclusion.IsSequence()) {
          for (const auto& item : inclusion) {
            this->aggregation_inclusion_rules.push_back(item.as<std::string>());
          }
        }
      }
      if (agg_config[DFT_YAML_FEATURES_AGGREGATION_EXCLUSION_FILTERS]) {
        const auto& exclusion =
            agg_config[DFT_YAML_FEATURES_AGGREGATION_EXCLUSION_FILTERS];
        if (exclusion.IsSequence()) {
          for (const auto& item : exclusion) {
            this->aggregation_exclusion_rules.push_back(item.as<std::string>());
          }
        }
      }
      DFTRACER_LOG_DEBUG("Aggregation inclusion rules");
      for (const auto& rule : this->aggregation_inclusion_rules) {
        (void)rule;
        DFTRACER_LOG_DEBUG(" - %s", rule.c_str());
      }
      DFTRACER_LOG_DEBUG("Aggregation exclusion rules");
      for (const auto& rule : this->aggregation_exclusion_rules) {
        (void)rule;
        DFTRACER_LOG_DEBUG(" - %s", rule.c_str());
      }
    } else {
      DFTRACER_LOG_WARN("Aggregation configuration file %s not found",
                        this->aggregation_file.c_str());
    }
  }
  DFTRACER_LOG_DEBUG("ConfigurationManager::derive_configurations finished",
                     "");
}

void dftracer::ConfigurationManager::populate_metadata(
    dftracer::Metadata* meta) const {
  std::ostringstream cfg;
  cfg << "{"
      << "\"enable\":" << (int)this->enable << ","
      << "\"metadata\":" << (int)this->metadata << ","
      << "\"core_affinity\":" << (int)this->core_affinity << ","
      << "\"time_metric\":\"" << to_string(this->time_metric) << "\","
      << "\"io\":" << (int)this->io << ","
      << "\"posix\":" << (int)this->posix << ","
      << "\"stdio\":" << (int)this->stdio << ","
      << "\"compression\":" << (int)this->compression << ","
      << "\"trace_all_files\":" << (int)this->trace_all_files << ","
      << "\"tids\":" << (int)this->tids << ","
      << "\"bind_signals\":" << (int)this->bind_signals << ","
      << "\"write_buffer_size\":" << this->write_buffer_size << ","
      << "\"trace_interval_ms\":" << this->trace_interval_ms << ","
      << "\"libuv_thread_count\":" << this->libuv_thread_count << ","
      << "\"aggregation_enable\":" << (int)this->aggregation_enable << ","
      << "\"aggregation_type\":\"" << to_string(this->aggregation_type) << "\""
      << "}";
  meta->insert_or_assign("cfg", dftracer::RawJson(cfg.str()));

  // Compile-time layer availability: what this build supports, regardless of
  // whether it was exercised this run (see DFTLogger's used-layer bitmask for
  // that).
  std::ostringstream build;
  build << "{"
        << "\"mpi\":"
#ifdef DFTRACER_MPI_ENABLE
        << 1
#else
        << 0
#endif
        << ",\"hdf5\":"
#ifdef DFTRACER_HDF5_ENABLE
        << 1
#else
        << 0
#endif
        << ",\"hip\":"
#ifdef DFTRACER_HIP_TRACING_ENABLE
        << 1
#else
        << 0
#endif
        << ",\"finstrument\":"
#ifdef DFTRACER_FTRACING_ENABLE
        << 1
#else
        << 0
#endif
        << "}";
  meta->insert_or_assign("build", dftracer::RawJson(build.str()));
}