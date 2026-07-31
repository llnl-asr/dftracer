#!/bin/bash
# dlio-benchmark.yml -> DLIO integration (workload matrix kept via CI
# parallel:matrix; $WORKLOAD comes from the job environment; clang/LLVM
# python-binding detection dropped as in build-test.sh).
set -eo pipefail
cd "$CI_PROJECT_DIR"
export INSTALL_MODE=pip
export DLIO_LOG_LEVEL="${DLIO_LOG_LEVEL:-info}"
export VALGRIND_DLIO_TIMEOUT="${VALGRIND_DLIO_TIMEOUT:-3600}"
export RDMAV_FORK_SAFE=1
source .gitlab/flux-ci/toolchain.sh
command -v valgrind && valgrind --version
command -v gdb && gdb --version
# No LC modules inside the container: HDF5 comes from the image (libhdf5-mpich-dev).
command -v h5pcc >/dev/null && DFTRACER_HDF5=1 || DFTRACER_HDF5=0
export RUNNER_TEMP="$CI_PROJECT_DIR/tmp" && mkdir -p "$RUNNER_TEMP"
python -m pip install pybind11 ninja "setuptools>=64" "setuptools-scm>=8"
python -m pip install "pytest>=6.0" "numpy>=1.24.3" "pandas>=2.0.3"
python -m pip install -r scripts/requirements-valgrind-runners.txt
export CFLAGS="${CFLAGS:-} -g3 -fno-omit-frame-pointer"
export CXXFLAGS="${CXXFLAGS:-} -g3 -fno-omit-frame-pointer"
export DFTRACER_CMAKE_ARGS="-DHDF5_PREFER_PARALLEL=ON;-DDFTRACER_TEST_LOG_LEVEL=INFO"
export DFTRACER_PIP_NO_BUILD_ISOLATION=1
python -m pip install "git+https://github.com/argonne-lcf/dlio_benchmark.git@main"
BUILD_FLAGS="--enable-mpi"
[ "$DFTRACER_HDF5" = "1" ] && BUILD_FLAGS="$BUILD_FLAGS --enable-hdf5"
./autobuild.sh $BUILD_FLAGS
python - <<'PY'
import ctypes, ctypes.util
lib = ctypes.util.find_library("mpi")
print(f"ctypes found MPI library: {lib}")
if not lib:
    raise SystemExit("MPI runtime library not found")
ctypes.CDLL(lib)
print("MPI runtime load check passed")
PY
# generate data (dftracer disabled)
DFTRACER_ENABLE=0 DFTRACER_INC_METADATA=0 python3 scripts/dlio_non_valgrind_runner.py \
  --workload "${WORKLOAD}" \
  --phase generate \
  --dftracer-enable 0 \
  --log-dir "$RUNNER_TEMP/dftracer-dlio-generate/${WORKLOAD}/logs" \
  --run-root "$RUNNER_TEMP/dftracer-dlio-shared/${WORKLOAD}/run" \
  --summary-json "$RUNNER_TEMP/dftracer-dlio-generate/${WORKLOAD}/logs/summary.json" \
  --log-level INFO \
  --gdb-log-level DEBUG
# train phase under valgrind, then without valgrind if it passed
export DFTRACER_ENABLE=1 DFTRACER_INC_METADATA=1
VALGRIND_RC=0
python3 scripts/valgrind_dlio_runner.py \
  --workload "${WORKLOAD}" \
  --phase train \
  --no-clean-run-root \
  --timeout "${VALGRIND_DLIO_TIMEOUT}" \
  --log-dir "$RUNNER_TEMP/dftracer-dlio-valgrind/${WORKLOAD}/logs" \
  --run-root "$RUNNER_TEMP/dftracer-dlio-shared/${WORKLOAD}/run" \
  --summary-json "$RUNNER_TEMP/dftracer-dlio-valgrind/${WORKLOAD}/logs/summary.json" \
  --suppression "$PWD/test/valgrind/test_cpp_known_syscall.supp" \
  --focus-dftracer-leaks-only \
  --valgrind-track-origins no \
  --valgrind-num-callers 20 \
  --log-level INFO \
  --gdb-log-level DEBUG || VALGRIND_RC=1
cat "$RUNNER_TEMP/dftracer-dlio-valgrind/${WORKLOAD}/logs/summary.json" || true
if [ "$VALGRIND_RC" -eq 0 ]; then
  python3 scripts/dlio_non_valgrind_runner.py \
    --workload "${WORKLOAD}" \
    --phase train \
    --no-clean-run-root \
    --log-dir "$RUNNER_TEMP/dftracer-dlio-non-valgrind/${WORKLOAD}/logs" \
    --run-root "$RUNNER_TEMP/dftracer-dlio-shared/${WORKLOAD}/run" \
    --summary-json "$RUNNER_TEMP/dftracer-dlio-non-valgrind/${WORKLOAD}/logs/summary.json" \
    --log-level INFO \
    --gdb-log-level DEBUG
  cat "$RUNNER_TEMP/dftracer-dlio-non-valgrind/${WORKLOAD}/logs/summary.json" || true
fi
if [ "$VALGRIND_RC" -ne 0 ]; then
  echo "DLIO valgrind detected command failures or dftracer leak issues"; exit 1
fi
