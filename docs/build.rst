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
nothing from the host. To trace MPI, HDF5, PAPI counters, HIP or CUDA too,
add the prebuilt modules and compose them for the machine
(:doc:`composable_install`):

.. code-block:: Bash

    pip install --pre "dftracer[modules]"
    dftracer-compose                     # in the environment you trace in
    dftracer_install_info --composition  # what was linked

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

    git clone https://github.com/llnl-asr/dftracer.git
    cd dftracer
    # You can skip this for installing the dev branch.
    # for latest stable version use master branch.
    git checkout tags/<Release> -b <Release>
    pip install .

From Github
************

.. code-block:: Bash

  DFT_VERSION=v1.0.4
  pip install git+https://github.com/llnl-asr/dftracer.git@${DFT_VERSION}

.. attention::

    For pip installations, all libraries will be present within the site-packages/dftracer/lib.
    This enables clean management of pip installation and uninstallations.

------------------------------------------
Enabling MPI, HDF5, HIP and CUDA
------------------------------------------

MPI, HDF5, HIP and CUDA support have to be compiled against the libraries you
run with: DFTracer intercepts calls into the MPI or HDF5 library the
application loads, and the interception is generated for a specific
implementation and version. There are two ways to get that.

The ``dftracer-modules`` wheel ships that support prebuilt for each library
version DFTracer is built against, and ``dftracer-compose`` links the ones
that fit the machine, building any it lacks from source. See
:doc:`composable_install`.

Or build all of DFTracer from the source distribution, the way ``mpi4py``
does:

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
passed to CMake. See `Build Variables`_ for the full list and `MPI and HDF5 Scenarios`_ for worked examples:

.. table:: section - optional tracing features
   :widths: auto

   ================================== ===========================================================================
   Environment Variable               Effect
   ================================== ===========================================================================
   DFTRACER_ENABLE_MPI                MPI rank in traces and MPI/MPI-IO interception (default OFF).
   DFTRACER_ENABLE_HDF5               HDF5 interception (default OFF).
   DFTRACER_ENABLE_HIP_TRACING        AMD GPU tracing; needs ROCm/rocprofiler-sdk (default OFF).
   DFTRACER_ENABLE_CUDA_TRACING       NVIDIA GPU tracing; needs a CUDA toolkit with CUPTI (default OFF).
   DFTRACER_CUDA_PATH                 CUDA toolkit root to build CUPTI tracing against (default: auto-detect).
   DFTRACER_ENABLE_PAPI_TRACING       PAPI hardware counter sampling; needs libpapi-dev (default OFF).
   DFTRACER_ENABLE_VARIORUM           Node-level power counters via variorum; fetched and built if not installed (default OFF).
   DFTRACER_ENABLE_FTRACING           Function tracing via ``-finstrument-functions`` (default OFF).
   DFTRACER_ENABLE_PROFILING          Time each phase of dftracer's own pipeline, reported at finalize; compiled out when OFF (default OFF).
   DFTRACER_ENABLE_DYNAMIC_DETECTION  Detect HWLOC, MPI, HIP, CUDA, PAPI and Variorum at run time rather than link time (default OFF).
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
* the development packages of whatever is enabled (MPI, HDF5, ROCm, CUDA).

DFTracer must be built against the same MPI and HDF5 the application uses.
The build detects their versions and forwards them to brahma, which generates the
matching interception:

.. code-block:: Bash

    -- [dftracer] dependency: MPI C probe: impl=MVAPICH brahma_version=200307
    -- [dftracer] Forwarding BRAHMA_MPI_IMPL=MVAPICH to brahma

Supported MPI
*******************************

DFTracer traces the MPI and MPI-IO calls through brahma, which splits them in two.
The *common* functions are the ones with the same signature in every supported MPI.
They are traced for any MPI implementation and version, including ones not listed
below, so an MPI that DFTracer has never seen still produces ``MPI`` and ``MPIIO``
events for the common calls.
The *compat* functions are the ones an MPI has beyond the common set, for example the
MPI-4 large-count ``_c`` calls. They are traced only for the families below, and cover
every patch release of a family.

