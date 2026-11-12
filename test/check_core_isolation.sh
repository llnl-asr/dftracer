#!/usr/bin/env bash
# Fail if the core includes an optional module's headers or tests one of its
# build macros. The core is compiled once and linked with whichever modules a
# build has (MPI, HDF5, PAPI, hwloc, HIP, CUDA, Variorum), so it reaches them
# only through the hooks in src/dftracer/core/common/module_hooks.h.
#   check_core_isolation.sh <source root>
set -euo pipefail
root=${1:?usage: check_core_isolation.sh <source root>}
cd "$root"

# The modules' own files, which are allowed to use their library.
modules='^src/dftracer/core/brahma/(mpi|hdf5)'
modules+='|^src/dftracer/core/function/(papi|hip|cuda)/'
modules+='|^src/dftracer/core/utils/hwloc_affinity\.cpp:'
modules+='|^src/dftracer/service/telemetry/variorum'

forbidden='#[[:space:]]*include[[:space:]]*[<"]('
forbidden+='mpi\.h|hdf5\.h|H5[A-Za-z]*\.h|papi\.h|hwloc\.h|rocprofiler|cupti|cuda|hip/'
forbidden+='|dftracer/core/brahma/(mpi|hdf5)|dftracer/core/function/(papi|hip|cuda)/)'
forbidden+='|DFTRACER_(MPI|HDF5|PAPI_TRACING|HWLOC|HIP_TRACING|CUDA_TRACING|VARIORUM)_ENABLE'
forbidden+='|BRAHMA_ENABLE_(MPI|HDF5)'

hits=$(grep -rnE "$forbidden" src include --include='*.h' --include='*.hpp' \
         --include='*.cpp' --include='*.in' | grep -vE "$modules" || true)
if [ -n "$hits" ]; then
  echo "core code depends on an optional module; use module_hooks.h instead:" >&2
  echo "$hits" >&2
  exit 1
fi
echo "core is free of optional-module dependencies"
