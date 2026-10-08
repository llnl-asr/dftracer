# manylinux wheel builds (cibuildwheel + podman)

Builds `dftracer` wheels for CPython 3.9 – 3.14 locally. Every C/C++ dependency
is a git tag listed in [`dependency/manifest.txt`](../../dependency/manifest.txt),
cloned and built once into a shared prefix and then bundled into the wheel.

## Quick start

```bash
scripts/wheel/build_wheels.sh                 # cp39..cp314, glibc 2.28 baseline
scripts/wheel/build_wheels.sh --list          # show the plan, build nothing
scripts/wheel/build_wheels.sh --python 3.12   # single interpreter
scripts/wheel/build_wheels.sh --glibc 2.34    # newer baseline
```

Wheels land in `wheelhouse/` (override with `--output`). Verified output of a
full run:

```
dftracer-2.0.3-cp39-cp39-manylinux_2_28_x86_64.whl
dftracer-2.0.3-cp310-cp310-manylinux_2_28_x86_64.whl
dftracer-2.0.3-cp311-cp311-manylinux_2_28_x86_64.whl
dftracer-2.0.3-cp312-cp312-manylinux_2_28_x86_64.whl
dftracer-2.0.3-cp313-cp313-manylinux_2_28_x86_64.whl
dftracer-2.0.3-cp314-cp314-manylinux_2_28_x86_64.whl
```

## The scripts

| script | where it runs | what it does |
| --- | --- | --- |
| `build_wheels.sh` | host | picks manylinux images, prepares podman, drives `cibuildwheel` |
| `manifest.py` | host or CI | checks that `dependency/manifest.txt` and the pins in `dependency/CMakeLists.txt` agree |
| `build_deps.sh` | inside the container (`CIBW_BEFORE_ALL`) | clones and builds gotcha → cpp-logger → yaml-cpp → libuv → brahma into `/opt/dftracer-deps` and normalises where their CMake configs live |
| `repair_wheel.sh` | inside the container (`CIBW_REPAIR_WHEEL_COMMAND`) | points `LD_LIBRARY_PATH` at dftracer's own libs and the dependency prefix, then runs `bundle_wheel.py prepare` → `auditwheel repair` → `bundle_wheel.py finish` |
| `bundle_wheel.py` | inside the container | bundles the dependency libraries under their real sonames and keeps auditwheel's patchelf pass away from the native executables |
| `test_wheel.py` | inside the container (`CIBW_TEST_COMMAND`) | validates each installed wheel: imports, bundled libraries, executables, a real trace |

## Testing

Every wheel is installed into a fresh virtualenv in the container and validated
before it reaches `wheelhouse/`; a failing check fails the build. The checks, in
order:

1. `import dftracer` works, from the wheel and not the source tree, and reports
   the version in `PACKAGE_VERSION`
2. both pybind11 extension modules import and expose the C API
   (`initialize`, `log_event`, `finalize`, ...)
3. every bundled shared library `dlopen()`s -- `dftracer/lib64/*.so*` and
   `dftracer.libs/*.so*`
4. every native executable in `dftracer/bin` starts, run from `/` with
   `LD_LIBRARY_PATH` and `LD_PRELOAD` scrubbed, so a wheel that only works
   because of the build environment fails here
5. end to end trace: `dftracer_service start`, an `LD_PRELOAD`ed workload doing
   1 MiB of file I/O, `dftracer_service stop`, then the `.pfw` trace is parsed
   and must contain POSIX events -- this is what proves gotcha/brahma
   interception survived packaging
6. the `pydftracer` API layer, if installed, can log through the wheel

```
=== end to end trace (service + LD_PRELOAD) ===
service start (exit 0):
workload (exit 0): workload done
trace      : trace-69fa2f3f4d45a11e-preload.pfw.gz (4842 bytes)
events     : 1219 (14 POSIX)
PASS
```

`--no-test` skips this entirely; `--test-packaging-only` runs checks 1-4 and
skips the tracing ones (useful when the container cannot run a daemon).

The same script runs in CI -- see
[`.github/workflows/wheels.yml`](../../.github/workflows/wheels.yml), one job per
interpreter, docker instead of podman, and nothing is published on a release
unless every wheel built *and* passed its checks.

## The native executables and `auditwheel`

`auditwheel repair` rewrites `DT_NEEDED` and RPATH with patchelf on every ELF
file in the wheel. For shared libraries that is fine. For
`dftracer/bin/dftracer_service` it is not: the rewrite has to grow `.dynstr`, and
the file it produces has `PT_DYNAMIC` sitting in a hole that no `PT_LOAD` segment
covers. The kernel faults before the loader prints anything, so the tool dies
with SIGSEGV and no diagnostic. `readelf` reports nothing unusual; only `gdb`
hints at it (`Loadable section ".dynstr" outside of ELF segments`).

Measured on a wheel repaired the plain way:

