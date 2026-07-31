#!/bin/bash
# wheels.yml -> wheel builds. NOTE: cibuildwheel needs a container engine;
# this will only succeed on a corona node where podman/docker is usable.
set -eo pipefail
cd "$CI_PROJECT_DIR"
source /etc/profile.d/z00_lmod.sh 2>/dev/null || true
# Python is provided by the container image.
python3 scripts/wheel/manifest.py --check
scripts/wheel/fetch_deps.sh --verify
scripts/wheel/build_wheels.sh --python "${PYTHON}" --glibc "${GLIBC}" --no-fetch --output wheelhouse