=============== ================================== ================================
Implementation  Compat functions traced for        Built and tested with
=============== ================================== ================================
OpenMPI         4.1.x, 5.0.x                       4.1.6, 4.1.8, 5.0.3, 5.0.6
MPICH           3.4.x, 4.2.x                       3.4.3, 4.2.3, 4.3.0 (common only)
MVAPICH2        2.3.x                              not in CI (spack build fails)
Cray MPICH      8.1.x, 9.0.x, 9.1.x                not in CI (proprietary)
=============== ================================== ================================

The exact MPI releases the interfaces were generated from are recorded in the header
comment of every generated file under ``src/dftracer/core/brahma``. The compat functions
of a family are those present in every release that was read for it, so a patch release
that was not read is covered only if it has the same functions.
The build detects the implementation and version and passes them to brahma, as shown
above. Set ``DFTRACER_MPI_IMPL`` to override the detected implementation. The same
brahma table, and how to add an MPI release, are in the brahma README.

The functions in the common set and in each family's compat set are listed in
:doc:`mpi_support`.

Dependencies
*******************************

The C/C++ dependencies (cpp-logger, GOTCHA, brahma, yaml-cpp, libuv, libdeflate)
are cloned and built automatically as part of the build, so a source install
needs access to their GitHub repositories. An installed libdeflate (1.9 or newer,
e.g. ``libdeflate-devel``) is used instead when found; traces are written as gzip
members with it, so zlib is not needed.

Verifying
*******************************

Run the application with ``DFTRACER_ENABLE=1`` and confirm the trace contains the
categories you enabled (``MPI``, ``MPIIO``, ``HDF5``) rather than only ``POSIX``.
An enabled feature that was linked against a mismatched library builds
successfully but produces no events of that category. For MPI this applies to the
compat calls only: an MPI release outside the table above still traces the common
calls, and is missing just the implementation-specific ones.

To check this without running an application, run ``dftracer_install_info``. It
lists which features were enabled, the MPI, HDF5, PAPI, ROCm and CUDA versions
the build found, whether MPI and HDF5 calls will be traced, and the versions
DFTracer supports. See :doc:`utilities`.

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
`https://github.com/llnl-asr/dftracer <https://github.com/llnl-asr/dftracer>`_.

---------------
Build Variables
---------------

Every variable below can be set with CMake as ``-DDFTRACER_DISABLE_HWLOC=OFF``.
Those read by ``setup.py`` can be set for a pip install as environment variables,
``export DFTRACER_DISABLE_HWLOC=OFF``.
``setup.py`` does not read ``CMAKE_ARGS`` or pip ``--config-settings``, so pass any
other CMake variable through ``DFTRACER_CMAKE_ARGS``.
That includes ``DFTRACER_MPI_IMPL``, ``BRAHMA_MPI_VERSION``, ``HDF5_ROOT`` and the
variables marked CMake only.

