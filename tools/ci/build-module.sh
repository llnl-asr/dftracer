#!/bin/bash
# Build one part of a composable DFTracer in the CI deps image and install it
# as a compose tree (<out>/compose):
#
#   build-module.sh --core --out DIR
#       the core archives, the C++ runtime and the compose project
#   build-module.sh --module mpi --spack openmpi@4.1.6 --out DIR
#   build-module.sh --module mpi --spack mpich@4.2.3 --cray 9.0 --out DIR
#       Cray MPICH 9.0, from the MPICH it derives from (see --cray below)
#   build-module.sh --module hdf5 --spack 'hdf5@1.14.6+mpi ^openmpi@4.1.6' --out DIR
#   build-module.sh --module papi --papi /opt/papi-7.1.0 --out DIR
#   build-module.sh --module hwloc --spack hwloc@2.11.1 --out DIR
#   build-module.sh --module hip --rocm /opt/rocm-6.4.3 --out DIR
#   build-module.sh --module cuda --cuda /usr/local/cuda-12.8 --out DIR
#
# Every other module is turned off, so the tree holds the core plus that one
# variant; tools/ci/assemble.py merges the trees of all the jobs. The variant
# directory gets the part's source hash (tools/ci/source_hash.py) in
# component.json.
#
# Options: --src DIR (default /src), --jobs N, --cmake-arg -DX=Y (repeatable).
set -euo pipefail

SRC=/src
OUT=
MODULE=
SPEC=
ROCM=
CUDA=
PAPI=
CRAY=
CRAY_FLAGS=()
CORE=0
JOBS=${JOBS:-$(nproc)}
EXTRA=()
while [ $# -gt 0 ]; do
  case "$1" in
    --core) CORE=1; shift ;;
    --module) MODULE=$2; shift 2 ;;
    --spack) SPEC=$2; shift 2 ;;
    --rocm) ROCM=$2; shift 2 ;;
    --cuda) CUDA=$2; shift 2 ;;
    --papi) PAPI=$2; shift 2 ;;
    --cray) CRAY=$2; shift 2 ;;
    --src) SRC=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --jobs) JOBS=$2; shift 2 ;;
    --cmake-arg) EXTRA+=("$2"); shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[ -n "$OUT" ] || { echo "--out is required" >&2; exit 2; }
[ "$CORE" = 1 ] || [ -n "$MODULE" ] || { echo "--core or --module is required" >&2; exit 2; }

if [ -n "$SPEC" ]; then
  # shellcheck disable=SC1091
  . "${SPACK_ROOT:-/opt/spack}/share/spack/setup-env.sh"
  # --first: the layered install builds some MPIs more than once (the MPI
  # layer's and an HDF5 layer's openmpi@4.1.6 differ only in their own
  # dependencies), and any of them is the version the variant names.
  # Its own step: inside "$(...)" a failure would be lost (set -e does not
  # see it), and the job would go on against whatever the OS has, building
  # a wrongly named variant (hwloc 1 came out as hwloc-2 that way).
  _env=$(spack -e deps load --sh --first "$SPEC") \
    || { echo "spack load $SPEC failed" >&2; exit 1; }
  eval "$_env"
fi
export CC=${CC:-gcc} CXX=${CXX:-g++}

