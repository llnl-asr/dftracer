Omnistat Telemetry
==================

DFTracer can ingest Omnistat CSV exports after an application run and merge the
samples into the DFTracer trace stream as timestamped accelerator telemetry
counter events.

What it does
------------

- Reads Omnistat CSV exports produced separately from DFTracer.
- Selects configured AMD GPU/APU counters or all numeric counters.
- Normalizes epoch timestamps into DFTracer's microsecond timestamp basis.
- Emits telemetry as counter events with category ``omnistat`` for correlation.

What it does not do
-------------------

- It does not launch, manage, or stop Omnistat.
- It does not query Prometheus or VictoriaMetrics.
- It does not provide per-kernel or per-thread attribution.
- It currently supports CSV ingestion only.
- It currently supports numeric epoch timestamps only.

Configuration example
---------------------

.. code-block:: yaml

   enable: true
   profiler:
     init: FUNCTION
     log_file: trace
   features:
     metadata: true
     omnistat:
       enable: true
       input: /path/to/omnistat.csv
       format: csv
       timestamp_column: timestamp
       timestamp_format: auto
       counters:
         - rocm_gpu_utilization
         - rocm_average_socket_power_watts
         - rocm_memory_usage
         - GRBM_COUNT
         - SQ_WAVES
       include_all_counters: false
       attach_to_trace: true
       export_raw: false
       time_sync:
         mode: absolute

Example output record
---------------------

.. code-block:: json

   {"name":"rocm_gpu_utilization","cat":"omnistat","ts":1710000000000000,
    "ph":"C","pid":0,"tid":0,
    "args":{"hhash":"...","source":"omnistat","metric":"rocm_gpu_utilization",
            "value":85.5,"hostname":"node001","device_id":"0",
            "original_timestamp":"1710000000.000"}}

Operational notes
-----------------

- Omnistat data is typically node-level or device-level telemetry, not per-process data.
- Shared GPUs can make attribution approximate.
- Hardware counters may be multiplexed by Omnistat or ROCprofiler-SDK.
- Short regions may not align well with sampled telemetry intervals.

Workflow
--------

1. Run Omnistat separately and export CSV telemetry.
2. Point DFTracer at the CSV file with ``features.omnistat.input``.
3. Enable ``features.omnistat.enable`` and keep ``attach_to_trace`` set.
4. Run DFTracer and inspect the emitted ``omnistat`` counter events.
