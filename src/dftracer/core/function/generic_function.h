//
// Created by druva on 6/9/25 from finstrument/functions.h
//
/* Config Header */
#include <dftracer/core/dftracer_config.hpp>

/* Internal Header */
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/posix_bypass.h>

/* External Header */
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace dftracer {
class GenericFunction {
  // static bool stop_trace;

 public:
  std::shared_ptr<DFTLogger> logger;
  GenericFunction() {
    DFTRACER_LOG_DEBUG("GenericFunction class intercepted");
    logger = DFT_LOGGER_INIT();
  }

  virtual void initialize() {}
  virtual void finalize() {}
  // virtual void log_event();

  virtual ~GenericFunction() {};
  // bool is_active() { return !stop_trace; }
};

}  // namespace dftracer