.. list-table::
   :header-rows: 1
   :widths: 35 8 57

   * - Variable
     - Type
     - Description
   * - ``DFTRACER_BUILD_TYPE``
     - STRING
     - Build type, Debug or Release (default Release).
   * - ``DFTRACER_ENABLE_FTRACING``
     - BOOL
     - Function tracing (default OFF).
   * - ``DFTRACER_ENABLE_PROFILING``
     - BOOL
     - Self-profiling of the tracing pipeline (default OFF); see :ref:`pipeline-profiling`. CMake only.
   * - ``DFTRACER_ENABLE_MPI``
     - BOOL
     - MPI rank and MPI/MPI-IO interception (default OFF). A loaded MPI is not used unless this is ON.
   * - ``DFTRACER_MPI_IMPL``
     - STRING
     - Override MPI implementation detection: OPENMPI, MPICH, CRAYMPICH, MVAPICH, INTELMPI (default empty, detect).
   * - ``BRAHMA_MPI_VERSION``
     - INT
     - Override the detected MPI library version, as ``major*100000 + minor*100 + patch``. Set it with ``DFTRACER_MPI_IMPL``.
   * - ``DFTRACER_ENABLE_HDF5``
     - BOOL
     - HDF5 interception (default OFF).
   * - ``HDF5_ROOT``
     - PATH
     - HDF5 install prefix to build against (default: found through ``h5pcc``/``h5cc`` on ``PATH``).
   * - ``DFTRACER_ENABLE_HIP_TRACING``
     - BOOL
     - AMD GPU tracing (default OFF).
   * - ``DFTRACER_ENABLE_CUDA_TRACING``
     - BOOL
     - NVIDIA GPU tracing via CUPTI (default OFF).
   * - ``DFTRACER_CUDA_PATH``
     - PATH
     - CUDA toolkit root for CUPTI tracing (default: detect).
   * - ``DFTRACER_ENABLE_PAPI_TRACING``
     - BOOL
     - PAPI counter tracing (default OFF).
   * - ``DFTRACER_ENABLE_VARIORUM``
     - BOOL
     - Variorum node power tracing (default OFF).
   * - ``DFTRACER_BUILD_VARIORUM``
     - STRING
     - When to build variorum from source: AUTO (only if none installed), ALWAYS, NEVER (default AUTO).
   * - ``DFTRACER_DISABLE_HWLOC``
     - BOOL
     - Disable HWLOC (default ON).
   * - ``DFTRACER_ENABLE_DYNAMIC_DETECTION``
     - BOOL
     - Enable whichever of HWLOC, MPI, HIP, CUDA, PAPI and Variorum is found (default OFF). See the note under `MPI and HDF5 Scenarios`_.
   * - ``DFTRACER_GENERATE_INTERFACES``
     - BOOL
     - Generate Brahma and DFTracer interfaces from discovered headers (default OFF).
   * - ``DFTRACER_ENABLE_NATIVE_SCRIPT``
     - BOOL
     - Native scripting support (default OFF).
   * - ``DFTRACER_PYTHON_EXE``
     - STRING
     - Python executable. CMake only.
   * - ``DFTRACER_PYTHON_SITE``
     - STRING
     - Python site-packages path.
   * - ``DFTRACER_BUILD_PYTHON_BINDINGS``
     - BOOL
     - Python bindings (default ON).
   * - ``DFTRACER_LIBDIR_AS_LIB``
     - BOOL
     - Install libraries under ``lib`` instead of an arch-specific subdirectory (default OFF).
   * - ``DFTRACER_WARNINGS_AS_ERRORS``
     - BOOL
     - Promote warnings to errors (default OFF).
   * - ``DFTRACER_USE_CLANG_LIBCXX``
     - BOOL
     - Build against Clang's ``libc++`` (default OFF).
   * - ``DFTRACER_INSTALL_DEPENDENCIES``
     - BOOL
     - Build and install dependencies instead of DFTracer (default OFF).
   * - ``DFTRACER_ENABLE_TESTS``
     - BOOL
     - Tests (default OFF).
   * - ``DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS``
     - BOOL
     - dlio_benchmark integration tests (default OFF).
   * - ``DFTRACER_ENABLE_PAPER_TESTS``
     - BOOL
     - Paper/reproducibility tests (default OFF).
   * - ``DFTRACER_ENABLE_PERF_TESTS``
     - BOOL
     - Performance benchmarks (default OFF). CMake only.
   * - ``DFTRACER_TEST_LD_LIBRARY_PATH``
     - STRING
     - Extra ``LD_LIBRARY_PATH`` entries for tests (default empty).
   * - ``DFTRACER_MPI_TEST_LAUNCHER_FLAGS``
     - STRING
     - Extra flags for the MPI test launcher (default: detected).
   * - ``DFTRACER_PC_RING_BLOCKS``
     - INT
     - Ring buffer block count (default 1024). CMake only.
   * - ``DFTRACER_PC_RING_BLOCK_SIZE``
     - INT
     - Ring buffer block size in bytes (default 1048576). CMake only.

