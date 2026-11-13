#!/usr/bin/env bash
# dftracer_service loads libvariorum when it starts (variorum_collector.cpp),
# so a build without Variorum still reads node power where Variorum 0.8 is.
# Runs the service for a few samples with each stand-in library and checks
# the node trace:
#   0.8 API (variorum_get_power_json)    power records, per family
#   0.7 API (variorum_get_node_power_json) none, and the service still runs
#   a library that does not exist          none, and the service still runs
#   check_variorum_runtime.sh <dftracer_service> <0.8 stub> <0.7 stub> <work dir>
set -uo pipefail
service=${1:?service} stub08=${2:?stub} stub07=${3:?stub} work=${4:?work dir}
mkdir -p "$work"
status=0

# Prints the number of power records and their families for one run.
run() {
  local name=$1 library=$2 log="$work/$1"
  rm -f "${log:?}"_*
  (cd "$work" && DFTRACER_ENABLE=1 DFTRACER_LOG_FILE="$log" DFTRACER_TRACE_INTERVAL_MS=100 \
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
  local records families
  records=$(zcat -f "$trace" | grep -c '"name":"power"')
  families=$(zcat -f "$trace" | grep -oE '"name":"power","cat":"[a-z]+"' |
    sed 's/.*"cat":"//; s/"//' | sort -u | tr '\n' ' ')
  echo "$name: $records power records ($families)"
  case $name in
    api-0.8)
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
exit "$status"
