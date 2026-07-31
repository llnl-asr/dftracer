#!/bin/bash
# Usage: run-in-podman.sh <image> <phase-script> [phase-script-args...]
#
# Runs a CI phase script inside a podman container on the allocated compute
# node, mirroring the images the GitHub Actions workflows used. Invoked from
# .gitlab-ci.yml as:
#   flux proxy "$JOBID" flux run -N 1 bash .gitlab/flux-ci/run-in-podman.sh <image> <script>
set -ex

IMAGE=$1
shift

# Rootless podman needs node-local storage (overlayfs does not work on NFS
# homes/workspaces). Keep image store + runroot in /var/tmp on the node.
PODMAN_STORE=/var/tmp/$USER/podman-root
PODMAN_RUNROOT=/var/tmp/$USER/podman-run
mkdir -p "$PODMAN_STORE" "$PODMAN_RUNROOT"

podman --root "$PODMAN_STORE" --runroot "$PODMAN_RUNROOT" run --rm \
  -v "$PWD:/ws" -w /ws \
  -e CI_PROJECT_DIR=/ws \
  -e PYPI_TOKEN -e DOCKER_USERNAME -e DOCKER_PASSWORD \
  "$IMAGE" bash "$@"
