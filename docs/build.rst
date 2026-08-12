===================
Build DFTracer
===================

This section describes how to build DFTracer.

There are three build options:

- build DFTracer with pip (recommended),
- build DFTracer with Spack, and
- build DFTracer with cmake

----------

------------------------------------------
Create Python environment (Recommended)
------------------------------------------
Creating a Python environment is the cleanest way to install DFTracer.

.. code-block:: Bash

    python -m venv <PYTHON-VENV-PATH>
    source <PYTHON-VENV-PATH>/bin/activate


------------------------------------------
Build DFTracer with pip (Recommended)
------------------------------------------

Users can easily install DFTracer using pip. This is the way most python packages are installed.
This method would work for both native python environments and conda environments.


Install DFTracer
*******************************

From PyPI (Recommended)
************************

.. code-block:: Bash

    pip install dftracer

This installs a prebuilt wheel that traces POSIX and STDIO I/O. It requires
nothing from the host, but it cannot trace MPI, HDF5 or HIP; for those see
`Enabling MPI, HDF5 and HIP`_.

Development builds are published from every merge into ``develop``, versioned
``<last release>.postN.dev0``, as both wheels and a source distribution, so a
prerelease can also be rebuilt with MPI or HDF5 support. The ``.dev0`` makes
them PEP 440 pre-releases, so pip only selects them when asked:

.. code-block:: Bash

    pip install --pre dftracer               # newest prerelease
    pip install dftracer==2.1.0.post5.dev0   # a specific one

.. attention::

    For pip installations, all libraries will be present within the site-packages/dftracer/lib.
    This enables clean management of pip installation and uninstallations.

On LLNL LC (dldl group)
************************

Wheels and the matching source distribution are published to the group
workspace, so no PyPI access is needed:

.. code-block:: Bash

    DFT_DIST=/usr/workspace/dldl/dftracer/distributions
    pip install dftracer --find-links $DFT_DIST/wheels        # last release
    pip install --pre dftracer --find-links $DFT_DIST/wheels  # develop build

HTML docs for each version are published alongside them, under
``$DFT_DIST/docs/dftracer/``, with ``latest`` and ``dev`` symlinks. Every
tagged release is kept; the last three develop builds are kept with them.

From source
************

.. code-block:: Bash

    git clone git@github.com:LLNL/dftracer.git
    cd dftracer
    # You can skip this for installing the dev branch.
    # for latest stable version use master branch.
    git checkout tags/<Release> -b <Release>
    pip install .

From Github
************

.. code-block:: Bash

  DFT_VERSION=v1.0.4
  pip install git+https://github.com/LLNL/dftracer.git@${DFT_VERSION}

.. attention::

    For pip installations, all libraries will be present within the site-packages/dftracer/lib.
    This enables clean management of pip installation and uninstallations.

------------------------------------------
Enabling MPI, HDF5 and HIP
------------------------------------------

MPI, HDF5 and HIP support are compile-time options, so they require a build
against the libraries you run with. The prebuilt wheel cannot provide them:
DFTracer intercepts calls into the MPI or HDF5 library the application loads, and
the interception is generated for a specific implementation and version, so a
wheel built elsewhere would trace nothing. A wheel tag cannot express "built
against OpenMPI 5.0.6" either, which is why only the portable configuration is
published.

Build from the source distribution instead, the way ``mpi4py`` does:

.. code-block:: Bash

    # MPI and HDF5 must be discoverable by CMake (module load, spack load, ...)
    DFTRACER_ENABLE_MPI=ON DFTRACER_ENABLE_HDF5=ON \
      pip install --no-binary dftracer dftracer

``--no-binary dftracer`` is what makes pip build from source rather than take the
wheel; only DFTracer itself is built from source, its build tools still come as
wheels. The same variables work for a checkout (``pip install .``), a release
tarball, ``autobuild.sh`` and a plain CMake build.

