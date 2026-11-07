#!/usr/bin/env bash
# Fail if a shared library needs static TLS (the STATIC_TLS dynamic flag).
# Any initial-exec thread_local makes the loader put the library's whole TLS
# block in the static TLS reserve, which only a library loaded at startup is
# sure to get: one loaded later with dlopen -- Python's `import dftracer`,
# the wheel's extension modules -- fails with "cannot allocate memory in
# static TLS block".
#   check_no_static_tls.sh <library>...
set -euo pipefail
status=0
for lib in "$@"; do
  if readelf -d "$lib" | grep -q STATIC_TLS; then
    echo "$lib needs static TLS (an initial-exec thread_local?):" >&2
    readelf -d "$lib" | grep FLAGS >&2
    status=1
  fi
done
exit "$status"
