=====================================
Composable install (MPI, HDF5, GPUs)
=====================================

DFTracer's MPI, HDF5, PAPI, hwloc, HIP and CUDA support each wraps a library
whose interface changes between versions, so it has to be compiled against
the version the application runs with. A composable install ships that
support prebuilt, once per library version, and links the ones that fit the
machine into ``libdftracer_core`` on the machine itself:

* ``dftracer`` (one wheel per Python version) holds the core: a
  ``libdftracer_core.so`` that traces POSIX and STDIO and works anywhere, the
  Python bindings, the tools, and a *compose tree* with the core as archives.
* ``dftracer-modules`` (one wheel for every Python version) adds a prebuilt
  archive per *module variant*: per MPI implementation and version, HDF5
  version (serial, or parallel per MPI ABI family), PAPI, hwloc, ROCm and
  CUDA release.
* ``dftracer-compose`` detects which of those libraries the machine has,
  picks the variant of each module that fits, builds from source any the
  wheel lacks, and relinks ``libdftracer_core.so`` in place.

Nothing is composed by ``pip install`` itself: run ``dftracer-compose`` once
after installing, in the environment you will trace in (the same ``module
load`` / ``spack load``), and again after that environment changes.

.. contents::
   :local:
   :depth: 1

Quick start
===========

.. code-block:: bash

    pip install --pre "dftracer[modules]"   # the core and the prebuilt modules
    dftracer-compose                        # link what this machine needs
    dftracer_install_info --composition     # what got linked

Step by step: a Cray EX system
==============================

This walk-through is on LLNL's Tuolumne (MI300A, Cray PE, ROCm), and shows
every stage. On another system, load your own MPI, HDF5, PAPI and ROCm or
CUDA instead.

1. Load the environment the application runs in
-----------------------------------------------

The compose step detects libraries the way a CMake build would: through the
loaded modules, ``PATH``, ``CMAKE_PREFIX_PATH`` and the variables below.

.. code-block:: bash

    module load PrgEnv-gnu cmake/3.29.2
    module load cray-mpich/9.0.1 cray-hdf5-parallel/1.14.3.7 papi/7.1.0.4 rocm/6.4.3
    export CC=cc CXX=CC                       # the Cray compiler wrappers
    export ROCM_PATH=/opt/rocm-6.4.3          # where the HIP module looks

2. Install the wheels
---------------------

.. code-block:: bash

    python3 -m venv ~/venvs/dftracer && . ~/venvs/dftracer/bin/activate
    pip install -v --pre "dftracer[modules]"

``-v`` shows pip resolving both wheels; nothing is compiled. The install
takes about 45 seconds, most of it downloading ``dftracer-modules`` (about
40 MB). Afterwards the package directory holds:

.. code-block:: text

    site-packages/dftracer/
      lib64/libdftracer_core.so            core only: POSIX and STDIO
      lib64/libdftracer_preload.so         for LD_PRELOAD
      lib64/dftracer/compose/              the compose tree
        CMakeLists.txt  DFTracerDetect.cmake  DFTracerCompose.cmake  ...
        core/      libdftracer_core_base.a, libbrahma.a, source and interface hashes
        runtime/   libstdc++.a, libgcc.a (linked in statically)
        modules/   from dftracer-modules:
          mpi/mpi-craympich-9.0/   component.json  component.cmake  lib*.a
          hdf5/hdf5-1.14-par+mpich-abi/ ...
      share/dftracer/source/               sources, for variants built here
      bin/dftracer_install_info

3. See what would be linked
---------------------------

.. code-block:: bash

    dftracer-compose --check

.. code-block:: text

      mpi    mpi-craympich-9.0            linked
      hdf5   hdf5-1.14-par+mpich-abi      linked
      papi   papi-7.1                     linked
      hwloc  hwloc-2                      linked
      hip    rocprof-0.6                  linked
      cuda   -                            absent

Each line is a module, the variant this machine needs, and its status:

