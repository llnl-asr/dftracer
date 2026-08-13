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
# PAPI counters, when the image provides the headers. Detected the same way as
# HDF5 rather than assumed, so the phase still builds on an image without it.
[ -f /usr/include/papi.h ] && DFTRACER_PAPI=1 || DFTRACER_PAPI=0
# An "if" rather than "test && assign": under set -e the latter aborts the
# phase when the test is false.
if [ "$DFTRACER_PAPI" = "1" ]; then
  BUILD_FLAGS="$BUILD_FLAGS --enable-papi"
fi
# Variorum node power. Nothing is installed, so this exercises the fetch-and-
# build path; both of variorum's own prerequisites come from the image and its
# CMake aborts rather than degrading when either is missing, so check for them
# the same way as for PAPI.
if [ -f /usr/include/hwloc.h ] && [ -f /usr/include/jansson.h ]; then
  DFTRACER_VARIORUM=1
  BUILD_FLAGS="$BUILD_FLAGS --enable-variorum"
else
  DFTRACER_VARIORUM=0
fi
./autobuild.sh $BUILD_FLAGS
pip install -r test/py/requirements.txt
# ctest (with DEBUG rerun of failures, as on GitHub)
# Match the running interpreter: build/ persists between pipelines, so a tree
# left by another python version would otherwise be picked and ctest would
# report success having registered no tests at all.
# PAPI end-to-end: run the standalone example and confirm the trace really
# carries advancing counters. check_papi_trace.py fails a frozen counter, which
# is what a broken sampler looks like -- a plain "the name appears" check would
# pass on garbage.
#
# The include and library directories are discovered rather than guessed: the
# example needs cpp-logger's headers and DFTracer's generated
# dftracer_config.hpp, and neither lives in the source tree.
if [ "$DFTRACER_PAPI" = "1" ]; then
  # Look in the install prefix first: that is where the build puts the
  # dependencies, and it avoids trawling the multi-GB venv.
  papi_find() {
    for root in "$CI_PROJECT_DIR/install" "$CI_PROJECT_DIR/build" "$CI_PROJECT_DIR"; do
      [ -d "$root" ] || continue
      found=$(find "$root" "$@" -print -quit 2>/dev/null || true)
      if [ -n "$found" ]; then echo "$found"; return; fi
    done
  }
  PAPI_LOGGER_HEADER=$(papi_find -path "*cpp-logger/logger.h")
  PAPI_CONFIG_HEADER=$(papi_find -path "*dftracer/core/dftracer_config.hpp")
  PAPI_CORE_LIB=$(papi_find -name "libdftracer_core.so")
  if [ -n "$PAPI_LOGGER_HEADER" ] && [ -n "$PAPI_CONFIG_HEADER" ] && [ -n "$PAPI_CORE_LIB" ]; then
    # <inc>/cpp-logger/logger.h -> <inc>, and likewise for the config header.
    PAPI_LOGGER_INC=$(dirname "$(dirname "$PAPI_LOGGER_HEADER")")
    PAPI_DFT_INC=$(dirname "$(dirname "$(dirname "$PAPI_CONFIG_HEADER")")")
    PAPI_LIBDIR=$(dirname "$PAPI_CORE_LIB")
    echo "PAPI e2e: dftracer include=$PAPI_DFT_INC cpp-logger include=$PAPI_LOGGER_INC lib=$PAPI_LIBDIR"
    pushd examples/papi_standalone
    make clean
    make run-single \
      DFTRACER_INCLUDEDIR="$PAPI_DFT_INC" \
      CPP_LOGGER_INCLUDEDIR="$PAPI_LOGGER_INC" \
      DFTRACER_LIBDIR="$PAPI_LIBDIR"
    # --skip-if-no-counters: the phase runs under rootless podman, whose default
    # seccomp profile blocks perf_event_open, so PAPI can produce nothing here
    # however correct DFTracer is. A trace that does carry counters is still
    # validated in full, including that they actually advance.
    python3 ../../scripts/check_papi_trace.py \
      traces/*.pfw.gz \
      --min-events 10 \
      --require-papi-event PAPI_TOT_CYC \
      --require-papi-event PAPI_TOT_INS \
      --min-papi-lines 2 \
      --skip-if-no-counters
    popd
  else
    echo "PAPI e2e skipped: could not locate cpp-logger headers, the generated"
    echo "dftracer_config.hpp or libdftracer_core.so under $CI_PROJECT_DIR"
    exit 1
  fi
fi

# Variorum end-to-end: run the service with variorum linked in and confirm the
# power records it writes are well formed. A container has no msr-safe, no
# /dev/hsmp and no ROCm, so variorum can read nothing here and the collector is
# expected to disable itself -- --skip-if-no-power accepts that, while still
# failing a trace whose power records are malformed. What this proves in CI is
# that the service starts, samples and shuts down cleanly with variorum linked;
# the grouping itself is covered by unit_test_variorum under ctest below.
if [ "$DFTRACER_VARIORUM" = "1" ]; then
  VARIORUM_SERVICE=$(find "$CI_PROJECT_DIR/build" "$CI_PROJECT_DIR/install" \
    -name dftracer_service -type f -perm -u+x 2>/dev/null | head -n 1)
  if [ -n "${VARIORUM_SERVICE:-}" ]; then
    VARIORUM_DIR=$(mktemp -d)
    echo "Variorum e2e: $VARIORUM_SERVICE -> $VARIORUM_DIR"
    DFTRACER_ENABLE=1 \
    DFTRACER_LOG_FILE="$VARIORUM_DIR/power" \
    DFTRACER_TRACE_INTERVAL_MS=200 \
      "$VARIORUM_SERVICE" start "$VARIORUM_DIR"
    sleep 2
    "$VARIORUM_SERVICE" stop "$VARIORUM_DIR"
    sleep 1
    VARIORUM_TRACE=$(find "$VARIORUM_DIR" -name "power_*.pfw*" | head -n 1)
    if [ -n "${VARIORUM_TRACE:-}" ]; then
      python3 scripts/check_variorum_trace.py "$VARIORUM_TRACE" \
        --min-events 1 \
        --require-family node \
        --skip-if-no-power
    else
      echo "Variorum e2e failed: the service wrote no trace to $VARIORUM_DIR"
      exit 1
    fi
    rm -rf "$VARIORUM_DIR"
  else
    echo "Variorum e2e skipped: dftracer_service not found under $CI_PROJECT_DIR"
    exit 1
  fi
fi

PY_ABI=cpython-$(python -c 'import sys; print(f"{sys.version_info.major}{sys.version_info.minor}")')
DFTRACER_DIR=$(realpath "$(find build -type d -name "dftracer.dftracer" -path "*${PY_ABI}*" | head -n 1)")
[ -d "$DFTRACER_DIR" ] || { echo "No DFTRACER build directory found for ${PY_ABI}"; exit 1; }
cd "$DFTRACER_DIR"
export DFTRACER_BIND_SIGNALS=1
TEST_COUNT=$(ctest --show-only=json-v1 | jq '.tests | length')
[ "${TEST_COUNT:-0}" -gt 0 ] || { echo "No tests registered in $DFTRACER_DIR"; exit 1; }
echo "Running ${TEST_COUNT} tests from ${DFTRACER_DIR}"
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
