# Module toolchain + fresh python venv for the migrated corona jobs
# (replaces the apt-get/PPA installs the GitHub ubuntu runners needed).
# Sourced by every phase script in this directory.
set -eo pipefail
source /etc/profile.d/z00_lmod.sh 2>/dev/null || true
module load $GCC_MODULE $PYTHON_MODULE $MPI_MODULE
export CC=gcc CXX=g++
gcc --version && python3 --version && (which mpicc || true)
cmake --version || module load cmake || true
rm -rf venv && python3 -m venv venv
export VENV_PATH=$PWD/venv
source venv/bin/activate
python -m pip install --upgrade pip setuptools wheel
