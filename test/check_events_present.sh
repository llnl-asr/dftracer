#!/bin/bash
# Usage: check_events_present.sh <trace-glob> <name-regex>[=<count>]...
# Each regex must match at least one event "name" in the newest trace, or
# exactly <count> events when =<count> is given. Earlier runs in the same build
# directory leave their own trace files, so only the newest one is read.
files=$(ls -t $1 | head -n 1)
shift

content=""
for file in $files; do
  if [[ "$file" == *.gz ]]; then
    content+=$(zgrep -h '"name"' "$file")$'\n'
  else
    content+=$(grep -h '"name"' "$file")$'\n'
  fi
done

missing=0
for spec in "$@"; do
  name=${spec%%=*}
  count=$(grep -Ec "\"name\":\"($name)\"" <<< "$content")
  if [[ "$spec" == *=* ]]; then
    if [[ "$count" -ne "${spec##*=}" ]]; then
      echo "event $name: found $count expected ${spec##*=}"
      missing=1
    fi
  elif [[ "$count" -eq 0 ]]; then
    echo "missing event: $name"
    missing=1
  fi
done
exit $missing