```
dftracer/bin/dftracer_service   PT_DYNAMIC=(0x50c8,688)  covered_by_PT_LOAD=False
                                PT_LOAD=[(0x0,0x50a8), (0x5f7e,0x11a2), ...]
```

Redoing the same rewrite with a newer patchelf gives a structurally valid file
that still crashes, so `bundle_wheel.py` avoids the rewrite altogether:

* **prepare** copies the vendored dependency libraries into `dftracer/lib64`
  under their real sonames (RPATH `$ORIGIN`), then lifts the ELF executables out
  of the wheel so auditwheel cannot touch them.
* **finish** puts them back with RPATH `$ORIGIN/../lib64:$ORIGIN/../lib` -- a
  string short enough to fit the existing `.dynstr`, with no soname replacement --
  and verifies each one starts from the unpacked wheel layout.

Note this only affects wheels. A `pip install .` bakes absolute RPATHs pointing
at the real install prefix and nothing rewrites the binaries, which is why the
problem never shows up in a source install (such an install is, in exchange, not
relocatable).

A related fix in [`CMakeLists.txt`](../../CMakeLists.txt): the RPATH block used
to overwrite `CMAKE_INSTALL_RPATH`, discarding any value the caller passed on the
command line. It now keeps the caller's entries and appends the dependency
directories after them.

## The CMake source path

`pip install .` and `autobuild.sh` do not go through this directory's scripts --
they run CMake's own dependency pass (`-DDFTRACER_INSTALL_DEPENDENCIES=ON`), which
clones each dependency at the tag pinned in
[dependency/CMakeLists.txt](../../dependency/CMakeLists.txt). `find_package`
short-circuits a clone when the dependency is already installed.

## Caching

Three host directories are mounted into the container, under
`$DFTRACER_WHEEL_CACHE` (default `$DFTRACER_WHEEL_STATE/cache`):

| cache | what it saves |
| --- | --- |
| the dependency prefix | the five C++ libraries are built once, not once per run |
| ccache | dftracer's own objects are identical for every interpreter apart from the pybind module, so each extra interpreter costs ~40s instead of ~2min |
| pip | stops re-downloading cmake/ninja/pybind11 for every build |

The prefix is reused only while `manifest.txt`'s dependency lines hash the same,
so bumping a dependency version rebuilds it. ccache invalidates itself on
included-header contents, the compiler and the command line, so a changed
dependency header recompiles what depends on it. `--rebuild-deps` discards the
prefix; `--no-cache` bypasses all three.

With warm caches, `--jobs 3` builds and tests all six interpreters in under
7 minutes (about 17 sequential and uncached):

```bash
scripts/wheel/build_wheels.sh --jobs 3
```

The first interpreter runs alone to populate the caches, the rest follow N at a
time.

## Source builds: MPI, HDF5 and the other compile-time options

The published wheel is the portable configuration: POSIX and STDIO interception,
which needs nothing from the host. MPI, HDF5 and HIP cannot be shipped that way --
brahma generates version-specific GOTCHA bindings (`BRAHMA_MPI_VERSION`,
`BRAHMA_HDF5_VERSION`), and interception has to hook the library the application
loads, not a copy bundled in the wheel. A wheel tag cannot express "built against
OpenMPI 5.0.6" either, so a matrix of them could not even be published.

Those builds come from the sdist instead, the way mpi4py works:

```bash
DFTRACER_ENABLE_MPI=ON DFTRACER_ENABLE_HDF5=ON \
  pip install --no-binary dftracer dftracer
```

`--no-binary` is what stops pip taking the portable wheel. `setup.py` reads the
`DFTRACER_ENABLE_*` toggles from the environment and CMake links against the
MPI/HDF5 it finds on the host.

The build clones the dependencies from GitHub. pybind11 is not cloned: pip
installs it as a build requirement and `setup.py` points CMake at it.

```
-- [dftracer] dependency: MPI C probe: impl=MVAPICH brahma_version=200307
-- [dftracer] Forwarding BRAHMA_MPI_IMPL=MVAPICH to brahma
```

## glibc baselines

The `--glibc` flag picks the manylinux image, i.e. the oldest glibc the wheels
will run on.

| `--glibc` | image | glibc | gcc | runs on |
| --- | --- | --- | --- | --- |
| `2.28` (default) | `manylinux_2_28_x86_64` | 2.28 | 14.2 | RHEL 8+, Ubuntu 18.10+ |
| `2.34` | `manylinux_2_34_x86_64` | 2.34 | — | RHEL 9+, Ubuntu 21.10+ |
| `2.17` | `manylinux2014_x86_64` | 2.17 | 10.2 | **does not build** |

**2.28 is the floor, not a preference.** brahma's POSIX bindings take the
address of `::fcntl64` (`GOTCHA_BINDING_MACRO(fcntl64, POSIX)`), and glibc only
declares and exports that symbol from 2.28 onwards, so the dependency build
fails on manylinux2014 with `'::fcntl64' has not been declared`. Wheels
therefore cannot support RHEL 7 / Ubuntu 14.04–18.04 until brahma guards that
binding. `--glibc 2.17` is still accepted (with a warning) for anyone who wants
to retry it after a brahma fix.

