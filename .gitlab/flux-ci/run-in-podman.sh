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

# The CMake dependencies (cpp-logger, brahma) are fetched from czgitlab over
# ssh, so the container needs the runner account's keys mounted read-only.
#
# --user 0:0 is REQUIRED: images with a non-root USER (e.g. brahma-ci) map to a
# subuid under rootless podman and cannot read the bind-mounted checkout
# ("Permission denied"). Container root maps to the host user, which owns them.
podman --root "$PODMAN_STORE" --runroot "$PODMAN_RUNROOT" run --rm \
  --user 0:0 \
  -v "$PWD:/ws" -w /ws \
  -v "$HOME/.ssh:/root/.ssh:ro" \
  -e GIT_SSH_COMMAND="ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null" \
  -e CI_PROJECT_DIR=/ws \
  -e PYPI_TOKEN -e DOCKER_USERNAME -e DOCKER_PASSWORD \
  "$IMAGE" bash "$@"
