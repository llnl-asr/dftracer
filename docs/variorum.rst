Variorum Power Telemetry
========================

DFTracer can sample node-level power through `variorum
<https://github.com/LLNL/variorum>`_, LLNL's vendor-neutral power API, and write
the readings into the trace as counter events alongside the rest of the node
telemetry.

Power is a node-wide quantity. The RAPL, HSMP and OPAL registers variorum reads
report what a socket, its memory and its GPUs draw in total, and that cannot be
attributed to any one process. So it is sampled by ``dftracer_service``, next to
the cpu, memory, io, network and omnistat collectors -- not inside a traced
process -- and the records carry no pid or tid.

What it does
------------

- Calls ``variorum_get_power_json()`` on every service tick.
- Groups the readings into families of measurement: ``cpu``, ``gpu``,
  ``memory``, ``network``, ``node``.
- Emits one counter event per family, with the family as the event category and
  every socket and device in that family as keys in ``args``.
- Reports whatever the machine supports. Nothing in DFTracer holds a list of
  families, so a platform DFTracer has never seen still groups correctly.

What it does not do
-------------------

- It does not cap or set power limits. DFTracer only reads.
- It does not attribute power to a process, thread, or kernel.
- It does not sample per-process energy; use PAPI counters for that.
- It does not install msr-safe or grant register access (see `Requirements`_).

Building
--------

Variorum is optional and off by default:

.. code-block:: bash

   ./autobuild.sh --enable-variorum

or with CMake directly:

.. code-block:: bash

   cmake -DDFTRACER_ENABLE_VARIORUM=ON ...

If a variorum installation is already on the system it is used as-is; point at
one explicitly with ``-DVARIORUM_DIR=/path/to/variorum``. Otherwise DFTracer
fetches and builds a pinned variorum release into its own install prefix.
``DFTRACER_BUILD_VARIORUM`` decides when that happens: ``AUTO`` (the default,
build only if none is installed), ``ALWAYS``, or ``NEVER``.

Reach for ``ALWAYS`` when an installed variorum turns out to be built for none
of the machine's power domains. A distribution package usually is -- the one on
an MI300A node answers::

   _ERROR_VARIORUM_UNSUPPORTED_PLATFORM: Cannot set function pointers

and reports no power at all. Nothing at build time can see that; it is a run
time check inside variorum. So DFTracer treats it like any other node it cannot
read power on, says so once in the service log and goes quiet. Rebuilding with

.. code-block:: bash

   cmake -DDFTRACER_ENABLE_VARIORUM=ON -DDFTRACER_BUILD_VARIORUM=ALWAYS ...

gets a variorum configured for the domains this machine actually has.

``DFTRACER_ENABLE_DYNAMIC_DETECTION=ON`` picks up a variorum that is already
installed, the same way it does for MPI, HWLOC, HIP and PAPI. It does not fetch
one: dynamic detection reports what a machine already has.

Which power domains variorum is built with is worked out from the build machine,
because each of variorum's ``VARIORUM_WITH_*`` options needs a vendor library
and aborts its configure when that library is missing:

============ ==================================== ====================================
Domain       Needs                                Detected from
============ ==================================== ====================================
Intel CPU    msr-safe or CAP_SYS_RAWIO            ``GenuineIntel`` in ``/proc/cpuinfo``
AMD CPU      AMD E-SMI (``libe_smi64``)           ``AuthenticAMD`` plus E-SMI installed
AMD GPU      ROCm SMI (``librocm_smi64``)         ``ROCM_PATH`` / ``CMAKE_PREFIX_PATH``
NVIDIA GPU   NVML                                 ``-DNVML_DIR=``
ARM CPU      --                                   ``aarch64``
IBM CPU      OPAL                                 ``ppc64le``
============ ==================================== ====================================

The build prints what it settled on::

   -- [DFTRACER] variorum power domains to build: AMD_GPU

Load a ``rocm`` module (which sets ``ROCM_PATH``) before configuring on an AMD
GPU node, or the GPU domain is silently left out. Variorum also needs hwloc and
jansson development packages; DFTracer checks for both before starting the build
so a missing one is reported up front.

Runtime
-------

Nothing to switch on: like the other service collectors, it samples on every
tick once the build has variorum. Disable it with ``features.variorum.enable:
false`` in the YAML config, or ``DFTRACER_DISABLE_VARIORUM_POWER=1``.

The sampling interval is the service's own ``DFTRACER_TRACE_INTERVAL_MS``.

At startup the collector takes one reading to settle which families this node
reports and where each of its keys belongs. A sample after that is a parse, one
lookup per reading and one record per family, so no key is picked apart twice.

Trace format
------------

One record per family per tick. ``cat`` is the family, ``type`` is 13
(``TRACE_TYPE_VARIORUM``) and ``ph`` is 2 (``TRACE_PHASE_COUNTER``):

.. code-block:: json

   {"name":"power","cat":"gpu","type":13,"ts":1786641565725200,"ph":2,
    "pid":0,"tid":0,
    "args":{"hhash":"5856312a860ade86","socket_0.GPU_0":129,
            "socket_1.GPU_1":127,"socket_2.GPU_2":129,"socket_3.GPU_3":129,
            "num_gpus_per_socket":1}}

Key names inside a family say which socket and device a reading came from.
Variorum's ``power_<family>_watts`` wrapper is not repeated in them, since the
category already says it, and a reading that is node-wide rather than
per-device is named ``total``:

.. code-block:: json

   {"name":"power","cat":"node","type":13,"ts":1786641565725200,"ph":2,
    "pid":0,"tid":0,"args":{"hhash":"5856312a860ade86","total":1024.5}}

A reading that is not watts but does describe a device -- ``num_gpus_per_socket``
above -- joins that device's family, so everything known about the GPUs arrives
in one record. Only a reading that names no device at all is grouped under
``other``. Variorum's own timestamp is dropped: the record already carries the
trace's, in the trace's time base.

Requirements
------------

Reading power registers needs privilege on most machines:

- **Intel**: msr-safe, or ``CAP_SYS_RAWIO`` on ``/dev/cpu/*/msr``.
- **AMD CPU**: ``/dev/hsmp`` plus AMD E-SMI.
- **AMD GPU**: ROCm SMI, which reads through the kernel driver and needs no
  extra privilege.
- **IBM**: OPAL, through ``/sys/firmware/opal``.

When variorum cannot read anything, the collector says so once and goes quiet
for the rest of the run, the same way the omnistat collector does when no
exporter is running. A trace from such a node simply has no power records; the
rest of the telemetry is unaffected.

Checking a trace
----------------

``scripts/check_variorum_trace.py`` validates that a trace really carries power:

.. code-block:: bash

   python3 scripts/check_variorum_trace.py trace_*.pfw.gz \
       --min-events 10 --require-family gpu --min-power-records 3

It checks the records are counters, that each family carries numeric readings,
and that the values actually move -- a frozen watt value means the registers are
not being read. Pass ``--skip-if-no-power`` on machines that cannot read power
registers at all, where their absence says nothing about DFTracer.
