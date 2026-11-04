========================
DFTracer Utility scripts
========================

This section describes the utilities provided by DFTracer to assist users with logs.

----------

All scripts are installed with DFTracer in the installation's directories bin folder.

Please see the `DFTracer Utilities Documentation <https://dftracer.readthedocs.io/projects/utils/en/latest/index.html>`_ for
more detailed documentation on these utilities.

To browse a trace interactively, the `DFTracer Viewer <https://marketplace.visualstudio.com/items?itemName=rayandrew.dftracer-viewer>`_
VS Code extension (`source <https://github.com/rayandrew/vscode-dftracer-viewer>`_)
opens :code:`.pfw` and :code:`.pfw.gz` files directly. See :doc:`perfetto` for
loading a trace into Perfetto UI instead.

++++++++++++++++++++
Additional Utilities
++++++++++++++++++++

Preload Library Path
^^^^^^^^^^^^^^^^^^^^

``dftracer-preload-library-path`` prints the absolute path to the preload
library belonging to its installation. Activate your Python environment after
installing with pip, or add ``<prefix>/bin`` to ``PATH`` after a native CMake
installation. Native installations do not require Python for this command.

.. code-block:: bash

    export DFTRACER_ENABLE=1
    export DFTRACER_INIT=PRELOAD
    LD_PRELOAD="$(dftracer-preload-library-path)" ./your_application

To enable preloading for subsequent commands in the shell:

.. code-block:: bash

    export LD_PRELOAD="$(dftracer-preload-library-path)"

Use ``--debug`` (or ``-d``) to select ``libdftracer_preload_dbg.so``:

.. code-block:: bash

    LD_PRELOAD="$(dftracer-preload-library-path --debug)" ./your_application

This selects the library only; configure logging separately as described in
:doc:`debugging`. Use ``-h`` or ``--help`` for usage information. Successful
lookups print only the path to standard output. A missing library produces an
error on standard error and exit status 1; invalid arguments exit with status 2.
The command never loads the tracer, and a missing debug library does not fall
back to the release library.

To stop a shell script if lookup fails, check the result before exporting:

.. code-block:: bash

    preload_library=$(dftracer-preload-library-path) || exit 1
    export LD_PRELOAD="$preload_library"

Install Info
^^^^^^^^^^^^

``dftracer_install_info`` shows how the installed DFTracer was configured and
built, so you can tell what an installation supports without rebuilding it.
Activate your Python environment after installing with pip, or add
``<prefix>/bin`` to ``PATH`` after a native CMake installation.

.. code-block:: bash

    dftracer_install_info          # readable report
    dftracer_install_info --json   # the same data for scripts

The report is written at build time, with full paths baked in. It needs only
Python 3 and its standard library. It does not load the tracer and does not need
the MPI, HDF5 or PAPI libraries available, so it works in a shell where the
build-time modules are not loaded. It lists:

* every feature (MPI, HDF5, function tracing, HIP, CUDA, PAPI, Variorum, hwloc)
  as requested at build time versus enabled, and warns when a feature was
  requested but not enabled;
* the MPI implementation and version, the HDF5 version, the PAPI counters that
  were detected and whether they are multiplexed, the rocprofiler-sdk and CUDA
  versions, and the compilers and paths the build used;
* whether MPI and HDF5 calls will actually be traced;
* the versions each component is supported against.

MPI and HDF5 interception is compiled for specific library versions. A build
against any other version succeeds and reports the feature as enabled, but
records no MPI or HDF5 events. The report shows this as
``calls traced: ✘ (version outside supported ranges: wrappers compiled out)``.
The supported versions are:

================ ==========================================================
Component        Supported versions
================ ==========================================================
MPI              OpenMPI 4.1.6 and 5.0.6 onward (same minor series);
                 MPICH 3.4.3 and 4.2.3 onward; MVAPICH2 2.3.x;
                 Cray MPICH 8.1.8 onward and 9.0.1 to 9.1.x
HDF5             1.8.23, 1.10.5, 1.12.3 and 1.14.5 onward (same minor series)
CUDA             10.1 through 13.x
ROCm             rocprofiler-sdk, shipped with ROCm 6.2.0 and later
PAPI, Variorum,  no version gate
hwloc
================ ==========================================================

Run it first when MPI or HDF5 events are missing from a trace. See
:doc:`build` for building with these features.

Sparse Git Clone
^^^^^^^^^^^^^^^^

The script enables sparse git-clone of dftracer traces of the specified branch into 
the specified local directory. It optionally provides interactive selection of directories to clone.

.. code-block:: bash

    <install-dir>/bin/usage: git_sparse_clone_interactive <remote repository> <branch> <local directory> [directory]

Arguments for this script are:

1. **-r repository name** name of remote repository
2. **-b branch name**     name of remote branch
3. **-l local directory** name of local directory to clone into       
4. **-d directory**       optional comma-separated list of directories to sparse-clone.

Sparse Git Push
^^^^^^^^^^^^^^^

The script enables sparse git-push of dftracer traces to the specified remote repository
and branch. It optionally provides interactive selection of directories to push.

.. code-block:: bash

    <install-dir>/bin/usage: git_sparse_push_interactive <remote repository> <branch> [directory]

Arguments for this script are:

1. **-r repository name** name of remote repository
2. **-b branch name**     name of remote branch
3. **-d directory**       optional comma-separated list of directories to sparse-push.