Variables read only by ``setup.py``:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Environment Variable
     - Description
   * - ``DFTRACER_CMAKE_ARGS``
     - Extra CMake arguments, passed to the dependency and the main configure.
   * - ``DFTRACER_INSTALL_DIR``
     - Install prefix.
   * - ``DFTRACER_BUILD_DEPENDENCIES``
     - Set to ``0`` to skip the dependency build (default ``1``).
   * - JOBS / CMAKE_BUILD_PARALLEL_LEVEL
     - Parallel build jobs.
   * - ``CMAKE_GENERATOR``
     - CMake generator.

Supported MPI and HDF5
**********************

Interception is compiled for specific implementations and versions, which come
from `brahma <https://github.com/llnl-asr/brahma>`_.
Implementation names are the values of ``DFTRACER_MPI_IMPL``.

.. list-table:: MPI versions with MPI-IO interception
   :header-rows: 1
   :widths: 30 25 45

   * - Implementation
     - ``DFTRACER_MPI_IMPL``
     - Versions
   * - Open MPI
     - ``OPENMPI``
     - 4.1.6 and later 4.1.x; 5.0.6 and later 5.0.x
   * - MPICH
     - ``MPICH``
     - 3.4.3 and later 3.4.x; 4.2.3 and later 4.2.x
   * - Cray MPICH
     - ``CRAYMPICH``
     - 8.1.8 and later 8.1.x; 9.0.1 through 9.1.x

.. list-table:: HDF5 versions with interception
   :header-rows: 1
   :widths: 30 70

   * - Series
     - Versions
   * - 1.8
     - 1.8.23 and later
   * - 1.10
     - 1.10.5 and later
   * - 1.12
     - 1.12.3 and later
   * - 1.14
     - 1.14.5 and later

Not all functions exist in every version; some start later in a series.

.. attention::

    ``MVAPICH`` and ``INTELMPI`` are detected, but brahma has no MPI-IO
    interception for them.
    An unsupported implementation or version still builds and installs, with
    that interception compiled out.
    Always run the checks in `Verifying a Build`_.

MPI and HDF5 Scenarios
**********************

Load the MPI and HDF5 you want in the shell that runs the build, and use the same
ones to build the application.
The examples use Cray MPICH 9.0.1 and HDF5 1.14.5; replace the values with yours.

.. note::

    ``DFTRACER_ENABLE_DYNAMIC_DETECTION=ON`` is not the way to detect MPI.
    It also enables every other feature found, and does not fail when MPI is
    missing.
    Use ``DFTRACER_ENABLE_MPI=ON``, which stops the build if MPI is not found.

In auto-detection, DFTracer finds MPI and HDF5, compiles a small probe for the MPI
implementation and version, and passes the results to brahma.
With manual values, DFTracer passes yours to brahma instead.

Auto-detect with pip
....................

.. code-block:: Bash

    module load <compiler> <mpi> <hdf5>     # mpicc and h5pcc must be on PATH
    export CC=$(which mpicc) CXX=$(which mpicxx)
    export DFTRACER_ENABLE_MPI=ON DFTRACER_ENABLE_HDF5=ON
    pip install --no-cache-dir --no-binary dftracer dftracer

Custom paths with pip
.....................

Name the compiler wrappers and the HDF5 prefix, and optionally the implementation
and version:

.. code-block:: Bash

    export CC=$(which mpicc) CXX=$(which mpicxx)
    export DFTRACER_ENABLE_MPI=ON DFTRACER_ENABLE_HDF5=ON
    export DFTRACER_CMAKE_ARGS="\
      -DMPI_C_COMPILER=$(which mpicc) \
      -DMPI_CXX_COMPILER=$(which mpicxx) \
      -DDFTRACER_MPI_IMPL=CRAYMPICH \
      -DBRAHMA_MPI_VERSION=900001 \
      -DHDF5_ROOT=/path/to/hdf5"
    pip install --no-cache-dir --no-binary dftracer dftracer

``BRAHMA_MPI_VERSION`` is the library version, not the MPI standard version:
Cray MPICH 9.0.1 is ``900001`` and Open MPI 4.1.6 is ``400106``.

If there are no working compiler wrappers, name the headers and library directly
with the standard CMake ``FindMPI`` variables.
The library name must match the file, here ``libmpi_cray.so``:

