// The MPI module's hooks into the core (see common/module_hooks.h): binding
// the MPI and MPI-IO interception, and the process's rank. Compiled only when
// the build has MPI, so the core itself never includes mpi.h.
// brahma.h first: BRAHMA_ENABLE_MPI, which decides whether there is
// anything to bind, comes from the brahma_config.hpp it includes, and none
// of the headers below include it.
#include <brahma/brahma.h>
#include <dftracer/core/common/module_hooks.h>
#include <dftracer/core/df_logger.h>

#if defined(BRAHMA_ENABLE_MPI)
#include <dftracer/core/brahma/mpi.h>
#include <dftracer/core/brahma/mpi_compat.h>
#include <dftracer/core/brahma/mpiio.h>
#include <dftracer/core/brahma/mpiio_compat.h>
#endif
#include <mpi.h>

#include <atomic>

// brahma's common MPI interface is compiled for every MPI implementation and
// version, so these calls are always intercepted when MPI tracing is on.
//
// dftracer's OWN MPI calls in dftracer_module_mpi_rank() below must not go
// through the public MPI_ names for any implementation in this set. If they
// do, they are caught by our own interceptors, and the result is unbounded
// recursion:
//   interceptor -> DFT_LOGGER_START_ALWAYS -> logger -> handle_mpi()
//     -> dftracer_module_mpi_rank() -> MPI_Comm_rank -> interceptor -> ...
// There is no reentrancy guard anywhere in the logger to stop it, and each
// level allocates a `new dftracer::Metadata`, so the process dies of OOM rather
// than a stack overflow. Measured on MVAPICH2 2.3.7: a SINGLE rank exhausted a
// 515 GB node and the trace was left 0 bytes.
//
// The common interface in core/brahma/mpi.cpp and core/brahma/mpiio.cpp has no
// implementation or version guards, so it holds for any MPI, including one
// brahma has never seen. Do not narrow this to a list of implementations: an
// MPI outside the list would still be intercepted and recurse.
//
// So they call PMPI_ or the real MPI_ functions without naming them: with no
// MPI symbol of its own (MPICH family) the core links no libmpi, and
// LD_PRELOAD adds no second MPI beside the program's. Bound, they are what
// gotcha found for brahma's interceptors; the handles are copied at bind, so a
// lookup never touches the brahma instance that unbind finalizes, and unbind
// leaves their wrappees on the real functions. Unbound (initialize_no_bind, or
// I/O tracing off), they are PMPI_ functions of a loaded libmpi. Either finds
// nothing until a libmpi is loaded, as before Python imports mpi4py.
#if defined(BRAHMA_ENABLE_MPI)
#include <dlfcn.h>
#include <gotcha/gotcha.h>
#include <link.h>

#include <cstring>

namespace {
struct MpiHandles {
  std::atomic<gotcha_wrappee_handle_t> initialized{nullptr};
  std::atomic<gotcha_wrappee_handle_t> finalized{nullptr};
  std::atomic<gotcha_wrappee_handle_t> comm_rank{nullptr};
};
MpiHandles& bound_handles() {
  static MpiHandles handles;
  return handles;
}

struct PmpiFunctions {
  std::atomic<MPI_Initialized_fptr> initialized{nullptr};
  std::atomic<MPI_Finalized_fptr> finalized{nullptr};
  // Stored last: once set, the other two are.
  std::atomic<MPI_Comm_rank_fptr> comm_rank{nullptr};
  std::atomic<unsigned long> attempts{0};
};
PmpiFunctions& pmpi() {
  static PmpiFunctions functions;
  return functions;
}

// A libmpi that Python loaded with RTLD_LOCAL (mpi4py) is not in the global
// scope, so each loaded libmpi* is also asked by name.
int find_pmpi_in_object(struct dl_phdr_info* info, size_t, void* out) {
  const char* name = info->dlpi_name;
  if (name == nullptr || std::strstr(name, "libmpi") == nullptr) return 0;
  void* lib = dlopen(name, RTLD_LAZY | RTLD_NOLOAD);
  if (lib == nullptr) return 0;
  void* rank = dlsym(lib, "PMPI_Comm_rank");
  if (rank != nullptr) {
    static_cast<void**>(out)[0] = dlsym(lib, "PMPI_Initialized");
    static_cast<void**>(out)[1] = dlsym(lib, "PMPI_Finalized");
    static_cast<void**>(out)[2] = rank;
  }
  dlclose(lib);
  return rank != nullptr ? 1 : 0;
}

// Searches on the 1st, 2nd, 4th, 8th... call until found, as a non-MPI
// program asks on every event.
void resolve_pmpi() {
  auto& fns = pmpi();
  unsigned long n = fns.attempts.fetch_add(1, std::memory_order_relaxed) + 1;
  if ((n & (n - 1)) != 0) return;
  void* found[3] = {dlsym(RTLD_DEFAULT, "PMPI_Initialized"),
                    dlsym(RTLD_DEFAULT, "PMPI_Finalized"),
                    dlsym(RTLD_DEFAULT, "PMPI_Comm_rank")};
  if (found[2] == nullptr) {
    found[0] = found[1] = nullptr;
    dl_iterate_phdr(find_pmpi_in_object, found);
  }
  if (found[0] == nullptr || found[1] == nullptr || found[2] == nullptr) return;
  fns.initialized.store(reinterpret_cast<MPI_Initialized_fptr>(found[0]),
                        std::memory_order_relaxed);
  fns.finalized.store(reinterpret_cast<MPI_Finalized_fptr>(found[1]),
                      std::memory_order_relaxed);
  fns.comm_rank.store(reinterpret_cast<MPI_Comm_rank_fptr>(found[2]),
                      std::memory_order_release);
}
}  // namespace
#endif

