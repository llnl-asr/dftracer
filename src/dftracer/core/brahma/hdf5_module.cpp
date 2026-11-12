// The HDF5 module's hooks into the core (see common/module_hooks.h): binding
// the HDF5 interception. Compiled only when the build has HDF5.
// brahma.h first: BRAHMA_ENABLE_HDF5, which decides whether there is
// anything to bind, comes from the brahma_config.hpp it includes, and none
// of the headers below include it.
#include <brahma/brahma.h>
#include <dftracer/core/common/module_hooks.h>
#include <dftracer/core/df_logger.h>

#if defined(BRAHMA_ENABLE_HDF5)
#include <dftracer/core/brahma/hdf5.h>
#endif

extern "C" {

void dftracer_module_hdf5_bind(int gotcha_priority) {
#if defined(BRAHMA_ENABLE_HDF5)
  auto hdf5 = brahma::HDF5DFTracer::get_instance();
  hdf5->bind<brahma::HDF5DFTracer>("dftracer", gotcha_priority);
#else
  (void)gotcha_priority;
#endif
}

void dftracer_module_hdf5_unbind(void) {
#if defined(BRAHMA_ENABLE_HDF5)
  auto hdf5_instance = brahma::HDF5DFTracer::get_instance();
  if (hdf5_instance != nullptr) {
    hdf5_instance->unbind();
    hdf5_instance->finalize();
  }
#endif
}

}  // extern "C"
