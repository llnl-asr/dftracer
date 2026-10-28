================
DFTracer Service
================

Overview
========

The DFTracer service binary is built as ``dftracer_service`` (from
``src/dftracer/service/service.cpp``) and runs one per node as:

.. code-block:: bash

   dftracer_service <start|stop>

``dftracer_service_dbg`` is the same service built like the other ``_dbg``
libraries, with cpp-logger: ``DFTRACER_LOG_LEVEL`` (``ERROR`` ... ``DEBUG``)
takes effect, and its messages go to the service's ``.log``. The release
``dftracer_service`` compiles that logging out and prints only its own reports.

It is configured like the application it serves, from the environment or
``DFTRACER_CONFIGURATION``:

- ``DFTRACER_ENABLE=1``. When tracing is off (unset, or not ``1``), ``start``
  and ``stop`` do nothing and exit at once, so a job script can always call
  them.
- ``DFTRACER_LOG_FILE=<output_prefix>``: the same prefix the application uses.
  The service writes next to the application's traces; there is no separate
  directory.

Useful optional variables:

- ``DFTRACER_TRACE_INTERVAL_MS=<milliseconds>`` (default is 1000)
- ``DFTRACER_LIBUV_THREADS=<count>`` (default is 1)
- ``DFTRACER_ENABLE_PC_MODE=1``: producer/consumer mode, see below.

Optional YAML key (when using ``DFTRACER_CONFIGURATION``):

- ``tracer.libuv_threads``
- ``profiler.libuv_threads``

Files
=====

Everything is named from ``DFTRACER_LOG_FILE`` and the node's hostname, so the
services of a multi-node job can share one prefix on a shared filesystem
without their files colliding:

==================================  ==================================================
``<log_file>_<host>.pfw.gz``         the node trace (the service's telemetry; in
                                     producer/consumer mode also every traced
                                     process's events)
``<log_file>_<host>-<n>.pfw.gz``     producer/consumer mode: the node trace's next
                                     files, once one holds ``DFTRACER_PC_ROLLOVER_MB``
                                     (default 1024) of compressed events
``<log_file>_<host>.log``            the service's output and errors, including its
                                     report at shutdown
``<log_file>_<host>.pid``            the running service's PID; ``stop`` reads it, and
                                     it is removed when the service exits
``<log_file>-<hash>-spill.pfw.gz``   producer/consumer mode only: events a process
                                     could not hand to the service (see below);
                                     appended to the node trace and deleted at stop
==================================  ==================================================

Rollover keeps each file small enough to read in parallel from a parallel
filesystem: every file is complete gzip members, so a reader can split a
node's trace across files as well as within one. ``DFTRACER_PC_ROLLOVER_MB``
takes fractions; ``0`` keeps a single file.

``start`` returns once the service is ready (in producer/consumer mode, once it
accepts events), and refuses to start a second service for the same
``DFTRACER_LOG_FILE`` on a node. ``stop`` needs the same ``DFTRACER_LOG_FILE``
(and ``DFTRACER_ENABLE=1``) as ``start``.

The daemon also treats ``SIGTERM`` the same as ``SIGINT``: both trigger a
graceful shutdown that flushes and compresses the trace before exiting. This
matters because job schedulers (Flux, Slurm) send ``SIGTERM`` -- not
``SIGINT`` -- when cancelling a job/cgroup, so a job cancellation still produces
a valid, flushed trace instead of an abruptly killed one.

Producer/consumer mode and spill files
======================================

With ``DFTRACER_ENABLE_PC_MODE=1`` (for the service and the application),
traced processes hand their events to the service through a shared-memory ring,
and the service writes them all to the node trace (see
:doc:`design_pc_service`).

An event a process cannot hand over is not dropped: it goes to a file of that
process's own, ``<log_file>-<hash>-spill.pfw.gz``, buffered and compressed as
in-process tracing would. That happens when

- the ring is full: the service has fallen behind (more
  ``DFTRACER_PC_CONSUMER_THREADS``, a larger ring or aggregation help). The
  process does not wait for room (``DFTRACER_PC_FULL_WAIT_US``, default 0),
  which would hold the application to the service's pace: it traces
  in-process, as if producer/consumer mode were off, and tries the ring again
  every millisecond,
- no service was running when the process started, or
- the service crashed or stopped while the process was running (a process
  checks every 100 ms; events the service had already taken but not yet
  written die with it).

Only what did not fit is in a spill file -- the process's start and end events
and everything else are in the node trace. A process that never spills creates
no spill file. Its events are aggregated and serialized in the process already,
so at ``stop`` the service appends every finished spill file of its node's
processes (and its own) to the node trace as it is, then deletes it: a run that
spilled still ends up as the node trace alone. A spill file a process is still
writing (it holds a lock on it) is left in place. Processes report how many
events they spilled on stderr, and the service reports in its ``.log`` how many
did not fit and how many spill files it appended.

If the service crashed, nothing appends the spill files: they stay next to the
node trace, and they are ordinary trace files, so read them with it.

Single-node quick start
=======================

.. code-block:: bash

   export DFTRACER_ENABLE=1
   export DFTRACER_LOG_FILE=/path/to/output/trace
   export DFTRACER_TRACE_INTERVAL_MS=1000
   export DFTRACER_LIBUV_THREADS=1

   # Start in daemon mode
   dftracer_service start

   # ... run the application with the same environment ...

   # Stop
   dftracer_service stop

Multi-node notes
================

Start one service per node, with the same environment as the application. As
the files are named by hostname, all nodes can use the same
``DFTRACER_LOG_FILE``.

If your binary is not in ``PATH``, point ``SERVICE_BIN`` to either:

- ``<build_dir>/bin/dftracer_service``
- ``<install_prefix>/bin/dftracer_service``

Examples below assume:

.. code-block:: bash

   export SERVICE_BIN=dftracer_service
   export DFTRACER_ENABLE=1
   export DFTRACER_LOG_FILE=/path/to/output/trace
   export DFTRACER_TRACE_INTERVAL_MS=1000
   export DFTRACER_LIBUV_THREADS=1

Run on multiple nodes with mpirun
=================================

.. code-block:: bash

   # one service per node
   mpirun -np 4 --map-by ppr:1:node "${SERVICE_BIN}" start
   # ... the application ...
   mpirun -np 4 --map-by ppr:1:node "${SERVICE_BIN}" stop

Run on multiple nodes with flux run
===================================

.. code-block:: bash

   flux run -N 4 -n 4 "${SERVICE_BIN}" start
   # ... the application ...
   flux run -N 4 -n 4 "${SERVICE_BIN}" stop

Run on multiple nodes with srun
===============================

.. code-block:: bash

   srun -N 4 -n 4 "${SERVICE_BIN}" start
   # ... the application ...
   srun -N 4 -n 4 "${SERVICE_BIN}" stop

Troubleshooting
===============

- If startup fails, verify ``DFTRACER_LOG_FILE`` is set and its folder is
  writable, and read ``<log_file>_<host>.log`` on that node.
- If ``start`` or ``stop`` does nothing, check that ``DFTRACER_ENABLE=1``: with
  tracing off both are no-ops.
- If ``stop`` reports no running server, verify it runs with the same
  ``DFTRACER_LOG_FILE`` as ``start`` on that node.
- Spill files left next to the node trace mean the service was not running,
  crashed, or stopped while a process still wrote one; read them with the node
  trace. The service's ``.log`` says how many events did not fit in the ring.
