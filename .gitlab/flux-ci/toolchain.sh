# Toolchain setup for CI phases — now runs INSIDE a podman container
# (ubuntu:22.04 or the brahma-ci image), so LC modules are no longer used.
# Sourced by every phase script in this directory.
set -eo pipefail

# APT::Sandbox::User=root: rootless podman has no mapped _apt uid, so apts
# privilege drop fails with "setgroups (22: Invalid argument)".
export DEBIAN_FRONTEND=noninteractive
if command -v apt-get >/dev/null; then
  apt-get -o APT::Sandbox::User=root update -qq
  apt-get -o APT::Sandbox::User=root install -y -qq \
    build-essential cmake ninja-build git openssh-client pkg-config jq \
    python3 python3-pip python3-venv python3-dev \
    valgrind gdb curl ca-certificates libpapi-dev
fi

# MPI and HDF5 come from the image's spack stack when it has one. brahma only
# generates GOTCHA MPI bindings for specific library versions, and apt's mpich
# (4.2.0 on 24.04, 4.0 on 22.04) falls in a gap between them, which silently
# disables every MPI interceptor. Keep this pair matched: hdf5@1.12.3 is the
# one built against mpich@4.2.3.
_spack_env=/home/spack/spack/share/spack/setup-env.sh
if [ -f "${_spack_env}" ]; then
  . "${_spack_env}"
  spack load hdf5@1.12.3 mpich@4.2.3 cmake
  _dftracer_spack_mpi=1
elif command -v apt-get >/dev/null; then
  apt-get -o APT::Sandbox::User=root install -y -qq \
    libmpich-dev mpich libhdf5-mpich-dev
fi

# Not when the spack MPI is loaded: that phase builds with the MPI wrappers, and
# forcing CXX=g++ makes FindMPI probe g++ instead of mpicxx and fail to resolve
# MPI_CXX_LIB_NAMES.
if [ -z "${_dftracer_spack_mpi:-}" ]; then
  export CC=gcc CXX=g++
fi
gcc --version && python3 --version && (which mpicc || true)
cmake --version || true

rm -rf venv && python3 -m venv venv
export VENV_PATH=$PWD/venv
source venv/bin/activate
python -m pip install --upgrade pip setuptools wheel

# cmake >= 3.24 is required by cmake_minimum_required and by --fresh in the
# package_find tests, and the phases build with --no-build-isolation so
# pyproject's own requirement is never installed for them. Take pip's only when
# what is already on PATH is too old, since the venv would otherwise shadow the
# image's own cmake.
_cmake_ver=$(cmake --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)
if [ -z "${_cmake_ver}" ] ||
   [ "$(printf '%s\n3.24\n' "${_cmake_ver}" | sort -V | head -1)" != "3.24" ]; then
  python -m pip install "cmake>=3.24"
fi

# The package_find tests deliberately invoke the system cmake to check the
# exported config stands alone, and ubuntu 22.04 ships 3.22 - below every
# cmake_minimum_required in this repo. Redirect it rather than teaching those
# tests to accept an old one.
_sys_cmake_ver=$(/usr/bin/cmake --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)
if [ -n "${_sys_cmake_ver}" ] &&
   [ "$(printf '%s\n3.24\n' "${_sys_cmake_ver}" | sort -V | head -1)" != "3.24" ]; then
  ln -sf "${VENV_PATH}/bin/cmake" /usr/bin/cmake
fi
