#!/bin/bash
# Runs INSIDE the CI-dev container: builds DFTracer's dependencies and DFTracer
# with every optional module the image has, then runs the tests.
#
#   ci-build.sh [--mpi openmpi|mpich] [--no-test] [--name NAME]
#               [--cmake-arg -DX=Y]...
#
# --cmake-arg is added after the defaults, so it can turn a module off
# (e.g. --cmake-arg -DDFTRACER_ENABLE_HIP_TRACING=OFF).
#
# The source is /src (read-only is fine); everything is written under
# /work/<name>, so builds of different trees or MPIs sit side by side.
set -euo pipefail

MPI=openmpi
TEST=1
NAME=
EXTRA=()
while [ $# -gt 0 ]; do
  case "$1" in
    --mpi) MPI=$2; shift 2 ;;
    --no-test) TEST=0; shift ;;
    --name) NAME=$2; shift 2 ;;
    --cmake-arg) EXTRA+=("$2"); shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
NAME=${NAME:-$MPI}

case "$MPI" in
  openmpi|mpich) ;;
  *) echo "--mpi must be openmpi or mpich" >&2; exit 2 ;;
esac
# What `module load mpi/<mpi>-x86_64` does on EL8.
export PATH=/usr/lib64/$MPI/bin:$PATH
export LD_LIBRARY_PATH=/usr/lib64/$MPI/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export MPI_HOME=/usr/lib64/$MPI
export CC=gcc CXX=g++

W=/work/$NAME
JOBS=${JOBS:-$(nproc)}
PY=$(command -v python)
FEATURES=(
  -DCMAKE_BUILD_TYPE=Release
  -DDFTRACER_ENABLE_MPI=ON
  -DDFTRACER_ENABLE_HDF5=ON
  -DHDF5_PREFER_PARALLEL=ON
  -DDFTRACER_ENABLE_PAPI_TRACING=ON
  -DDFTRACER_DISABLE_HWLOC=OFF
  -DDFTRACER_ENABLE_HIP_TRACING=ON
  -DDFTRACER_ENABLE_CUDA_TRACING=ON
  -DDFTRACER_CUDA_PATH=/usr/local/cuda
  -DDFTRACER_ENABLE_VARIORUM=ON
  -DDFTRACER_ENABLE_TESTS=ON
  -DDFTRACER_BUILD_PYTHON_BINDINGS=ON
  -DDFTRACER_PYTHON_EXE=$PY
  -Dpybind11_DIR=$($PY -c 'import pybind11; print(pybind11.get_cmake_dir())')
  -DCMAKE_PREFIX_PATH=$W/deps-prefix
  "${EXTRA[@]}"
)

if [ ! -f "$W/deps-prefix/.done" ]; then
  cmake -S /src -B "$W/deps-build" -DCMAKE_INSTALL_PREFIX="$W/deps-prefix" \
    -DDFTRACER_INSTALL_DEPENDENCIES=ON "${FEATURES[@]}"
  cmake --build "$W/deps-build" -j "$JOBS"
  touch "$W/deps-prefix/.done"
fi

cmake -S /src -B "$W/build" -DCMAKE_INSTALL_PREFIX="$W/prefix" "${FEATURES[@]}" \
  2>&1 | tee "$W/configure.log"
cmake --build "$W/build" -j "$JOBS" 2>&1 | tee "$W/build.log" | grep -E 'error|warning: unused|Error' || true
cmake --build "$W/build" -j "$JOBS" >/dev/null
cmake --install "$W/build" >/dev/null
echo "BUILD OK: $NAME"

if [ "$TEST" = 1 ]; then
  cd "$W/build"
  # Several checks glob every trace a test name ever wrote, so traces left by
  # an earlier run (one that crashed, say) would fail this one.
  find "$W/build" \( -name '*.pfw' -o -name '*.pfw.gz' \) -delete
  ctest --output-on-failure -j "${CTEST_JOBS:-$JOBS}" 2>&1 | tee "$W/ctest.log" | tail -40
fi