=================  =============================================================
``linked``         the tree has the variant; it will be linked
``missing``        the machine has the library but the tree has no variant for
                   it; ``dftracer-compose`` builds it from source
``stale``          the tree's variant was compiled against other core headers
                   than this core (another DFTracer version); it is rebuilt
                   from source
``absent``         the machine does not have the library; the module is left out
=================  =============================================================

4. Compose, watching each step
------------------------------

.. code-block:: bash

    dftracer-compose --verbose

``--verbose`` prints every command it runs and streams its output (each
line prefixed with ``|``); without it, only the steps are printed and the
output goes to log files. The steps are:

#. *detect*: configure the compose project in detect mode, which runs the
   same probes as a DFTracer build (an MPI test program, the HDF5, PAPI,
   hwloc, rocprofiler-sdk and CUPTI headers) and writes the variant keys;
#. *build* (only for ``missing`` or ``stale`` modules, and for those named
   with ``--from-source``): ``build-module.sh`` from the shipped sources,
   one module at a time; each part is copied into the tree under a
   temporary name and renamed into place, so one that fails to copy leaves
   the tree's previous part usable;
#. *link*: build ``libdftracer_core.so`` and ``libdftracer_core_dbg.so``
   from the core archives, the chosen variants and the static C++ runtime;
#. *install*: install into a staging directory, then move each file into
   ``lib64`` with a rename, ``lib64/dftracer/composition.txt`` last. A
   process starting meanwhile finds the old library or the new one, never
   neither; one that already has the library loaded keeps the old copy.

Before any of this, ``dftracer-compose`` checks that it can write the
install (the tree, ``lib64`` and the settings) and stops with a message if
not: compose an install you own, such as one in a virtual environment. It
holds a lock (``lib64/dftracer/.compose.lock``) for the whole run, so a
second ``dftracer-compose`` of the same install waits for the first.

When everything is prebuilt this takes 15 to 20 seconds; each module built
from source adds one to two minutes. ``--keep`` keeps the work directory
(the logs of every step), which is also kept whenever a step fails.

The command exits non-zero if a module the machine has could not be linked
(it says which and where the logs are), or if a module asked for with
``--from-source`` did not build, in which case its prebuilt variant is
linked instead and the command says so.

5. Check the result
-------------------

.. code-block:: bash

    dftracer_install_info --composition

.. code-block:: text

    Composition   (libdftracer_core as dftracer-compose linked it)
      compose tree          : .../site-packages/dftracer/lib64/dftracer/compose
      record                : .../site-packages/dftracer/lib64/dftracer/composition.txt
      core                  : prebuilt, interface a5c1c6316a16

      module variant                      status   origin    built against
      ✔ mpi   mpi-craympich-9.0            linked   prebuilt  impl=CRAYMPICH version=9.0.0
      ✔ hdf5  hdf5-1.14-par+mpich-abi      linked   prebuilt  parallel=ON version=1.14.6
      ✔ papi  papi-7.1                     linked   prebuilt  version=7.1.0
      ✔ hwloc hwloc-2                      linked   prebuilt  version=2.11.1
      ✔ hip   rocprof-0.6                  linked   prebuilt  rocprofiler-sdk=0.6.0
        cuda  -                            absent   -
             this machine does not have the library

*origin* is ``prebuilt`` for a variant from the wheels and ``source`` for
one built on this machine (with the host and time). *built against* is the
library release the variant was compiled with; a variant covers every
release of its series (``hdf5-1.14`` covers HDF5 1.14.x). Without
``--composition``, ``dftracer_install_info`` prints the same section ahead
of the core build's own configuration.

6. Trace
--------

.. code-block:: bash

    export DFTRACER_ENABLE=1 DFTRACER_INIT=PRELOAD DFTRACER_LOG_FILE=$PWD/trace
    LD_PRELOAD=$(dftracer-preload-library-path) ./my_mpi_hdf5_app
    zcat trace*.pfw.gz | grep -o '"cat":"[^"]*"' | sort | uniq -c

