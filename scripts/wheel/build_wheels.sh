#!/usr/bin/env bash
#
# Build manylinux wheels for DFTracer with cibuildwheel + podman, using the
# C/C++ dependency archives in dependency/source/ instead of cloning them.
# Nothing here needs access to a private git remote.
#
#   scripts/wheel/build_wheels.sh                     # 3.9 .. 3.14, glibc 2.28
#   scripts/wheel/build_wheels.sh --python 3.11,3.12  # subset
#   scripts/wheel/build_wheels.sh --glibc 2.34        # newer baseline
#   scripts/wheel/build_wheels.sh --list              # show plan and exit
#
# Options:
#   --python LIST     comma/space separated python versions (default 3.9 .. 3.14;
#                     3.9 is the floor because pydftracer requires >=3.9)
#   --glibc VERSION   manylinux glibc baseline: 2.28 (default) | 2.34 | 2.17
#                     (2.17/manylinux2014 does not build: brahma binds ::fcntl64,
#                      which glibc only declares and exports from 2.28)
#   --arch ARCH       x86_64 (default) | aarch64
#   --output DIR      wheel output directory (default <repo>/wheelhouse)
#   --free-threaded   additionally build the free-threaded (t) variants
#   --no-test         do not test the wheels (default: every wheel is installed
#                     in the container and validated by test_wheel.py)
#   --test-packaging-only
#                     run only the import/library/executable checks, skipping the
#                     end-to-end tracing checks
#   --jobs N          build N interpreters concurrently (default 1). The first
#                     build runs alone to populate the caches, the rest follow N
#                     at a time.
#   --version-scheme S  how to version a commit that is not exactly a tag:
#                     post (default)  2.1.1.post5 -- sorts after the tag, and is
#                       a final release, so plain pip install resolves to it
#                     postdev         2.1.1.post5.dev0 -- same ordering but a
#                       prerelease, so it needs pip --pre
#                     dev             2.1.1.dev5 -- what setup.py emits
#   --no-cache        do not reuse the host-side dependency prefix and ccache
#   --no-fetch        do not run fetch_deps.sh first
#   --rebuild-deps    discard the cached dependency prefix and rebuild it
#   --in-place        hand the repo itself to cibuildwheel instead of a clean
#                     staged copy of the git-tracked files (the repo carries
#                     multi-GB venv/build dirs that would be copied into every
#                     container)
#   --list            print the resolved build plan and exit
#
# Environment:
#   CIBW_VERSION           cibuildwheel version to use (default 3.2.1)
#   DFTRACER_WHEEL_STATE   local-disk dir for podman storage + venv
#                          (default ${TMPDIR:-/tmp}/$USER/dftracer-wheels)
#   DFTRACER_WHEEL_CACHE   dependency prefix + ccache reused across runs
#                          (default $DFTRACER_WHEEL_STATE/cache)
#   DFTRACER_VERSION       version stamped into the wheel
#                          (default: contents of PACKAGE_VERSION)
#   Any DFTRACER_* CMake toggle you export is NOT forwarded; edit
#   CIBW_ENVIRONMENT below if you need MPI/HDF5 enabled wheels.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

PYTHONS="3.9 3.10 3.11 3.12 3.13 3.14"
GLIBC="2.28"
ARCH="x86_64"
OUTPUT="$PROJECT_DIR/wheelhouse"
FREE_THREADED=0
RUN_TESTS="full"
DO_FETCH=1
REBUILD_DEPS=0
IN_PLACE=0
LIST_ONLY=0
JOBS=1
USE_CACHE=1
VERSION_SCHEME="${DFTRACER_VERSION_SCHEME:-post}"
CIBW_VERSION="${CIBW_VERSION:-3.2.1}"
DEPS_PREFIX="/opt/dftracer-deps"

