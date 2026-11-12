#!/bin/bash
# Build the CI-dev image and run commands in it with podman, the same
# container CI uses, so a CI failure can be reproduced on a workstation or an
# LC node.
#
#   infrastructure/ci/podman.sh image                 build the CI-dev image
#   infrastructure/ci/podman.sh deps-image            build the CI deps image:
#                                                     every version in
#                                                     dependency/variants.yaml
#   infrastructure/ci/podman.sh deps-push [repository]  push it for GitHub CI
#                                                     (default
#                                                     docker.io/hdevarajan92/dftracer-ci;
#                                                     podman login first)
#   infrastructure/ci/podman.sh build [ci-build args] build + test /src in it
#   infrastructure/ci/podman.sh shell                 interactive shell
#   infrastructure/ci/podman.sh exec <command>...     run one command in it
#
# Environment:
#   DFTRACER_SRC       source tree mounted at /src (default: this checkout)
#   DFTRACER_CI_STATE  local-disk dir for podman storage and /work
#                      (default $TMPDIR/dftracer-ci, or /tmp/$USER/dftracer-ci)
#   DFTRACER_CI_IMAGE  image to run (default localhost/dftracer-ci-dev:latest)
#   DFTRACER_CI_DEPS_IMAGE  deps image tag (default localhost/dftracer-ci-deps:latest)
#   DFTRACER_CI_SHM    /dev/shm size for the container (default 16g)
#   JOBS, CTEST_JOBS   build and test parallelism (default: all cores)
#   DFTRACER_CI_MOUNTS extra mounts, space separated host:container pairs
#                      (e.g. a local brahma clone to build dependencies from)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${DFTRACER_SRC:-$(cd "$HERE/../.." && pwd)}"
STATE="${DFTRACER_CI_STATE:-${TMPDIR:-/tmp/${USER:-$(id -un)}}/dftracer-ci}"
IMAGE="${DFTRACER_CI_IMAGE:-localhost/dftracer-ci-dev:latest}"
DEPS_IMAGE="${DFTRACER_CI_DEPS_IMAGE:-localhost/dftracer-ci-deps:latest}"

# Rootless podman keeps its storage under $HOME by default, which on NFS or
# Lustre cannot hold image layers; podman 4.9 also ignores
# CONTAINERS_STORAGE_CONF for that, so pass --root/--runroot.
mkdir -p "$STATE/storage" "$STATE/run" "$STATE/work"
pm() { podman --root "$STATE/storage" --runroot "$STATE/run" "$@"; }

# Sites that inspect TLS (LLNL does) sign traffic with their own CA, which the
# image does not trust. Lend it the host's CA bundle for builds and runs
# instead of baking a site certificate into the image. Absent on GitHub's
# runners, so nothing is mounted there.
CA_MOUNT=()
if [ -d /etc/pki/ca-trust/extracted ]; then
  CA_MOUNT=(-v /etc/pki/ca-trust/extracted:/etc/pki/ca-trust/extracted:ro)
fi

# dftracer_service's producer/consumer rings live in /dev/shm, and podman's
# default of 64 MiB fills after a few concurrent tests.
SHM_SIZE="${DFTRACER_CI_SHM:-16g}"

EXTRA_MOUNTS=()
for _mount in ${DFTRACER_CI_MOUNTS:-}; do
  EXTRA_MOUNTS+=(-v "$_mount:Z")
done

run() {
  pm run --rm ${TTY:-} "${CA_MOUNT[@]}" "${EXTRA_MOUNTS[@]}" --shm-size="$SHM_SIZE" \
    -v "$SRC:/src:Z" -v "$STATE/work:/work:Z" -v "$HERE:/ci:ro,Z" \
    -e JOBS="${JOBS:-}" -e CTEST_JOBS="${CTEST_JOBS:-}" \
    "$IMAGE" "$@"
}

cmd=${1:-}
shift || true
case "$cmd" in
  image) pm build "${CA_MOUNT[@]}" -t "$IMAGE" -f "$HERE/Containerfile.dev" "$@" "$HERE" ;;
  deps-image)
    # The build context comes from variants.yaml, generated in the CI-dev
    # image (it has PyYAML) so the host needs nothing but podman.
    # A host python with PyYAML does it without the CI-dev image.
    ctx="$STATE/work/deps-context"
    mkdir -p "$ctx"
    gen='v=$0/tools/ci/variants.py; c=$1
      $2 $v check && $2 $v spack-env >$c/spack.yaml &&
      for g in mpi hdf5-serial hdf5-parallel other; do
        $2 $v spack-group $g >$c/spack-$g.txt; done &&
      $2 $v coverage-specs >$c/coverage-mpi.txt &&
      $2 $v papi-versions >$c/papi-versions.txt &&
      $2 $v rocm-repos >$c/rocm.repo && $2 $v gpu-packages >$c/gpu-packages.txt'
    if python3 -c 'import yaml' 2>/dev/null; then
      bash -c "$gen" "$SRC" "$ctx" python3
    else
      run bash -c "$gen" /src /work/deps-context python
    fi
    cp "$HERE/Containerfile.deps" "$SRC/tools/ci/spack_externals.py" "$ctx/"
    pm build "${CA_MOUNT[@]}" -t "$DEPS_IMAGE" -f "$ctx/Containerfile.deps" "$@" "$ctx" ;;
  deps-push)
    # Tagged by a hash of what goes into it (tools/ci/deps-image-tag.sh), the
    # tag .github/workflows/modules.yml pulls, and as latest.
    repo=${1:-docker.io/hdevarajan92/dftracer-ci}
    tag=$("$SRC/tools/ci/deps-image-tag.sh")
    for t in "$tag" latest; do
      pm tag "$DEPS_IMAGE" "$repo:$t"
      pm push "$repo:$t"
    done
    echo "pushed $repo:$tag (and :latest)" ;;
  build) run bash /ci/ci-build.sh "$@" ;;
  shell) TTY=-it run bash ;;
  exec) run "$@" ;;
  *) sed -n '2,27p' "$0"; exit 2 ;;
esac