extern "C" {

void dftracer_module_mpi_bind(int gotcha_priority) {
#if defined(BRAHMA_ENABLE_MPI)
  auto mpi = brahma::MPIDFTracer::get_instance();
  mpi->bind<brahma::MPIDFTracer>("dftracer", gotcha_priority);
  auto& handles = bound_handles();
  handles.initialized.store(mpi->MPI_Initialized_brahma_handle,
                            std::memory_order_relaxed);
  handles.finalized.store(mpi->MPI_Finalized_brahma_handle,
                          std::memory_order_relaxed);
  handles.comm_rank.store(mpi->MPI_Comm_rank_brahma_handle,
                          std::memory_order_release);
  auto mpiio = brahma::MPIIODFTracer::get_instance();
  mpiio->bind<brahma::MPIIODFTracer>("dftracer", gotcha_priority);
  brahma::dftracer_mpi_compat_bind(gotcha_priority);
  brahma::dftracer_mpiio_compat_bind(gotcha_priority);
#else
  (void)gotcha_priority;
#endif
}

void dftracer_module_mpi_unbind(void) {
#if defined(BRAHMA_ENABLE_MPI)
  auto mpi_instance = brahma::MPIDFTracer::get_instance();
  if (mpi_instance != nullptr) {
    mpi_instance->unbind();
    mpi_instance->finalize();
  }
  auto mpiio_instance = brahma::MPIIODFTracer::get_instance();
  if (mpiio_instance != nullptr) {
    mpiio_instance->unbind();
    mpiio_instance->finalize();
  }
  // implementation-specific APIs beyond the common set; nothing is
  // compiled here for an MPI without a compat interface
  brahma::dftracer_mpi_compat_finalize();
  brahma::dftracer_mpiio_compat_finalize();
#endif
}

int dftracer_module_mpi_rank(int* rank) {
#if defined(BRAHMA_ENABLE_MPI)
  MPI_Initialized_fptr initialized_fn = nullptr;
  MPI_Finalized_fptr finalized_fn = nullptr;
  MPI_Comm_rank_fptr comm_rank_fn = nullptr;
  auto& handles = bound_handles();
  gotcha_wrappee_handle_t comm_rank_handle =
      handles.comm_rank.load(std::memory_order_acquire);
  if (comm_rank_handle != nullptr) {
    comm_rank_fn = reinterpret_cast<MPI_Comm_rank_fptr>(
        gotcha_get_wrappee(comm_rank_handle));
    initialized_fn = reinterpret_cast<MPI_Initialized_fptr>(gotcha_get_wrappee(
        handles.initialized.load(std::memory_order_relaxed)));
    finalized_fn = reinterpret_cast<MPI_Finalized_fptr>(
        gotcha_get_wrappee(handles.finalized.load(std::memory_order_relaxed)));
  } else {
    auto& fns = pmpi();
    comm_rank_fn = fns.comm_rank.load(std::memory_order_acquire);
    if (comm_rank_fn == nullptr) {
      resolve_pmpi();
      comm_rank_fn = fns.comm_rank.load(std::memory_order_acquire);
    }
    initialized_fn = fns.initialized.load(std::memory_order_relaxed);
    finalized_fn = fns.finalized.load(std::memory_order_relaxed);
  }
  if (initialized_fn == nullptr || finalized_fn == nullptr ||
      comm_rank_fn == nullptr)
    return 0;
  int initialized = 0;
  int finalized = 0;
  // MPI_Initialized stays true forever after MPI_Init, even once
  // MPI_Finalize has run, so it alone cannot tell us whether it is
  // still safe to call MPI_Comm_rank.
  if (initialized_fn(&initialized) == MPI_SUCCESS && initialized &&
      finalized_fn(&finalized) == MPI_SUCCESS && !finalized) {
    comm_rank_fn(MPI_COMM_WORLD, rank);
    return 1;
  }
#else
  (void)rank;
#endif
  return 0;
}

}  // extern "C"