MPI calls appear under the ``env``, ``comm``, ``collective`` and ``MPIIO``
categories, HDF5 ones under ``h5f``, ``h5d``, ``h5p`` and so on.

7. Rebuild from source (optional)
---------------------------------

To build a module here instead of using its prebuilt variant, for example
for an MPI build the wheel does not cover:

.. code-block:: bash

    dftracer-compose --from-source mpi --verbose
    dftracer-compose --from-source all          # the core and every module
    DFTRACER_BUILD_FROM_SOURCE=hdf5 dftracer-compose

A source build needs CMake 3.24 or newer and a C/C++ compiler, and fetches
DFTracer's dependencies (brahma, gotcha, ...) from GitHub. It uses the same
loaded environment.

On a ROCm machine whose PAPI is missing or has no ``rocp_sdk`` component (the
Cray PAPI modules have none), ``dftracer-compose`` builds PAPI with
``rocp_sdk`` into ``<libdir>/dftracer/papi`` and links the PAPI module against
it, so GPU counters can be collected:

.. code-block:: bash

    dftracer-compose                       # --build-papi auto, the default
    dftracer-compose --build-papi always   # also on a machine without ROCm
    dftracer-compose --build-papi never    # keep the PAPI this machine has

Later runs keep using that PAPI. ``DFTRACER_BUILD_PAPI`` sets the default.

``dftracer-compose`` also provides the Variorum that ``dftracer_service``
reads node power with (see `Variorum`_). Each library it can build has a
``--build-*`` flag and an environment variable for its default:

======================  ===========================  ==========================================
Flag                    Default from                 ``auto`` builds it when
======================  ===========================  ==========================================
``--build-papi``        ``DFTRACER_BUILD_PAPI``      this ROCm machine has no PAPI, or a PAPI
                                                     with no ``rocp_sdk`` component
``--build-variorum``    ``DFTRACER_BUILD_VARIORUM``  no Variorum 0.8 or later reads a power
                                                     domain here, and the machine has a power
                                                     interface Variorum can read
======================  ===========================  ==========================================

Each flag takes ``auto`` (the default), ``always`` or ``never``. A library
built by an earlier run is used again and not rebuilt, except with
``always``. ``--no-build`` builds no Variorum.

Changing a composition
======================

A composition can be edited one step at a time. The choices are kept in
``lib64/dftracer/compose-settings.json``, and every later ``dftracer-compose``
starts from them, so a plain ``dftracer-compose`` after an environment change
keeps them too.

.. code-block:: bash

    dftracer-compose --status                      # what is linked now; relinks nothing
    dftracer-compose --disable hip,papi            # relink without HIP and PAPI
    dftracer-compose --enable papi                 # PAPI back in
    dftracer-compose --variant mpi=mpi-openmpi-5.0 # that variant, not the detected one
    dftracer-compose --variant mpi=auto            # back to the detected one
    dftracer-compose --reset                       # forget every choice above
    dftracer-compose --disable hip --check         # preview an edit; change nothing

.. code-block:: text

    $ dftracer-compose --status
    dftracer-compose: .../site-packages/dftracer/lib64/libdftracer_core.so
      mpi    mpi-craympich-8.1            linked   prebuilt
      hdf5   hdf5-1.14-par+mpich-abi      linked   prebuilt
      hwloc  hwloc-2                      linked   prebuilt
      cuda   -                            absent
      papi   -                            off
      hip    -                            off
    settings: disabled: hip, papi

A disabled module is recorded as ``off``, and ``dftracer_install_info`` shows
it that way. An edit is saved only once the library it describes is
installed: if the link or the staged install fails, the settings and the
installed library stay as they were. The settings file is written through a
temporary file; one that cannot be read (cut short, edited into another
shape) is ignored with a message, and ``--reset`` removes it. A variant chosen with ``--variant`` still links this machine's
library; when that library would need another variant, ``dftracer-compose``
warns (``linking mpi-craympich-9.1 as asked, but this machine's library is
mpi-craympich-8.1``), since the module may then wrap functions the library
does not have.

