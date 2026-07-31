#!/bin/bash
# hdf5-mpi-trace-ci.yml -> HDF5 + MPI trace verification. NOTE: the GitHub
# spack-image matrix (mpich/openmpi x hdf5 1.12/1.14) is collapsed to the
# single corona module combination (mvapich2/2.3.7 + hdf5-parallel) —
# multi-version coverage stays on GitHub.
set -eo pipefail
cd "$CI_PROJECT_DIR"
export DFTRACER_BUILD="${DFTRACER_BUILD:-${CI_PROJECT_DIR}/dftracer-build}"
export DFTRACER_INSTALL="${DFTRACER_INSTALL:-${CI_PROJECT_DIR}/dftracer-install}"
source .gitlab/flux-ci/toolchain.sh
# HDF5 is provided by the container image (spack/apt), not LC modules.
HDF5_DIR="${HDF5:-$(dirname "$(dirname "$(command -v h5pcc || command -v h5cc)")")}"
echo "HDF5_DIR=${HDF5_DIR}"
echo "MPI wrappers: $(which mpicc) $(which mpicxx)"
./autobuild.sh \
  --install-mode cmake \
  --build-dir "${DFTRACER_BUILD}" \
  --install-prefix "${DFTRACER_INSTALL}" \
  --with-hdf5 "${HDF5_DIR}" \
  --with-c-compiler mpicc \
  --with-cxx-compiler mpicxx \
  --enable-mpi \
  --enable-tests \
  --jobs 4
VERIFY_TRACE_DIR="${DFTRACER_BUILD}/trace-verify"
VERIFY_DATA_DIR="${VERIFY_TRACE_DIR}/data"
mkdir -p "${VERIFY_DATA_DIR}"
PRELOAD_LIB=$(find "${DFTRACER_BUILD}" -name "libdftracer_preload_dbg.so" | head -1)
[ -n "${PRELOAD_LIB}" ] || { echo "ERROR: libdftracer_preload_dbg.so not found"; exit 1; }
TEST_BIN="${DFTRACER_BUILD}/bin/test_c_hdf5_mpi"
[ -x "${TEST_BIN}" ] || { echo "ERROR: test binary not found: ${TEST_BIN}"; exit 1; }
export DFTRACER_ENABLE=1 DFTRACER_INC_METADATA=1
export DFTRACER_LOG_FILE="${VERIFY_TRACE_DIR}/hdf5_mpi_verify"
export DFTRACER_DATA_DIR=/ DFTRACER_TRACE_COMPRESSION=1 DFTRACER_BIND_SIGNALS=0
export LD_LIBRARY_PATH="${HDF5_DIR}/lib:${HDF5_DIR}/lib64:${LD_LIBRARY_PATH:-}"
ulimit -c unlimited
# Preload the rank only. Exporting it would also trace mpirun/hydra and gdb,
# whose signal handlers deadlock inside the tracer.
PRELOAD_ENV=(env "LD_PRELOAD=${PRELOAD_LIB}" DFTRACER_INIT=PRELOAD)
# mvapich2's mpirun (hydra) needs no root/oversubscribe flags on corona
if command -v gdb >/dev/null 2>&1; then
  timeout 240 mpirun -np 2 --bind-to core \
    gdb -q -batch -ex "set exec-wrapper ${PRELOAD_ENV[*]}" \
    -ex run -ex "thread apply all bt full" -ex "quit" \
    --args "${TEST_BIN}" "${VERIFY_DATA_DIR}" 2>&1 | tee "${VERIFY_TRACE_DIR}/gdb.log"
  if grep -q "SIGSEGV\|Program terminated" "${VERIFY_TRACE_DIR}/gdb.log"; then
    echo "ERROR: test_c_hdf5_mpi crashed, see backtrace above"; exit 1
  fi
else
  mpirun -np 2 --bind-to core "${PRELOAD_ENV[@]}" "${TEST_BIN}" "${VERIFY_DATA_DIR}"
fi
unset DFTRACER_ENABLE DFTRACER_DATA_DIR
TRACE_FILES=$(find "${VERIFY_TRACE_DIR}" -name "*.pfw.gz" 2>/dev/null)
[ -n "${TRACE_FILES}" ] || { echo "ERROR: No .pfw.gz trace files found"; ls -la "${VERIFY_TRACE_DIR}" || true; exit 1; }
python3 - "${VERIFY_TRACE_DIR}" << 'PYEOF'
import sys, gzip, json, glob, os
trace_dir = sys.argv[1]
files = list(set(glob.glob(os.path.join(trace_dir, "*.pfw.gz")) + glob.glob(os.path.join(trace_dir, "**", "*.pfw.gz"), recursive=True)))
if not files:
    print("ERROR: no trace files found", flush=True); sys.exit(1)
TRACE_TYPE_HDF5 = 5
TRACE_TYPE_MPI = 10
hdf5_funcs, mpi_funcs = set(), set()
for path in files:
    with gzip.open(path, "rt") as f:
        for line in f:
            line = line.strip().rstrip(",")
            if not line or line in ("[", "]"): continue
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                continue
            t = ev.get("type", -1); name = ev.get("name", "")
            if t == TRACE_TYPE_HDF5: hdf5_funcs.add(name)
            elif t == TRACE_TYPE_MPI: mpi_funcs.add(name)
print(f"HDF5 functions intercepted: {sorted(hdf5_funcs)}")
print(f"MPI functions intercepted:  {sorted(mpi_funcs)}")
missing_hdf5 = {"H5Fcreate", "H5Dcreate2", "H5Dwrite", "H5Dread", "H5Fclose"} - hdf5_funcs
missing_mpi = {"MPI_File_open"} - mpi_funcs
ok = True
if missing_hdf5:
    print(f"FAIL: missing HDF5 events: {sorted(missing_hdf5)}"); ok = False
else:
    print("PASS: all required HDF5 events present")
if missing_mpi:
    print(f"FAIL: missing MPI file I/O events: {sorted(missing_mpi)}"); ok = False
else:
    print("PASS: MPI_File_open intercepted (MPIO driver active)")
sys.exit(0 if ok else 1)
PYEOF
