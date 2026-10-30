#!/usr/bin/env bash
# Fail if a binary has a relative RPATH/RUNPATH entry: those resolve against the
# working directory of whoever starts it, not against the binary.
#   check_rpath.sh <binary>
set -euo pipefail
rpath=$(readelf -d "$1" | sed -n 's/.*\(RPATH\|RUNPATH\).*\[\(.*\)\].*/\2/p')
status=0
IFS=:
for entry in $rpath; do
  case "$entry" in
    /*|'$ORIGIN'*) ;;
    *) echo "relative rpath entry in $1: $entry" >&2; status=1 ;;
  esac
done
exit "$status"