die() {
  echo "build_wheels.sh: $*" >&2
  exit 1
}
log() { echo "==> $*"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --python)
      PYTHONS="${2//,/ }"
      shift 2
      ;;
    --glibc)
      GLIBC="$2"
      shift 2
      ;;
    --arch)
      ARCH="$2"
      shift 2
      ;;
    --output)
      OUTPUT="$2"
      shift 2
      ;;
    --free-threaded)
      FREE_THREADED=1
      shift
      ;;
    --no-test)
      RUN_TESTS="none"
      shift
      ;;
    --test-packaging-only)
      RUN_TESTS="packaging"
      shift
      ;;
    --jobs)
      JOBS="$2"
      shift 2
      ;;
    --version-scheme)
      VERSION_SCHEME="$2"
      shift 2
      ;;
    --no-cache)
      USE_CACHE=0
      shift
      ;;
    --no-fetch)
      DO_FETCH=0
      shift
      ;;
    --rebuild-deps)
      REBUILD_DEPS=1
      shift
      ;;
    --in-place)
      IN_PLACE=1
      shift
      ;;
    --list)
      LIST_ONLY=1
      shift
      ;;
    -h | --help)
      sed -n '2,42p' "${BASH_SOURCE[0]}" | sed 's/^#\s\?//'
      exit 0
      ;;
    *) die "unknown argument '$1'" ;;
  esac
done

# Verified image contents (2026-07):
#   manylinux2014_x86_64:latest     glibc 2.17  gcc 10.2  cp39-cp315
#   manylinux_2_28_x86_64:latest    glibc 2.28  gcc 14.2  cp39-cp315
#   manylinux_2_34_x86_64:latest    glibc 2.34            cp39+
case "$GLIBC" in
  2.17)
    # brahma takes the address of ::fcntl64, which glibc did not declare or
    # export before 2.28, so the dependency build fails on manylinux2014.
    log "WARNING: glibc 2.17 (manylinux2014) is expected to fail building brahma (::fcntl64)"
    IMAGE_MAIN="quay.io/pypa/manylinux2014_${ARCH}:latest"
    ;;
  2.28)
    IMAGE_MAIN="quay.io/pypa/manylinux_2_28_${ARCH}:latest"
    ;;
  2.34)
    IMAGE_MAIN="quay.io/pypa/manylinux_2_34_${ARCH}:latest"
    ;;
  *) die "unsupported --glibc '$GLIBC' (use 2.28 or 2.34)" ;;
esac

builds_main=""
for py in $PYTHONS; do
  case "$py" in
    3.9 | 3.1[0-9]) ;;
    3.[0-8])
      die "python $py is not supported: pydftracer requires >=3.9, so the wheel could not be installed"
      ;;
    *) die "unrecognised python version '$py'" ;;
  esac
  tag="cp${py//./}"
  builds_main="$builds_main ${tag}-manylinux_${ARCH}"
  if [ "$FREE_THREADED" -eq 1 ] && [[ "$py" == "3.13" || "$py" == "3.14" ]]; then
    builds_main="$builds_main ${tag}t-manylinux_${ARCH}"
  fi
done

# cibuildwheel runs from inside the staged source copy, so a relative output path
# would resolve there rather than where the caller meant.
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"

log "project      : $PROJECT_DIR"
log "output       : $OUTPUT"
log "glibc        : $GLIBC"
log "arch         : $ARCH"
# The staged copy has no .git, so setuptools-scm is told the version instead. An
# exact tag gives 2.1.1; off-tag commits are versioned per --version-scheme, and
# PACKAGE_VERSION is the fallback when there are no tags at all.
resolve_version() {
  [ -n "${DFTRACER_VERSION:-}" ] && {
    echo "$DFTRACER_VERSION"
    return
  }
  local described tag distance
  # v2.1.1-0-g7b4bced -> tag v2.1.1, distance 0
  if described="$(git -C "$PROJECT_DIR" describe --tags --match 'v*' --long 2>/dev/null)"; then
    tag="${described%-*-g*}"
    distance="${described#"$tag"-}"
    distance="${distance%-g*}"
    tag="${tag#v}"
    if [ "$distance" = "0" ]; then
      echo "$tag"
      return
    fi
    case "$VERSION_SCHEME" in
      post) echo "$tag.post$distance" ;;
      postdev) echo "$tag.post$distance.dev0" ;;
      dev) echo "$tag.dev$distance" ;;
      *) die "unknown --version-scheme '$VERSION_SCHEME' (post, postdev or dev)" ;;
    esac
    return
  fi
  tr -d '[:space:]' <"$PROJECT_DIR/PACKAGE_VERSION"
}
VERSION="$(resolve_version)"
log "wheel version: $VERSION"
log "image        : $IMAGE_MAIN"
log "  builds     :$builds_main"
[ "$LIST_ONLY" -eq 1 ] && exit 0