.. attention::

    On LC corona, load ``gcc/11.2.1`` or newer first. The default
    ``gcc/10.3.1`` fails to link with ``undefined reference to
    std::filesystem::...``.

    ``DFTRACER_ENABLE_HDF5=ON`` does not build against the LC ``hdf5-parallel``
    and ``hdf5-serial`` modules: they are HDF5 1.14.0, whose ``H5Apublic.h``
    declares ``H5Aread_async`` with an undefined ``chid_t`` type, an upstream
    typo fixed in 1.14.1. Any C++ build including ``hdf5.h`` fails. Use a spack
    or self-built HDF5 instead.

Commonly enabled options, all read from the environment by ``setup.py`` and
passed to CMake. See `Build Variables`_ for the full list:

.. table:: section - optional tracing features
   :widths: auto

   ================================== ===========================================================================
   Environment Variable               Effect
   ================================== ===========================================================================
   DFTRACER_ENABLE_MPI                MPI rank in traces and MPI/MPI-IO interception (default OFF).
   DFTRACER_ENABLE_HDF5               HDF5 interception (default OFF).
   DFTRACER_ENABLE_HIP_TRACING        AMD GPU tracing; needs ROCm/rocprofiler-sdk (default OFF).
   DFTRACER_ENABLE_FTRACING           Function tracing via ``-finstrument-functions`` (default OFF).
   DFTRACER_ENABLE_DYNAMIC_DETECTION  Detect HWLOC, MPI and HIP at run time rather than link time (default OFF).
   DFTRACER_DISABLE_HWLOC             HWLOC support; ``ON`` (disabled) by default.
   DFTRACER_MPI_IMPL                  Override MPI implementation detection (default: auto-detect).
   ================================== ===========================================================================

Requirements
*******************************

* a C++17 compiler whose standard library provides ``std::filesystem``: GCC 9 or
  newer. A system ``libstdc++`` older than the compiler on the ``PATH`` can
  shadow the newer one and fail the link with undefined references to
  ``std::filesystem``; loading a compiler module (for example
  ``module load gcc/12.1.1``) resolves it.
* CMake 3.24 or newer.
* the development packages of whatever is enabled (MPI, HDF5, ROCm).

DFTracer must be built against the same MPI and HDF5 the application uses.
The build detects their versions and forwards them to brahma, which generates the
matching interception:

.. code-block:: Bash

    -- [dftracer] dependency: MPI C probe: impl=MVAPICH brahma_version=200307
    -- [dftracer] Forwarding BRAHMA_MPI_IMPL=MVAPICH to brahma

Dependencies
*******************************

The C/C++ dependencies (cpp-logger, GOTCHA, brahma, yaml-cpp, libuv) are built
automatically as part of the build. Their source archives ship inside the source
distribution, so a source install needs no access to their repositories. When
building from a git clone, fetch the archives that are not committed first:

.. code-block:: Bash

    scripts/wheel/fetch_deps.sh

Verifying
*******************************

Run the application with ``DFTRACER_ENABLE=1`` and confirm the trace contains the
categories you enabled (``MPI``, ``MPIIO``, ``HDF5``) rather than only ``POSIX``.
An enabled feature that was linked against a mismatched library builds
successfully but produces no events of that category.

-----------------------------------------
Build DFTracer with Spack
-----------------------------------------


One may install DFTracer with Spack_.
If you already have Spack, make sure you have the latest release.
If you use a clone of the Spack develop branch, be sure to pull the latest changes.

.. _build-label:

Install Spack
*************
.. code-block:: Bash

    $ git clone https://github.com/spack/spack
    $ # create a packages.yaml specific to your machine
    $ . spack/share/spack/setup-env.sh

Use `Spack's shell support`_ to add Spack to your ``PATH`` and enable use of the
``spack`` command.

Build and Install DFTracer
*******************************

.. code-block:: Bash

    $ spack install py-pydftracer
    $ spack load py-pydftracer