Prebuilt variants
=================

``dependency/variants.yaml`` lists every library version a variant is built
against; ``dftracer_install_info --composition`` lists the ones an install
holds. At the time of writing:

======  ======================================================================
mpi     OpenMPI 4.1 and 5.0, MPICH 3.4 and 4.2, MVAPICH2 2.3, Cray MPICH 8.1,
        9.0 and 9.1
hdf5    1.10, 1.12 and 1.14, each serial and parallel (``par+mpich-abi`` for
        MPICH, MVAPICH, Cray MPICH and Intel MPI; ``par+openmpi-abi`` for
        OpenMPI)
papi    5.6 (the RHEL 8 / TOSS 4 system PAPI), 6.0, 7.0, 7.1 and 7.2
hwloc   1 and 2
hip     rocprofiler-sdk 0.4, 0.6 and 1.0 (ROCm 6.2, 6.4 and 7.0)
cuda    CUPTI from CUDA 11.8, 12.4 and 12.8
======  ======================================================================

A parallel HDF5 variant is per MPI *ABI family*, not per MPI: HDF5's parallel
API is the same with every MPI, but MPI handles are ``int`` in MPICH and its
derivatives and pointers in OpenMPI, so a module compiled for one family
would truncate the other's handles.

Cray MPICH is not redistributable, so its variants are compiled against the
MPICH release each Cray release derives from, with Cray's version macro
(``CRAY_MPICH_VERSION``) supplied; every function DFTracer's Cray MPICH
interfaces wrap is declared by that MPICH. They are tested against Cray MPICH
8.1.33 and 9.0.1 on Tuolumne.

Variorum
--------

Node power from `Variorum <https://github.com/LLNL/variorum>`_ needs no
module variant: ``dftracer_service`` loads ``libvariorum.so`` when it starts,
so the wheel's service reads power on any machine that has Variorum 0.8 or
later (the release that added ``variorum_get_power_json``; 0.6 and 0.7 name
it differently). Variorum decides which power domains it can read (CPU, GPU,
memory) when it is built, and a distribution's build often reads none.

So ``dftracer-compose`` checks the Variorum this machine has. It loads
``libvariorum.so`` in a child process, calls ``variorum_get_power_json``
once and lists the domains that hold a reading. When there is no Variorum
0.8 or later, or the one found reads no domain, it builds Variorum 0.8.0
into ``<libdir>/dftracer/variorum`` with the back ends this machine has:

==========  ==================================================================
AMD CPU     E-SMI through ``/dev/hsmp``. ``dftracer-compose`` builds E-SMI
            too when it is not installed.
AMD GPU     ROCm SMI from ``$ROCM_PATH`` (or ``ROCM_DIR``, ``hipcc`` on
            ``PATH``, ``/opt/rocm`` or the newest ``/opt/rocm-<version>``).
NVIDIA GPU  NVML: the driver's ``libnvidia-ml.so.1`` and ``nvml.h`` from a
            CUDA toolkit (``CUDA_HOME``, ``CUDA_PATH``, ``nvcc`` on ``PATH``
            or ``/usr/local/cuda``). Set ``CUDA_HOME`` where the system CUDA
            has no ``nvml.h``.
Intel CPU   msr-safe (``/dev/cpu/msr_batch``). Without it the Intel part is
            left out, since it would fail every power reading, GPU included.
==========  ==================================================================

The build needs git, CMake, a C compiler and the hwloc and jansson
development files. Variorum 0.8.0 is patched so that it builds with its AMD
CPU back end, accepts the MI300A CPU (family 0x19, model 0x90) and, on the
MI300A, does not add the GPU power to ``power_node_watts`` a second time
(the CPU socket power of an APU already holds it). When a later release no
longer fits a patch, the build warns, and ``dftracer-compose`` warns when
the Variorum it built reads no power for an interface the machine has:

