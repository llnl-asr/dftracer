#!/bin/bash
# Can the producer/consumer service keep up? Event duration x ring size, on one
# exclusive node.
#
# PROCS single-threaded processes (one per core of APP_CPUS) log events back to
# back for SECONDS_PER_RUN; each event lasts WORK_NS (busy work inside the
# traced region), so a process logs about 1 / (WORK_NS + tracing cost) events
# per second. Per WORK_NS, the same run happens without dftracer (baseline),
# in-process (local) and through dftracer_service (on SERVICE_CPUS, no
# aggregation) with each ring size. Per run:
#   ns_per_event   median over processes of the time per event (incl. WORK_NS);
#                  overhead = this minus the baseline's, in the summary
#   produced/s     events the processes logged per second (node)
#   consumed/s     events the service wrote / (run + time to drain after it)
#   drain_s        how long the service needed after the processes ended
#   consumers      mean / peak drain threads the service ran
#   spilled        events producers could not fit in the ring and wrote to their
#                  own spill files instead (<log_file>-<hash>-spill.pfw.gz)
#   dropped        events in neither (only if a spill file cannot be written)
#
# Usage: run_pc_ring.sh <prefix with bin/ and lib64/> <out dir>
# Knobs (env): PROCS (96), APP_CPUS (0-95), SERVICE_CPUS (96-111), REPS (5),
#   WORKS ("100 1000 10000 100000 1000000" ns), RINGS_MB ("512 1024 2048 4096";
#   1 MiB blocks), SECONDS_PER_RUN (5), PAYLOAD (160: 256 B records),
#   SCRATCH (node-local trace dir, default /tmp), DEADLINE_S (3000: stop
#   starting new reps after this).
set -uo pipefail

PREFIX=$(realpath "${1:?prefix with bin/ and lib64/}")
OUT=${2:?output dir}
mkdir -p "$OUT" && OUT=$(realpath "$OUT")
PROCS=${PROCS:-96}
APP_CPUS=${APP_CPUS:-0-95}
SERVICE_CPUS=${SERVICE_CPUS:-96-111}
REPS=${REPS:-5}
WORKS=${WORKS:-"100 1000 10000 100000 1000000"}
RINGS_MB=${RINGS_MB-"512 1024 2048 4096"}  # "" runs no PC mode (a build without it)
SECONDS_PER_RUN=${SECONDS_PER_RUN:-5}
PAYLOAD=${PAYLOAD:-160}
WARMUP=1000
SCRATCH=${SCRATCH:-/tmp}/dft-ring-$$
DEADLINE_S=${DEADLINE_S:-3000}
BASE=$PREFIX/bin/c_overhead_base
DFT=$PREFIX/bin/c_overhead_dft
SERVICE=$PREFIX/bin/dftracer_service
export DFTRACER_SERVICE_SHM=/dftracer-ring-$$
FIRST=${APP_CPUS%-*}
T_START=$(date +%s)

{
  echo "# $(date -Is) $(hostname) nproc=$(nproc)"
  echo "# procs=$PROCS app_cpus=$APP_CPUS service_cpus=$SERVICE_CPUS reps=$REPS"
  echo "# works_ns=$WORKS rings_mb=$RINGS_MB seconds=$SECONDS_PER_RUN payload=$PAYLOAD"
  lscpu | grep -E "Model name|^CPU\(s\)|NUMA node\(s\)" || true
  df -h /dev/shm "${SCRATCH%/*}" | tail -2
  uptime
} > "$OUT/node.txt"
CSV=$OUT/runs.csv
echo "rep,work_ns,mode,ring_mb,procs,run_s,produced,produced_per_s,ns_per_event_p50,ns_per_event_max,spilled,dropped,written,consumed_per_s,drain_s,consumers_mean,consumers_peak,service_cpu_s,ring_bytes" > "$CSV"

