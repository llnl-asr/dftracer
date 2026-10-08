#!/bin/bash
# wheels.yml -> wheel builds. Runs on the node, not through run-in-podman.sh:
# cibuildwheel drives podman itself and that image has none.
set -eo pipefail
cd "$CI_PROJECT_DIR"
source /etc/profile.d/z00_lmod.sh 2>/dev/null || true
python3 scripts/wheel/manifest.py
# postdev: .postN outranks the tag, .dev0 keeps it a pre-release. A bare
# .postN is a final release and would be installed without --pre.
scripts/wheel/build_wheels.sh --python "${PYTHON}" --glibc "${GLIBC}" \
  --version-scheme postdev --jobs "${WHEEL_JOBS:-6}" --output wheelhouse

# The wheels carry no MPI or HDF5, so `pip install --no-binary dftracer` needs
# an sdist beside them. Versioned from git describe to match the wheels.
version="$(scripts/wheel/build_wheels.sh --version-scheme postdev --list |
  awk '/wheel version/ {print $NF}')"
sdist_venv="$(mktemp -d)/venv"
python3 -m venv "${sdist_venv}"
"${sdist_venv}/bin/pip" install -q --upgrade pip build
SETUPTOOLS_SCM_PRETEND_VERSION="${version}" \
  "${sdist_venv}/bin/python" -m build --sdist --outdir wheelhouse .
