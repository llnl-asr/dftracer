#!/bin/bash
# Whole-node measurement of producer/consumer mode against the in-process path.
# Run on an exclusive compute node.
#
#   A. One process, 1..64 threads: per-call cost of each mode (run_c_overhead.sh).
#   B. 64 single-threaded processes, one per core, the service on the remaining
#      cores: per-call cost, producer drops, and how the service keeps up --
#      drain threads (mean / peak), service CPU, and how long it needs after
#      the applications end -- at fixed per-process rates and unpaced.
#
# Usage: run_pc_node.sh <dftracer build or install prefix> <deps lib dir|-> <out dir>
# Needs C_OVERHEAD_BASE and C_OVERHEAD_DFT (prebuilt benchmark binaries).
# Knobs (env): PROCS (64), APP_CPUS (0-63), SERVICE_CPUS (64-95), REPS (5),
#   RATES ("10000 100000 0", calls/s per process, 0 = unpaced), SECONDS_PER_RUN
#   (5), MAX_OPS (500000), SKIP_A (0).
set -euo pipefail

PREFIX=${1:?dftracer build or install prefix}
DEPS_LIB=${2:?deps lib dir or -}
OUT=${3:?output dir}
[ "$DEPS_LIB" = "-" ] && DEPS_LIB=""
HERE=$(cd "$(dirname "$0")" && pwd)
: "${C_OVERHEAD_BASE:?prebuilt c_overhead without dftracer}"
: "${C_OVERHEAD_DFT:?prebuilt c_overhead with dftracer}"
PROCS=${PROCS:-64}
APP_CPUS=${APP_CPUS:-0-63}
SERVICE_CPUS=${SERVICE_CPUS:-64-95}
REPS=${REPS:-5}
RATES=${RATES:-"10000 100000 0"}
SECONDS_PER_RUN=${SECONDS_PER_RUN:-5}
MAX_OPS=${MAX_OPS:-500000}
SKIP_A=${SKIP_A:-0}
LIB=${DFTRACER_LIB_DIR:-$PREFIX/lib64}
SERVICE=${DFTRACER_SERVICE:-$PREFIX/bin/dftracer_service}
export LD_LIBRARY_PATH=$LIB${DEPS_LIB:+:$DEPS_LIB}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

mkdir -p "$OUT"
{
  echo "# $(date -Is) $(hostname) nproc=$(nproc)"
  lscpu | grep -E "Model name|^CPU\(s\)|NUMA node\(s\)|Thread\(s\) per core" || true
  uptime
} > "$OUT/node.txt"

# ---- A: threads in one process.
if [ "$SKIP_A" != "1" ]; then
  THREADS=${THREADS:-"1 2 4 8 16 32 64"} OPS=${OPS_A:-100000} REPS=$REPS \
    META="0 1" FIRST_CPU=0 \
    "$HERE/run_c_overhead.sh" "$PREFIX" "${DEPS_LIB:--}" "$OUT/threads"
fi

# ---- B: processes.
B=$OUT/procs
mkdir -p "$B"
CSV=$B/procs.csv
echo "rep,rate,mode,procs,ops_per_proc,ns_per_call_p50,ns_per_call_p99,ns_per_call_max,agg_Mcalls_per_s,run_s,drain_s,drops,drain_threads_mean,drain_threads_peak,service_cpu_s,taken_over" > "$CSV"
export DFTRACER_SERVICE_SHM=/dftracer-node-$$
COMMON="DFTRACER_ENABLE=1 DFTRACER_INIT=FUNCTION DFTRACER_DATA_DIR=all"

start_service() {  # <aggregation 0|1> <dir>
  env DFTRACER_ENABLE=1 DFTRACER_ENABLE_PC_MODE=1 DFTRACER_ENABLE_AGGREGATION=$1 \
    DFTRACER_LOG_FILE="$2/node" taskset -c "$SERVICE_CPUS" \
    "$SERVICE" start >/dev/null
  for _ in $(seq 100); do
    [ -s "/dev/shm$DFTRACER_SERVICE_SHM" ] && break
    sleep 0.1
  done
  sleep 0.5
}
stop_service() {  # <dir>; prints seconds until the segment is gone
  local t0 t1
  t0=$(date +%s.%N)
  DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$1/node" "$SERVICE" stop >/dev/null || true
  for _ in $(seq 6000); do
    [ -e "/dev/shm$DFTRACER_SERVICE_SHM" ] || break
    sleep 0.05
  done
  t1=$(date +%s.%N)
  echo "$t1 - $t0" | bc
}