If the most recent changes on the development branch ('dev') of DFTracer are
desired, then do ``spack install py-pydftracer@develop``.

.. attention::

    The initial install could take a while as Spack will install build
    dependencies (autoconf, automake, m4, libtool, and pkg-config) as well as
    any dependencies of dependencies (cmake, perl, etc.) if you don't already
    have these dependencies installed through Spack or haven't told Spack where
    they are locally installed on your system (i.e., through a custom
    packages.yaml_).
    Run ``spack spec -I py-dftracer-py`` before installing to see what Spack is going
    to do.

----------

------------------------------
Build DFTracer with CMake
------------------------------

Download the latest DFTracer release from the Releases_ page or clone the develop
branch ('develop') from the DFTracer repository
`https://github.com/LLNL/dftracer <https://github.com/LLNL/dftracer>`_.

---------------
Build Variables
---------------

.. table:: section - main build settings using env variables or cmake flags
   :widths: auto

   ================================ ======  ===========================================================================
   Environment Variable             Type    Description
   ================================ ======  ===========================================================================
   DFTRACER_BUILD_TYPE              STRING  Sets the build type for DFTRACER (default Release). Values are Debug or Release
   DFTRACER_ENABLE_FTRACING         BOOL    Enables function tracing (default OFF).
   DFTRACER_ENABLE_HIP_TRACING      BOOL    Enables AMD GPU tracing (default OFF).
   DFTRACER_ENABLE_PAPI_TRACING     BOOL    Enables PAPI counter tracing support (default OFF).
   DFTRACER_ENABLE_MPI              BOOL    Enables MPI Rank (default OFF).
   DFTRACER_MPI_IMPL                STRING  Selects the MPI implementation to build against (default: empty/auto-detect).
   DFTRACER_DISABLE_HWLOC           BOOL    Disables HWLOC (default ON).
   DFTRACER_ENABLE_HDF5             BOOL    Enables HDF5 tracing support (default OFF).
   DFTRACER_ENABLE_DYNAMIC_DETECTION BOOL   Enables Dynamic library detection for HWLOC, MPI, HIP, and PAPI (default OFF).
   DFTRACER_GENERATE_INTERFACES     BOOL    Generate Brahma and DFTracer interfaces from discovered headers (default OFF).
   DFTRACER_ENABLE_NATIVE_SCRIPT    BOOL    Build with native scripting support (default OFF).
   DFTRACER_PYTHON_EXE              STRING  Sets path to python executable. Only Cmake.
   DFTRACER_PYTHON_SITE             STRING  Sets path to python site-packages. Only Cmake.
   DFTRACER_BUILD_PYTHON_BINDINGS   BOOL    Enable python bindings for DFTracer (default ON).
   DFTRACER_LIBDIR_AS_LIB           BOOL    Install libraries directly under ``lib`` instead of an arch-specific subdir (default OFF).
   DFTRACER_WARNINGS_AS_ERRORS      BOOL    Build with warnings promoted to errors (default OFF).
   DFTRACER_USE_CLANG_LIBCXX        BOOL    Build against Clang's ``libc++`` instead of ``libstdc++`` (default OFF).
   DFTRACER_INSTALL_DEPENDENCIES    BOOL    Install DFTracer's dependencies (cpp-logger, GOTCHA, brahma) as part of the build (default OFF).
   DFTRACER_ENABLE_TESTS            BOOL    Enable tests for DFTracer (default OFF).
   DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS BOOL Enable dlio_benchmark integration tests (default OFF).
   DFTRACER_ENABLE_PAPER_TESTS      BOOL    Enable paper/reproducibility tests (default OFF).
   DFTRACER_TEST_LD_LIBRARY_PATH    STRING  Additional ``LD_LIBRARY_PATH`` entries to include when running tests (default: empty).
   DFTRACER_MPI_TEST_LAUNCHER_FLAGS STRING  Additional flags passed to the MPI test launcher (default: auto-detected).
   ================================ ======  ===========================================================================

