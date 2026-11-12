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

// brahma's common MPI interface is compiled for every MPI implementation and
// version, so these calls are always intercepted when MPI tracing is on.
//
// dftracer's OWN MPI calls in dftracer_module_mpi_rank() below must use the
// PMPI_ entry points for every implementation in this set. If they use the
// public MPI_ names they are caught by our own interceptors, and the result is
// unbounded recursion:
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
#if defined(BRAHMA_ENABLE_MPI)
#define DFTRACER_MPI_SELF_CALLS_ARE_INTERCEPTED 1
#endif

extern "C" {

void dftracer_module_mpi_bind(int gotcha_priority) {
#if defined(BRAHMA_ENABLE_MPI)
  auto mpi = brahma::MPIDFTracer::get_instance();
  mpi->bind<brahma::MPIDFTracer>("dftracer", gotcha_priority);
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
  int initialized;
  int finalized;
  int status = MPI_SUCCESS;
  int finalized_status = MPI_SUCCESS;
#ifdef DFTRACER_MPI_SELF_CALLS_ARE_INTERCEPTED
  status = PMPI_Initialized(&initialized);
  finalized_status = PMPI_Finalized(&finalized);
#else
  status = MPI_Initialized(&initialized);
  finalized_status = MPI_Finalized(&finalized);
#endif
  // MPI_Initialized stays true forever after MPI_Init, even once
  // MPI_Finalize has run, so it alone cannot tell us whether it is
  // still safe to call MPI_Comm_rank.
  if (status == MPI_SUCCESS && initialized == true &&
      finalized_status == MPI_SUCCESS && finalized == false) {
#ifdef DFTRACER_MPI_SELF_CALLS_ARE_INTERCEPTED
    PMPI_Comm_rank(MPI_COMM_WORLD, rank);
#else
    MPI_Comm_rank(MPI_COMM_WORLD, rank);
#endif
    return 1;
  }
#else
  (void)rank;
#endif
  return 0;
}

}  // extern "C"
