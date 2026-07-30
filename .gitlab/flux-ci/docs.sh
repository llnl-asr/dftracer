#!/bin/bash
# Sphinx docs build (kept ReadTheDocs-compatible); output in public/ is
# published by the `pages` job.
set -eo pipefail
cd "$CI_PROJECT_DIR"
source .gitlab/flux-ci/toolchain.sh
pip install -r docs/requirements.txt
sphinx-build -b html docs public
