// The HIP module's hooks into the core (see common/module_hooks.h): starting
// and stopping rocprofiler-sdk tracing. Empty without HIP tracing, so the
// hooks stay undefined and the core skips them.
#include <dftracer/core/function/hip/intercept.h>

#ifdef DFTRACER_HIP_TRACING_ENABLE
#include <dftracer/core/common/module_hooks.h>
#include <dftracer/core/common/singleton.h>

extern "C" {

void dftracer_module_hip_start(void) {
  auto hip_instance =
      dftracer::Singleton<dftracer::HIPFunction>::get_instance();
  hip_instance->initialize();
}

void dftracer_module_hip_stop(void) {
  auto hip_instance =
      dftracer::Singleton<dftracer::HIPFunction>::get_instance();
  if (hip_instance != nullptr) hip_instance->finalize();
}

}  // extern "C"
#endif  // DFTRACER_HIP_TRACING_ENABLE
