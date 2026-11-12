#!/bin/bash
# The tag of the CI deps image for this tree: a hash of everything that goes
# into the image, so a workflow finds the image built for its variants.yaml.
set -euo pipefail
cd "$(dirname "$0")/../.."
cat dependency/variants.yaml infrastructure/ci/Containerfile.deps tools/ci/variants.py |
  sha256sum | cut -c1-16
