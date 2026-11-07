=============
Testing Guide
=============

We can never have enough testing. Any additional tests you can write are always
greatly appreciated.

----------
Unit Tests
----------

Testing new core features within DFTracer should be implemented in the ``test`` using simple programs
.. _pipeline-profiling:

-------------------------------
Profiling dftracer's own phases
-------------------------------

Configure with ``-DDFTRACER_ENABLE_PROFILING=ON`` to measure where each
event's overhead goes. The probes are compiled out of every other build. Each
phase's time and count is summed over all threads, and at finalize the
average is printed to stderr (``[DFTRACER PROFILE]`` lines). When
``DFTRACER_PROFILE_FILE`` is set, the same numbers are appended to that file
as CSV (``pid,mode,phase,layer,count,total_ns,avg_ns,ns_per_event``).

================= ==================================== ================================
phase             in-process (``local``)               producer/consumer (``pc``)
================= ==================================== ================================
capture[<layer>]  the interceptor or API region        the same
filtered          intercepted calls not traced         the same
buffer            buffer manager lock and bookkeeping  ring slot and liveness checks
aggregation       Aggregator rule check and bucketing  (in the service)
serialization     the JSON line                        the binary ring record
compression       gzip of a full buffer                (in the service)
store             fwrite of the compressed buffer      copy into the shared-memory ring
================= ==================================== ================================

``<layer>`` is the event's layer: ``LIBC_IO`` for POSIX and STDIO calls,
``C_APP`` and ``CPP_APP`` for the C and C++ APIs, ``MPI``, ``HDF5`` and so on.
Phases nest, and each records only its own (self) time, so the phases add up
to the whole per-event overhead. The phase and whether a probe counts are
fixed at compile time; at run time a probe reads the TSC twice and adds to
the calling thread's counters. The report states what one probe costs.

``test/perf/pipeline_profile`` measures this for a load of POSIX calls and C
API regions in each mode, against a build without dftracer
(``-DDFTRACER_ENABLE_PERF_TESTS=ON``, then ``ctest -L perf -R
pipeline_profile``, or run ``run_pipeline_profile.sh <build dir> <out dir>``
directly). ``summary.txt`` lists, per thread count and metadata setting, each
phase's average time and its share of an event in every mode, next to the
wall-clock overhead per event.