.. code-block:: Bash

    export MPI_ROOT=/path/to/mpi
    export DFTRACER_ENABLE_MPI=ON
    export DFTRACER_CMAKE_ARGS="\
      -DMPI_C_HEADER_DIR=$MPI_ROOT/include \
      -DMPI_CXX_HEADER_DIR=$MPI_ROOT/include \
      -DMPI_C_LIB_NAMES=mpi_cray \
      -DMPI_CXX_LIB_NAMES=mpi_cray \
      -DMPI_mpi_cray_LIBRARY=$MPI_ROOT/lib/libmpi_cray.so"
    pip install --no-cache-dir --no-binary dftracer dftracer

Auto-detect with CMake
......................

CMake takes two passes: one builds the dependencies, the next builds DFTracer.

.. code-block:: Bash

    module load <compiler> <mpi> <hdf5>
    export CC=$(which mpicc) CXX=$(which mpicxx)
    OPTS="-DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_PREFIX_PATH=$PREFIX \
          -DDFTRACER_BUILD_PYTHON_BINDINGS=OFF \
          -DDFTRACER_ENABLE_MPI=ON -DDFTRACER_ENABLE_HDF5=ON"
    mkdir build && cd build
    cmake .. $OPTS -DDFTRACER_INSTALL_DEPENDENCIES=ON
    cmake --build . -j8
    cmake .. $OPTS -DDFTRACER_INSTALL_DEPENDENCIES=OFF
    cmake --build . -j8
    cmake --install .

Custom paths with CMake
.......................

Add the same variables to ``OPTS`` as for pip:

.. code-block:: Bash

    OPTS="$OPTS \
      -DMPI_C_COMPILER=$(which mpicc) \
      -DMPI_CXX_COMPILER=$(which mpicxx) \
      -DDFTRACER_MPI_IMPL=CRAYMPICH \
      -DBRAHMA_MPI_VERSION=900001 \
      -DHDF5_ROOT=/path/to/hdf5"

Verifying a Build
*****************

A build can succeed with an interception missing, so check the installed library.
Each count must be non-zero for what you enabled:

.. code-block:: Bash

    LIB=$(find <prefix> -name libdftracer_core.so | head -1)
    nm -D --defined-only $LIB | grep -c MPI_File_open
    nm -D --defined-only $LIB | grep -c H5Fopen
    ldd $LIB | grep -E 'libmpi|libhdf5'     # the libraries the application uses

If a count is zero, check the configured values:

.. code-block:: Bash

    find <build-dir> -name brahma_config.hpp | xargs grep -E 'BRAHMA_(MPI|HDF5)_VERSION|MPI_IMPL_NAME'

Then compare them with `Supported MPI and HDF5`_.
Before retrying, run ``pip uninstall dftracer`` and remove the old build directory,
because a stale build keeps its old settings.

When DFTracer is built with PAPI support, runtime counter sampling can be enabled with
``DFTRACER_ENABLE_PAPI_TRACING=1``. ``DFTRACER_PAPI_SAMPLE_INTERVAL_MS`` overrides the
sampling interval in milliseconds. When it is unset or set to ``0``, PAPI sampling
inherits the main ``DFTRACER_TRACE_INTERVAL_MS`` value.

Choosing the counters
.....................

Name the counters you want, either with an environment variable:

.. code-block:: bash

   export DFTRACER_PAPI_EVENTS="PAPI_TOT_INS,PAPI_TOT_CYC,PAPI_BR_NTK"

or in the YAML configuration:

.. code-block:: yaml

   features:
     papi:
       enable: true
       interval: 200
       events:
         - PAPI_TOT_INS
         - PAPI_TOT_CYC
         - PAPI_BR_NTK

With neither set, DFTracer uses the counters its build-time probe found for the
machine. ``papi_avail`` lists what is available.

Exact counts versus estimates
.............................

A CPU has a small number of hardware counter registers -- five on an AMD MI300A,
which ``papi_avail`` reports as ``Number Hardware Counters``. What happens next
depends only on whether your selection fits:

