#!/bin/bash
# valgrind-ci.yml -> valgrind gates (system valgrind).
set -eo pipefail
cd "$CI_PROJECT_DIR"
export INSTALL_MODE=pip
source .gitlab/flux-ci/toolchain.sh
command -v valgrind && valgrind --version
module load hdf5-parallel 2>/dev/null && DFTRACER_HDF5=1 || DFTRACER_HDF5=0
python -m pip install pybind11 ninja "setuptools>=64" "setuptools-scm>=8"
python -m pip install "pytest>=6.0" "numpy>=1.24.3" "pandas>=2.0.3"
python -m pip install -r scripts/requirements-valgrind-runners.txt
export CFLAGS="${CFLAGS:-} -g3 -fno-omit-frame-pointer"
export CXXFLAGS="${CXXFLAGS:-} -g3 -fno-omit-frame-pointer"
export DFTRACER_CMAKE_ARGS="-DHDF5_PREFER_PARALLEL=ON;-DDFTRACER_TEST_LOG_LEVEL=INFO"
export DFTRACER_PIP_NO_BUILD_ISOLATION=1
BUILD_FLAGS="--enable-tests --enable-mpi"
[ "$DFTRACER_HDF5" = "1" ] && BUILD_FLAGS="$BUILD_FLAGS --enable-hdf5"
./autobuild.sh $BUILD_FLAGS
pip install -r test/py/requirements.txt
DFTRACER_DIR=$(realpath $(find build -type d -name "dftracer.dftracer" | head -n 1))
VALGRIND_RC=0
python3 scripts/valgrind_ctest_runner.py \
  --build-dir "$DFTRACER_DIR" \
  --log-dir "$DFTRACER_DIR/valgrind-ctest-ci" \
  --summary-json "$DFTRACER_DIR/valgrind-ctest-ci/summary.json" \
  --log-level INFO \
  --debug-rerun-on-failure \
  --debug-log-level DEBUG \
  --suppression "$PWD/test/valgrind/test_cpp_known_syscall.supp" || VALGRIND_RC=1
PY_VALGRIND_RC=0
PYTHONUNBUFFERED=1 python3 -u scripts/valgrind_ctest_python_runner.py \
  --build-dir "$DFTRACER_DIR" \
  --log-dir "$DFTRACER_DIR/valgrind-python-ctest-ci" \
  --summary-json "$DFTRACER_DIR/valgrind-python-ctest-ci/summary.json" \
  --log-level INFO \
  --valgrind-track-origins no \
  --valgrind-num-callers 20 \
  --fail-on-project-leaks-only \
  --project-frame-regex '(/dftracer/(src|include|python|scripts|test)/|libdftracer|dft_)' \
  --suppression "$PWD/test/valgrind/test_cpp_known_syscall.supp" || PY_VALGRIND_RC=1
cat "$DFTRACER_DIR/valgrind-ctest-ci/summary.json" || true
cat "$DFTRACER_DIR/valgrind-python-ctest-ci/summary.json" || true
python3 - "$DFTRACER_DIR" "$DFTRACER_DIR/valgrind-ctest-ci/summary.json" "$DFTRACER_DIR/valgrind-python-ctest-ci/summary.json" <<'PY'
import json, subprocess, sys
from pathlib import Path
build_dir = Path(sys.argv[1]); common_summary = Path(sys.argv[2]); python_summary = Path(sys.argv[3])
if not common_summary.is_file() or not python_summary.is_file():
  print("[valgrind-coverage] missing summary file"); sys.exit(1)
raw = subprocess.check_output(["ctest", "--show-only=json-v1"], cwd=build_dir, text=True)
ctest_total = len(json.loads(raw).get("tests", []))
common = json.loads(common_summary.read_text()); py = json.loads(python_summary.read_text())
common_selected = int(common.get("selected_tests", 0)); common_executed = int(common.get("valgrind_executed", 0))
common_skipped = int(common.get("wrapper_skipped", 0))
common_skipped_python = int(common.get("wrapper_skipped_python", common_skipped))
common_skipped_other = int(common.get("wrapper_skipped_other", max(0, common_skipped - common_skipped_python)))
py_selected = int(py.get("selected_tests", 0)); py_executed = int(py.get("valgrind_executed", 0))
errors = []
if common_selected != ctest_total: errors.append("non-python runner did not select all ctests")
if common_skipped_python != py_selected: errors.append("python-skipped != python selected")
if py_selected != py_executed: errors.append("python selected != executed")
if common_executed + py_executed + common_skipped_other != ctest_total: errors.append("combined executed != ctest total")
if errors:
  print("[valgrind-coverage] FAIL: not all CTests were executed under valgrind")
  for err in errors: print(f"[valgrind-coverage] {err}")
  sys.exit(1)
print(f"[valgrind-coverage] PASS (total={ctest_total}, non-python={common_executed}, python={py_executed}, skipped-other={common_skipped_other})")
PY
if [ "$VALGRIND_RC" -ne 0 ]; then echo "Valgrind gate detected unsuppressed issues"; exit 1; fi
if [ "$PY_VALGRIND_RC" -ne 0 ]; then echo "Python valgrind gate detected unsuppressed issues"; exit 1; fi
