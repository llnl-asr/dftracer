#!/bin/bash
# Event loss in producer/consumer mode against event size and event rate.
# Run on an exclusive compute node.
#
# PROCS single-threaded processes (one per core of APP_CPUS) log paced events
# of a given size through dftracer_service (on SERVICE_CPUS) for SECONDS_PER_RUN.
# Per run: events offered, events the producers dropped (in neither the ring
# nor their spill file <log_file>-<hash>-spill.pfw.gz), events the service
# wrote, how long the service needed to finish after the producers did, and
# from that the rate the
# service sustains (written / (run + drain)): a short run can end without loss
# while the 1 GiB ring is still absorbing a backlog a longer run would drop.
#
# Usage: run_pc_loss.sh <dftracer build or install prefix> <deps lib dir|-> <out dir>
# Needs C_OVERHEAD_DFT. Knobs (env): PROCS (64), APP_CPUS (0-63),
#   SERVICE_CPUS (64-95), REPS (5), AGG ("0 1"), PAYLOADS ("0 160 928 4000",
#   giving records of 96 B, 256 B, 1 KiB, 4 KiB), RATES (per process, events/s:
#   "1000 2000 5000 10000 20000 50000 100000"), SECONDS_PER_RUN (4).
set -euo pipefail

PREFIX=$(realpath "${1:?dftracer build or install prefix}")
DEPS_LIB=${2:?deps lib dir or -}
[ "$DEPS_LIB" = "-" ] && DEPS_LIB="" || DEPS_LIB=$(realpath "$DEPS_LIB")
OUT=${3:?output dir}
mkdir -p "$OUT" && OUT=$(realpath "$OUT")
: "${C_OVERHEAD_DFT:?prebuilt c_overhead with dftracer}"
PROCS=${PROCS:-64}
APP_CPUS=${APP_CPUS:-0-63}
SERVICE_CPUS=${SERVICE_CPUS:-64-95}
REPS=${REPS:-5}
AGG=${AGG:-"0 1"}
PAYLOADS=${PAYLOADS:-"0 160 928 4000"}
RATES=${RATES:-"1000 2000 5000 10000 20000 50000 100000"}
SECONDS_PER_RUN=${SECONDS_PER_RUN:-4}
LIB=${DFTRACER_LIB_DIR:-$PREFIX/lib64}
SERVICE=${DFTRACER_SERVICE:-$PREFIX/bin/dftracer_service}
export LD_LIBRARY_PATH=$LIB${DEPS_LIB:+:$DEPS_LIB}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export DFTRACER_SERVICE_SHM=/dftracer-loss-$$

{
  echo "# $(date -Is) $(hostname) nproc=$(nproc)"
  echo "# procs=$PROCS app_cpus=$APP_CPUS service_cpus=$SERVICE_CPUS reps=$REPS"
  echo "# agg=$AGG payloads=$PAYLOADS rates=$RATES seconds=$SECONDS_PER_RUN"
  uptime
} > "$OUT/node.txt"
CSV=$OUT/loss.csv
echo "rep,agg,record_bytes,rate_per_proc,node_rate,offered,dropped,written,lost_pct,run_s,drain_s,service_events_per_s,drain_threads_mean,drain_threads_peak,service_cpu_s" > "$CSV"

