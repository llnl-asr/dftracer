================
Example Programs
================`

------------
C++ Example
------------

Application Level Example:
**************************

.. code-block:: c
   :linenos:

    #include <dftracer/dftracer.h>

    void foo() {
      DFTRACER_CPP_FUNCTION(); // Add at the begining of each function
      sleep(1);
      {
        DFTRACER_CPP_REGION(CUSTOM); // Add at the beginning of code block. keep name unique
        sleep(1);
        DFTRACER_CPP_REGION_START(CUSTOM_BLOCK); // add start. keep name unique
        sleep(1);
        DFTRACER_CPP_REGION_END(CUSTOM_BLOCK); // add end. Match name from START.
      }
    }

    int main(int argc, char *argv[]) {
      // Basic Bookkeeping
      int init = 0;
      if (argc > 2) {
        if (strcmp(argv[2], "1") == 0) {
          // Initialize Application Profiler
          DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
          init = 1;
        }
      }
      char filename[1024];
      sprintf(filename, "%s/demofile.txt", argv[1]);

      // Run functions
      foo();
      // Implicit I/O calls No need for marking.
      FILE *fh = fopen(filename, "w+");
      if (fh != NULL) {
        fwrite("hello", sizeof("hello"), 1, fh);
        fclose(fh);
      }
      if (init == 1) {
        // Finalize Application Profiler
        DFTRACER_CPP_FINI();
      }
      return 0;
    }

For this example, link with libdftracer.so at compile time.
As the DFTRACER_CPP_INIT do not pass log file or data dir, we need to set ``DFTRACER_LOG_FILE`` and ``DFTRACER_DATA_DIR``.
By default the DFTracer mode is set to FUNCTION.
Example of running this configurations are:

.. code-block:: bash
   :linenos:

    # the process id, app_name and .pfw will be appended by the profiler for each app and process.
    # name of final log file is ~/log_file-<APP_NAME>-<PID>.pfw
    DFTRACER_LOG_FILE=~/log_file
    # Colon separated paths for including for profiler
    DFTRACER_DATA_DIR=/dev/shm/:/p/gpfs1/$USER/dataset
    # Enable profiler
    DFTRACER_ENABLE=1

.. warning::

    Note: DFTRACER_DATA_DIR uses a prefix tree. If both ``/local/scratch`` and
    ``/local/scratch/data`` are in the list, the order matters—
    the last one will override the first. As a result, the first path won’t be traced.
    To avoid this, only use ``/local/scratch``.

LD_PRELOAD Example:
**************************

.. code-block:: c
   :linenos:

    #include <dftracer/dftracer.h>

    int main(int argc, char *argv[]) {
      char filename[1024];
      sprintf(filename, "%s/demofile.txt", argv[1]);
      foo(); # function will be ignored in pure LD_PRELOAD mode.
      // Implicit I/O calls No need for marking.
      FILE *fh = fopen(filename, "w+");
      if (fh != NULL) {
        fwrite("hello", sizeof("hello"), 1, fh);
        fclose(fh);
      }
      return 0;
    }

For this example, LD_PRELOAD the executable with libdftracer_preload.so at runtime.
We need to set ``DFTRACER_LOG_FILE`` and ``DFTRACER_DATA_DIR``.
Example of running this configurations are:

.. code-block:: bash
   :linenos:

    # the process id, app_name and .pfw will be appended by the profiler for each app and process.
    # name of final log file is ~/log_file-<APP_NAME>-<PID>.pfw
    export DFTRACER_LOG_FILE=~/log_file
    # Colon separated paths for including for profiler
    export DFTRACER_DATA_DIR=/dev/shm/:/p/gpfs1/$USER/dataset
    # Set the mode to PRELOAD
    export DFTRACER_INIT=PRELOAD
    # Enable profiler
    export DFTRACER_ENABLE=1

Minimal PAPI example:
**************************

The standalone sample under ``examples/papi_standalone`` shows PAPI counters embedded in
the regular DFTracer trace stream. Build DFTracer with ``DFTRACER_ENABLE_PAPI_TRACING=ON``,
then run:

.. code-block:: bash

    cd examples/papi_standalone
    make run-single \
      DFTRACER_INCLUDEDIR=/path/to/dftracer/include \
      CPP_LOGGER_INCLUDEDIR=/path/to/dftracer/include \
      DFTRACER_LIBDIR=/path/to/dftracer/lib64

This emits trace files under ``examples/papi_standalone/traces`` containing both regular
DFTracer events and PAPI counter metadata/events such as ``PAPI_TOT_CYC`` and
``PAPI_TOT_INS``.

Hybrid Example:
**************************

.. code-block:: c
   :linenos:

    #include <dftracer/dftracer.h>

    void foo() {
      DFTRACER_CPP_FUNCTION(); // Add at the begining of each function
      sleep(1);
      {
        DFTRACER_CPP_REGION(CUSTOM); // Add at the beginning of code block. keep name unique
        sleep(1);
        DFTRACER_CPP_REGION_START(CUSTOM_BLOCK); // add start. keep name unique
        sleep(1);
        DFTRACER_CPP_REGION_END(CUSTOM_BLOCK); // add end. Match name from START.
      }
    }

    int main(int argc, char *argv[]) {
      // Basic Bookkeeping
      int init = 0;
      if (argc > 2) {
        if (strcmp(argv[2], "1") == 0) {
          // Initialize Application Profiler
          DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
          init = 1;
        }
      }
      char filename[1024];
      sprintf(filename, "%s/demofile.txt", argv[1]);

      // Run functions
      foo();
      // Implicit I/O calls No need for marking.
      FILE *fh = fopen(filename, "w+");
      if (fh != NULL) {
        fwrite("hello", sizeof("hello"), 1, fh);
        fclose(fh);
      }
      if (init == 1) {
        // Finalize Application Profiler
        DFTRACER_CPP_FINI();
      }
      return 0;
    }

For this example, link with libdftracer.so at compile time and LD_PRELOAD the executable with libdftracer_preload.soat runtime.
As the DFTRACER_CPP_INIT do not pass log file or data dir, we need to set ``DFTRACER_LOG_FILE`` and ``DFTRACER_DATA_DIR``.
By default the DFTracer mode is set to FUNCTION.
Example of running this configurations are:

.. code-block:: bash
   :linenos:

    # the process id, app_name and .pfw will be appended by the profiler for each app and process.
    # name of final log file is ~/log_file-<APP_NAME>-<PID>.pfw
    DFTRACER_LOG_FILE=~/log_file
    # Colon separated paths for including for profiler
    DFTRACER_DATA_DIR=/dev/shm/:/p/gpfs1/$USER/dataset
    # Set the mode to PRELOAD
    export DFTRACER_INIT=PRELOAD
    # Enable profiler
    DFTRACER_ENABLE=1

------------
C Example
------------

Application Level Example:
**************************

.. code-block:: c
   :linenos:

    #include <dftracer/dftracer.h>

    void foo() {
      DFTRACER_C_FUNCTION_START();
      sleep(1);
      if (<CONDITION>) {
        DFTRACER_C_FUNCTION_END();
        return; // Define DFTRACER_C_FUNCTION_END on every branch
      }
      {
        DFTRACER_C_REGION_START(CUSTOM);
        sleep(1);
        DFTRACER_C_REGION_END(CUSTOM); // END region CUSTOM.
      }
      DFTRACER_C_FUNCTION_END(); // Define DFTRACER_C_FUNCTION_END on every branch
    }

    int main(int argc, char *argv[]) {
      // Basic Bookkeeping
      int init = 0;
      if (argc > 2) {
        if (strcmp(argv[2], "1") == 0) {
          // Initialize Application Profiler
          DFTRACER_C_INIT(nullptr, nullptr, nullptr);
          init = 1;
        }
      }
      char filename[1024];
      sprintf(filename, "%s/demofile.txt", argv[1]);

      // Run functions
      foo();
      // Implicit I/O calls No need for marking.
      FILE *fh = fopen(filename, "w+");
      if (fh != NULL) {
        fwrite("hello", sizeof("hello"), 1, fh);
        fclose(fh);
      }
      if (init == 1) {
        // Finalize Application Profiler
        DFTRACER_C_FINI();
      }
      return 0;
    }

For this example, link with libdftracer.so at compile time.
As the DFTRACER_CPP_INIT do not pass log file or data dir, we need to set ``DFTRACER_LOG_FILE`` and ``DFTRACER_DATA_DIR``.
By default the DFTracer mode is set to FUNCTION.
Example of running this configurations are:

.. code-block:: bash
   :linenos:

    # the process id, app_name and .pfw will be appended by the profiler for each app and process.
    # name of final log file is ~/log_file-<APP_NAME>-<PID>.pfw
    DFTRACER_LOG_FILE=~/log_file
    # Colon separated paths for including for profiler
    DFTRACER_DATA_DIR=/dev/shm/:/p/gpfs1/$USER/dataset
    # Enable profiler
    DFTRACER_ENABLE=1

LD_PRELOAD Example:
**************************

.. code-block:: c
   :linenos:

    #include <dftracer/dftracer.h>

    int main(int argc, char *argv[]) {
      char filename[1024];
      sprintf(filename, "%s/demofile.txt", argv[1]);
      foo(); # function will be ignored in pure LD_PRELOAD mode.
      // Implicit I/O calls No need for marking.
      FILE *fh = fopen(filename, "w+");
      if (fh != NULL) {
        fwrite("hello", sizeof("hello"), 1, fh);
        fclose(fh);
      }
      return 0;
    }

For this example, LD_PRELOAD the executable with libdftracer_preload.so at runtime.
We need to set ``DFTRACER_LOG_FILE`` and ``DFTRACER_DATA_DIR``.
Example of running this configurations are:

.. code-block:: bash
   :linenos:

    # the process id, app_name and .pfw will be appended by the profiler for each app and process.
    # name of final log file is ~/log_file-<APP_NAME>-<PID>.pfw
    export DFTRACER_LOG_FILE=~/log_file
    # Colon separated paths for including for profiler
    export DFTRACER_DATA_DIR=/dev/shm/:/p/gpfs1/$USER/dataset
    # Set the mode to PRELOAD
    export DFTRACER_INIT=PRELOAD
    # Enable profiler
    export DFTRACER_ENABLE=1

Hybrid Example:
**************************

.. code-block:: c
   :linenos:

    #include <dftracer/dftracer.h>

    void foo() {
      DFTRACER_C_FUNCTION_START();
      sleep(1);
      if (<CONDITION>) {
        DFTRACER_C_FUNCTION_END();
        return; // Define DFTRACER_C_FUNCTION_END on every branch
      }
      {
        DFTRACER_C_REGION_START(CUSTOM);
        sleep(1);
        DFTRACER_C_REGION_END(CUSTOM); // END region CUSTOM.
      }
      DFTRACER_C_FUNCTION_END(); // Define DFTRACER_C_FUNCTION_END on every branch
    }

    int main(int argc, char *argv[]) {
      // Basic Bookkeeping
      int init = 0;
      if (argc > 2) {
        if (strcmp(argv[2], "1") == 0) {
          // Initialize Application Profiler
          DFTRACER_C_INIT(nullptr, nullptr, nullptr);
          init = 1;
        }
      }
      char filename[1024];
      sprintf(filename, "%s/demofile.txt", argv[1]);

      // Run functions
      foo();
      // Implicit I/O calls No need for marking.
      FILE *fh = fopen(filename, "w+");
      if (fh != NULL) {
        fwrite("hello", sizeof("hello"), 1, fh);
        fclose(fh);
      }
      if (init == 1) {
        // Finalize Application Profiler
        DFTRACER_C_FINI();
      }
      return 0;
    }

For this example, link with libdftracer.so at compile time and LD_PRELOAD the executable with libdftracer_preload.so at runtime.
As the DFTRACER_CPP_INIT do not pass log file or data dir, we need to set ``DFTRACER_LOG_FILE`` and ``DFTRACER_DATA_DIR``.
By default the DFTracer mode is set to FUNCTION.
Example of running this configurations are:

.. code-block:: bash
   :linenos:

    # the process id, app_name and .pfw will be appended by the profiler for each app and process.
    # name of final log file is ~/log_file-<APP_NAME>-<PID>.pfw
    DFTRACER_LOG_FILE=~/log_file
    # Colon separated paths for including for profiler
    DFTRACER_DATA_DIR=/dev/shm/:/p/gpfs1/$USER/dataset
    # Set the mode to PRELOAD
    export DFTRACER_INIT=PRELOAD
    # Enable profiler
    DFTRACER_ENABLE=1

----------------
Python Example
----------------

For more detailed information about using DFTracer Python logging features,
please refer to the `python dftracer documentation <https://dftracer.readthedocs.io/projects/python/en/latest/examples.html>`_.

-----------------------
Integrated Applications
-----------------------

Here is the list applications that currently use DFTracer.

1. `DLIO Benchmark <https://github.com/argonne-lcf/dlio_benchmark>`_
2. MuMMI
3. Resnet50 with pytorch and torchvision

----------------------------
Example Chrome Tracing Plots
----------------------------

Example of Unet3D application with DLIO Benchmark. This trace shows the first few steps of the benchmark.
Here, we can see that we can get application level calls (e.g., ``train`` and ``TorchDataset``) as well as low-level I/O calls (dark green color).

.. image:: images/tracing/trace.png
  :width: 400
  :alt: Unet3D applications