if [ "$DO_FETCH" -eq 1 ]; then
  log "fetching vendored dependencies"
  "$SCRIPT_DIR/fetch_deps.sh"
else
  log "skipping dependency fetch (--no-fetch)"
fi

# Rootless podman defaults its storage to $HOME, which on NFS/Lustre cannot hold
# the xattrs image layers need ("lsetxattr: operation not supported").
STATE_DIR="${DFTRACER_WHEEL_STATE:-${TMPDIR:-/tmp}/$USER/dftracer-wheels}"
mkdir -p "$STATE_DIR"

CONTAINER_ENGINE="${CIBW_CONTAINER_ENGINE:-podman}"
if [ "$CONTAINER_ENGINE" != "podman" ]; then
  log "container engine: $CONTAINER_ENGINE (skipping podman storage setup)"
  command -v "$CONTAINER_ENGINE" >/dev/null 2>&1 ||
    die "$CONTAINER_ENGINE not found on PATH"
elif [ "${DFTRACER_PODMAN_KEEP_STORAGE:-0}" = "1" ]; then
  log "podman storage: default (DFTRACER_PODMAN_KEEP_STORAGE=1)"
  command -v podman >/dev/null 2>&1 || die "podman not found on PATH"
else
  command -v podman >/dev/null 2>&1 || die "podman not found on PATH"
  PODMAN_BIN="$(command -v podman)"
  # podman 4.9 ignores CONTAINERS_STORAGE_CONF for the rootless graphroot, so
  # override it with --root/--runroot through a wrapper on PATH.
  mkdir -p "$STATE_DIR/bin" "$STATE_DIR/storage" "$STATE_DIR/run"
  cat >"$STATE_DIR/bin/podman" <<EOF
#!/usr/bin/env bash
# Generated by scripts/wheel/build_wheels.sh -- keeps container storage off the
# NFS/Lustre home directory, which cannot hold the xattrs image layers need.
exec "$PODMAN_BIN" --root "$STATE_DIR/storage" --runroot "$STATE_DIR/run" "\$@"
EOF
  chmod +x "$STATE_DIR/bin/podman"
  export PATH="$STATE_DIR/bin:$PATH"
  log "podman storage: $STATE_DIR/storage"
fi

# Mounted into the container: the dependency prefix (built once, not once per
# run), a ccache (dftracer's objects are identical for every interpreter apart
# from the pybind module) and a pip cache.
CACHE_DIR="${DFTRACER_WHEEL_CACHE:-$STATE_DIR/cache}"
CREATE_ARGS=""
if [ "$USE_CACHE" -eq 1 ]; then
  mkdir -p "$CACHE_DIR/deps" "$CACHE_DIR/ccache" "$CACHE_DIR/pip"
  if [ "$REBUILD_DEPS" -eq 1 ]; then
    log "discarding cached dependency prefix"
    rm -rf "$CACHE_DIR/deps"
    mkdir -p "$CACHE_DIR/deps"
  fi
  CREATE_ARGS="--volume=$CACHE_DIR/deps:$DEPS_PREFIX"
  CREATE_ARGS="$CREATE_ARGS --volume=$CACHE_DIR/ccache:/root/.ccache"
  CREATE_ARGS="$CREATE_ARGS --volume=$CACHE_DIR/pip:/root/.cache/pip"
  log "cache        : $CACHE_DIR"
