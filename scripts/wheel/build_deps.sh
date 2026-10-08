#!/usr/bin/env bash
#
# Build and install DFTracer's C/C++ dependencies into a single prefix, cloning
# each pinned tag listed in dependency/manifest.txt. This runs INSIDE the
# manylinux container as cibuildwheel's CIBW_BEFORE_ALL step.
#
# It also writes $PREFIX/cmake-args.txt, the -D flags the wheel build feeds back
# to setup.py via DFTRACER_CMAKE_ARGS so CMake resolves these packages with
# find_package() instead of cloning them with ExternalProject/FetchContent.
#
# Environment:
#   DFTRACER_PROJECT_DIR   project root inside container (default /project)
#   DFTRACER_DEPS_PREFIX   install prefix            (default /opt/dftracer-deps)
#   DFTRACER_DEPS_WORKDIR  scratch build dir         (default /tmp/dftracer-deps)
#   JOBS                   parallel build jobs       (default nproc)

set -euo pipefail

PROJECT_DIR="${DFTRACER_PROJECT_DIR:-/project}"
PREFIX="${DFTRACER_DEPS_PREFIX:-/opt/dftracer-deps}"
WORK="${DFTRACER_DEPS_WORKDIR:-/tmp/dftracer-deps}"
JOBS="${JOBS:-$(nproc)}"
MANIFEST="$PROJECT_DIR/dependency/manifest.txt"

STAMP="$PREFIX/.dftracer-deps-complete"

log() { echo "[deps] $*"; }

[ -f "$MANIFEST" ] || {
  echo "[deps] manifest not found: $MANIFEST" >&2
  exit 1
}

# Set up before the reuse shortcut below: the wheel build passes ccache as a
# compiler launcher, so it must exist in every container, not just the one that
# builds the dependencies. The shim keeps that launcher harmless when ccache
# cannot be installed.
if ! command -v ccache >/dev/null 2>&1; then
  log "installing ccache"
  if command -v dnf >/dev/null 2>&1; then
    dnf install -y ccache >/dev/null 2>&1 || true
  elif command -v yum >/dev/null 2>&1; then
    yum install -y ccache >/dev/null 2>&1 || true
  fi
fi
if command -v ccache >/dev/null 2>&1; then
  export CCACHE_DIR="${CCACHE_DIR:-/root/.ccache}"
  mkdir -p "$CCACHE_DIR"
  ccache --set-config max_size="${CCACHE_MAXSIZE:-5G}" 2>/dev/null || true
  log "ccache $(ccache --version | head -1 | awk '{print $NF}') cache=$CCACHE_DIR"
else
  log "ccache unavailable, installing a pass-through shim"
  printf '#!/bin/sh\nexec "$@"\n' >/usr/local/bin/ccache
  chmod +x /usr/local/bin/ccache
fi

# The prefix is usually a cache mounted from the host, so the stamp records which
# manifest built it. Only dependency lines are hashed, so editing a comment does
# not invalidate a good prefix.
MANIFEST_ID="$(grep -vE '^[[:space:]]*(#|$)' "$MANIFEST" | sha256sum | cut -d" " -f1)"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$MANIFEST_ID" ]; then
  log "reusing dependencies already built in $PREFIX"
  command -v ccache >/dev/null 2>&1 && ccache --show-stats 2>/dev/null | head -3
  exit 0
fi
if [ -f "$STAMP" ]; then
  log "manifest changed since $PREFIX was built, rebuilding"
  # Empty it rather than remove it: the prefix is usually a mount point.
  find "$PREFIX" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
fi

if [ ! -f /usr/include/zlib.h ]; then
  log "installing zlib-devel"
  if command -v dnf >/dev/null 2>&1; then
    dnf install -y zlib-devel
  elif command -v yum >/dev/null 2>&1; then
    yum install -y zlib-devel
  elif command -v apt-get >/dev/null 2>&1; then
    apt-get update && apt-get install -y zlib1g-dev
  else
    echo "[deps] no package manager found and zlib.h is missing" >&2
    exit 1
  fi
fi

mkdir -p "$PREFIX" "$WORK/src"
# Some dependencies install to lib, others to lib64; dftracer's CMake expects
# lib64, so pin it and make lib a synonym.
mkdir -p "$PREFIX/lib64"
[ -e "$PREFIX/lib" ] || ln -s lib64 "$PREFIX/lib"

CMAKE="${CMAKE:-cmake}"
command -v "$CMAKE" >/dev/null 2>&1 || {
  # cibuildwheel's build env has cmake, but BEFORE_ALL runs outside it.
  log "cmake not on PATH, installing via pip"
  /opt/python/cp311-cp311/bin/python -m pip install --quiet "cmake>=3.24" ninja
  export PATH="/opt/python/cp311-cp311/bin:$PATH"
}

# CMAKE_POLICY_VERSION_MINIMUM: these projects declare cmake_minimum_required
# 3.4-3.10, which CMake 4.x refuses.
common_args=(
  "-DCMAKE_BUILD_TYPE=Release"
  "-DCMAKE_INSTALL_PREFIX=$PREFIX"
  "-DCMAKE_INSTALL_LIBDIR=lib64"
  "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
  "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
  "-DBUILD_SHARED_LIBS=ON"
  "-DBUILD_TESTING=OFF"
  "-DCMAKE_PREFIX_PATH=$PREFIX"
)
if command -v ccache >/dev/null 2>&1; then
  common_args+=(
    "-DCMAKE_C_COMPILER_LAUNCHER=ccache"
    "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
  )
