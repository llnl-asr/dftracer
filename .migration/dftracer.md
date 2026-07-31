# Migration Plan: dftracer

Selected: 2026-07-30. Source: `git@github.com:llnl/dftracer.git` (develop). Target: `ssh://git@czgitlab.llnl.gov:7999/dftracer/dftracer.git` — **repo already exists with `develop` @ 26fb8d9, identical to GitHub develop**; migration commits go on `gitlab-migration` branched from that.

## Findings

- Local `develop` up to date; gitlab/develop == origin/develop (26fb8d9).
- CI: 10 workflows — ci, benchmark, dlio-benchmark, docker-publish, format-check, hdf5-mpi-trace-ci, prerelease, python-publish, valgrind-ci, wheels (`pull_request_event.json` is a fixture, not a workflow).
- Docs: full Sphinx tree in `docs/` (conf.py + many .rst) — convert to Pages as-is, keep RTD-compatible.
- Dependencies (`dependency/CMakeLists.txt`), in-place URL changes to GitLab (with `NOTE(gitlab-migration)` comments, tracked in REVERT.md):
  - cpp-logger v0.0.8: `https://github.com/hariharan-devarajan/cpp-logger.git` → `ssh://git@czgitlab.llnl.gov:7999/dftracer/cpp-logger.git` (tag verified on gitlab)
  - brahma v1.1.0: `https://github.com/hariharan-devarajan/brahma.git` → `ssh://git@czgitlab.llnl.gov:7999/dftracer/brahma.git` (tag verified on gitlab)
  - yaml-cpp / pybind11 / libuv: third-party upstreams, NOT mirrored in the dftracer group — left on their GitHub URLs.

## Steps

1. [x] Branch `gitlab-migration` from develop
2. [x] Convert workflows → single `.gitlab-ci.yml` (publish/docker/wheels jobs `when: manual` on tags; benchmark/valgrind/hdf5-mpi as regular or manual jobs per complexity; format-check as lint job)
3. [x] Dependency URL changes (cpp-logger, brahma) in place + NOTE comments
4. [x] `pages` job building existing Sphinx docs (develop + temporary gitlab-migration rule)
5. [x] Local tests: YAML parses; sphinx build of docs/ succeeds; cmake configure skipped (dependency clones need auth from the login node — environment-only)
6. [x] Update REVERT.md in-place-changes section; sync `.migration/` into repo
7. [x] Push tags + `gitlab-migration` to gitlab (develop already there)
8. [ ] Pipeline: <https://czgitlab.llnl.gov/dftracer/dftracer/-/pipelines>
9. [ ] User merges after green pipeline

## Executed changes (what to undo on revert)

- Branch `gitlab-migration` created from develop @ 26fb8d9 and pushed to gitlab, with:
  - Commit A `576b045` "ci: add GitLab CI; fetch cpp-logger and brahma from GitLab" — rewrote `.gitlab-ci.yml` (merging the converted GitHub jobs with the PRE-EXISTING LC HPC tuolumne/corona jobs, which keep web-only triggering via explicit rules) and made the two dependency URL changes below.
  - Commit B — `.migration/REVERT.md` + `.migration/dftracer.md` copied into the repo.
- **IN-PLACE changes (MUST fix on revert)** in `dependency/CMakeLists.txt`, each marked with a `NOTE(gitlab-migration)` comment:
  - cpp-logger v0.0.8: `https://github.com/hariharan-devarajan/cpp-logger.git` → `ssh://git@czgitlab.llnl.gov:7999/dftracer/cpp-logger.git`
  - brahma v1.1.0 `GIT_REPOSITORY`: `https://github.com/hariharan-devarajan/brahma.git` → `ssh://git@czgitlab.llnl.gov:7999/dftracer/brahma.git`