if [ -n "$CRAY" ]; then
  # Cray MPICH is not redistributable, so the image cannot hold it. Every
  # function brahma's Cray compat interfaces bind is declared by the MPICH
  # it derives from (loaded by --spack), and what makes an MPI Cray MPICH to
  # the probes (cmake/tests/mpi_detect.c) and to brahma is the one macro of
  # Cray's cray_version.h. So stand in that header: an mpi.h that defines
  # CRAY_MPICH_VERSION and includes MPICH's, first on the include path of
  # MPICH's compiler wrappers, which is where FindMPI takes it from.
  real_mpicc=$(command -v mpicc)
  real_mpicxx=$(command -v mpicxx)
  cray_dir=$OUT/cray-mpich-$CRAY
  mkdir -p "$cray_dir/include" "$cray_dir/bin"
  {
    echo "/* Stand-in for Cray MPICH $CRAY: its cray_version.h, then the MPICH"
    echo " * it derives from (tools/ci/build-module.sh --cray). */"
    echo "#ifndef CRAY_MPICH_VERSION"
    echo "#define CRAY_MPICH_VERSION $CRAY.0"
    echo "#endif"
    echo "#include_next <mpi.h>"
  } >"$cray_dir/include/mpi.h"
  printf '#!/bin/sh\nexec %s -I%s "$@"\n' "$real_mpicc" "$cray_dir/include" >"$cray_dir/bin/mpicc"
  printf '#!/bin/sh\nexec %s -I%s "$@"\n' "$real_mpicxx" "$cray_dir/include" >"$cray_dir/bin/mpicxx"
  chmod +x "$cray_dir/bin/mpicc" "$cray_dir/bin/mpicxx"
  export PATH="$cray_dir/bin:$PATH"
  # Named, not just first on PATH: FindMPI finds the loaded MPICH's own
  # wrapper through its prefix before it looks at PATH.
  CRAY_FLAGS=("-DMPI_C_COMPILER=$cray_dir/bin/mpicc" "-DMPI_CXX_COMPILER=$cray_dir/bin/mpicxx")
fi

# Everything off, then the one module on.
FLAGS=(
  -DCMAKE_BUILD_TYPE=Release
  -DDFTRACER_ENABLE_MPI=OFF -DDFTRACER_ENABLE_HDF5=OFF
  -DDFTRACER_ENABLE_PAPI_TRACING=OFF -DDFTRACER_DISABLE_HWLOC=ON
  -DDFTRACER_ENABLE_HIP_TRACING=OFF -DDFTRACER_ENABLE_CUDA_TRACING=OFF
  -DDFTRACER_ENABLE_VARIORUM=OFF -DDFTRACER_ENABLE_TESTS=OFF
  -DDFTRACER_BUILD_PYTHON_BINDINGS=OFF
)
FLAGS+=("${CRAY_FLAGS[@]}")
case "$MODULE" in
  "") ;;
  mpi) FLAGS+=(-DDFTRACER_ENABLE_MPI=ON) ;;
  hdf5) FLAGS+=(-DDFTRACER_ENABLE_HDF5=ON -DHDF5_PREFER_PARALLEL=ON)
        # spack builds HDF5 with CMake, whose h5cc/h5pcc do not implement
        # -show (1.10, 1.12) or pass it on to the compiler (1.14), and
        # FindHDF5 then gives up instead of searching. Give it a wrapper that
        # reports nothing, so it searches the loaded HDF5's prefix instead;
        # H5pubconf.h still says if it is parallel. (A -NOTFOUND value does
        # not stick: under the project's CMake 3.24 policies FindHDF5's
        # find_program looks again and finds the real wrapper.)
        _h5=$(command -v h5pcc || command -v h5cc || true)
        if [ -n "$_h5" ]; then
          FLAGS+=("-DHDF5_ROOT=$(dirname "$(dirname "$_h5")")"
                  -DHDF5_C_COMPILER_EXECUTABLE:FILEPATH=/bin/false)
        fi
        # A parallel HDF5's MPI is on the path too (spack load brought it).
        if command -v mpicc >/dev/null 2>&1; then
          FLAGS+=(-DDFTRACER_ENABLE_MPI=ON)
          # MPI on adds the MPI module to the install, so it is built too;
          # assemble.py takes only the HDF5 variant from this tree.
          WITH_MPI=1
        fi ;;
  papi) FLAGS+=(-DDFTRACER_ENABLE_PAPI_TRACING=ON)
        if [ -n "$PAPI" ]; then export PAPI_DIR=$PAPI; FLAGS+=("-DPAPI_DIR=$PAPI"); fi ;;
  hwloc) FLAGS+=(-DDFTRACER_DISABLE_HWLOC=OFF) ;;
  hip) [ -n "$ROCM" ] || { echo "--rocm is required for hip" >&2; exit 2; }
       export ROCM_PATH=$ROCM
       # ROCm goes on CMAKE_PREFIX_PATH with the dependency prefix (below);
       # a second -DCMAKE_PREFIX_PATH here would replace that one.
       FLAGS+=(-DDFTRACER_ENABLE_HIP_TRACING=ON) ;;
  cuda) [ -n "$CUDA" ] || { echo "--cuda is required for cuda" >&2; exit 2; }
        FLAGS+=(-DDFTRACER_ENABLE_CUDA_TRACING=ON "-DDFTRACER_CUDA_PATH=$CUDA") ;;
  *) echo "unknown module: $MODULE" >&2; exit 2 ;;
