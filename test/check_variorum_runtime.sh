#!/usr/bin/env bash
# dftracer_service loads libvariorum when it starts (variorum_collector.cpp),
# so a build without Variorum still reads node power where Variorum 0.8 is.
# Runs the service for a few samples with each stand-in library and checks
# the node trace:
#   0.8 API (variorum_get_power_json)    power records, per family
#   0.7 API (variorum_get_node_power_json) none, and the service still runs
#   a library that does not exist          none, and the service still runs
#   a library that exits, crashes or hangs none, and the service still runs
#   none named, <libdir>/dftracer/variorum  power records: dftracer-compose's
#     has the 0.8 stub                     Variorum is loaded
#   the 0.7 stub named, same libdir        none: a named library wins
#   check_variorum_runtime.sh <dftracer_service> <0.8 stub> <0.7 stub> <work dir>
#     <exit stub> <crash stub> <hang stub> <libdftracer_core>
set -uo pipefail
service=${1:?service} stub08=${2:?stub} stub07=${3:?stub} work=${4:?work dir}
stub_exit=${5:?stub} stub_crash=${6:?stub} stub_hang=${7:?stub} core=${8:?core}
mkdir -p "$work"
status=0

# Prints the number of power records and their families for one run.
# $3, when given, is put first on LD_LIBRARY_PATH.
run() {
  local name=$1 library=$2 libpath=${3:-} log="$work/$1"
  rm -f "${log:?}"_*
  (cd "$work" &&
    if [ -n "$libpath" ]; then export LD_LIBRARY_PATH="$libpath${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"; fi &&
    DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$log" DFTRACER_TRACE_INTERVAL_MS=100 \
      DFTRACER_VARIORUM_LIBRARY="$library" "$service" start >/dev/null 2>&1)
  sleep 1
  (cd "$work" && DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$log" "$service" stop >/dev/null 2>&1)
  local trace
  trace=$(ls "$log"_*.pfw.gz 2>/dev/null | head -n 1)
  if [ -z "$trace" ]; then
    echo "FAIL $name: the service wrote no node trace" >&2
    status=1
    return
  fi
  local records families cpu
  # CPU telemetry is sampled whatever Variorum does: none means the service
  # did not keep running.
  cpu=$(zcat -f "$trace" | grep -c '"name":"cpu"')
  if [ "$cpu" -eq 0 ]; then
    echo "FAIL $name: the service did not keep running (no cpu records)" >&2
    status=1
    return
  fi
  records=$(zcat -f "$trace" | grep -c '"name":"power"')
  families=$(zcat -f "$trace" | grep -oE '"name":"power","cat":"[a-z]+"' |
    sed 's/.*"cat":"//; s/"//' | sort -u | tr '\n' ' ')
  echo "$name: $records power records ($families)"
  case $name in
    api-0.8 | composed)
      if [ "$records" -eq 0 ] || [ "$families" != "cpu gpu memory node " ]; then
        echo "FAIL $name: expected cpu, gpu, memory and node power records" >&2
        status=1
      fi ;;
    *)
      if [ "$records" -ne 0 ]; then
        echo "FAIL $name: expected no power records" >&2
        status=1
      fi ;;
  esac
}

run api-0.8 "$stub08"
run api-0.7 "$stub07"
run missing "$work/no-such-libvariorum.so"
run exits "$stub_exit"
run crashes "$stub_crash"
run hangs "$stub_hang"

# The service finds <libdir>/dftracer/variorum from the libdftracer_core it
# loaded, so a copy of the build's core in another directory, found first on
# LD_LIBRARY_PATH, stands in for an install that dftracer-compose built into.
libdir="$work/composed-lib"
rm -rf "$libdir"
mkdir -p "$libdir/dftracer/variorum/lib"
for lib in "$(dirname "$core")"/libdftracer_core.so*; do
  ln -s "$lib" "$libdir/$(basename "$lib")"
done
cp "$stub08" "$libdir/dftracer/variorum/lib/libvariorum.so"
run composed "" "$libdir"
run composed-named "$stub07" "$libdir"
exit "$status"