run_procs() {  # <rep> <rate> <mode>
  local rep=$1 rate=$2 mode=$3 ops dir agg pc drain=0 t0 t1
  if [ "$rate" = "0" ]; then ops=$MAX_OPS; else ops=$((rate * SECONDS_PER_RUN)); fi
  [ "$ops" -gt "$MAX_OPS" ] && ops=$MAX_OPS
  dir=$B/run_${rep}_${rate}_${mode}
  rm -rf "$dir" && mkdir -p "$dir/traces"
  case $mode in
    baseline) bin=$C_OVERHEAD_BASE; envs="" ;;
    orig_noagg) bin=$C_OVERHEAD_DFT; envs="$COMMON DFTRACER_ENABLE_AGGREGATION=0" ;;
    orig_agg) bin=$C_OVERHEAD_DFT; envs="$COMMON DFTRACER_ENABLE_AGGREGATION=1" ;;
    pc_noagg) bin=$C_OVERHEAD_DFT; envs="$COMMON DFTRACER_ENABLE_PC_MODE=1"; agg=0 ;;
    pc_agg) bin=$C_OVERHEAD_DFT; envs="$COMMON DFTRACER_ENABLE_PC_MODE=1"; agg=1 ;;
  esac
  [[ $mode == pc_* ]] && start_service "$agg" "$dir"
  local first=${APP_CPUS%-*}
  t0=$(date +%s.%N)
  for p in $(seq 0 $((PROCS - 1))); do
    # In PC mode the same log_file as the service: where a producer spills.
    local log="$dir/traces/app"
    [[ $mode == pc_* ]] && log="$dir/node"
    env $envs DFTRACER_LOG_FILE="$log" \
      "$bin" --threads=1 --ops="$ops" --warmup=1000 --meta=1 --rate="$rate" \
      --first-cpu=$((first + p)) --label=p$p >"$dir/p$p.csv" 2>"$dir/p$p.err" &
  done
  wait
  t1=$(date +%s.%N)
  [[ $mode == pc_* ]] && drain=$(stop_service "$dir")
  python3 - "$dir" "$rep" "$rate" "$mode" "$PROCS" "$ops" "$(echo "$t1 - $t0" | bc)" "$drain" >> "$CSV" <<'PY'
import glob, re, statistics, sys
d, rep, rate, mode, procs, ops, run_s, drain_s = sys.argv[1:9]
ns, mops = [], 0.0
for f in glob.glob(f"{d}/p*.csv"):
    for line in open(f):
        parts = line.strip().split(",")
        if len(parts) >= 7 and parts[0].startswith("p"):
            ns.append(float(parts[4])); mops += float(parts[6])
ns.sort()
drops = 0
for f in glob.glob(f"{d}/p*.err"):
    for m in re.finditer(r"(\d+) events dropped", open(f).read()):
        drops += int(m.group(1))
mean = peak = cpu = taken = ""
for f in glob.glob(f"{d}/node_*.log"):
    m = re.search(r"\((\d+) taken over.*mean ([\d.]+), peak (\d+), ([\d.]+) cpu s", open(f).read())
    if m:
        taken, mean, peak, cpu = m.group(1), m.group(2), m.group(3), m.group(4)
q = lambda p: ns[min(len(ns) - 1, int(p * len(ns)))] if ns else 0
print(f"{rep},{rate},{mode},{len(ns)},{ops},{q(0.5):.1f},{q(0.99):.1f},"
      f"{ns[-1] if ns else 0:.1f},{mops:.3f},{float(run_s):.2f},{drain_s},{drops},"
      f"{mean},{peak},{cpu},{taken}")
PY
  rm -rf "$dir/traces"
}

for rep in $(seq "$REPS"); do
  for rate in $RATES; do
    # Interleaved, so drift on the node affects every mode alike.
    for mode in baseline orig_noagg orig_agg pc_noagg pc_agg; do
      run_procs "$rep" "$rate" "$mode"
      tail -1 "$CSV"
    done
  done
done
python3 - "$CSV" > "$B/summary.csv" <<'PY'
import collections, csv, statistics, sys
rows = collections.defaultdict(list)
for r in csv.DictReader(open(sys.argv[1])):
    rows[(int(r["rate"]), r["mode"])].append(r)
out = csv.writer(sys.stdout)
out.writerow(["rate_per_proc", "mode", "reps", "ns_per_call_p50", "added_ns",
              "ns_per_call_p99", "agg_Mcalls_per_s", "drain_s", "drops",
              "drain_threads_mean", "drain_threads_peak", "service_cpu_s"])
med = lambda rs, k: statistics.median(float(r[k]) for r in rs if r[k] != "") \
    if any(r[k] != "" for r in rs) else ""
for rate in sorted({k[0] for k in rows}, key=lambda x: (x == 0, x)):
    base = med(rows.get((rate, "baseline"), []), "ns_per_call_p50") or 0
    for mode in ["baseline", "orig_noagg", "orig_agg", "pc_noagg", "pc_agg"]:
        rs = rows.get((rate, mode))
        if not rs:
            continue
        p50 = med(rs, "ns_per_call_p50")
        fmt = lambda v: f"{v:.2f}" if isinstance(v, float) else v
        out.writerow([rate or "unpaced", mode, len(rs), fmt(p50), fmt(p50 - base),
                      fmt(med(rs, "ns_per_call_p99")), fmt(med(rs, "agg_Mcalls_per_s")),
                      fmt(med(rs, "drain_s")), sum(int(r["drops"]) for r in rs),
                      fmt(med(rs, "drain_threads_mean")),
                      fmt(med(rs, "drain_threads_peak")), fmt(med(rs, "service_cpu_s"))])
PY
column -t -s, "$B/summary.csv"
echo "results: $OUT/threads/summary.csv $B/summary.csv"