run() {  # <rep> <work_ns> <mode: base|local|pc> <ring_mb>
  local rep=$1 work=$2 mode=$3 ring=$4 dir t0 t1 t2 bin spid=0
  dir=$SCRATCH/r${rep}_w${work}_${mode}_${ring}
  rm -rf "$dir" && mkdir -p "$dir/traces"
  local envs=(DFTRACER_ENABLE=1 DFTRACER_INIT=FUNCTION DFTRACER_DATA_DIR=all
              DFTRACER_ENABLE_AGGREGATION=0 DFTRACER_LOG_FILE="$dir/traces/app")
  case $mode in
    base) bin=$BASE; envs=() ;;
    local) bin=$DFT ;;
    # Same log_file as the service: where a producer spills.
    pc) bin=$DFT; envs+=(DFTRACER_ENABLE_PC_MODE=1 DFTRACER_LOG_FILE="$dir/node") ;;
  esac
  if [ "$mode" = pc ]; then
    env DFTRACER_ENABLE=1 DFTRACER_ENABLE_PC_MODE=1 DFTRACER_ENABLE_AGGREGATION=0 \
      DFTRACER_PC_BLOCKS="$ring" DFTRACER_LOG_FILE="$dir/node" \
      taskset -c "$SERVICE_CPUS" "$SERVICE" start >"$dir/start.out" 2>&1
    spid=$(cat "$dir"/node_*.pid 2>/dev/null || echo 0)
    stat -c %s "/dev/shm$DFTRACER_SERVICE_SHM" >"$dir/ring_bytes" 2>/dev/null
  fi
  t0=$(date +%s.%N)
  for p in $(seq 0 $((PROCS - 1))); do
    env "${envs[@]}" "$bin" --threads=1 --seconds="$SECONDS_PER_RUN" --warmup="$WARMUP" \
      --meta=1 --payload="$PAYLOAD" --work-ns="$work" --first-cpu=$((FIRST + p)) \
      --label=p$p >"$dir/p$p.csv" 2>"$dir/p$p.err" &
  done
  wait
  t1=$(date +%s.%N)
  if [ "$mode" = pc ]; then
    DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$dir/node" "$SERVICE" stop >/dev/null 2>&1 || true
    for _ in $(seq 2400); do  # up to 120 s to drain
      [ -e "/dev/shm$DFTRACER_SERVICE_SHM" ] || break
      sleep 0.05
    done
    for _ in $(seq 600); do
      kill -0 "$spid" 2>/dev/null || break
      sleep 0.05
    done
  fi
  t2=$(date +%s.%N)
  python3 - "$dir" "$rep" "$work" "$mode" "$ring" "$PROCS" "$WARMUP" \
    "$(echo "$t1 - $t0" | bc)" "$(echo "$t2 - $t1" | bc)" >> "$CSV" <<'PY'
import glob, re, statistics, sys
d, rep, work, mode, ring, procs, warmup, run_s, drain_s = sys.argv[1:10]
ns, produced, rate = [], 0, 0.0
for f in glob.glob(f"{d}/p*.csv"):
    for line in open(f):
        p = line.strip().split(",")
        if len(p) >= 7 and p[0].startswith("p"):
            produced += int(p[2]); ns.append(float(p[4])); rate += float(p[6]) * 1e6
# "... N events spilled to <file>, M events dropped ..." per producer
spilled = dropped = 0
for f in glob.glob(f"{d}/p*.err"):
    text = open(f).read()
    spilled += sum(int(m) for m in re.findall(r"(\d+) events spilled", text))
    dropped += sum(int(m) for m in re.findall(r"(\d+) events dropped", text))
written = consumed = mean = peak = cpu = ring_bytes = ""
try:
    ring_bytes = open(f"{d}/ring_bytes").read().strip()
except OSError:
    pass
if mode == "pc":
    for f in glob.glob(f"{d}/node_*.log"):
        m = re.search(r"mean ([\d.]+), peak (\d+), ([\d.]+) cpu s\): data=(\d+)",
                      open(f).read())
        if m:
            mean, peak, cpu = m.group(1), m.group(2), m.group(3)
            # less the start/end and warmup events of each process
            written = int(m.group(4)) - int(procs) * (2 + int(warmup))
            consumed = f"{written / (float(run_s) + float(drain_s)):.0f}"
ns.sort()
print(f"{rep},{work},{mode},{ring if mode == 'pc' else ''},{len(ns)},{float(run_s):.2f},"
      f"{produced},{rate:.0f},{statistics.median(ns) if ns else 0:.1f},"
      f"{ns[-1] if ns else 0:.1f},{spilled},{dropped},{written},{consumed},"
      f"{float(drain_s):.2f},{mean},{peak},{cpu},{ring_bytes}")
PY
  tail -1 "$CSV"
  rm -rf "$dir"
}

for rep in $(seq "$REPS"); do
  if [ $(( $(date +%s) - T_START )) -gt "$DEADLINE_S" ]; then
    echo "deadline reached before rep $rep" | tee -a "$OUT/node.txt"
    break
  fi
  # Interleaved, so drift on the node affects every configuration alike.
  for work in $WORKS; do
    run "$rep" "$work" base 0
    run "$rep" "$work" local 0
    for ring in $RINGS_MB; do run "$rep" "$work" pc "$ring"; done
  done
done
rm -rf "$SCRATCH"

python3 "$(dirname "$0")/summarize_pc_ring.py" "$CSV" | tee "$OUT/summary.txt"
