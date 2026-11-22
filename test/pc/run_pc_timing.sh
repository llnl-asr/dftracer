#!/bin/bash
# Check that ts and dur in a trace agree with the wall clock (timing.c,
# check_timing.py), in-process and through dftracer_service, with the TSC
# clock off, auto and forced (so CI VMs whose clock source is not tsc still
# run the counter path).
#
# Usage: run_pc_timing.sh <dftracer_service> <timing binary> <work dir>
set -u

SERVICE=${1:?dftracer_service binary}
APP=${2:?timing binary}
WORK=${3:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/service_lib.sh"
COMMON="DFTRACER_ENABLE=1 DFTRACER_INIT=FUNCTION DFTRACER_DATA_DIR=all DFTRACER_ENABLE_AGGREGATION=0"

rm -rf "$WORK"
status=0
for tsc in 0 1 2; do
  dir=$WORK/tsc$tsc
  mkdir -p "$dir/local" "$dir/service"

  echo "tsc=$tsc in-process:"
  env $COMMON DFTRACER_TSC_CLOCK=$tsc DFTRACER_LOG_FILE="$dir/local/app" \
    "$APP" "$dir/local" "$dir/local.expected" || status=1
  python3 "$HERE/check_timing.py" "$dir/local.expected" "$dir"/local/app*.pfw* \
    || status=1

  echo "tsc=$tsc producer/consumer:"
  export DFTRACER_SERVICE_SHM=/dftracer-timing-$$-$tsc
  LOG=$dir/service/app
  pc_service_start "$SERVICE" DFTRACER_ENABLE=1 DFTRACER_ENABLE_PC_MODE=1 \
    DFTRACER_ENABLE_AGGREGATION=0 DFTRACER_TSC_CLOCK=$tsc \
    DFTRACER_LOG_FILE="$LOG" >/dev/null
  env $COMMON DFTRACER_TSC_CLOCK=$tsc DFTRACER_ENABLE_PC_MODE=1 \
    DFTRACER_LOG_FILE="$LOG" "$APP" "$dir/service" "$dir/service.expected" \
    || status=1
  pc_service_stop "$SERVICE" "$LOG" 300 >/dev/null
  python3 "$HERE/check_timing.py" "$dir/service.expected" "$LOG"_*.pfw.gz \
    || status=1
done
exit $status
