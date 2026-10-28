#!/bin/bash
# Per-call overhead of the dftracer C API: no dftracer vs the original
# in-process path (aggregation on and off) vs producer/consumer mode
# (DFTRACER_ENABLE_PC_MODE=1, events pushed to dftracer_service).
#
# Usage: run_c_overhead.sh <dftracer build or install prefix> <deps lib dir|-> <out dir>
#   The prefix holds include/, lib64/ (or lib/) and bin/dftracer_service.
#   Overrides (env), e.g. for a pip install whose files are spread out:
#   DFTRACER_INCLUDE_DIR, DFTRACER_LIB_DIR, DFTRACER_SERVICE, and
#   C_OVERHEAD_BASE / C_OVERHEAD_DFT to use prebuilt benchmark binaries.
# Knobs (env): THREADS, OPS, REPS, META, FIRST_CPU, CC
# Writes c_overhead.csv (every run) and summary.csv (summarize_c_overhead.py).
set -euo pipefail

PREFIX=${1:?dftracer build or install prefix}
DEPS_LIB=${2:?lib dir holding brahma, gotcha, cpp-logger, libuv (or -)}
OUT=${3:?output dir}
[ "$DEPS_LIB" = "-" ] && DEPS_LIB=""
# Absolute paths: everything below runs from inside $OUT.
PREFIX=$(realpath "$PREFIX")
[ -n "$DEPS_LIB" ] && DEPS_LIB=$(realpath "$DEPS_LIB")
mkdir -p "$OUT" && OUT=$(realpath "$OUT")
HERE=$(cd "$(dirname "$0")" && pwd)
SRC_ROOT=$(cd "$HERE/../../.." && pwd)
CC=${CC:-gcc}
THREADS=${THREADS:-"1 2 4 8 16"}
OPS=${OPS:-1000000}
REPS=${REPS:-5}
META=${META:-"0 1"}
FIRST_CPU=${FIRST_CPU:-0}
INC=${DFTRACER_INCLUDE_DIR:-$PREFIX/include}
LIB=${DFTRACER_LIB_DIR:-}
if [ -z "$LIB" ]; then
  LIB=$PREFIX/lib64
  [ -d "$LIB" ] || LIB=$PREFIX/lib
fi
SERVICE=${DFTRACER_SERVICE:-$PREFIX/bin/dftracer_service}
INC=$(realpath "$INC")
LIB=$(realpath "$LIB")
SERVICE=$(realpath "$SERVICE")

mkdir -p "$OUT/traces" "$OUT/service"
cd "$OUT"
SRC=$HERE/c_overhead.c
BASE=${C_OVERHEAD_BASE:-}
DFT=${C_OVERHEAD_DFT:-}
if [ -z "$BASE" ]; then
  $CC -O2 -o c_overhead_base "$SRC" -lpthread
  BASE=$OUT/c_overhead_base
fi
if [ -z "$DFT" ]; then
  $CC -O2 -DWITH_DFTRACER -I"$SRC_ROOT/include" -I"$INC" \
    -o c_overhead_dft "$SRC" -L"$LIB" -ldftracer_core \
    -Wl,-rpath,"$LIB${DEPS_LIB:+:$DEPS_LIB}" -lpthread
  DFT=$OUT/c_overhead_dft
fi
export LD_LIBRARY_PATH=$LIB${DEPS_LIB:+:$DEPS_LIB}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export DFTRACER_SERVICE_SHM=/dftracer-cbench-$$

COMMON="DFTRACER_ENABLE=1 DFTRACER_INIT=FUNCTION DFTRACER_DATA_DIR=all"
ARGS="--ops=$OPS --first-cpu=$FIRST_CPU"
CSV=$OUT/c_overhead.csv
echo "label,threads,ops_per_thread,meta,ns_per_op_p50,ns_per_op_max,agg_Mops,init_ms,fini_ms" > "$CSV"
{
  echo "# $(date -Is) $(hostname) nproc=$(nproc)"
  echo "# threads=$THREADS ops=$OPS reps=$REPS meta=$META first_cpu=$FIRST_CPU"
  echo "# prefix=$PREFIX lib=$LIB service=$SERVICE"
} > meta.txt

start_service() {
  env DFTRACER_ENABLE=1 DFTRACER_ENABLE_PC_MODE=1 DFTRACER_ENABLE_AGGREGATION=0 \
    DFTRACER_LOG_FILE="$OUT/service/node" \
    "$SERVICE" start >/dev/null
  # The segment exists before it is sized and formatted; wait for both.
  for _ in $(seq 100); do
    size=$(stat -c %s "/dev/shm$DFTRACER_SERVICE_SHM" 2>/dev/null || echo 0)
    [ "$size" -gt 0 ] && break
    sleep 0.1
  done
  if [ "$size" -eq 0 ]; then
    echo "dftracer_service did not come up" >&2
    cat "$OUT"/service/node_*.log >&2 || true
    exit 1
  fi
  sleep 0.5
}
stop_service() {
  DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$OUT/service/node" "$SERVICE" stop >/dev/null || true
  for _ in $(seq 600); do
    [ -e "/dev/shm$DFTRACER_SERVICE_SHM" ] || break
    sleep 0.1
  done
}

start_service
trap stop_service EXIT
for rep in $(seq "$REPS"); do
  for t in $THREADS; do
    for m in $META; do
      a="$ARGS --threads=$t --meta=$m"
      "$BASE" $a --label=baseline >> "$CSV"
      env $COMMON DFTRACER_LOG_FILE="$OUT/traces/agg" \
        "$DFT" $a --label=orig_agg >> "$CSV"
      env $COMMON DFTRACER_ENABLE_AGGREGATION=0 DFTRACER_LOG_FILE="$OUT/traces/noagg" \
        "$DFT" $a --label=orig_noagg >> "$CSV"
      env $COMMON DFTRACER_ENABLE_AGGREGATION=0 DFTRACER_ENABLE_PC_MODE=1 \
        DFTRACER_LOG_FILE="$OUT/service/node" "$DFT" $a --label=pc >> "$CSV"
      rm -f "$OUT"/traces/*.pfw.gz
    done
  done
done
stop_service
trap - EXIT
grep -h "drained" "$OUT"/service/node_*.log | tee -a meta.txt || true
python3 "$HERE/summarize_c_overhead.py" "$CSV" > summary.csv
cat summary.csv
echo "results: $CSV $OUT/summary.csv"
