#!/bin/bash
# prerelease.yml -> publish .postN prerelease to PyPI (manual, develop).
# Needs PYPI_TOKEN and a container engine usable on the node (cibuildwheel).
set -eo pipefail
cd "$CI_PROJECT_DIR"
source /etc/profile.d/z00_lmod.sh 2>/dev/null || true
module load $PYTHON_MODULE || true
version="$(scripts/wheel/build_wheels.sh --list | awk '/wheel version/ {print $NF}')"
echo "wheel version: $version"
case "$version" in
  *.post*) echo "publishing $version" ;;
  *) echo "$version is a tagged release, leaving it to python-publish"; exit 0 ;;
esac
scripts/wheel/fetch_deps.sh --verify
for PY in 3.9 3.10 3.11 3.12 3.13 3.14; do scripts/wheel/build_wheels.sh --python "$PY" --glibc "${GLIBC}" --no-fetch --output dist; done
python3 -m pip install --upgrade pip build twine
SETUPTOOLS_SCM_PRETEND_VERSION="$version" python3 -m build --sdist --outdir dist .
python3 -m twine check dist/*
python3 -m twine upload --skip-existing -u __token__ -p "${PYPI_TOKEN}" dist/*