* **It fits.** Every counter is read exactly, and nothing is time-shared. This
  holds even if ``DFTRACER_PAPI_MULTIPLEX=1`` is set, which is ignored in that
  case: multiplexing a selection the hardware can hold would trade exact counts
  for estimates and buy nothing.
* **It does not fit.** PAPI time-shares the counters and scales each reading up
  by the fraction of time it was actually counting. Every such reading is an
  estimate. DFTracer says so at startup:

  .. code-block:: text

     PAPI: 17 counters were requested but this machine has 5 hardware counters,
     so they are time-shared and every multiplexed reading is a scaled estimate
     (measured error 1% to 6%, worse on a workload with phases). For exact
     counts, name a set of counters that fits in DFTRACER_PAPI_EVENTS and take
     one run per set.

Records carry ``multiplex: 1`` when their family was time-shared and ``0`` when
it was counted exactly, so a trace says which of its own numbers are estimates.

The error is not a fixed small percentage. Measured against an exact baseline on
an MI300A it was under 1% on a steady loop but 5-6% on a workload alternating
between floating-point and branch-heavy phases -- and biased in a consistent
direction rather than averaging out. **To measure exactly, name a set of counters
that fits and take one run per set.** There is no way to count more counters
than the hardware has in a single run without estimating.

Note that "fits" is not simply "no more names than registers": presets can share
native events, so seven branch counters may fit in five registers while a
different four do not. DFTracer tests the real hardware rather than counting
names, so a selection is only time-shared when it genuinely has to be.

Counters that cannot be time-shared
...................................

Some PAPI presets are one native event subtracted from another -- ``PAPI_BR_NTK``
is ``RETIRED_BRANCH_INSTRUCTIONS`` minus ``RETIRED_TAKEN_BRANCH_INSTRUCTIONS``.
Time-sharing counts the two in different slices and scales them independently, so
subtracting two ~1% estimates of two nearly equal large numbers leaves only the
error. On an MI300A ``PAPI_BR_NTK`` reads ``64000010`` when its set fits and
``-191117`` when the same set is multiplexed -- a negative count of branches.

DFTracer therefore drops subtractive presets from a family it has to time-share,
and says which:

.. code-block:: text

   PAPI counter PAPI_BR_NTK is one native event subtracted from another and the
   PAPI family does not fit this machine; dropping it, because time-sharing the
   two would report impossible values such as a negative count

Select a set that fits to get them. Additive presets such as ``PAPI_FP_INS`` are
kept: summing two estimates preserves the relative error and cannot change sign.

Counters are chosen at build time, and everything the machine offers is taken.
``cmake/probes/papi_probe.c`` walks the whole PAPI preset table, keeps every
preset the CPU implements and PAPI will program, and bakes the list into
``dftracer_config.hpp`` as ``DFTRACER_PAPI_DETECTED_EVENTS``; a run uses it
directly and does no discovery of its own. Nothing is hand-picked, because which
presets exist varies enormously between CPUs: on an AMD MI300A node only 17 of
the 108 presets are available and not one of them is a cache counter, so a fixed
list would mostly miss.

Where there are more counters than hardware slots, DFTracer multiplexes so all of
them are still collected. Multiplexed readings are extrapolated from a time slice
rather than counted exactly, so they are estimates and can occasionally move
backwards; each record carries ``multiplex`` in ``args`` to say which it is. The
build log reports the gap::

    -- [DFTRACER] detected 17 PAPI counters (7 fit in 5 hardware slots, the rest
       need multiplexing): PAPI_BR_UCN,PAPI_BR_CN,...

Set ``DFTRACER_PAPI_EVENTS`` to a comma-separated list to pin the counters
explicitly for a run, and ``DFTRACER_PAPI_MULTIPLEX=1`` to force multiplexing even
when everything would fit. Counters the run host will not program -- a CPU
different from the build host -- are skipped with a warning rather than failing
the run.

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

.. _Releases: https://github.com/llnl-asr/dftracer/releases
.. _Spack: https://github.com/spack/spack
.. _Spack's shell support: https://spack.readthedocs.io/en/latest/getting_started.html#add-spack-to-the-shell
.. _packages.yaml: https://spack.readthedocs.io/en/latest/build_settings.html#external-packages
