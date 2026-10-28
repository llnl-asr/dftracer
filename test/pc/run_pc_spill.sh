#!/bin/bash
# Overflow the producer/consumer ring on purpose and check that nothing is
# lost: what the producers cannot fit in the ring spills to
# <log_file>-<hash>-spill.pfw.gz, which the service appends to the node trace
# at stop and then deletes. So every event a producer logged must be in the
# node trace, and no spill file may be left. The node trace rolls over every
# 0.25 MB here, so that is checked too: every file must be whole gzip members.
#
# Usage: run_pc_spill.sh <dftracer_service> <c_overhead_dft> <work dir>
set -u

SERVICE=${1:?dftracer_service binary}
BENCH=${2:?c_overhead_dft binary}
WORK=${3:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
PROCS=4
WARMUP=1000

rm -rf "$WORK" && mkdir -p "$WORK"
export DFTRACER_SERVICE_SHM=/dftracer-spill-$$
LOG=$WORK/run
# The smallest ring, drained by one thread, against unpaced producers.
env DFTRACER_ENABLE=1 DFTRACER_ENABLE_PC_MODE=1 DFTRACER_ENABLE_AGGREGATION=0 \
  DFTRACER_PC_BLOCKS=16 DFTRACER_PC_CONSUMER_THREADS=1 \
  DFTRACER_PC_CONSUMER_MIN_THREADS=1 DFTRACER_PC_ROLLOVER_MB=0.25 \
  DFTRACER_LOG_FILE="$LOG" "$SERVICE" start || exit 1
for p in $(seq 1 $PROCS); do
  env DFTRACER_ENABLE=1 DFTRACER_INIT=FUNCTION DFTRACER_DATA_DIR=all \
    DFTRACER_ENABLE_AGGREGATION=0 DFTRACER_ENABLE_PC_MODE=1 \
    DFTRACER_LOG_FILE="$LOG" "$BENCH" --threads=1 --seconds=1 \
    --warmup=$WARMUP --meta=1 --payload=160 >"$WORK/p$p.csv" 2>"$WORK/p$p.err" &
done
wait
DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$LOG" "$SERVICE" stop >/dev/null
for _ in $(seq 600); do
  [ -e "/dev/shm$DFTRACER_SERVICE_SHM" ] || break
  sleep 0.1
done
grep -h "drained\|spill\|WARNING" "$LOG"_*.log
files=("$LOG"_*.pfw.gz)
for f in "${files[@]}"; do python3 "$HERE/check_gzip_members.py" "$f" || exit 1; done
if [ ${#files[@]} -lt 2 ]; then
  echo "the node trace did not roll over: ${files[*]}"
  exit 1
fi

python3 - "$WORK" "$WARMUP" <<'PY'
import collections, glob, gzip, json, re, sys
work, warmup = sys.argv[1], int(sys.argv[2])
# Events a producer logged: its measured ones, the warmup, start and end.
logged = sorted(int(open(f).read().split(",")[2]) + warmup + 2
                for f in glob.glob(f"{work}/p*.csv"))
found = collections.Counter()
for path in glob.glob(f"{work}/run_*.pfw.gz"):
    for line in gzip.open(path, "rt"):
        line = line.strip().rstrip(",")
        if line.startswith("{"):
            e = json.loads(line)
            if e.get("ph") == 1 and e.get("pid") not in (0, None):
                found[e["pid"]] += 1
log = "".join(open(p).read() for p in glob.glob(f"{work}/run_*.log"))
m = re.search(r"(\d+) events did not fit in the ring; (\d+) spill files", log)
spilled, appended = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
left = glob.glob(f"{work}/*-spill.pfw.gz")
print(f"logged {sum(logged)} events, found {sum(found.values())} in the node "
      f"trace; {spilled} spilled, {appended} spill files appended, "
      f"{len(left)} left")
ok = sorted(found.values()) == logged and spilled > 0 and appended > 0 and not left
print("NO EVENT LOST" if ok else "EVENTS LOST, NOTHING SPILLED OR SPILL FILES LEFT")
sys.exit(0 if ok else 1)
PY
