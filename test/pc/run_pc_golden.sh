#!/bin/bash
# Golden check of producer/consumer mode: run one traced program in-process and
# through dftracer_service, with aggregation off and on, and compare the events
# each wrote for it (compare_traces.py).
#
# Usage: run_pc_golden.sh <dftracer build dir> <deps lib dir> <out dir>
# Knobs (env): THREADS, OPS, CC
set -euo pipefail

BUILD=${1:?dftracer build dir}
DEPS_LIB=${2:?lib dir holding brahma, gotcha, cpp-logger, libuv}
OUT=${3:?output dir}
SRC_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CC=${CC:-gcc}
THREADS=${THREADS:-4}
OPS=${OPS:-20000}

mkdir -p "$OUT"
cd "$OUT"
$CC -O2 -DWITH_DFTRACER -I"$SRC_ROOT/include" -I"$BUILD/include" \
  -o c_overhead_dft "$SRC_ROOT/test/perf/c_overhead/c_overhead.c" \
  -L"$BUILD/lib64" -ldftracer_core -Wl,-rpath,"$BUILD/lib64:$DEPS_LIB" -lpthread
export LD_LIBRARY_PATH=$BUILD/lib64:$DEPS_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export DFTRACER_SERVICE_SHM=/dftracer-golden-$$
APP="./c_overhead_dft --threads=$THREADS --ops=$OPS --warmup=0 --meta=1 --label=golden"
COMMON="DFTRACER_ENABLE=1 DFTRACER_INIT=FUNCTION DFTRACER_DATA_DIR=all"

status=0
for agg in 0 1; do
  dir=$OUT/agg$agg
  rm -rf "$dir" && mkdir -p "$dir/local" "$dir/service"
  env $COMMON DFTRACER_ENABLE_AGGREGATION=$agg DFTRACER_LOG_FILE="$dir/local/app" \
    $APP >/dev/null
  # Aggregation is the service's to apply: it gets the same setting.
  # Service and application share one log_file.
  env DFTRACER_ENABLE=1 DFTRACER_ENABLE_PC_MODE=1 \
    DFTRACER_ENABLE_AGGREGATION=$agg DFTRACER_LOG_FILE="$dir/service/app" \
    "$BUILD/bin/dftracer_service" start >/dev/null
  for _ in $(seq 100); do
    [ -s "/dev/shm$DFTRACER_SERVICE_SHM" ] && break
    sleep 0.1
  done
  sleep 0.5
  env $COMMON DFTRACER_ENABLE_AGGREGATION=$agg DFTRACER_ENABLE_PC_MODE=1 \
    DFTRACER_LOG_FILE="$dir/service/app" $APP >/dev/null
  DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$dir/service/app" "$BUILD/bin/dftracer_service" stop >/dev/null
  for _ in $(seq 100); do
    [ -e "/dev/shm$DFTRACER_SERVICE_SHM" ] || break
    sleep 0.1
  done
  grep -h "drained" "$dir"/service/app_*.log || true
  echo "aggregation=$agg:"
  python3 "$SRC_ROOT/test/pc/check_gzip_members.py" --min-members 2 \
    "$dir"/service/app_*.pfw.gz || status=1
  python3 "$SRC_ROOT/test/pc/compare_traces.py" \
    --node "$dir"/service/app_*.pfw.gz -- "$dir"/local/app*.pfw.gz || status=1
done
exit $status