fi

dep_args() {
  case "$1" in
    gotcha)
      echo "-DGOTCHA_ENABLE_TESTS=OFF;-DGOTCHA_ENABLE_TESTING=OFF;-DGOTCHA_ENABLE_EXAMPLE=OFF"
      ;;
    cpp-logger)
      echo "-DCPP_LOGGER_ENABLE_TESTING=OFF;-DCPP_LOGGER_WARNINGS_AS_ERRORS=OFF"
      ;;
    yaml-cpp)
      echo "-DYAML_CPP_BUILD_TESTS=OFF;-DYAML_CPP_BUILD_TOOLS=OFF;-DYAML_BUILD_SHARED_LIBS=ON"
      ;;
    libuv)
      echo "-DLIBUV_BUILD_TESTS=OFF;-DLIBUV_BUILD_BENCH=OFF"
      ;;
    brahma)
      # brahma's fetch_package() tries find_package() first, and gotcha and
      # cpp-logger are installed in $PREFIX by now, so nothing is fetched.
      # FETCHCONTENT_SOURCE_DIR_* is the fallback if that misses.
      echo "-DBRAHMA_BUILD_DEPENDENCIES=ON;-DBRAHMA_BUILD_WITH_MPI=OFF;-DBRAHMA_BUILD_WITH_HDF5=OFF;-DBRAHMA_ENABLE_TESTING=OFF;-DFETCHCONTENT_FULLY_DISCONNECTED=OFF;-DFETCHCONTENT_SOURCE_DIR_GOTCHA=$WORK/src/gotcha;-DFETCHCONTENT_SOURCE_DIR_CPP-LOGGER=$WORK/src/cpp-logger"
      ;;
    *) echo "" ;;
  esac
}

fetch() {
  local name="$1" tag="$2" url="$3"
  rm -rf "$WORK/src/$name"
  # Fetching by ref works for a tag, a branch or a commit hash; clone --branch
  # does not accept a hash.
  git init --quiet "$WORK/src/$name"
  git -C "$WORK/src/$name" remote add origin "$url"
  git -C "$WORK/src/$name" fetch --quiet --depth 1 origin "$tag"
  git -C "$WORK/src/$name" checkout --quiet FETCH_HEAD
  echo "$WORK/src/$name"
}

while read -r name tag url; do
  case "${name:-#}" in '' | '#'*) continue ;; esac

  src="$(fetch "$name" "$tag" "$url")"
  build="$WORK/build/$name"
  rm -rf "$build"
  mkdir -p "$build"

  if [ "$name" = "gotcha" ]; then
    # GOTCHA builds with -D_POSIX_C_SOURCE and calls getpagesize(), which glibc
    # 2.34 and newer then leave undeclared; gcc 14 rejects that.
    wrappers="$src/src/libc_wrappers.h"
    grep -q "extern int getpagesize" "$wrappers" ||
      sed -i 's/^#define gotcha_getpagesize getpagesize$/extern int getpagesize(void);\n&/' "$wrappers"
  fi

  if [ "$name" = "brahma" ]; then
    # The same patch dftracer applies in its ExternalProject path.
    patch_file="$PROJECT_DIR/cmake/patches/brahma_version_override.cmake"
    if [ -f "$patch_file" ]; then
      log "patching brahma"
      "$CMAKE" -DSOURCE_DIR="$src" -P "$patch_file" || log "brahma patch failed (continuing)"
    fi
  fi

  IFS=';' read -r -a extra <<<"$(dep_args "$name")"

  log "configuring $name"
  "$CMAKE" -S "$src" -B "$build" "${common_args[@]}" ${extra[@]+"${extra[@]}"}
  log "building $name"
  "$CMAKE" --build "$build" --parallel "$JOBS"
  log "installing $name into $PREFIX"
  "$CMAKE" --install "$build"
done <"$MANIFEST"

# The wheel build passes -D<pkg>_DIR=$PREFIX/lib64/cmake/<pkg>, a path it must
# know up front because cibuildwheel evaluates CIBW_ENVIRONMENT before this
# script runs. Link configs installed elsewhere into that location.
mkdir -p "$PREFIX/lib64/cmake"
args="-DCMAKE_PREFIX_PATH=$PREFIX -DDFTRACER_INSTALL_DEPENDENCIES=OFF"
for pkg in cpp-logger brahma gotcha yaml-cpp libuv; do
  want="$PREFIX/lib64/cmake/$pkg"
  if [ ! -d "$want" ]; then
    dir="$(find "$PREFIX" -mindepth 2 -maxdepth 5 -type d -name "$pkg" -path '*cmake*' 2>/dev/null | head -1)"
    if [ -n "$dir" ]; then
      log "linking $pkg config: $dir -> $want"
      ln -sfn "$dir" "$want"
    else
      log "WARNING: no CMake package config found for $pkg"
      continue
    fi
  fi
  args="$args -D${pkg}_DIR=$want"
done
printf '%s\n' "$args" >"$PREFIX/cmake-args.txt"
log "wrote $PREFIX/cmake-args.txt:"
log "  $args"

printf '%s\n' "$MANIFEST_ID" >"$STAMP"
command -v ccache >/dev/null 2>&1 && ccache --show-stats 2>/dev/null | head -5
log "done"
