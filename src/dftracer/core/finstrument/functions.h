//
// Created by hariharan on 8/8/22.
//

#ifndef DFTRACER_FUNCTION_H
#define DFTRACER_FUNCTION_H
/* Config Header */
#include <dftracer/core/dftracer_config.hpp>
#ifdef DFTRACER_FTRACING_ENABLE
/* Internal Header */
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/posix_bypass.h>
/* External Header */
#include <dlfcn.h>

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

static ConstEventNameType CATEGORY = "FUNC";
extern "C" {
void __cyg_profile_func_enter(void*, void*)
    __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void*, void*)
    __attribute__((no_instrument_function));
}
namespace dftracer {
class Function {
 private:
  static std::shared_ptr<Function> instance;
  static bool stop_trace;
  thread_local static std::unordered_map<std::string, TimeResolution> map;

 public:
  std::shared_ptr<DFTLogger> logger;
  bool enable;
  Function() : enable(false) {
    DFTRACER_LOG_DEBUG("Function class intercepted");
    logger = DFT_LOGGER_INIT();
    auto conf =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    enable = conf && conf->enable;
  }

  void finalize() {
    DFTRACER_LOG_DEBUG("Finalizing Function");
    stop_trace = true;
  }
  ~Function() {}
  static std::shared_ptr<Function> get_instance() {
    DFTRACER_LOG_DEBUG("POSIX class get_instance");
    if (!stop_trace && instance == nullptr) {
      instance = std::make_shared<Function>();
    }
    return instance;
  }
  bool is_active() { return !stop_trace && enable; }
  int enter_event(std::string& name);
  int exit_event(std::string& name, TimeResolution& start);
};

}  // namespace dftracer
#endif
#endif  // DFTRACER_FUNCTION_H
