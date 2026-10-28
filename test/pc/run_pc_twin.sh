#!/bin/bash
# Run one traced test command twice -- in-process, then in producer/consumer
# mode through a private dftracer_service -- and check that both runs traced
# the same events (compare_traces.py). Registered by test/CMakeLists.txt as
# pc_<test> for every test that writes a trace, with that test's environment.
#
# Usage: run_pc_twin.sh <dftracer_service> <work dir> <command> [args...]
set -u

SERVICE=${1:?dftracer_service binary}
WORK=${2:?work dir}
shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
shopt -s nullglob

# A preloaded test gets its LD_PRELOAD as DFTRACER_TWIN_LD_PRELOAD, so that
# only its command is preloaded -- not this script, the service or python.
app() {
  if [ -n "${DFTRACER_TWIN_LD_PRELOAD:-}" ]; then
    env LD_PRELOAD="$DFTRACER_TWIN_LD_PRELOAD" "$@"
  else
    "$@"
  fi
}

rm -rf "$WORK"
mkdir -p "$WORK/local" "$WORK/service"

# 0. Once untraced, so that both traced runs find the files the program
# leaves behind (an output directory that exists makes os.makedirs add a
# stat). Normally the test itself has run first, but a runner can reorder
# tests (the valgrind runners do).
DFTRACER_ENABLE=0 DFTRACER_LOG_FILE="$WORK/prime" "$@" >"$WORK/prime.out" 2>&1

# The twin's own data directory (DFTRACER_TWIN_DATA_DIR) is put back to this
# state before each traced run: a program that leaves files behind (temp
# files) would otherwise list more of them the second time.
DATA=${DFTRACER_TWIN_DATA_DIR:-}
# An exact copy, also of files the program left unreadable (test_cpp chmods
# one to S_ISUID only): readable while copying, then the modes put back on
# both trees, deepest first so that a closed directory is closed last.
copy_tree() {  # <from> <to>
  local modes
  modes=$(cd "$1" && find . -printf '%m %p\n' | tac)
  chmod -R u+rwX "$1"
  cp -a "$1" "$2"
  local dir
  for dir in "$1" "$2"; do
    (cd "$dir" && while read -r mode path; do chmod "$mode" "$path"; done \
      <<<"$modes")
  done
}
[ -n "$DATA" ] && mkdir -p "$DATA" && copy_tree "$DATA" "$WORK/data"
reset_data() {
  if [ -n "$DATA" ]; then
    chmod -R u+rwX "$DATA" && rm -rf "$DATA" && copy_tree "$WORK/data" "$DATA"
  fi
}

# 1. In-process, as the test itself runs.
reset_data
DFTRACER_ENABLE_PC_MODE=0 DFTRACER_LOG_FILE="$WORK/local/app" app "$@" \
  >"$WORK/local.out" 2>&1
rc_local=$?

# 2. Through a service of its own: same configuration (aggregation, rules,
# time metric ...), which the service applies, but never preloaded. Service
# and application share one log_file, as they would in a job: the node trace
# is <log_file>_<host>.pfw.gz, next to the service's .log and .pid, and any
# events that did not fit in the ring spill to <log_file>-<hash>-spill.pfw.gz.
export DFTRACER_SERVICE_SHM=/dftracer-twin-$$
LOG=$WORK/service/app
env -u DFTRACER_INIT DFTRACER_ENABLE=1 DFTRACER_ENABLE_PC_MODE=1 \
  DFTRACER_LOG_FILE="$LOG" "$SERVICE" start >"$WORK/service.out" 2>&1
for _ in $(seq 100); do
  [ -s "/dev/shm$DFTRACER_SERVICE_SHM" ] && break
  sleep 0.1
done
if [ ! -s "/dev/shm$DFTRACER_SERVICE_SHM" ]; then
  echo "dftracer_service did not come up"
  cat "$WORK/service.out" "$LOG"_*.log 2>/dev/null
  exit 1
fi
reset_data
DFTRACER_ENABLE_PC_MODE=1 DFTRACER_LOG_FILE="$LOG" app "$@" >"$WORK/pc.out" 2>&1
rc_pc=$?
DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$LOG" "$SERVICE" stop >>"$WORK/service.out" 2>&1
for _ in $(seq 300); do
  [ -e "/dev/shm$DFTRACER_SERVICE_SHM" ] || break
  sleep 0.1
done
grep -h "drained" "$LOG"_*.log 2>/dev/null

if [ "$rc_local" -ne "$rc_pc" ]; then
  echo "exit status differs: in-process $rc_local, producer/consumer $rc_pc"
  tail -n 20 "$WORK/local.out" "$WORK/pc.out"
  exit 1
fi
# In producer/consumer mode a process writes only spill files of its own.
own=()
for f in "$LOG"-*; do
  case $f in *-spill.pfw.gz) ;; *) own+=("$f") ;; esac
done
if [ ${#own[@]} -ne 0 ]; then
  echo "producer/consumer mode wrote a trace of its own:" "${own[@]}"
  exit 1
fi
# The node file must stay splittable: independent gzip members of whole lines.
# (Several files if it rolled over.)
for f in "$LOG"_*.pfw.gz; do
  python3 "$HERE/check_gzip_members.py" "$f" || exit 1
done
python3 "$HERE/compare_traces.py" --node "$LOG"_*.pfw.gz \
  --spill "$LOG"-*-spill.pfw.gz -- "$WORK"/local/app*