else
  log "cache        : disabled (--no-cache)"
fi

VENV="$STATE_DIR/cibw-venv"
if [ ! -x "$VENV/bin/cibuildwheel" ] ||
  ! "$VENV/bin/cibuildwheel" --version 2>/dev/null | grep -q "$CIBW_VERSION"; then
  log "creating cibuildwheel $CIBW_VERSION venv in $VENV"
  rm -rf "$VENV"
  python3 -m venv "$VENV"
  "$VENV/bin/python" -m pip install --quiet --upgrade pip
  "$VENV/bin/python" -m pip install --quiet "cibuildwheel==$CIBW_VERSION"
fi
CIBW="$VENV/bin/cibuildwheel"

# cibuildwheel copies the whole project directory into each container, and this
# repo also holds build/, .git and several multi-GB virtualenvs, so stage a clean
# copy of the working tree's git-tracked files plus the dependency archives.
if [ "$IN_PLACE" -eq 1 ]; then
  SOURCE_DIR="$PROJECT_DIR"
  log "source       : $SOURCE_DIR (in place)"
else
  SOURCE_DIR="$STATE_DIR/src"
  log "source       : $SOURCE_DIR (staged copy of git-tracked files + archives)"
  git -C "$PROJECT_DIR" rev-parse --git-dir >/dev/null 2>&1 ||
    die "not a git checkout; rerun with --in-place"
  rm -rf "$SOURCE_DIR"
  mkdir -p "$SOURCE_DIR"
  # Tracked plus untracked-but-not-ignored, so the venv/build trees stay out.
  (cd "$PROJECT_DIR" && git ls-files -z --cached --others --exclude-standard) |
    tar -C "$PROJECT_DIR" --null -T - --ignore-failed-read -cf - |
    tar -C "$SOURCE_DIR" -xf -
  # The public archives are gitignored, so copy the whole set explicitly.
  mkdir -p "$SOURCE_DIR/dependency/source"
  cp "$PROJECT_DIR"/dependency/source/manifest.txt \
    "$PROJECT_DIR"/dependency/source/*.tar.gz "$SOURCE_DIR/dependency/source/"
fi

# setup.py appends DFTRACER_CMAKE_ARGS last, so its -D values override the ones
# setup.py aims at the wheel staging tree, where no dependency lives.
CIBW_ENV="DFTRACER_WHEEL=1"
CIBW_ENV="$CIBW_ENV DFTRACER_BUILD_DEPENDENCIES=0"
CIBW_ENV="$CIBW_ENV DFTRACER_DEPS_PREFIX=$DEPS_PREFIX"
CIBW_ENV="$CIBW_ENV DFTRACER_ENABLE_MPI=OFF DFTRACER_ENABLE_HDF5=OFF DFTRACER_DISABLE_HWLOC=ON"
CIBW_ENV="$CIBW_ENV DFTRACER_ENABLE_TESTS=OFF"
DEPS_CMAKE_ARGS="-DCMAKE_PREFIX_PATH=$DEPS_PREFIX -DDFTRACER_INSTALL_DEPENDENCIES=OFF"
for pkg in cpp-logger brahma gotcha yaml-cpp libuv; do
  DEPS_CMAKE_ARGS="$DEPS_CMAKE_ARGS -D${pkg}_DIR=$DEPS_PREFIX/lib64/cmake/$pkg"
done
if [ "$USE_CACHE" -eq 1 ]; then
  DEPS_CMAKE_ARGS="$DEPS_CMAKE_ARGS -DCMAKE_C_COMPILER_LAUNCHER=ccache"
  DEPS_CMAKE_ARGS="$DEPS_CMAKE_ARGS -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi
# RPATHs are not set here: a $ORIGIN passed through CIBW_ENVIRONMENT is
# shell-expanded to nothing before CMake sees it, so bundle_wheel.py sets them.
CIBW_ENV="$CIBW_ENV DFTRACER_CMAKE_ARGS=\"$DEPS_CMAKE_ARGS\""
CIBW_ENV="$CIBW_ENV LD_LIBRARY_PATH=\"$DEPS_PREFIX/lib64:\$LD_LIBRARY_PATH\""
CIBW_ENV="$CIBW_ENV SETUPTOOLS_SCM_PRETEND_VERSION=$VERSION"
CIBW_ENV="$CIBW_ENV DFTRACER_EXPECTED_VERSION=$VERSION"
CIBW_ENV="$CIBW_ENV CCACHE_DIR=/root/.ccache CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-5G}"

export CIBW_PLATFORM=linux
export CIBW_ARCHS="$ARCH"
if [ -n "$CREATE_ARGS" ]; then
  export CIBW_CONTAINER_ENGINE="$CONTAINER_ENGINE; create_args: $CREATE_ARGS"
else
  export CIBW_CONTAINER_ENGINE="$CONTAINER_ENGINE"
fi
export CIBW_SKIP="*musllinux*"
export CIBW_ENVIRONMENT="$CIBW_ENV"
export CIBW_BEFORE_ALL="bash {project}/scripts/wheel/build_deps.sh"
export CIBW_BEFORE_BUILD="rm -rf {project}/build"
# Only {wheel} and {dest_dir} are substituted, so the project path has to be the
# container's fixed mount point rather than {project}.
export CIBW_REPAIR_WHEEL_COMMAND="bash /project/scripts/wheel/repair_wheel.sh {wheel} {dest_dir}"

if [ "$REBUILD_DEPS" -eq 1 ] && [ "$USE_CACHE" -eq 0 ]; then
  CIBW_BEFORE_ALL="rm -rf $DEPS_PREFIX && $CIBW_BEFORE_ALL"
  export CIBW_BEFORE_ALL
fi

case "$RUN_TESTS" in
  full)
    export CIBW_TEST_COMMAND="python {project}/scripts/wheel/test_wheel.py"
    export CIBW_TEST_EXTRAS=""
    unset CIBW_TEST_SKIP
    ;;
  packaging)
    export CIBW_TEST_COMMAND="python {project}/scripts/wheel/test_wheel.py --no-trace"
    export CIBW_TEST_EXTRAS=""
    unset CIBW_TEST_SKIP
    ;;
  none)
    export CIBW_TEST_COMMAND=""
    export CIBW_TEST_EXTRAS=""
    export CIBW_TEST_SKIP="*"
    ;;
esac
log "wheel tests  : $RUN_TESTS"

if [ "$FREE_THREADED" -eq 1 ]; then
  export CIBW_ENABLE="cpython-freethreading"
fi

run_group() {
  local image="$1" builds="$2" src="$3"
  [ -n "$builds" ] || return 0
  log "building$builds in $image"
  # cibuildwheel requires the package dir inside the working directory.
  (
    cd "$src"
    CIBW_BUILD="$builds" \
      CIBW_MANYLINUX_X86_64_IMAGE="$image" \
      CIBW_MANYLINUX_AARCH64_IMAGE="$image" \
      "$CIBW" --output-dir "$OUTPUT" .
  )
}

if [ "$JOBS" -le 1 ]; then
  run_group "$IMAGE_MAIN" "$builds_main" "$SOURCE_DIR"
else
  # The first runs alone to populate the caches the others then read.
  set -- $builds_main
  first="$1"
  shift
  log "warming caches with $first"
  run_group "$IMAGE_MAIN" "$first" "$SOURCE_DIR"
  running=0
  for build in "$@"; do
    run_group "$IMAGE_MAIN" "$build" "$SOURCE_DIR" &
    running=$((running + 1))
    if [ "$running" -ge "$JOBS" ]; then
      wait -n 2>/dev/null || wait
      running=$((running - 1))
    fi
  done
  wait
fi

log "wheels in $OUTPUT:"
ls -1 "$OUTPUT"/*.whl 2>/dev/null || die "no wheels produced"