run() {  # <rep> <agg> <payload> <rate>
  local rep=$1 agg=$2 payload=$3 rate=$4 ops dir t0 t1 t2 first
  ops=$((rate * SECONDS_PER_RUN))
  # A fresh directory per run: the service may still hold files open in the
  # last one (NFS), so it is removed later, best effort.
  dir=$OUT/runs/r${rep}_a${agg}_p${payload}_r${rate}
  mkdir -p "$dir"
  env DFTRACER_ENABLE=1 DFTRACER_ENABLE_PC_MODE=1 DFTRACER_ENABLE_AGGREGATION=$agg \
    DFTRACER_LOG_FILE="$dir/node" taskset -c "$SERVICE_CPUS" \
    "$SERVICE" start >/dev/null
  for _ in $(seq 100); do
    [ -s "/dev/shm$DFTRACER_SERVICE_SHM" ] && break
    sleep 0.1
  done
  sleep 0.5
  first=${APP_CPUS%-*}
  t0=$(date +%s.%N)
  for p in $(seq 0 $((PROCS - 1))); do
    env DFTRACER_ENABLE=1 DFTRACER_INIT=FUNCTION DFTRACER_DATA_DIR=all \
      DFTRACER_ENABLE_AGGREGATION=$agg DFTRACER_ENABLE_PC_MODE=1 \
      DFTRACER_LOG_FILE="$dir/node" \
      "$C_OVERHEAD_DFT" --threads=1 --ops="$ops" --warmup=0 --meta=1 \
      --payload="$payload" --rate="$rate" --first-cpu=$((first + p)) \
      >"$dir/p$p.csv" 2>"$dir/p$p.err" &
  done
  wait
  t1=$(date +%s.%N)
  local spid
  spid=$(cat "$dir"/node_*.pid 2>/dev/null || echo 0)
  DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$dir/node" "$SERVICE" stop >/dev/null || true
  for _ in $(seq 12000); do
    [ -e "/dev/shm$DFTRACER_SERVICE_SHM" ] || break
    sleep 0.05
  done
  # and the process itself is gone before the next service starts
  for _ in $(seq 600); do
    kill -0 "$spid" 2>/dev/null || break
    sleep 0.05
  done
  t2=$(date +%s.%N)
  python3 - "$dir" "$rep" "$agg" "$payload" "$rate" "$PROCS" "$ops" \
    "$(echo "$t1 - $t0" | bc)" "$(echo "$t2 - $t1" | bc)" >> "$CSV" <<'PY'
import glob, re, sys
d, rep, agg, payload, rate, procs, ops, run_s, drain_s = sys.argv[1:10]
procs, ops, rate = int(procs), int(ops), int(rate)
record = (96 + int(payload) + 15) // 16 * 16
offered = procs * ops
dropped = 0
for f in glob.glob(f"{d}/p*.err"):
    for m in re.finditer(r"(\d+) events dropped", open(f).read()):
        dropped += int(m.group(1))
written = mean = peak = cpu = ""
for f in glob.glob(f"{d}/node_*.log"):
    text = open(f).read()
    m = re.search(r"mean ([\d.]+), peak (\d+), ([\d.]+) cpu s\): data=(\d+)", text)
    if m:
        mean, peak, cpu = m.group(1), m.group(2), m.group(3)
        # data records of the producers (warmup 0); the service's own
        # telemetry is counter records, not data.
        written = int(m.group(4)) - 2 * procs  # start + end events
sustained = (written / (float(run_s) + float(drain_s))) if written != "" else ""
print(f"{rep},{agg},{record},{rate},{rate * procs},{offered},{dropped},{written},"
      f"{100.0 * dropped / offered:.3f},{float(run_s):.2f},{float(drain_s):.2f},"
      f"{sustained:.0f},{mean},{peak},{cpu}")
PY
  rm -rf "$dir/node_"*.pfw.gz 2>/dev/null || true
}

for rep in $(seq "$REPS"); do
  for agg in $AGG; do
    for payload in $PAYLOADS; do
      for rate in $RATES; do
        run "$rep" "$agg" "$payload" "$rate"
        tail -1 "$CSV"
      done
    done
  done
done

python3 - "$CSV" > "$OUT/summary.csv" <<'PY'
import collections, csv, statistics, sys
rows = collections.defaultdict(list)
for r in csv.DictReader(open(sys.argv[1])):
    rows[(int(r["agg"]), int(r["record_bytes"]), int(r["rate_per_proc"]))].append(r)
out = csv.writer(sys.stdout)
out.writerow(["aggregation", "record_bytes", "rate_per_proc", "node_events_per_s",
              "node_MB_per_s", "reps", "lost_pct_median", "lost_pct_max",
              "drain_s_median", "service_Mevents_per_s", "drain_threads_mean",
              "drain_threads_peak"])
for (agg, size, rate), rs in sorted(rows.items()):
    lost = sorted(float(r["lost_pct"]) for r in rs)
    med = lambda k: statistics.median(float(r[k]) for r in rs if r[k] != "")
    node = int(rs[0]["node_rate"])
    out.writerow([agg, size, rate, node, f"{node * size / 1e6:.0f}", len(rs),
                  f"{statistics.median(lost):.3f}", f"{lost[-1]:.3f}",
                  f"{med('drain_s'):.2f}",
                  f"{med('service_events_per_s') / 1e6:.2f}",
                  f"{med('drain_threads_mean'):.2f}",
                  f"{max(int(r['drain_threads_peak']) for r in rs if r['drain_threads_peak'])}"])
PY
column -t -s, "$OUT/summary.csv"