esac

PART=${MODULE:-core}
WORK="$OUT/work-$PART"
mkdir -p "$WORK"

# Dependencies (brahma for the core and, for mpi/hdf5, the module's own) are
# built per job: the module's brahma has to match the library it wraps.
cmake -S "$SRC" -B "$WORK/deps-build" -DCMAKE_INSTALL_PREFIX="$WORK/deps" \
  -DDFTRACER_INSTALL_DEPENDENCIES=ON "${FLAGS[@]}" "${EXTRA[@]}" >"$WORK/deps.log" 2>&1
cmake --build "$WORK/deps-build" -j "$JOBS" >>"$WORK/deps.log" 2>&1

cmake -S "$SRC" -B "$WORK/build" -DCMAKE_INSTALL_PREFIX="$WORK/prefix" \
  -DCMAKE_PREFIX_PATH="$WORK/deps${ROCM:+;$ROCM}" "${FLAGS[@]}" "${EXTRA[@]}" \
  >"$WORK/configure.log" 2>&1
# The install lists the core archives too, so they are built either way;
# tools/ci/assemble.py takes only the module's directory from a module job.
targets=(dftracer_core_base dftracer_core_base_dbg)
if [ -n "$MODULE" ]; then
  targets+=(dftracer_module_${MODULE} dftracer_module_${MODULE}_dbg)
  if [ "${WITH_MPI:-0}" = 1 ]; then
    targets+=(dftracer_module_mpi dftracer_module_mpi_dbg)
  fi
fi
cmake --build "$WORK/build" -j "$JOBS" --target "${targets[@]}" >"$WORK/build.log" 2>&1
cmake --install "$WORK/build" --component dftracer_compose --prefix "$OUT/tree" \
  >"$WORK/install.log" 2>&1

compose=$(find "$OUT/tree" -type d -path '*/dftracer/compose' | head -n 1)
brahma_tag=$(grep -m1 -oE 'DFTRACER_BRAHMA_GIT_TAG:STRING=.*' "$WORK/deps-build/CMakeCache.txt" | cut -d= -f2 || true)
PY=$(command -v python3 || command -v python)
hash=$("$PY" "$SRC/tools/ci/source_hash.py" "$SRC" "$PART" --compiler "$CXX" --brahma "$brahma_tag")
interface=$("$PY" "$SRC/tools/ci/source_hash.py" "$SRC" interface)
if [ -n "$MODULE" ]; then
  # Only this job's module: a parallel HDF5 job builds the MPI module too
  # (above), and tools/ci/assemble.py takes every module a tree holds.
  find "$compose/modules" -mindepth 1 -maxdepth 1 -type d ! -name "$MODULE" \
    -exec rm -rf {} +
  variant_dir=$(find "$compose/modules/$MODULE" -mindepth 1 -maxdepth 1 -type d | head -n 1)
  "$PY" - "$variant_dir/component.json" "$hash" "$interface" <<'EOF'
import json, sys
path, digest, interface = sys.argv[1:4]
data = json.load(open(path))
data["source_hash"] = digest
data["interface_hash"] = interface
json.dump(data, open(path, "w"), indent=2)
EOF
  echo "built $MODULE: ${variant_dir##*/} ($hash)"
else
  echo "$hash" >"$compose/core/source_hash"
  echo "$interface" >"$compose/core/interface_hash"
  # The shared libraries the core links from its dependency build (gotcha,
  # libuv, libdeflate, cpp-logger), next to dftracer/compose as in a wheel:
  # core.cmake names them by this job's paths, and composing on another
  # machine links the copies there instead.
  libdir=$(dirname "$(dirname "$compose")")
  find "$WORK/deps" -maxdepth 2 -path '*/lib*/lib*.so*' \( -type f -o -type l \) \
    -exec cp -a {} "$libdir/" \;
  echo "built core ($hash), bundled: $(cd "$libdir" && ls lib*.so* 2>/dev/null | tr '\n' ' ')"
fi
