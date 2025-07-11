======================
DFTracer APIs
======================

In this document, we detail how to use DFTracer APIs for languages.
Please refer to `chrome tracing document`_ for definition on ``cat``, ``name``, and ``event``.

----------

----------------------------------------
Modes of DFTracer
----------------------------------------

The DFTracer can be used in three main modes.

1. APP MODE: In this mode, the application can use function/region level API's to profile application codes.
2. PRELOAD MODE: In this mode, the application can transparently load up DFTracer to profile POSIX or STDIO calls.
3. HYBRID MODE: In this mode, application can use both APP and PRELOAD Mode to perform I/O from all dynamically spawned processes and function profiling from application.

----------------------------------------
Configurations of DFTracer
----------------------------------------

DFTracer can be configured through ENV variables and YAML file.
The ENV variables would override any yaml configurations provided.

YAML configuration supported. WE need to set DFTRACER_CONFIGURATION ENV variable for YAML to load.

.. code-block:: yaml

    # contents of conf.yaml

    enable: True        # Enable DFTracer (default False).
    profiler:
      init: FUNCTION    # DFTracer Mode FUNCTION/PRELOAD (default FUNCTION). For Hybrid use PRELOAD mode.
      log_file: trace   # PATH To log file. In this case process id and app name is appended to file.
      data_dirs: ./data # Colon separated paths that will be traced for I/O accesses by profiler. For tracing all directories use the string "all" (not recommended).
      log_level: DEBUG  # Logging level within DFTracer ERROR/WARN/INFO/DEBUG (default ERROR).
      compression: True # Enable trace compression (default True)
    gotcha:
      priority: 1       # PRIORITY of DFTracer in GOTCHA (default: True).
    features:
      metadata: True    # Include metadata (default False)
      core_affinity: True # Include core affinity (default True). metadata needs to be enabled.
      io:
        enable: True    # Enable automatic binding of all I/O calls (default True).
        posix: True     # Enable automatic binding of POSIX I/O calls (default True).
        stdio: True     # Enable automatic binding of STDIO I/O calls (default True).
      tid: True         # Enable tracing of thread ids (default True).

ENV Variables supported

.. table:: section - main configuration settings using env variables
   :widths: auto

   ================================ ======  ===========================================================================
   Environment Variable             Type    Description
   ================================ ======  ===========================================================================
   DFTRACER_CONFIGURATION           STRING  PATH to the yaml configuration
   DFTRACER_ENABLE                  INT     Enable or Disable DFTracer (default 0).
   DFTRACER_INIT                    STRING  DFTracer Mode ``FUNCTION``/``PRELOAD`` (default ``FUNCTION``).
                                            For Hybrid use ``PRELOAD`` mode.
   DFTRACER_LOG_FILE                STRING  PATH To log file. In this case process id and app name is appended to file.
   DFTRACER_DATA_DIR                STRING  Colon separated paths that will be traced for I/O accesses by profiler.
                                            For tracing all directories use the string "all" (not recommended).
                                            Note: DFTRACER_DATA_DIR uses a prefix tree. If both ``/local/scratch`` and
                                            ``/local/scratch/data`` are in the list, the order matters—
                                            the last one will override the first. As a result, the first path won’t be traced.
                                            To avoid this, only use ``/local/scratch``.
   DFTRACER_INC_METADATA            INT     Include or exclude metadata (default 0)
   DFTRACER_SET_CORE_AFFINITY       INT     Include or exclude core affinity (default 0).
                                            ``DFTRACER_INC_METADATA`` needs to be enabled.
   DFTRACER_GOTCHA_PRIORITY         INT     PRIORITY of DFTracer in GOTCHA (default: 1).
   DFTRACER_LOG_LEVEL               STRING  Logging level within DFTracer ``ERROR``/``WARN``/``INFO``/``DEBUG`` (default ``ERROR``).
   DFTRACER_DISABLE_IO              INT     Disable automatic binding of all I/O calls (default: 0).
   DFTRACER_DISABLE_POSIX           INT     Disable automatic binding of POSIX I/O calls (default: 0).
   DFTRACER_DISABLE_STDIO           INT     Disable automatic binding of STDIO I/O calls (default: 0).
   DFTRACER_TRACE_COMPRESSION       INT     Enable trace compression (default 0).
   DFTRACER_DISABLE_TIDS            INT     Disable tracing of thread ids (default 0).
   DFTRACER_WRITE_BUFFER_SIZE       INT     Setup the buffering size for write optimization (default 0). Note: Disabled as
                                            this won't work for AI workloads which uses ``fork`` and ``spawn`` without a clear ``exit``.
                                            Also, it does not work for workloads which uses ``exec`` and rewrite process buffer state.
   ================================ ======  ===========================================================================

