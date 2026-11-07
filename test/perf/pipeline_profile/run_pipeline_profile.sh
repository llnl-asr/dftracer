#!/bin/bash
# Where dftracer's per-event overhead goes, phase by phase, for events from
# intercepted POSIX I/O and from the C API, in each mode:
#   baseline    no dftracer
#   local_agg   in-process: capture, aggregation, serialization, compression,
#               store (fwrite)
#   local_noagg the same without aggregation
#   pc          producer/consumer (DFTRACER_ENABLE_PC_MODE=1): capture,
#               serialization (binary ring record), store (copy into the ring)
#
# Needs a dftracer built with -DDFTRACER_ENABLE_PROFILING=ON: the per-phase
# numbers come from its finalize report (src/dftracer/core/common/profiler.h).
#
# Usage: run_pipeline_profile.sh <dftracer build or install prefix> <out dir>
#   The prefix holds lib64/ (or lib/), bin/dftracer_service and
#   bin/pipeline_profile_{base,dft}; override with DFTRACER_LIB_DIR,
#   DFTRACER_SERVICE, PIPELINE_PROFILE_BASE, PIPELINE_PROFILE_DFT.
# Knobs (env): THREADS ("1 4"), ITERS (100000), REPS (3), META ("0 1"),
#   POSIX (1: POSIX calls per iteration), API (1: API regions per
#   iteration), IO_BYTES (64), FIRST_CPU (0), MODES (all four above).
# Writes runs.csv (wall time of every run), profile.csv (every run's phase
# report) and summary.txt / summary.csv (summarize_pipeline_profile.py: each
# phase's count, average and ns per event, averaged over REPS).
set -euo pipefail

PREFIX=$(realpath "${1:?dftracer build or install prefix}")
OUT=${2:?output dir}
mkdir -p "$OUT" && OUT=$(realpath "$OUT")
HERE=$(cd "$(dirname "$0")" && pwd)
THREADS=${THREADS:-"1 4"}
ITERS=${ITERS:-100000}
REPS=${REPS:-3}
META=${META:-"0 1"}
POSIX=${POSIX:-1}
API=${API:-1}
IO_BYTES=${IO_BYTES:-64}
FIRST_CPU=${FIRST_CPU:-0}
MODES=${MODES:-"baseline local_agg local_noagg pc"}
LIB=${DFTRACER_LIB_DIR:-$PREFIX/lib64}
[ -d "$LIB" ] || LIB=$PREFIX/lib
SERVICE=${DFTRACER_SERVICE:-$PREFIX/bin/dftracer_service}
BASE=${PIPELINE_PROFILE_BASE:-$PREFIX/bin/pipeline_profile_base}
DFT=${PIPELINE_PROFILE_DFT:-$PREFIX/bin/pipeline_profile_dft}
for f in "$BASE" "$DFT" "$SERVICE"; do
  [ -x "$f" ] || { echo "missing $f" >&2; exit 1; }
done

export LD_LIBRARY_PATH=$LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export DFTRACER_SERVICE_SHM=/dftracer-pprof-$$
mkdir -p "$OUT/traces" "$OUT/service" "$OUT/data" "$OUT/tmp"
cd "$OUT"
COMMON="DFTRACER_ENABLE=1 DFTRACER_INIT=FUNCTION DFTRACER_DATA_DIR=all"
ARGS="--iters=$ITERS --posix=$POSIX --api=$API --io-bytes=$IO_BYTES"
ARGS="$ARGS --first-cpu=$FIRST_CPU --dir=$OUT/data"
RUNS=$OUT/runs.csv
PROFILE=$OUT/profile.csv
echo "label,threads,iters,posix,api,ns_per_iter_p50,ns_per_iter_max,ns_per_event_p50,init_ms,fini_ms,meta,rep" > "$RUNS"
echo "label,threads,meta,rep,pid,mode,phase,layer,count,total_ns,avg_ns,ns_per_event" > "$PROFILE"
{
  echo "# $(date -Is) $(hostname) nproc=$(nproc)"
  echo "# threads=$THREADS iters=$ITERS reps=$REPS meta=$META posix=$POSIX api=$API io_bytes=$IO_BYTES"
  echo "# prefix=$PREFIX modes=$MODES"
} > meta.txt

service_up=0
start_service() {
  env DFTRACER_ENABLE=1 DFTRACER_ENABLE_PC_MODE=1 DFTRACER_ENABLE_AGGREGATION=0 \
    DFTRACER_LOG_FILE="$OUT/service/node" "$SERVICE" start >/dev/null
  local size=0
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
  service_up=1
}
stop_service() {
  [ "$service_up" = 1 ] || return 0
  DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$OUT/service/node" "$SERVICE" stop >/dev/null || true
  for _ in $(seq 600); do
    [ -e "/dev/shm$DFTRACER_SERVICE_SHM" ] || break
    sleep 0.1
  done
  service_up=0
}
trap stop_service EXIT
case " $MODES " in *" pc "*) start_service ;; esac

# One traced run: its wall time to runs.csv, its phase report to profile.csv.
traced() {
  local label=$1 t=$2 m=$3 rep=$4
  shift 4
  local report=$OUT/tmp/$label.csv
  rm -f "$report"
  env $COMMON DFTRACER_INC_METADATA=$m DFTRACER_PROFILE_FILE="$report" "$@" \
    "$DFT" $ARGS --threads=$t --meta=$m --label=$label 2>>"$OUT/stderr.log" |
    sed "s/\$/,$m,$rep/" >> "$RUNS"
  if [ ! -s "$report" ]; then
    echo "no profile report from $label: is dftracer built with" \
      "-DDFTRACER_ENABLE_PROFILING=ON?" >&2
    exit 1
  fi
  sed "s/^/$label,$t,$m,$rep,/" "$report" >> "$PROFILE"
}

for rep in $(seq "$REPS"); do
  for t in $THREADS; do
    for m in $META; do
      for mode in $MODES; do
        case $mode in
          baseline)
            "$BASE" $ARGS --threads=$t --meta=$m --label=baseline |
              sed "s/\$/,$m,$rep/" >> "$RUNS" ;;
          local_agg)
            traced local_agg "$t" "$m" "$rep" DFTRACER_ENABLE_AGGREGATION=1 \
              DFTRACER_LOG_FILE="$OUT/traces/agg" ;;
          local_noagg)
            traced local_noagg "$t" "$m" "$rep" DFTRACER_ENABLE_AGGREGATION=0 \
              DFTRACER_LOG_FILE="$OUT/traces/noagg" ;;
          pc)
            traced pc "$t" "$m" "$rep" DFTRACER_ENABLE_AGGREGATION=0 \
              DFTRACER_ENABLE_PC_MODE=1 DFTRACER_LOG_FILE="$OUT/service/node" ;;
          *) echo "unknown mode $mode" >&2; exit 2 ;;
        esac
        rm -f "$OUT"/traces/*.pfw.gz
      done
    done
  done
done
stop_service
trap - EXIT
python3 "$HERE/summarize_pipeline_profile.py" "$RUNS" "$PROFILE" \
  "$OUT/summary.csv" | tee "$OUT/summary.txt"
echo "results: $RUNS $PROFILE $OUT/summary.csv $OUT/summary.txt"
