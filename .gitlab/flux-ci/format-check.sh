#!/bin/bash
# format-check.yml -> format-check phase (clang-format from PyPI instead of
# the ubuntu clang-format-19 package).
set -eo pipefail
cd "$CI_PROJECT_DIR"
source .gitlab/flux-ci/toolchain.sh
pip install "clang-format==19.*"
./script/formatting/check-formatting.sh "$(command -v clang-format)"