## How the wheel build hangs together

`setup.py` normally clones cpp-logger, brahma, yaml-cpp, libuv with
`ExternalProject`/`FetchContent`. Instead:

* `DFTRACER_BUILD_DEPENDENCIES=0` disables that dependency pass entirely.
* `build_deps.sh` installs everything into `/opt/dftracer-deps` (libraries in
  `lib64`, with a `lib` symlink) and makes sure every package config is
  reachable at `lib64/cmake/<pkg>`, symlinking the ones that install elsewhere.
  It also records the flags it used in `/opt/dftracer-deps/cmake-args.txt` for
  debugging.
* `DFTRACER_CMAKE_ARGS` carries `-DCMAKE_PREFIX_PATH` plus a `-D<pkg>_DIR` for
  each dependency. `setup.py` appends `DFTRACER_CMAKE_ARGS` last, so these win
  over the defaults it aims at the wheel staging directory. The value is fixed
  on the host rather than read from `cmake-args.txt`, because cibuildwheel
  evaluates `CIBW_ENVIRONMENT` before `CIBW_BEFORE_ALL` runs.
* brahma is built with `BRAHMA_BUILD_DEPENDENCIES=ON`, but since gotcha and
  cpp-logger are already installed its `fetch_package()` resolves them through
  `find_package()`; `FETCHCONTENT_SOURCE_DIR_*` point at the cloned local
  sources as a second line of defence, so nothing is cloned twice.
  The same `cmake/patches/brahma_version_override.cmake` patch dftracer applies
  in its network path is applied here too.
* the dependency libraries are copied into `dftracer/lib64` under their real
  sonames before `auditwheel repair` runs, and auditwheel additionally grafts the
  copies the extension modules link against into `dftracer.libs` with mangled
  names and rewritten RPATHs.

Wheels are built without MPI, HDF5 and hwloc — the portable configuration. For
an MPI/HDF5 enabled wheel, change `DFTRACER_ENABLE_MPI` / `DFTRACER_ENABLE_HDF5`
in the `CIBW_ENVIRONMENT` block of `build_wheels.sh` and add the matching system
packages to `build_deps.sh`; such wheels are only usable against the same MPI
and HDF5 installation.

## Python version floor

3.9 is the lowest supported interpreter, because `pyproject.toml` requires
`pydftracer>=2.0.4` and pydftracer declares `requires-python = ">=3.9"`. A cp38
wheel builds and repairs fine but cannot be installed:

```
ERROR: Could not find a version that satisfies the requirement pydftracer>=2.0.4
       (from versions: 1.0.1, ..., 1.0.13, 1.0.14)
```

so `--python 3.8` is rejected up front rather than producing a wheel nobody can
install. Two things would have to happen first: a pydftracer release that
supports 3.8, and a matching bump of the pin here.

`typing_extensions` is a direct dependency of dftracer even though only
pydftracer uses it: pydftracer 2.0.4 imports it unconditionally while declaring
it only for `python_version < '3.10'`, so without it `import dftracer.python`
fails on 3.10+ with `ModuleNotFoundError`. It can be dropped again once a
pydftracer release fixes that import.

## Notes on this cluster

* Rootless podman defaults its image store to `$HOME`, which on NFS/Lustre
  cannot hold the xattrs image layers need (`lsetxattr: operation not
  supported`). `build_wheels.sh` generates a small `podman` wrapper that adds
  `--root`/`--runroot` under `$DFTRACER_WHEEL_STATE`
  (default `${TMPDIR:-/tmp}/$USER/dftracer-wheels`) on local disk. Set
  `DFTRACER_PODMAN_KEEP_STORAGE=1` to use podman's own configuration instead.
* The repo working tree carries several GB of virtualenvs and build trees, and
  cibuildwheel copies the project into every container. By default the script
  stages a clean copy of the git-tracked files and builds from that; `--in-place` builds from the repo directly.
* Version stamping: setuptools-scm has no usable git checkout in the staged
  copy, so `SETUPTOOLS_SCM_PRETEND_VERSION` is derived the way setuptools-scm
  would from the git tag -- `v2.1.1` gives `2.1.1`, N commits past it gives
  `2.1.1.devN` -- falling back to `PACKAGE_VERSION` when there are no tags.
  Override with `DFTRACER_VERSION`.

## Useful knobs

```bash
--rebuild-deps        # discard /opt/dftracer-deps and rebuild the C++ deps
--test                # pip install each wheel and import dftracer
--free-threaded       # additionally build cp313t/cp314t
--arch aarch64        # emulated/native aarch64 images
CIBW_VERSION=3.2.1    # cibuildwheel pin used by the script
```
