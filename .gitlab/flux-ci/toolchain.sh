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
    build-essential cmake ninja-build git pkg-config \
    python3 python3-pip python3-venv python3-dev \
    libmpich-dev mpich libhdf5-mpich-dev valgrind gdb curl ca-certificates
fi

export CC=gcc CXX=g++
gcc --version && python3 --version && (which mpicc || true)
cmake --version || true

rm -rf venv && python3 -m venv venv
export VENV_PATH=$PWD/venv
source venv/bin/activate
python -m pip install --upgrade pip setuptools wheel
