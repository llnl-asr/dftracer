#!/usr/bin/env bash
#
# cibuildwheel's CIBW_REPAIR_WHEEL_COMMAND. Runs inside the container.
#
#   bash scripts/wheel/repair_wheel.sh <wheel> <dest_dir>
#
# 1. bundle_wheel.py prepare: bundle the dependency libraries and lift the native
#    executables out, because auditwheel's patchelf pass makes them unloadable.
# 2. auditwheel repair. dftracer's own libs (in the staging tree under
#    build/lib.*/) and the dependency prefix are not on the default search path,
#    so both go on LD_LIBRARY_PATH for it to resolve DT_NEEDED entries.
# 3. bundle_wheel.py finish: restore the executables and verify they start.

set -euo pipefail

wheel="$1"
dest="$2"

PREFIX="${DFTRACER_DEPS_PREFIX:-/opt/dftracer-deps}"
PROJECT_DIR="${DFTRACER_PROJECT_DIR:-/project}"
BUNDLE="$PROJECT_DIR/scripts/wheel/bundle_wheel.py"

log() { echo "[repair] $*"; }

paths="$PREFIX/lib64:$PREFIX/lib"
for d in "$PROJECT_DIR"/build/lib.*/dftracer/lib64 "$PROJECT_DIR"/build/lib.*/dftracer/lib; do
  [ -d "$d" ] && paths="$paths:$d"
done
export LD_LIBRARY_PATH="$paths:${LD_LIBRARY_PATH:-}"
log "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"

stash="$(mktemp -d)"
trap 'rm -rf "$stash"' EXIT

python "$BUNDLE" prepare "$wheel" "$stash"

auditwheel show "$wheel" || true
auditwheel repair -w "$dest" "$wheel"

repaired="$(find "$dest" -maxdepth 1 -name '*.whl' | head -1)"
[ -n "$repaired" ] || {
  log "auditwheel produced no wheel in $dest"
  exit 1
}
log "repaired wheel: $repaired"

python "$BUNDLE" finish "$repaired" "$stash"
