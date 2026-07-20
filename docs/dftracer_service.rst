================
DFTracer Service
================

Overview
========

The DFTracer service binary is built as ``dftracer_service`` (from
``src/dftracer/service/service.cpp``) and runs as:

.. code-block:: bash

   dftracer_service <start|stop> [log_dir]

Required environment variables:

- ``DFTRACER_ENABLE=1``
- ``DFTRACER_LOG_FILE=<output_prefix>``

Useful optional variable:

- ``DFTRACER_TRACE_INTERVAL_MS=<milliseconds>`` (default is 1000)
- ``DFTRACER_LIBUV_THREADS=<count>`` (default is 1)

Optional YAML key (when using ``DFTRACER_CONFIGURATION``):

- ``tracer.libuv_threads``
- ``profiler.libuv_threads``

The service appends hostname information to ``DFTRACER_LOG_FILE`` and writes
one PID file per service process at
``<log_dir>/dftracer_server_<hostname>.pid`` (``.out``/``.err`` logs are
namespaced by hostname the same way). This means multiple nodes can safely
share the same ``log_dir`` (e.g. on a shared filesystem) without their PID
files colliding — each node's ``start``/``stop`` only ever touches its own
PID file.

The daemon also treats ``SIGTERM`` the same as ``SIGINT``: both trigger a
graceful shutdown that flushes and compresses the trace buffer before
exiting. This matters because job schedulers (Flux, Slurm) send ``SIGTERM``
— not ``SIGINT`` — when cancelling a job/cgroup, so a job cancellation still
produces a valid, flushed trace instead of an abruptly killed one.

Single-node quick start
=======================

.. code-block:: bash

   export DFTRACER_ENABLE=1
   export DFTRACER_LOG_FILE=/path/to/output/dftracer-service
   export DFTRACER_TRACE_INTERVAL_MS=1000
   export DFTRACER_LIBUV_THREADS=1

   # Start in daemon mode
   dftracer_service start /tmp/dftracer_service

   # Stop
   dftracer_service stop /tmp/dftracer_service

Multi-node notes
================

When launching on multiple nodes, use a per-node ``log_dir`` so PID files do
not conflict on shared filesystems. A safe pattern is to include hostname in
the directory path.

If your binary is not in ``PATH``, point ``SERVICE_BIN`` to either:

- ``<build_dir>/bin/dftracer_service``
- ``<install_prefix>/bin/dftracer_service``

Examples below assume:

.. code-block:: bash

   export SERVICE_BIN=dftracer_service
   export DFTRACER_ENABLE=1
   export DFTRACER_LOG_FILE=/path/to/output/dftracer-service
   export DFTRACER_TRACE_INTERVAL_MS=1000
   export DFTRACER_LIBUV_THREADS=1

Run on multiple nodes with mpirun
=================================

Start one service process per rank/node:

.. code-block:: bash

   mpirun -np 4 bash -lc '
     node_tag=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}
     node_name=$(hostname -s)
     log_dir=/tmp/dftracer_service_${node_name}_${node_tag}
     mkdir -p "${log_dir}"
     "${SERVICE_BIN}" start "${log_dir}"
   '

Stop all service processes:

.. code-block:: bash

   mpirun -np 4 bash -lc '
     node_tag=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}
     node_name=$(hostname -s)
     log_dir=/tmp/dftracer_service_${node_name}_${node_tag}
     "${SERVICE_BIN}" stop "${log_dir}"
   '

Run on multiple nodes with flux run
===================================

Start one service process per task:

.. code-block:: bash

   flux run -N 4 -n 4 bash -lc '
     node_tag=${FLUX_TASK_RANK:-0}
     node_name=$(hostname -s)
     log_dir=/tmp/dftracer_service_${node_name}_${node_tag}
     mkdir -p "${log_dir}"
     "${SERVICE_BIN}" start "${log_dir}"
   '

Stop all service processes:

.. code-block:: bash

   flux run -N 4 -n 4 bash -lc '
     node_tag=${FLUX_TASK_RANK:-0}
     node_name=$(hostname -s)
     log_dir=/tmp/dftracer_service_${node_name}_${node_tag}
     "${SERVICE_BIN}" stop "${log_dir}"
   '

Run on multiple nodes with srun
===============================

Start one service process per task:

.. code-block:: bash

   srun -N 4 -n 4 bash -lc '
     node_tag=${SLURM_PROCID:-0}
     node_name=${SLURMD_NODENAME:-$(hostname -s)}
     log_dir=/tmp/dftracer_service_${node_name}_${node_tag}
     mkdir -p "${log_dir}"
     "${SERVICE_BIN}" start "${log_dir}"
   '

Stop all service processes:

.. code-block:: bash

   srun -N 4 -n 4 bash -lc '
     node_tag=${SLURM_PROCID:-0}
     node_name=${SLURMD_NODENAME:-$(hostname -s)}
     log_dir=/tmp/dftracer_service_${node_name}_${node_tag}
     "${SERVICE_BIN}" stop "${log_dir}"
   '

Troubleshooting
===============

- If startup fails, verify ``DFTRACER_LOG_FILE`` is set and writable.
- Check ``<log_dir>/dftracer_server.out`` and
  ``<log_dir>/dftracer_server.err`` on each node.
- If ``stop`` reports no running server, verify you are using the same
  ``log_dir`` path used for ``start`` on that node.