.. code-block:: text

    dftracer-compose: warning: the Variorum built in .../variorum reads no cpu power, though this machine has AMD CPU (/dev/hsmp); ...

On a machine with none of ``/dev/hsmp``, ROCm SMI, NVML or msr-safe nothing
is built, and the service runs as before, with no power records.

``dftracer_service`` picks its library in this order:

1. ``DFTRACER_VARIORUM_LIBRARY=/path/to/libvariorum.so``, when it is set.
   The service uses that library as is, and reads no power when it does not
   load.
2. ``<libdir>/dftracer/variorum/lib/libvariorum.so`` (or ``lib64``), the one
   ``dftracer-compose`` built next to the ``libdftracer_core`` the service
   loaded, when it exists.
3. The first ``libvariorum.so`` the loader finds. On a system that ships a
   Variorum package (TOSS 4 does, in ``/lib``) that is the package's, ahead
   of ``LD_LIBRARY_PATH``.

``dftracer-compose --status`` and ``dftracer_install_info --composition``
show which Variorum the service loads and its domains:

.. code-block:: text

    $ dftracer-compose --status
    ...
      variorum built here (tuolumne1001, 2026-11-24T10:12:03) .../lib64/dftracer/variorum/lib/libvariorum.so, domains: cpu, gpu

The status is ``found`` for a Variorum the machine already had and
``absent`` when the service has none. On an MI300A node the CPU and GPU
readings are both the power of the whole APU socket, as E-SMI and ROCm SMI
report it. E-SMI needs only read access to ``/dev/hsmp`` for power.

A pip install puts ``dftracer_service`` on ``PATH``; it runs the service in
``site-packages/dftracer/bin``.

Troubleshooting
===============

``hwloc-unknown`` / ``papi-unknown``
    The library was found but its version header was not. Point the search at
    the install (``HWLOC_DIR``, ``PAPI_DIR``) or load its module.

The MPI variant is the wrong Cray MPICH release
    The MPI probe reads ``CRAY_MPICH_VERSION`` from the ``mpi.h`` the compiler
    sees. Check that ``cc`` is the Cray wrapper (``export CC=cc CXX=CC``) and
    that the ``cray-mpich`` module you want is the loaded one. (Asking the
    library instead gives the release ``/opt/cray/pe/lib64`` points to, the
    system default, whatever module is loaded.)

``undefined symbol`` from ``libdftracer_core.so`` at run time
    Usually a core rebuilt from source against a library the wheel also
    bundles, at another version. Rerun ``dftracer-compose --from-source core
    --verbose`` and check which ``yaml-cpp`` it found; DFTracer requires
    0.6.3 or newer.

Starting over
    ``pip install --force-reinstall --no-deps dftracer`` restores the core-only
    library; ``pip install --force-reinstall --no-deps dftracer-modules``
    restores the prebuilt variants (source-built ones replace them in the
    tree).

How the variants are built
==========================

CI builds each variant in its own job, in a container image that holds every
library version in ``variants.yaml`` (built with spack, on the toolchain the
wheels use). The image is on Docker Hub, tagged by a hash of what goes into it,
and is built and pushed from a machine with the cores for it:

.. code-block:: bash

    infrastructure/ci/podman.sh deps-image     # a few hours from scratch
    podman login docker.io
    infrastructure/ci/podman.sh deps-push      # docker.io/hdevarajan92/dftracer-ci:<hash>

A change to ``variants.yaml`` or ``Containerfile.deps`` changes the hash, and
the modules workflow stops until that image is pushed. CI then merges the jobs' trees, checks that every variant is there
and was built from the same sources as the core, and packs
``dftracer-modules`` (``.github/workflows/modules.yml``). Each variant's
``component.json`` records its library versions, a hash of the sources it was
built from, and a hash of the core headers it was compiled against (its
interface): ``dftracer-compose`` links a variant only when that interface hash
matches the core's, and marks it ``stale`` otherwise.
