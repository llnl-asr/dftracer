//
// Created by haridev on 10/5/23.
//

#ifndef DFTRACER_DFTRACER_MAIN_H
#define DFTRACER_DFTRACER_MAIN_H

#include <brahma/brahma.h>
#include <cpp-logger/logger.h>
#include <dftracer/core/brahma/posix.h>
#include <dftracer/core/brahma/stdio.h>
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/error.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/df_logger.h>
#include <execinfo.h>

#include <any>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace dftracer {
class DFTracerCore {
 private:
  std::string log_file;
  std::string data_dirs;
  std::shared_ptr<dftracer::ConfigurationManager> conf;
  ProcessID process_id;
  bool is_initialized;
  bool bind;
  std::string log_file_suffix;
  std::shared_ptr<DFTLogger> logger;
  void initialize(bool _bind, const char *_log_file = nullptr,
                  const char *_data_dirs = nullptr,
                  const int *_process_id = nullptr);

 public:
  bool include_metadata;
  DFTracerCore(ProfilerStage stage, ProfileType type,
               const char *log_file = nullptr, const char *data_dirs = nullptr,
               const int *process_id = nullptr);

  void reinitialize();
  inline bool is_active() {
    DFTRACER_LOG_DEBUG("DFTracerCore.is_active", "");
    return conf->enable;
  }

  TimeResolution get_time();

  void log(ConstEventNameType event_name, ConstEventNameType category,
           TimeResolution start_time, TimeResolution duration,
           dftracer::Metadata *metadata);

  void log_metadata(ConstEventNameType key, ConstEventNameType value);

  inline int enter_event() { return logger->enter_event(); }

  inline void exit_event() { logger->exit_event(); }

  bool finalize();

  void initialize() {}
  ~DFTracerCore() { DFTRACER_LOG_DEBUG("Destructing DFTracerCore", ""); }
};
}  // namespace dftracer

#define DFTRACER_MAIN_SINGLETON_INIT(stage, type, ...)                   \
  dftracer::Singleton<dftracer::DFTracerCore>::get_instance(stage, type, \
                                                            __VA_ARGS__)

#define DFTRACER_MAIN_SINGLETON(stage, type) \
  dftracer::Singleton<dftracer::DFTracerCore>::get_instance(stage, type)
#endif  // DFTRACER_DFTRACER_MAIN_H
