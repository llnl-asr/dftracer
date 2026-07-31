#!/bin/bash
# ci.yml -> build and test. NOTE: the GitHub python(3.9-3.12) x gcc(9-13)
# matrix is collapsed to the single corona module toolchain
# (gcc/11.2.1 + python/3.13.2) — version-coverage testing stays on GitHub.
# Coveralls upload, step summaries and the separate gdb-rerun step are
# GitHub-only and omitted; the clang/LLVM python-binding detection is skipped
# (no matching libclang dev tree on corona), and HDF5 support is enabled only
# if an hdf5-parallel module is available.
set -eo pipefail
cd "$CI_PROJECT_DIR"
export INSTALL_MODE=pip
source .gitlab/flux-ci/toolchain.sh
# No LC modules inside the container: HDF5 comes from the image (libhdf5-mpich-dev).
command -v h5pcc >/dev/null && DFTRACER_HDF5=1 || DFTRACER_HDF5=0
python -m pip install pybind11 ninja "setuptools>=64" "setuptools-scm>=8"
python -m pip install "pytest>=6.0" "numpy>=1.24.3" "pandas>=2.0.3"
pip install -r scripts/requirements-valgrind-runners.txt
# build with pip install mode
export CFLAGS="${CFLAGS:-} -g3 -fno-omit-frame-pointer"
export CXXFLAGS="${CXXFLAGS:-} -g3 -fno-omit-frame-pointer"
export DFTRACER_CMAKE_ARGS="-DHDF5_PREFER_PARALLEL=ON;-DDFTRACER_TEST_LOG_LEVEL=INFO"
export DFTRACER_PIP_NO_BUILD_ISOLATION=1
BUILD_FLAGS="--enable-tests --enable-mpi"
[ "$DFTRACER_HDF5" = "1" ] && BUILD_FLAGS="$BUILD_FLAGS --enable-hdf5"
./autobuild.sh $BUILD_FLAGS
pip install -r test/py/requirements.txt
# ctest (with DEBUG rerun of failures, as on GitHub)
DFTRACER_DIR=$(realpath $(find build -type d -name "dftracer.dftracer" | head -n 1))
[ -d "$DFTRACER_DIR" ] || { echo "No DFTRACER build directory found"; exit 1; }
cd "$DFTRACER_DIR"
export DFTRACER_BIND_SIGNALS=1
set +e
ctest --output-on-failure
CTEST_RC=$?
set -e
if [ "$CTEST_RC" -ne 0 ]; then
  echo "CTest failed at INFO log level; rerunning failed tests with DEBUG logging"
  LAST_FAILED_FILE="$DFTRACER_DIR/Testing/Temporary/LastTestsFailed.log"
  [ -f "$LAST_FAILED_FILE" ] || LAST_FAILED_FILE=$(find "$DFTRACER_DIR" -path "*/Testing/Temporary/LastTestsFailed.log" | head -n 1)
  if [ -n "${LAST_FAILED_FILE:-}" ] && [ -f "$LAST_FAILED_FILE" ]; then
    mapfile -t FAILED_TESTS < <(awk -F: '{print $2}' "$LAST_FAILED_FILE" | sed '/^$/d')
    TESTS_JSON=$(ctest --show-only=json-v1)
    for TEST_NAME in "${FAILED_TESTS[@]}"; do
      CMD_JSON=$(echo "$TESTS_JSON" | jq -c --arg t "$TEST_NAME" '.tests[] | select(.name==$t) | .command' | head -n 1)
      if [ -z "${CMD_JSON:-}" ] || [ "$CMD_JSON" = "null" ]; then continue; fi
      EXE=$(echo "$CMD_JSON" | jq -r '.[0]')
      mapfile -t CMD_ARGS < <(echo "$CMD_JSON" | jq -r '.[1:][]')
      export DFTRACER_LOG_LEVEL=DEBUG
      echo "Re-running with DEBUG: $EXE ${CMD_ARGS[*]}"
      "$EXE" "${CMD_ARGS[@]}" || true
    done
  fi
fi
exit "$CTEST_RC"
