#!/usr/bin/env bash
# Links libdftracer_core again from the archives a DFTracer install keeps
# (<libdir>/dftracer/compose) and checks the result:
#   all     every module: exports exactly what the library the build linked
#           does, and, with the C++ runtime from the tree, needs no libstdc++.so
#   detect  the modules whose libraries this machine has (the build machine
#           has them all): every one linked, in the variant the build made
#   none  no module: defines none of the module hooks and needs none of the
#         libraries the modules add (PAPI, hwloc, CUPTI, rocprofiler-sdk), so
#         the core really stands alone. When the core links a shared brahma
#         built with MPI and HDF5, that brahma brings them in, so they are
#         checked only when the core's brahma is linked in statically.
# The none and detect libraries then trace a small read through the build's
# preload library, standing in for the library it was built with.
#   check_compose.sh <build dir> <built libdftracer_core.so> <work dir>
set -euo pipefail
build=${1:?build dir}
built=${2:?built libdftracer_core.so}
work=${3:?work dir}
runtime_symbols="$work/runtime.symbols"

rm -rf "$work"
mkdir -p "$work"
cmake --install "$build" --prefix "$work/prefix" --component dftracer_compose >/dev/null
compose=$(find "$work/prefix" -type d -path '*/dftracer/compose' | head -n 1)
[ -n "$compose" ] || { echo "no dftracer/compose in the install" >&2; exit 1; }
if ls "$compose"/runtime/*.a >/dev/null 2>&1; then
  nm --defined-only "$compose"/runtime/*.a 2>/dev/null |
    awk 'NF >= 3 {print $3}' | sort -u >"$runtime_symbols"
else
  : >"$runtime_symbols"
fi

compose_with() {
  local name=$1 modules=$2
  shift 2
  cmake -S "$compose" -B "$work/$name" -DCMAKE_INSTALL_PREFIX="$work/$name-prefix" \
    -DDFTRACER_COMPOSE_MODULES="$modules" "$@" >"$work/$name.configure.log" 2>&1 ||
    { cat "$work/$name.configure.log" >&2; exit 1; }
  cmake --build "$work/$name" >"$work/$name.build.log" 2>&1 ||
    { cat "$work/$name.build.log" >&2; exit 1; }
  find "$work/$name" -maxdepth 1 -name 'libdftracer_core.so.*' -type f | head -n 1
}

# Exported symbols, less those of the C++ runtime: the built library
# re-exports what the toolset links statically (libstdc++_nonshared), while a
# library composed with the tree's runtime/ keeps that runtime hidden.
dynamic_symbols() {
  nm -D --defined-only "$1" | awk '{print $NF}' | sort -u |
    comm -23 - "$runtime_symbols"
}
# Module hooks the library defines (hidden, so in the static symbol table).
defined_hooks() {
  nm "$1" | awk '$2 ~ /^[TtWw]$/ && $3 ~ /^dftracer_module_/ {print $3}' | sort -u
}

status=0

all=$(compose_with all all)
if ! diff <(dynamic_symbols "$built") <(dynamic_symbols "$all") >"$work/all.diff"; then
  echo "FAIL all: composed library exports differ from the built one:" >&2
  cat "$work/all.diff" >&2
  status=1
fi
if ! diff <(defined_hooks "$built") <(defined_hooks "$all") >/dev/null; then
  echo "FAIL all: module hooks differ from the built library" >&2
  status=1
fi
# The fully static runtime must leave no libstdc++.so behind.
if [ -f "$compose/runtime/libstdc++.a" ]; then
  static=$(compose_with static-runtime all -DDFTRACER_COMPOSE_RUNTIME=static)
  if readelf -d "$static" | grep -q 'libstdc++\.so'; then
    echo "FAIL static runtime: still needs libstdc++.so" >&2
    status=1
  fi
fi
echo "all : $(defined_hooks "$all" | wc -l) module hooks, $(dynamic_symbols "$all" | wc -l) exported symbols"

detect=$(compose_with detect detect)
# A build without optional modules installs no modules/ directory; under
# pipefail a failing ls would end the script here without a message.
expected=$(find "$compose/modules" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' 2>/dev/null | sort || true)
linked=$(awk '$3 == "linked" {print $1}' "$work/detect/composition.txt" | sort)
if [ "$linked" != "$expected" ]; then
  echo "FAIL detect: linked [$(echo $linked)], expected [$(echo $expected)]:" >&2
  cat "$work/detect/composition.txt" >&2
  status=1
fi
if ! diff <(defined_hooks "$built") <(defined_hooks "$detect") >/dev/null; then
  echo "FAIL detect: module hooks differ from the built library" >&2
  status=1
fi
echo "detect: $(echo $linked)"

none=$(compose_with none none)
hooks=$(defined_hooks "$none")
if [ -n "$hooks" ]; then
  echo "FAIL none: defines module hooks:" $hooks >&2
  status=1
fi
needed=$(readelf -d "$none" | awk '/NEEDED/ {print $NF}' | tr -d '[]')
module_libs='papi|hwloc|cupti|rocprofiler-sdk'
if ! echo "$needed" | grep -q '^libbrahma\.'; then
  module_libs+='|mpi|mpi_cxx|mpich|hdf5'
fi
if echo "$needed" | grep -Eq "^lib($module_libs)\."; then
  echo "FAIL none: needs a module library:" $needed >&2
  status=1
fi
echo "none: no module hooks; needs" $needed

# Trace a read of a file in the data dir with a composed library in place of
# the built one; the open and the read must be in the trace.
preload="$(dirname "$built")/libdftracer_preload.so"
trace_with() {
  local name=$1 lib=$2 out="$work/trace-$1"
  mkdir -p "$out/data"
  echo data >"$out/data/file"
  (cd "$out" && DFTRACER_ENABLE=1 DFTRACER_INIT=PRELOAD DFTRACER_ENABLE_AGGREGATION=0 \
    DFTRACER_LOG_FILE="$out/trace" DFTRACER_DATA_DIR="$out/data" \
    LD_LIBRARY_PATH="$(dirname "$lib"):$(dirname "$built")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    LD_PRELOAD="$preload" cat "$out/data/file" >/dev/null) || {
    echo "FAIL $name: traced program failed" >&2; status=1; return; }
  local events
  events=$(zcat -f "$out"/trace*.pfw* 2>/dev/null | grep '"cat":"POSIX"' |
    grep -oE '"name":"[a-z_0-9]+"' | sort -u | tr '\n' ' ')
  if ! echo "$events" | grep -q '"open' || ! echo "$events" | grep -q '"read"'; then
    echo "FAIL $name: no open/read in the trace (POSIX events: $events)" >&2
    status=1
  else
    echo "$name: traced $events"
  fi
}
if [ -f "$preload" ]; then
  trace_with none "$none"
  trace_with detect "$detect"
fi

exit "$status"
