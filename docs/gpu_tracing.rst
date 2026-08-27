============
GPU Tracing
============

DFTracer can trace GPU activity on both vendors, writing GPU events into the
same trace, on the same timeline, as the CPU-side I/O and application events:

* **AMD** via `rocprofiler-sdk <https://github.com/ROCm/rocprofiler-sdk>`_
  (``DFTRACER_ENABLE_HIP_TRACING``), producing ``HIP`` events.
* **NVIDIA** via `CUPTI <https://docs.nvidia.com/cupti/>`_, the CUDA Profiling
  Tools Interface (``DFTRACER_ENABLE_CUDA_TRACING``), producing ``CUDA`` events.

Both are compile-time options and both default to ``OFF``.

--------------------------
NVIDIA GPU tracing (CUPTI)
--------------------------

Building
********

CUPTI ships inside the CUDA toolkit, so no separate package is needed. Enable
the backend and let the build find the toolkit:

.. code-block:: Bash

    # autobuild.sh
    ./autobuild.sh --enable-cuda

    # pip / setup.py
    DFTRACER_ENABLE_CUDA_TRACING=ON pip install --no-binary dftracer dftracer

    # plain CMake
    cmake -DDFTRACER_ENABLE_CUDA_TRACING=ON ..

To build against a specific toolkit rather than the detected one, point
``DFTRACER_CUDA_PATH`` at its root (the directory containing ``bin/nvcc``):

.. code-block:: Bash

    ./autobuild.sh --with-cuda /usr/tce/packages/cuda/cuda-12.6.0

    DFTRACER_ENABLE_CUDA_TRACING=ON DFTRACER_CUDA_PATH=/usr/local/cuda-12.6 \
      pip install --no-binary dftracer dftracer

    cmake -DDFTRACER_ENABLE_CUDA_TRACING=ON \
          -DDFTRACER_CUDA_PATH=/usr/local/cuda-12.6 ..

``--with-cuda`` implies ``--enable-cuda``.

Toolkit detection
*****************

When ``DFTRACER_CUDA_PATH`` is not set, the CUDA toolkit is searched for in this
order, and the first one that actually contains ``cupti.h`` wins:

#. ``CUDAToolkit_ROOT`` / ``CUDA_TOOLKIT_ROOT_DIR`` CMake variables
#. ``CUDAToolkit_ROOT``, ``CUDA_HOME`` or ``CUDA_PATH`` in the environment --
   this is what ``module load cuda/12.6.0`` sets on most clusters
#. CMake's own ``find_package(CUDAToolkit)``
#. ``nvcc`` on ``PATH``
#. ``/usr/local/cuda``

Both toolkit layouts are handled: CUDA 10.1 and older keep CUPTI under
``<cuda>/extras/CUPTI``, while 10.2 and newer merge it into the toolkit's
``include/`` and ``lib64/``.

With ``DFTRACER_ENABLE_DYNAMIC_DETECTION=ON``, CUPTI is enabled automatically
whenever it is found, without having to pass ``DFTRACER_ENABLE_CUDA_TRACING``.

The configure step reports what it settled on:

.. code-block:: text

    -- [DFTRACER] found CUPTI at /usr/tce/packages/cuda/cuda-12.6.0/include (cuda root /usr/tce/packages/cuda/cuda-12.6.0)
    -- [DFTRACER] CUDA toolkit version: 12.6.0

Supported toolkit versions
**************************

CUDA 10.1 through 13.x are supported. NVIDIA renumbers the CUPTI activity
record structs (``CUpti_ActivityKernel4`` ... ``CUpti_ActivityKernel11``) as the
layouts change, and removes the superseded typedefs a few releases later, so
DFTracer selects the right struct version at compile time from the detected
toolkit version. That version is baked into the generated config header as
``DFTRACER_CUDA_VERSION``.

Traced events
*************

All CUDA events are written with type ``CUDA`` (see :doc:`trace_format`) and a
``cat`` naming the activity kind:

.. table:: section - CUDA event categories
   :widths: auto

   ====================== ==========================================================
   Category               Events
   ====================== ==========================================================
   CUDA_RUNTIME_API       CUDA runtime API calls (``cudaMalloc``, ``cudaMemcpy``, ...)
   CUDA_DRIVER_API        CUDA driver API calls (``cuLaunchKernel``, ...)
   CUDA_KERNEL            Kernel launches, named by the kernel symbol
   CUDA_MEMCPY            Host/device memory copies, named by direction
   CUDA_MEMCPY_P2P        Peer-to-peer (device-to-device) copies
   CUDA_MEMSET            Device memsets
   CUDA_MEMORY            Device allocations and frees
   CUDA_SYNC              Stream, event and context synchronization
   CUDA_GRAPH             CUDA graph execution
   CUDA_MARKER            NVTX markers
   CUDA_UNIFIED_MEMORY    Unified-memory page faults, migrations and thrashing
   CUDA_OVERHEAD          CUPTI's own tracing overhead
   ====================== ==========================================================

Each event carries the relevant identifiers in ``args`` -- ``device_id``,
``context_id``, ``stream_id`` and ``correlation_id`` throughout, plus
kind-specific fields such as ``bytes`` and ``copy_kind`` for copies, or
``grid_x``/``block_x``, ``registers_per_thread`` and the shared-memory sizes for
kernels. ``correlation_id`` links a GPU-side record back to the API call that
issued it.

.. _cuda-clock-alignment:

Clock alignment
***************

CUPTI timestamps are nanoseconds on CUPTI's own monotonic clock, whereas
DFTracer's events come from ``gettimeofday`` rendered in the configured
``time_metric`` unit. The two are not comparable as-is.

The CUPTI backend therefore rebases every record: it scales CUPTI nanoseconds
into the active ``time_metric`` unit and adds a fixed offset sampled once by
reading both clocks back to back. GPU events consequently land on the same
timeline as CPU-side I/O, MPI and application events, and nest correctly inside
the application regions that issued them. The HIP/rocprofiler backend does the
same, so a trace can mix both without further work.

This is what ``test/cuda/check_cuda_trace.py`` asserts: that CUDA events fall
inside the application region that produced them.

Running
*******

Nothing extra is required at run time -- with a CUPTI-enabled build, GPU tracing
starts with DFTracer:

.. code-block:: Bash

    DFTRACER_ENABLE=1 DFTRACER_LOG_FILE=./trace ./my_cuda_app

Confirm the build has the backend compiled in by looking at the ``build`` key of
the ``DFTRACER`` metadata record in the trace, which reports ``"cuda": 1``.

.. note::

    CUPTI allows only one profiling client per process. Running under another
    CUPTI-based profiler at the same time (``nsys``, ``ncu``, or a framework
    profiler such as ``torch.profiler``) will cause one of them to fail to
    attach.

Testing
*******

Two tests cover the backend when built with ``DFTRACER_ENABLE_TESTS=ON``:

* ``test_cuda_tracing`` -- a CUDA runtime API workload (allocations, host/device
  copies, memset, stream synchronization) followed by
  ``check_cuda_trace_test_cuda_tracing``, which verifies the events are present,
  correctly typed, and clock-aligned.
* ``test/py/cuda_test.py`` -- a PyTorch workload covering real GPU kernels.

Both skip rather than fail on machines without an NVIDIA device, so the suite
stays green on CPU-only build nodes.

-----------------------
AMD GPU tracing (HIP)
-----------------------

HIP tracing requires ROCm 6.2 or newer, which is when ``rocprofiler-sdk`` was
introduced. Enable it with ``DFTRACER_ENABLE_HIP_TRACING=ON`` (or
``./autobuild.sh --enable-hip``); the build finds the SDK via ``ROCM_PATH``.
Events are written with type ``HIP`` and a ``cat`` of the rocprofiler kind name,
covering the HIP runtime API, HSA APIs, kernel dispatches, memory copies,
scratch memory, page migration and RCCL.
