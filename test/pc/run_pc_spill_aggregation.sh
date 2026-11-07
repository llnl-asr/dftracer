#!/bin/bash
# Producer/consumer mode with no service: every event spills to the
# producer's own file, through a LocalBufferManager that aggregates them.
# PC mode defers the aggregation hashes of event metadata
# (Metadata::defer_key_hashes) and the spill path computes them; the
# aggregated rows must match those of the same program in-process.
#
# Usage: run_pc_spill_aggregation.sh <work dir> <command> [args...]
set -u
WORK=${1:?work dir}
shift
HERE=$(cd "$(dirname "$0")" && pwd)
shopt -s nullglob
rm -rf "$WORK"
mkdir -p "$WORK/local" "$WORK/spill"
export DFTRACER_ENABLE=1 DFTRACER_ENABLE_AGGREGATION=1 \
  DFTRACER_AGGREGATION_TYPE=FULL DFTRACER_INC_METADATA=1
DFTRACER_ENABLE_PC_MODE=0 DFTRACER_LOG_FILE="$WORK/local/app" "$@" \
  >"$WORK/local.out" 2>&1 || { cat "$WORK/local.out"; exit 1; }
# A segment no service owns: nothing is accepting, so everything spills.
DFTRACER_ENABLE_PC_MODE=1 DFTRACER_SERVICE_SHM=/dftracer-nosvc-$$ \
  DFTRACER_LOG_FILE="$WORK/spill/app" "$@" >"$WORK/spill.out" 2>&1 ||
  { cat "$WORK/spill.out"; exit 1; }
spill=("$WORK"/spill/*-spill.pfw.gz)
local_traces=("$WORK"/local/*.pfw.gz)
if [ ${#spill[@]} -eq 0 ] || [ ${#local_traces[@]} -eq 0 ]; then
  echo "missing traces: spill=${spill[*]} local=${local_traces[*]}"
  exit 1
fi
python3 "$HERE/compare_traces.py" --node "${spill[@]}" -- "${local_traces[@]}"
