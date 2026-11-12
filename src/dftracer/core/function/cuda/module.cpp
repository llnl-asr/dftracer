// The CUDA module's hooks into the core (see common/module_hooks.h): starting
// and stopping CUPTI tracing. Empty without CUDA tracing, so the hooks stay
// undefined and the core skips them.
#include <dftracer/core/function/cuda/intercept.h>

#ifdef DFTRACER_CUDA_TRACING_ENABLE
#include <dftracer/core/common/module_hooks.h>
#include <dftracer/core/common/singleton.h>

extern "C" {

void dftracer_module_cuda_start(void) {
  auto cuda_instance =
      dftracer::Singleton<dftracer::CUDAFunction>::get_instance();
  cuda_instance->initialize();
}

void dftracer_module_cuda_stop(void) {
  auto cuda_instance =
      dftracer::Singleton<dftracer::CUDAFunction>::get_instance();
  if (cuda_instance != nullptr) cuda_instance->finalize();
}

}  // extern "C"
#endif  // DFTRACER_CUDA_TRACING_ENABLE
