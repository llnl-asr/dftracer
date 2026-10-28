#!/bin/bash
# Ring bandwidth vs. node memory bandwidth (STREAM), on the node it runs on.
#
#   run_ring_bw.sh <work_dir>
#
# Builds McCalpin's STREAM (fetched at a pinned checksum) and ring_bw, then for
# every producer count runs STREAM with the same number of threads and the same
# core placement, followed by ring_bw (REPS replicates per configuration).
# Writes <work_dir>/stream.csv, <work_dir>/ring.csv and a summary of ring
# bandwidth as a fraction of STREAM Copy (<work_dir>/summary.csv).
#
# Run it on a compute node, not a login node. Knobs (environment):
#   PRODUCERS   producer/thread counts     (default "1 2 4 8 16 23")
#   RECORDS     payload bytes per record   (default "64 256 1024 4096 16384")
#   MODES       ring_bw modes              (default "produce consume both")
#   LAUNCH      threads | processes        (default threads)
#   RING_MIB    ring size, MiB             (default 4096: well past every cache level)
#   BLOCK_KIB   block size, KiB            (default 64)
#   WINDOW_S    measurement window, s      (default 3)
#   REPS        replicates                 (default 5)
#   FIRST_CPU   first core used            (default 0)
#   NONTEMPORAL 1 = streaming-store payload copy (default 0)
#   CPP_LOGGER_INCLUDE  cpp-logger include dir (default: sibling checkout)
#   STREAM_ARRAY_SIZE  elements per STREAM array (default: >= 4x LLC, >= 2^28)
set -euo pipefail

WORK=${1:?usage: run_ring_bw.sh <work_dir>}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC_ROOT=$(cd "$HERE/../../.." && pwd)
CXX=${CXX:-g++}
CC=${CC:-gcc}

PRODUCERS=${PRODUCERS:-"1 2 4 8 16 23"}
RECORDS=${RECORDS:-"64 256 1024 4096 16384"}
MODES=${MODES:-"produce consume both"}
LAUNCH=${LAUNCH:-threads}
RING_MIB=${RING_MIB:-1024}
BLOCK_KIB=${BLOCK_KIB:-1024}
WINDOW_S=${WINDOW_S:-3}
REPS=${REPS:-5}
FIRST_CPU=${FIRST_CPU:-0}
NONTEMPORAL=${NONTEMPORAL:-0}

STREAM_URL=https://www.cs.virginia.edu/stream/FTP/Code/stream.c
STREAM_SHA256=a52bae5e175bea3f7832112af9c085adab47117f7d2ce219165379849231692b  # v5.10

mkdir -p "$WORK"
cd "$WORK"

# --- STREAM ------------------------------------------------------------------
if [ ! -f stream.c ]; then
  curl -sSfL -o stream.c.tmp "$STREAM_URL"
  mv stream.c.tmp stream.c
fi
echo "$STREAM_SHA256  stream.c" | sha256sum -c --quiet - || {
  echo "stream.c checksum mismatch; refusing to use it" >&2
  exit 1
}

# Distinct last-level caches the kernel reports (one line per L3 instance).
llc_bytes=0
while read -r size; do
  llc_bytes=$(( llc_bytes + ${size%K} * 1024 ))
done < <(for idx in /sys/devices/system/cpu/cpu*/cache/index3; do
           [ -r "$idx/size" ] && echo "$(cat "$idx/shared_cpu_list") $(cat "$idx/size")"
         done | sort -u | awk '{print $2}')
if [ -z "${STREAM_ARRAY_SIZE:-}" ]; then
  # STREAM's rule is arrays of at least 4x the LLC. Floored at 2^28 doubles
  # (2 GiB per array) because memory-side caches (e.g. MI300A's Infinity
  # Cache) are not listed in sysfs.
  STREAM_ARRAY_SIZE=$(( llc_bytes * 4 / 8 ))
  (( STREAM_ARRAY_SIZE < 268435456 )) && STREAM_ARRAY_SIZE=268435456
fi
echo "LLC total: $(( llc_bytes >> 20 )) MiB; STREAM_ARRAY_SIZE=$STREAM_ARRAY_SIZE"

"$CC" -O3 -march=native -fopenmp -mcmodel=medium \
  -DSTREAM_ARRAY_SIZE="$STREAM_ARRAY_SIZE" -DNTIMES=20 stream.c -o stream

# --- ring_bw -----------------------------------------------------------------
# The ring headers include dftracer's common headers, which need cpp-logger's.
CPP_LOGGER_INCLUDE=${CPP_LOGGER_INCLUDE:-$SRC_ROOT/../cpp-logger/include}
[ -f "$CPP_LOGGER_INCLUDE/cpp-logger/logger.h" ] || {
  echo "cpp-logger headers not found; set CPP_LOGGER_INCLUDE" >&2
  exit 1
}
"$CXX" -std=c++17 -O3 -march=native -I"$SRC_ROOT/src" -I"$SRC_ROOT/include" \
  -I"$CPP_LOGGER_INCLUDE" "$HERE/ring_bw.cpp" -o ring_bw -pthread -lrt

{
  echo "# host=$(hostname) date=$(date -Is) cpu=$(lscpu | sed -n 's/^Model name: *//p' | head -1)"
  echo "# ring_mib=$RING_MIB block_kib=$BLOCK_KIB seconds=$WINDOW_S reps=$REPS launch=$LAUNCH nontemporal=$NONTEMPORAL stream_array_size=$STREAM_ARRAY_SIZE llc_mib=$(( llc_bytes >> 20 ))"
} > meta.txt
cat meta.txt

echo "threads,rep,copy_GBps,scale_GBps,add_GBps,triad_GBps" > stream.csv
first=1
: > ring.csv

for p in $PRODUCERS; do
  last=$(( FIRST_CPU + p - 1 ))
  for rep in $(seq "$REPS"); do
    out=$(OMP_NUM_THREADS=$p OMP_PROC_BIND=close OMP_PLACES="{$FIRST_CPU}:$p" ./stream)
    rates=$(echo "$out" | awk '/^(Copy|Scale|Add|Triad):/ {printf ",%.3f", $2/1000}')
    echo "$p,$rep$rates" >> stream.csv
  done
  for mode in $MODES; do
    for rec in $RECORDS; do
      for rep in $(seq "$REPS"); do
        args=(--mode="$mode" --launch="$LAUNCH" --producers="$p" --record="$rec"
              --ring-mib="$RING_MIB" --block-kib="$BLOCK_KIB" --seconds="$WINDOW_S"
              --first-cpu="$FIRST_CPU" --consumer-cpu=$(( last + 1 )))
        [ "$NONTEMPORAL" = 1 ] && args+=(--nontemporal)
        if [ $first = 1 ]; then args+=(--header); first=0; fi
        ./ring_bw "${args[@]}" | sed "s/^/$rep,/;s/^$rep,mode,/rep,mode,/" >> ring.csv
      done
    done
  done
  echo "done: $p producer(s)"
done

python3 -I "$HERE/summarize.py" stream.csv ring.csv | tee summary.csv
