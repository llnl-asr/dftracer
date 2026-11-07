#!/bin/bash
# Usage: check_aggregated_counts.sh <trace-glob> <name-regex>=<count>...
# As check_events_present.sh, but an aggregated row (ph 3) counts as the
# dft_cnt calls it holds. Only the newest trace is read.
file=$(ls -t $1 | head -n 1)
shift

missing=0
for spec in "$@"; do
  name=${spec%%=*}
  count=$(zcat -f "$file" |
    jq -R --arg re "^(${name})$" \
      'fromjson? | select(.name? and (.name | test($re)))
       | if .ph == 3 then .args.dft_cnt else 1 end' |
    awk '{ s += $1 } END { print s + 0 }')
  if [[ "$count" -ne "${spec##*=}" ]]; then
    echo "event $name: found $count expected ${spec##*=}"
    missing=1
  fi
done
exit $missing