These build variables can be set with cmake as ``-DDISABLE_HWLOC=OFF`` or as environment variables ``export DFTRACER_DISABLE_HWLOC=OFF``

When DFTracer is built with PAPI support, runtime counter sampling can be enabled with
``DFTRACER_ENABLE_PAPI_TRACING=1``. ``DFTRACER_PAPI_MULTIPLEX=1`` enables PAPI
multiplexing, and ``DFTRACER_PAPI_SAMPLE_INTERVAL_MS`` overrides the sampling
interval in milliseconds. When it is unset or set to ``0``, PAPI sampling inherits
the main ``DFTRACER_TRACE_INTERVAL_MS`` value.

Counters are chosen at build time. ``cmake/probes/papi_probe.c`` works out which
PAPI presets the build machine can program at the same time and bakes the answer
into ``dftracer_config.hpp`` as ``DFTRACER_PAPI_DETECTED_EVENTS``; a run uses that
list directly and does no discovery of its own. Set ``DFTRACER_PAPI_EVENTS`` to a
comma-separated list to pin the counters explicitly for a run. Counters the run
host will not program -- a CPU different from the build host, or one counter slot
too many -- are skipped with a warning rather than failing the run.

Because the list is fixed at build time, building on a login node and running on a
compute node with a different CPU can leave counters on the table. Build on the
target architecture, or pin ``DFTRACER_PAPI_EVENTS``, when that matters.

Sampling runs on a dedicated libuv timer thread inside the traced process, so the
tracing hot path does no PAPI work. The counters are attached to the process with
``PAPI_INHERIT_ALL``, which covers every thread the application starts *after*
DFTracer initializes; initialize DFTracer before spawning worker threads.

Each sample is written as one record per counter, with the counter name in ``name``,
its family (``CYCLE``, ``CACHE``, ``BRANCH``, ...) in ``cat``, ``type`` 11 for the
PAPI layer, and the reading in ``args``:

.. code-block:: JSON

    {"name":"PAPI_TOT_CYC","cat":"CYCLE","type":11,"ts":1786573445491884,"ph":2,
     "pid":2527286,"tid":2527286,"args":{"value":369676941,"delta":369676941}}

To compile DFTracer with PAPI support, enable ``DFTRACER_ENABLE_PAPI_TRACING`` and make
sure the PAPI development package is available to CMake. For example:

.. code-block:: Bash

    export DFTRACER_ENABLE_PAPI_TRACING=ON
    cmake . -B build -DDFTRACER_ENABLE_PAPI_TRACING=ON
    cmake --build build

At runtime, PAPI tracing can be turned off without rebuilding by setting:

.. code-block:: Bash

    export DFTRACER_ENABLE_PAPI_TRACING=0

Build DFTracer Dependencies
********************************

The main dependencies DFTracer are
1. cpp-logger : `https://github.com/hariharan-devarajan/cpp-logger.git <https://github.com/hariharan-devarajan/cpp-logger.git>`_ version: 0.0.1
2. gotcha: `https://github.com/LLNL/GOTCHA.git <https://github.com/LLNL/GOTCHA.git>`_ version: develop
3. brahma: `https://github.com/hariharan-devarajan/brahma.git <https://github.com/hariharan-devarajan/brahma.git>`_ version: 0.0.1

These dependencies can be either installed using spack or through cmake from respective respositories.

.. code-block:: Bash
    
    cmake . -B build -DCMAKE_INSTALL_PREFIX=<where you want to install DFTracer>
    cmake --build build
    cmake --install build

-----------

.. explicit external hyperlink targets

.. _Releases: https://github.com/LLNL/dftracer/releases
.. _Spack: https://github.com/spack/spack
.. _Spack's shell support: https://spack.readthedocs.io/en/latest/getting_started.html#add-spack-to-the-shell
.. _packages.yaml: https://spack.readthedocs.io/en/latest/build_settings.html#external-packages