- **Caveat**: dftracer already had a tracked `.gitlab-ci.yml` (LC HPC jobs, web-only workflow rules). The migration commit MODIFIES that file (not purely additive): workflow rules widened to web + push + merge_request_event; each pre-existing job got an explicit `web`-only rule so its behavior is unchanged. On revert, restore the file from develop @ 26fb8d9 rather than deleting it.
- Conversion caveats:
  - ci.yml Coveralls upload + GitHub step summaries + gdb-rerun step: GitHub-only, omitted.
  - benchmark.yml `store-results` job (pushes CSVs back to the PR branch with GITHUB_TOKEN): GitHub-only, replaced by job artifacts.
  - wheels/prerelease/python-publish/docker-publish are `when: manual` and need a docker-capable runner (cibuildwheel / docker:dind); secrets: `PYPI_TOKEN`, `DOCKER_USERNAME`, `DOCKER_PASSWORD` as GitLab CI/CD variables.
  - 2026-07-30 follow-up: converted jobs no longer use docker images — they run on the LC **corona batch runner (1 node)** via a `.corona-batch` template (`extends: .corona`, `tags: [batch, corona]`, `SCHEDULER_PARAMETERS: "-N 1 -q $SMALL_QUEUE -t $SMALL_QUEUE_WALLTIME"`), using `module load $GCC_MODULE $PYTHON_MODULE $MPI_MODULE` + a python venv instead of apt/PPA installs.
  - Matrix reductions (module toolchain makes version matrices meaningless on corona):
    - ci:build-test: python(3.9–3.12) × gcc(9–13) matrix → single gcc/11.2.1 + python/3.13.2 job; version coverage stays on GitHub.
    - hdf5-mpi-trace: spack mpich/openmpi × hdf5 1.12/1.14 matrix → single mvapich2/2.3.7 + hdf5-parallel module combo.
    - HDF5 support in build jobs is conditional on an `hdf5-parallel` module being available.
  - wheels / python-publish / prerelease:publish remain manual and CANNOT run on corona (cibuildwheel needs docker/manylinux); docker-publish keeps docker:dind. All need a docker-capable runner assigned before triggering.
  - pages runs on corona batch too (venv + sphinx). Note: corona $SMALL_QUEUE (pdebug) walltime is 60 min — heavy jobs (valgrind, dlio) may need a larger queue/walltime; tune SCHEDULER_PARAMETERS per job if they hit the limit.

## Status log

- 2026-07-30: gitlab remote added; gitlab develop verified identical to github; plan created.
- 2026-07-30: migration executed — `.gitlab-ci.yml` conversion merged with pre-existing LC HPC CI, dependency URLs switched, pages job added, YAML + sphinx build verified locally (sphinx: build succeeded, 50 warnings), commits pushed to gitlab `gitlab-migration`.
- 2026-07-30 (follow-up): per user request, converted GitHub jobs moved off docker images onto the corona batch runner (1 node, `.corona-batch` template); matrices collapsed to the corona module toolchain; YAML re-validated; committed "ci: run converted jobs on corona batch runner (1 node)" and pushed.
- 2026-07-30: Dependency URLs switched from https (lc.llnl.gov) to ssh form ssh://git@czgitlab.llnl.gov:7999/dftracer/{cpp-logger,brahma}.git (user request); both verified reachable (v0.0.8, v1.1.0).

- 2026-07-30: CI restructured to corona flux-allocation flow — one allocation per normal pipeline (single `ci` job runs format-check, build-test, valgrind, hdf5-mpi-trace, docs phases via `flux proxy` on one `flux batch` allocation, pbatch/480m); benchmark and dlio-benchmark are now manual with their own allocations; MR opened.
- 2026-07-30: Flux allocation made global via allocate/.flux-jobid artifact/release-allocation jobs; wait-event timeouts removed everywhere (manual jobs keep self-contained flows).
- 2026-07-30: branch rebuilt onto merged develop; allocate (and manual jobs) switched to flux alloc --bg.
- 2026-07-30: CI phases now run inside podman containers on the allocated node via 'flux run -N 1 bash .gitlab/flux-ci/run-in-podman.sh <image> <phase>.sh' (ubuntu:22.04 for build/test/valgrind/format, hdevarajan92/brahma-ci for hdf5-mpi-trace, python:3.11 for docs/publish). toolchain.sh now does in-container apt setup instead of LC module loads; no module loads remain. Pattern validated on cpp-logger.
