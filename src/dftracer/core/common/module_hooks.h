#ifndef DFTRACER_MODULE_HOOKS_H
#define DFTRACER_MODULE_HOOKS_H
// The core reaches the optional modules -- MPI, HDF5, PAPI, hwloc, HIP and
// CUDA -- only through these hooks, so it compiles the same whichever of them
// a build has and is linked with whichever modules are available.
//
// Every hook is a weak reference: a module that is linked in defines its
// hooks, and the address of a hook whose module is absent is null. The core
// calls them where it used to have `#ifdef DFTRACER_<MODULE>_ENABLE` blocks,
// in the same order, so tracing starts and stops exactly as before.
//
//   if (dftracer_module_papi_stop) dftracer_module_papi_stop();
//
// Hidden visibility keeps them out of the dynamic symbol table: a hook binds
// to the module linked into the same library and nothing else can interpose.
//
// Linking from static archives: a weak reference does not pull an archive
// member in, so module archives are linked with --whole-archive.
#include <cstddef>
#include <vector>

#define DFTRACER_MODULE_HOOK __attribute__((weak, visibility("hidden")))

extern "C" {

// MPI (MPI and MPI-IO interception, implementation-specific compat APIs).
// Defined whenever the MPI module is linked; bind/unbind do nothing when
// brahma was built without MPI.
void dftracer_module_mpi_bind(int gotcha_priority) DFTRACER_MODULE_HOOK;
void dftracer_module_mpi_unbind(void) DFTRACER_MODULE_HOOK;
// Writes this process's rank in MPI_COMM_WORLD and returns 1 while MPI is
// initialized and not yet finalized; returns 0 otherwise.
int dftracer_module_mpi_rank(int* rank) DFTRACER_MODULE_HOOK;

// HDF5 interception.
void dftracer_module_hdf5_bind(int gotcha_priority) DFTRACER_MODULE_HOOK;
void dftracer_module_hdf5_unbind(void) DFTRACER_MODULE_HOOK;

// PAPI hardware-counter sampler.
void dftracer_module_papi_start(void) DFTRACER_MODULE_HOOK;
void dftracer_module_papi_stop(void) DFTRACER_MODULE_HOOK;

// GPU activity tracing through rocprofiler-sdk (HIP) and CUPTI (CUDA).
void dftracer_module_hip_start(void) DFTRACER_MODULE_HOOK;
void dftracer_module_hip_stop(void) DFTRACER_MODULE_HOOK;
void dftracer_module_cuda_start(void) DFTRACER_MODULE_HOOK;
void dftracer_module_cuda_stop(void) DFTRACER_MODULE_HOOK;

// hwloc: appends the cores this process is bound to.
void dftracer_module_hwloc_core_affinity(std::vector<unsigned>* cores)
    DFTRACER_MODULE_HOOK;

// The modules this library was composed with, comma separated
// ("mpi,hdf5,papi"). Generated when the library is linked, so it names the
// modules actually in it, including ones that live outside the core library
// (Variorum is linked into dftracer_service).
const char* dftracer_composition_modules(void) DFTRACER_MODULE_HOOK;
}

// Whether `name` is one of the modules this library was composed with.
inline bool dftracer_module_composed(const char* name) {
  if (dftracer_composition_modules == nullptr || name == nullptr) return false;
  const char* list = dftracer_composition_modules();
  const std::size_t len = __builtin_strlen(name);
  for (const char* p = list; p != nullptr && *p != '\0';) {
    const char* end = __builtin_strchr(p, ',');
    const std::size_t n = end != nullptr ? static_cast<std::size_t>(end - p)
                                         : __builtin_strlen(p);
    if (n == len && __builtin_strncmp(p, name, n) == 0) return true;
    p = end != nullptr ? end + 1 : nullptr;
  }
  return false;
}

#endif  // DFTRACER_MODULE_HOOKS_H
