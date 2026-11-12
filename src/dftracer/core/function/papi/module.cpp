// The PAPI module's hooks into the core (see common/module_hooks.h): starting
// and stopping the hardware-counter sampler. Empty without PAPI, so the hooks
// stay undefined and the core skips them.
#include <dftracer/core/function/papi/counters.h>

#ifdef DFTRACER_PAPI_TRACING_ENABLE
#include <dftracer/core/common/module_hooks.h>
#include <dftracer/core/common/singleton.h>

extern "C" {

void dftracer_module_papi_start(void) {
  auto papi_instance =
      dftracer::Singleton<dftracer::PAPICounterFunction>::get_instance();
  if (papi_instance != nullptr) papi_instance->initialize();
}

void dftracer_module_papi_stop(void) {
  auto papi_instance =
      dftracer::Singleton<dftracer::PAPICounterFunction>::get_instance();
  if (papi_instance != nullptr) papi_instance->finalize();
}

}  // extern "C"
#endif  // DFTRACER_PAPI_TRACING_ENABLE