----------------------------------------
DFTracer C++ APIs
----------------------------------------

This section describes how to use DFTracer for profiling C++ application using C++ APIs.

-----

Include the DFTracer Header for C++
****************************************

In C or C++ applications, include ``dftracer/dftracer.h``.

.. code-block:: c

    #include <dftracer/dftracer.h>

Initialization of DFTracer
****************************************

To initialize DFTracer for application, applications needs to use ``DFTRACER_CPP_INIT``.
We need to pass ``log_file``, ``data directories``, and ``process id`` to be used by the profiler.
``data_dirs`` is a parameter which is a Colon ``:`` separated PATH of directories to be traced.
If users pass nullptr to log_file and data_dirs, the environment variables for these will be used.
Additionally, if users pass nullptr to process_id, then getpid() function would be used to automatically fetch process id.

.. code-block:: c

    DFTRACER_CPP_INIT(log_file, data_dirs, process_id);

Finalization of DFTracer
****************************************

Finalization call to clean DFTracer entries (Optional). If users do not call this, they have to manually add ``[`` at the start of the log file.

.. code-block:: c

    DFTRACER_CPP_FINI();

Function Profiling
****************************************

To profile a function, add the wrapper ``DFTRACER_CPP_FUNCTION`` at the start of the function

.. code-block:: c

    void foo() {
      DFTRACER_CPP_FUNCTION();
      sleep(1);
    } // DFTRACER_CPP_FUNCTION ends here.

Region Level Profiling for Code blocks
****************************************

To profile a block of code which has a scope using ``{ .... }``, we can use ``DFTRACER_CPP_REGION(<NAME>)``.
The name of the region should unique within the scope of the function/code block.

.. code-block:: c

    void foo() {
      DFTRACER_CPP_FUNCTION();
      sleep(1);
      {
        DFTRACER_CPP_REGION(CUSTOM);
        sleep(1);

      } // DFTRACER_CPP_REGION ends here implicitly
    } // DFTRACER_CPP_FUNCTION ends here.

Region Level Profiling for lines of code
****************************************

To profile a specific set of lines within your code, use the ``DFTRACER_CPP_REGION_<START/END>`` APIs.
The ``START`` and ``END`` calls should be in the same scope of the function.

.. code-block:: c

    void foo() {
      DFTRACER_CPP_FUNCTION();
      sleep(1);
      {
        DFTRACER_CPP_REGION(CUSTOM);
        sleep(1);
        DFTRACER_CPP_REGION_START(CUSTOM_BLOCK);
        sleep(1);
        DFTRACER_CPP_REGION_END(CUSTOM_BLOCK); // CUSTOM_BLOCK started by DFTRACER_CPP_REGION_START ends
      } // DFTRACER_CPP_REGION ends here implicitly
    } // DFTRACER_CPP_FUNCTION ends here.

---------------------
DFTracer C APIs
---------------------

This section describes how to use DFTracer for profiling C application using C APIs.

-----

Include the DFTracer Header for C
****************************************

In C application, include ``dftracer/dftracer.h``.

.. code-block:: c

    #include <dftracer/dftracer.h>

Initialization of DFTracer
****************************************

To initialize DFTracer for application, applications needs to use ``DFTRACER_C_INIT``.
We need to pass ``log_file``, ``data directories``, and ``process id`` to be used by the profiler.
``data_dirs`` is a parameter which is a Colon ``:`` separated PATH of directories to be traced.
If users pass NULL to log_file and data_dirs, the environment variables for these will be used.
Additionally, if users pass NULL to process_id, then getpid() function would be used to automatically fetch process id.

.. code-block:: c

    DFTRACER_C_INIT(log_file, data_dirs, process_id);

Finalization of DFTracer
****************************************

Finalization call to clean DFTracer entries (Optional). If users do not call this, they have to manually add ``[`` at the start of the log file.

.. code-block:: c

    DFTRACER_C_FINI();

Function Profiling
****************************************

To profile a function, add the wrapper ``DFTRACER_C_FUNCTION_START`` at the start of the function and
``DFTRACER_C_FUNCTION_END`` at the end of the function.

.. code-block:: c

    void foo() {
      DFTRACER_C_FUNCTION_START();
      sleep(1);
      if (<CONDITION>) {
        DFTRACER_C_FUNCTION_END();
        return; // Define DFTRACER_C_FUNCTION_END on every branch
      }
      DFTRACER_C_FUNCTION_END(); // Define DFTRACER_C_FUNCTION_END on every branch
    }

.. attention::

    For capturing all code branches, every return statement should have a corresponding ``DFTRACER_C_FUNCTION_END`` block within the function.

Region Level Profiling for lines of code
****************************************

To profile a specific set of lines within your code, use the ``DFTRACER_C_REGION_<START/END>`` APIs.
The ``START`` and ``END`` calls should be in the same scope of the function.
The name passed to the function should be unique in every scope.

.. code-block:: c

    void foo() {
      DFTRACER_C_FUNCTION_START();
      sleep(1);
      DFTRACER_C_REGION_START(CUSTOM);
      sleep(1);
      DFTRACER_C_REGION_END(CUSTOM); // END region CUSTOM.
      DFTRACER_C_FUNCTION_END(); // END FUNCTION foo.
    }

----------------------------------------
DFTracer C/C++ Function Profiling using GCC
----------------------------------------

GCC supports function level tracing using ``-finstrument-functions``.
DFTracer allows application to compile with ``-g -finstrument-functions -Wl,-E -fvisibility=default``.
If the applications are using cmake, they can find_package and then use the CMAKE Variable `DFTRACER_FUNCTION_FLAGS` for compile flags.
This can be applied globally or on a target.

Internally DFTracer uses ``dladdr`` to resolve symbol names which work for shared libraries.
For executables or binaries, we store the address and the name which can be used to derive the function name at analysis time.
This can be done using ``nm -D`` or ``readelf -S`` utilities.

-------------------------
DFTracer Python APIs
-------------------------

This section describes how to use DFTracer for profiling python applications.

-----

Include the DFTracer module
****************************************

In C application, include ``dftracer/dftracer.h``.

.. code-block:: python

    from dftracer.logger import dftracer

Initialization of DFTracer
****************************************

To initialize DFTracer for application, applications needs to use ``dftracer.initialize_log``.
We need to pass ``log_file``, ``data directories``, and ``process id`` to be used by the profiler.
``data_dir`` is a parameter which is a Colon ``:`` separated PATH of directories to be traced.
If users pass None to log_file and data_dirs, the environment variables for these will be used.
Additionally, if users pass -1 to process_id, then getpid() function would be used to automatically fetch process id.

.. code-block:: python

    dft_logger = dftracer.initialize_log(logfile, data_dir, process_id)

Finalization of DFTracer
****************************************

Finalization call to clean DFTracer entries (Optional). If users do not call this, they have to manually add ``[`` at the start of the log file.

.. code-block:: python

    dft_logger.finalize()

.. _dft_fn:

Function decorator style profiling
****************************************

With python applications, developers can use decorator provided within dftracer to tag functions that need to be profiled.
To use the function decorators, they can be initialized in place or globally to reuse within many functions.
The ``dft_fn`` is the decorator for the application.
It takes two arguments: 1) ``cat`` represents the category for the event and 2) an optional ``name`` represents the name of the event.
In general, the name of the event can be automatically loaded by the function during decoration as well.

.. code-block:: python

    from dftracer.logger import dft_fn
    dft_fn = dft_fn("COMPUTE")

    @dft_fn.log
    def log_events(index):
        sleep(1)

For logging ``__init__`` function within a class, applications can use ``log_init`` function.

.. code-block:: python

    from dftracer.logger import dft_fn
    dft_fn = dft_fn("COMPUTE")

    class Test:
        @dft_fn.log_init
        def __init__(self):
            sleep(1)

        @dft_fn.log
        def log_events(self, index):
            sleep(1)

For logging ``@staticmethod`` function within a class, applications can use ``log_static`` function.

Iteration/Loop Profiling
****************************************

For logging every block within a loop, we have an ``dft_fn.iter`` which takes a generator function and wraps around the element yield block.

.. code-block:: python

    from dftracer.logger import dft_fn
    dft_fn = dft_fn("COMPUTE")

    for batch in dft_fn.iter(loader.next()):
        sleep(1)

Context style Profiling
****************************************

We can also profile a block of code using Python's context managers using ``dft_fn``.

.. code-block:: python

    from dftracer.logger import dft_fn
    with dft_fn(cat="block", name="step") as dft:
        sleep(1)
        dft.update(step=1)

Custom Profiling
****************************************

Lastly, users can use specific logger entries to log events within their application.
In general this should be only used when other cases cannot be applied.

.. code-block:: python

    from dftracer.logger import dftracer
    dft_logger = dftracer.initialize_log(logfile, data_dir, process_id)
    start = dft_logger.get_time()
    sleep(1)
    end = dft_logger.get_time()
    dft_logger.log_event(name="test", cat="cat2", start, end - start, int_args=args)

.. _`chrome tracing document`: https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview#heading=h.yr4qxyxotyw
.. _symbol: https://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-PDA/LSB-PDA.junk/symversion.html
