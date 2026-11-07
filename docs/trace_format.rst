===================
DFTracer Format
===================

At a high-level, DFTracer events are inspired by the `chrome tracing document`_.
The similarity to chrome tracing format is in the structure of each event type.
However, it does not strictly follow the format due to parallelization reasons.
A DFTracer trace is newline-delimited JSON and nothing else: one complete JSON
object per line, with no enclosing array.

.. code-block:: bash

    JSON LINE
    JSON LINE
    JSON LINE

Earlier versions wrapped the trace in "[" and "]" for Perfetto. Those are gone,
so a trace can be piped straight into line-oriented tools:

.. code-block:: bash

    jq -c 'select(.ph == 1)' trace.pfw

Feeding a trace to Perfetto now requires wrapping it yourself, for example
``jq --slurp '.' trace.pfw``.


----------

.. _`Event Types`:

----------------------------------------
Event Types
----------------------------------------

Every event carries a "type" column: a small integer naming the instrumentation
layer that produced it. It is a closed vocabulary, unlike "cat", which stays
free-form and holds the sub-category within that layer.

========  =============  ===================================================
 Value     Name           Produced by
========  =============  ===================================================
 0         UNKNOWN        Never written; fallback for unrecognised values
 1         DFTRACER       Internal events: start, end, hash and metadata
 2         C_APP          C API (``DFTRACER_C_*``)
 3         LIBC_IO        POSIX and STDIO interception (cat: POSIX, STDIO)
 4         HIP            rocprofiler (cat: the rocprofiler kind name)
 5         HDF5           HDF5 interception
 6         PYTHON         Python API (cat: caller-supplied)
 7         PSUTIL         dftracer_service telemetry (cat: sys, net, io)
 8         FINSTRUMENT    ``-finstrument-functions`` (cat: FUNC)
 9         CPP_APP        C++ API (``DFTRACER_CPP_*``)
 10        MPI            MPI and MPI-IO interception (cat: MPIIO, or the
                          MPI function family: env, p2p, collective, ...)
 11        CUDA           CUPTI (cat: the CUDA activity kind)
========  =============  ===================================================

The numbering is append-only: values are never renumbered or reused, so a
reader can always interpret a trace written by an older DFTracer. A reader that
meets a value it does not know should treat it as UNKNOWN rather than fail.

Custom metadata events carry the type of the API that logged them, so metadata
written from Python is PYTHON and metadata written through the C API is C_APP.
The hash events (FH, SH, HH), the FS records, and the process and thread metadata are DFTRACER:
they are emitted by the tracer's own bookkeeping. A hash record in particular
is written once by whichever layer first saw the string, so attributing it to
that layer would not be meaningful.

----------

.. _`Record Phases`:

----------------------------------------
Record Phases
----------------------------------------

The "ph" column says what kind of record a line is, as a small integer. It
replaces the single letters DFTracer used to borrow from the Chrome tracing
format; the letter each value supersedes is given for reference.

========  =============  =========  ==========================================
 Value     Name           Was        Meaning
========  =============  =========  ==========================================
 0         UNKNOWN        --         Never written; fallback for unknown values
 1         COMPLETE       "X"        An individual event with a duration
 2         COUNTER        "C"        Time series counter, from dftracer_service
 3         AGGREGATED     "A"        An aggregated event
 4         METADATA       "M"        A metadata record
========  =============  =========  ==========================================

AGGREGATED is new. Aggregated records used to be written as counters, so a
consumer could not tell a genuine psutil counter sample apart from an
aggregate. COUNTER now means only the former.

Like "type", this numbering is append-only and never renumbered or reused.

----------

.. _`Common Fields`:

----------------------------------------
Common Fields
----------------------------------------

Every event, regardless of "ph", is a flat JSON object built from the same
small set of columns. Which of them appear depends on the phase:

========  =======================================================  ==========================
 Column    Meaning                                                   Present on
========  =======================================================  ==========================
 id        Index of the record within the process                   Complete only
 name      Event name (a function name, region name, or a fixed      All
           tag like "start"/"end" for internal events)
 cat       Free-form sub-category within the "type" layer             All
 type      Instrumentation layer that produced the event              All
           (see `Event Types`_)
 ph        Kind of record (see `Record Phases`_)                      All
 pid       Process id                                                 All
 tid       Thread id                                                  All
 ts        Start timestamp                                            All
 dur       Duration                                                   Complete only
 args      Free-form dictionary of extra fields, always present       All
           when there is at least one to report
========  =======================================================  ==========================

Counter and Aggregated events (see below) share one shape: they carry
"name"/"cat"/"type"/"ts"/"ph"/"pid"/"tid"/"args" but no "id" or "dur" of
their own, since neither represents a single timed record.

Within DFTracer, filenames, hostnames, and other long strings are stored as
hash values (e.g. "hhash" in "args"). The actual string is recorded once, in
a separate Metadata Event (see `Hash Events`_ below), rather than repeated in
every event that references it.

----------------------------------------
Complete Events
----------------------------------------

A COMPLETE event ("ph": 1) is a single timed record: one function call, one
region, one intercepted syscall. A sample complete event looks like the
following

.. code-block:: bash

    {"id":8,"name":"CUSTOM_BLOCK","cat":"CPP_APP","type":9,"pid":3308801,"tid":6617602,"ts":1727286231145121,"dur":1000054,"ph":1,"args":{"hhash":39537,"p_idx":7,"key":0,"level":3}}

Here, "id" refers to the index of the record in the process. Only for complete events.
"name" refers to the event name.
"cat" refers to category.
"type" refers to the instrumentation layer that produced the event (see `Event Types`_).
"ph" refers to the kind of record (see `Record Phases`_).
"pid" and "tid" are the process id and thread id, respectively.
"ts" and "dur" is the timestamp and duration of the event.
Finally, "args" is a dictionary of other events.

----------------------------------------
Counter Events
----------------------------------------

A COUNTER event ("ph": 2) is a time-series sample: one snapshot of a value
that changes over time, such as system CPU usage or memory pressure. These
are emitted by ``dftracer_service``'s telemetry collectors (cat "sys", "io",
"net"; type PSUTIL), one event per collector per sampling interval. Unlike a
Complete event, a Counter event has no duration: it describes an instant, not
a span.

.. code-block:: bash

    {"name":"cpu","cat":"sys","type":7,"ts":1727286231145121,"ph":2,"pid":3308801,"tid":0,"args":{"hhash":"8e0caa2d595b2a4d","user_pct":12.5,"nice_pct":0.0,"system_pct":3.2,"idle_pct":83.1,"iowait_pct":0.8,"irq_pct":0.0,"softirq_pct":0.4,"steal_pct":0.0,"guest_pct":0.0,"guest_nice_pct":0.0}}

"name" identifies the counter series (here, aggregate CPU utilization; the
per-core collector emits one such event per core, named "cpu0", "cpu1", ...).
"args" holds the sampled values for that instant — here, the percentage of
time spent in each CPU state since the previous sample.

----------------------------------------
Aggregated Events
----------------------------------------

An AGGREGATED event ("ph": 3) replaces many Complete events of the same
"name"/"cat"/"type" (and, for the C/C++ APIs, the same thread) that occurred
within one aggregation interval, folding them into a single record. This is
what ``DFTRACER_ENABLE_AGGREGATION=1`` produces instead of one line per call:
it trades per-call detail for a bounded trace size when a function is called
many times.

.. code-block:: bash

    {"name":"openat","cat":"POSIX","type":3,"ts":1785087679000000,"ph":3,"pid":4165574,"tid":4165574,"args":{"hhash":"8e0caa2d595b2a4d","dft_cnt":2,"dur_min":379,"dur_max":444,"dur_sum":1232,"flags_min":0,"flags_max":65,"flags_sum":130,"mode_min":777,"mode_max":777,"mode_sum":1554}}

    {"name":"creat64","cat":"POSIX","type":3,"ts":1785087679000000,"ph":3,"pid":4165574,"tid":4165574,"args":{"hhash":"8e0caa2d595b2a4d","dft_cnt":1,"dur":185,"mode":2}}

"args.dft_cnt" is the number of calls folded into this record. Every numeric
field that varied across those calls (duration, and any numeric metadata
the API call logged, e.g. "flags" or "mode") is reported as three fields —
"<field>_min", "<field>_max", "<field>_sum" — instead of one; a field that
only ever took one value across all folded calls (or when "dft_cnt" is 1, as
in the second example above) is instead reported plain, without a suffix.
Non-numeric metadata is not aggregated: only its presence is recorded via
"dft_cnt".

.. _`Metadata Events`:

----------------------------------------
Metadata Events
----------------------------------------

Each process writes its own trace file, and DFTracer adds the metadata records below to it.
Every record has ``"type":1`` (``DFTRACER``) and ``"cat":"dftracer"``; all but ``start``
and ``end`` are metadata records (``"ph":4``). Every record, metadata or event, carries
``args.hhash``: the hash of the host it ran on. The excerpts below are rank 0 of a 4-rank
IOR MPI-IO run with OpenMPI 5.0.3, in file order.

Once, when the trace file is opened:

.. code-block:: bash

    {"name":"HH","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ph":4,"args":{"hhash":"2cbe8604f36e21bf","name":"083d7212109e","value":"2cbe8604f36e21bf"}}
    {"name":"thread_name","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ph":4,"args":{"hhash":"2cbe8604f36e21bf","name":"13916","value":"thread_name"}}
    {"name":"CM","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ph":4,"args":{"hhash":"2cbe8604f36e21bf","name":"time_metric","value":"US"}}
    {"name":"FH","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ph":4,"args":{"hhash":"2cbe8604f36e21bf","name":"/tmp/ior-openmpi-5.0.3","value":"9f2b1a4271ca33dc"}}
    {"name":"SH","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ph":4,"args":{"hhash":"2cbe8604f36e21bf","name":"/tmp/ior-openmpi-5.0.3/inst/bin/ior;-a;MPIIO;-c;-w;-r;-b;4m;-t;1m;-s;2;-e;-o;/tmp/d/openmpi-5.0.3/traced.dat","value":"4629c7cd2590fc22"}}
    {"name":"SH","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ph":4,"args":{"hhash":"2cbe8604f36e21bf","name":"ior","value":"a01da183974134df"}}
    {"id":1,"name":"start","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ts":1793772882984178,"dur":0,"ph":1,"args":{"hhash":"2cbe8604f36e21bf","ppid":13913,"cmd_hash":"4629c7cd2590fc22","exec_hash":"a01da183974134df","version":20003,"date":"Wed Nov  4 06:14:42 2026","cwd":"9f2b1a4271ca33dc"}}

Once, after ``MPI_Init`` or ``MPI_Init_thread`` returns:

.. code-block:: bash

    {"name":"PR","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ph":4,"args":{"hhash":"2cbe8604f36e21bf","name":"rank","value":"0"}}
    {"name":"process_name","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ph":4,"args":{"hhash":"2cbe8604f36e21bf","name":"Rank 0","value":"process_name"}}

Once, at finalize:

.. code-block:: bash

    {"id":257,"name":"end","cat":"dftracer","type":1,"pid":13916,"tid":13916,"ts":1793772883174841,"dur":0,"ph":1,"args":{"hhash":"2cbe8604f36e21bf","app":{},"used":{"LIBC_IO":1,"MPI":1},"cfg":{"enable":1,"metadata":1,"core_affinity":0,"time_metric":"US","io":1,"posix":1,"stdio":1,"trace_all_files":0,"tids":1,"bind_signals":0,"write_buffer_size":16777216,"trace_interval_ms":10,"min_duration_ns":0,"libuv_thread_count":1,"variorum_power":1,"aggregation_enable":1,"pc_mode":0,"aggregation_type":"SELECTIVE","bind":1,"log_file":"/work/traces/openmpi-5.0.3/ior-680a7086970519bf-preload.pfw.gz"},"build":{"mpi":1,"hdf5":0,"hip":0,"cuda":0,"finstrument":0,"variorum":0},"num_events":256}}

.. _`Hash Events`:

.. list-table::
   :header-rows: 1
   :widths: 16 22 30 32

   * - Record
     - Written
     - ``args``
     - Use in analysis
   * - ``HH``
     - Once per process, first record in the file
     - ``name``: the hostname; ``value``: its hash
     - Resolve every record's ``hhash`` to a node: group ranks by node, count nodes
   * - ``thread_name``
     - Once per process
     - ``name``: the process id
     - Label for trace viewers
   * - ``CM`` ``time_metric``
     - Once per process
     - ``value``: unit of ``ts`` and ``dur`` (``US``, microseconds, by default)
     - Convert times
   * - ``FH``
     - The first time each path is hashed: the working directory, then each file touched
     - ``name``: the path; ``value``: its hash
     - Resolve ``fhash`` in POSIX, STDIO and MPI-IO events, and ``cwd`` in ``start``, to a path
   * - ``SH``
     - The first time each string is hashed: the command line and executable name, among others
     - ``name``: the string; ``value``: its hash
     - Resolve ``cmd_hash`` and ``exec_hash`` in ``start``
   * - ``start``
     - Once per process, when the trace is opened
     - With ``DFTRACER_INC_METADATA=1``: ``ppid``, ``cmd_hash``, ``exec_hash``, ``cwd``, ``version``, ``date``
     - Which program, command line and directory each process ran
   * - ``PR`` ``rank``
     - Once per process, at the first record logged after ``MPI_Init`` or ``MPI_Init_thread`` returns and before ``MPI_Finalize``
     - ``value``: the rank in ``MPI_COMM_WORLD``
     - Map ``pid`` (and so the whole file) to an MPI rank
   * - ``process_name``
     - With ``PR`` ``rank``
     - ``name``: ``Rank <n>``
     - Label for trace viewers
   * - ``PR`` ``core_affinity``
     - Once per process, with ``DFTRACER_SET_CORE_AFFINITY=1`` in a build with hwloc
     - ``value``: the cores the process may run on
     - Check rank placement and oversubscription
   * - ``CM`` (other names)
     - Each time the application logs a key/value metadata event (see `Custom Metadata`_)
     - ``name``: the key; ``value``: the value
     - Application values at a point in the run
   * - ``end``
     - Once per process, at finalize
     - ``num_events``, ``cfg``, ``build``, ``used``, ``app``
     - Effective configuration, which layers produced events, and the application's ``set_app_metadata_*`` values

.. _`Custom Metadata`:

**Custom metadata (CM).** ``CM`` records hold a key and a value as strings: ``args.name`` is
the key and ``args.value`` the value. DFTracer writes one itself, ``time_metric``, with
``"type":1``. Every other ``CM`` record comes from the application, which can log one at any
point in the run:

========  ============================================  ==================
 API       Call                                          ``type`` written
========  ============================================  ==================
 C         ``DFTRACER_C_METADATA(name, key, value)``     2 (``C_APP``)
 C++       ``DFTRACER_CPP_METADATA(name, key, value)``   9 (``CPP_APP``)
 Python    ``log_metadata_event(key, value)``            6 (``PYTHON``)
========  ============================================  ==================

``name`` in the C and C++ macros only names the local variable; it is not written. The record
keeps ``"cat":"dftracer"``; ``type`` tells which layer wrote it. A C program calling
``DFTRACER_C_METADATA(meta, "solver", "cg")`` writes:

.. code-block:: bash

    {"name":"CM","cat":"dftracer","type":2,"pid":1574378,"tid":1574378,"ph":4,"args":{"hhash":"2d851440028991ea","name":"solver","value":"cg"}}

A key logged twice gives two records; nothing folds them. With fields instead of a single
value (``DFTRACER_C_RECORD_START``, ``DFTRACER_CPP_RECORD``, or
``log_metadata_event(key, fields=...)`` in Python), the application writes a metadata record
named after the key rather than a ``CM`` record (see `Metadata Records`_). For values that
describe the whole run, use ``set_app_metadata_int`` / ``set_app_metadata_string`` instead:
they land once in the ``app`` object of the ``end`` record, last value per key. The same C
program calling ``DFTRACER_C_APP_METADATA_INT("nx", 64)`` and
``DFTRACER_C_APP_METADATA_STR("input_deck", "sedov.in")`` ends with
``"app":{"input_deck":"sedov.in","nx":64}``.

The rank is read with ``PMPI_Comm_rank(MPI_COMM_WORLD)``, so a process that never initializes
MPI has no ``PR`` ``rank`` record. Because a trace file belongs to one process, the rank applies
to the whole file, including the events before ``MPI_Init``.

Run-level values an application sets itself, in the spirit of Adiak (problem size, input deck,
solver options), go in the ``app`` object of the ``end`` record: call
``set_app_metadata_int`` or ``set_app_metadata_string``, or ``DFTRACER_C_APP_METADATA_INT`` /
``_STR`` and ``DFTRACER_CPP_APP_METADATA_INT`` / ``_STR``. The last value set for a key wins.

DFTracer does not record the size of ``MPI_COMM_WORLD``, the MPI library version, or the job
id of the launcher. The world size can be counted from the distinct ``PR`` ``rank`` values when
every rank was traced. The MPI implementation and version DFTracer was built against are
reported by ``dftracer_install_info``, not in the trace.

.. _`Metadata Records`:

A metadata region with fields (``DFTRACER_CPP_RECORD``,
``DFTRACER_C_RECORD_START``, or ``log_metadata_event(key, fields=...)`` in
Python) writes one record named after the region. Unlike hash and custom
metadata, its ``args`` hold the fields rather than ``name``/``value``:

.. code-block:: bash

    {"name":"dataset","cat":"CPP_APP","type":9,"pid":1,"tid":1,"ph":4,"args":{"hhash":"...","id":"8f0de68c04eea0bf","uri":"/p/out/a.h5"}}

.. _`File System Records`:

Unless turned off with ``DFTRACER_ENABLE_FS_METADATA=0`` (or
``features.fs_metadata: False``), each process writes ``FS`` records at
finalize describing where its traced files live. Each fact is stored once, at
the level where it can change, and ``"level"`` says which:

=========  ==========  ===========================================================
level      keyed by    holds
=========  ==========  ===========================================================
``mount``  ``mhash``   once per mount: ``fs`` (type from ``statfs(2)``; hex magic
                       when unknown), ``bsize``, ``capacity`` (``total_bytes``,
                       ``free_bytes``, ``avail_bytes``, ``total_inodes``,
                       ``free_inodes``), ``namelen``, ``read_only``, ``mount``
                       (``type``, ``source`` and ``options`` from
                       ``/proc/self/mountinfo``, e.g. ``vers=3,rsize=65536,...``
                       on NFS), ``rotational`` (local block devices only: 1 disk,
                       0 flash) and, on Lustre, ``lustre``: ``fsname`` and
                       ``liblustreapi`` (0 when the library is not installed, so
                       there are no layouts below)
``dir``    ``dhash``   once per directory: the ``mhash`` of its mount and, on
                       Lustre, ``lustre``: its ``mdt`` and the layout new files in
                       it get (``pool``, ``pattern``, ``stripe_count``,
                       ``stripe_size``, or ``components``)
``file``   ``fhash``   once per traced file: the ``dhash`` of its directory,
                       ``size`` at finalize, ``blksize`` (``st_blksize``, the
                       preferred I/O size: stripe size times count on Lustre)
                       and, on Lustre, ``lustre``: its own layout, with ``osts``
                       the (sorted, unique) OSTs it has objects on
=========  ==========  ===========================================================

``fhash``, ``dhash`` and ``mhash`` are all ``FH`` hashes, so each resolves to
its path like any file hash, and a file's I/O events join to its file, then
its directory, then its mount. A traced directory (such as the cwd) has only a
``dir`` record. For example, two files in one Lustre directory:

.. code-block:: bash

    {"name":"FS",...,"args":{"hhash":"...","level":"mount","mhash":"940a3a65897f3c06","fs":"lustre","bsize":4096,
      "capacity":{"total_bytes":47080707004039168,"free_bytes":37021728739688448,"avail_bytes":37021718101884928,"total_inodes":10789490110,"free_inodes":8826710276},
      "namelen":256,"read_only":0,"mount":{"type":"lustre","source":"<nids>:/lustre5","options":"rw,nosuid,rw,flock,lazystatfs,..."},
      "lustre":{"liblustreapi":1,"fsname":"lustre5"}}}
    {"name":"FS",...,"args":{"hhash":"...","level":"dir","dhash":"5ecd366a4122c081","mhash":"940a3a65897f3c06",
      "lustre":{"mdt":3,"pool":"","mirrors":1,"pattern":"raid0","stripe_count":4,"stripe_size":1048576,"osts":[]}}}
    {"name":"FS",...,"args":{"hhash":"...","level":"file","fhash":"398df582dcb774ae","dhash":"5ecd366a4122c081","size":8388608,"blksize":4194304,
      "lustre":{"pool":"","mirrors":1,"pattern":"raid0","stripe_count":4,"stripe_size":1048576,"osts":[15,16,17,18]}}}
    {"name":"FS",...,"args":{"hhash":"...","level":"file","fhash":"e5aeb28e9909bdba","dhash":"5ecd366a4122c081","size":8388608,"blksize":4194304,
      "lustre":{"pool":"","mirrors":1,"pattern":"raid0","stripe_count":4,"stripe_size":1048576,"osts":[19,20,21,22]}}}

A plain Lustre layout has ``pattern``, ``stripe_count`` and ``stripe_size``
next to ``osts``. A composite (PFL or FLR) one instead has only ``osts`` (the
union) and each component under ``components``, ``end`` being -1 for end of
file. ``pattern`` ``mdt`` is Data-on-MDT (the extent lives on the MDT, no
OSTs). A component the file never reached is not instantiated: it has no
``osts``, and Lustre may report -1 for its ``stripe_count`` (the default).

Everything is collected at finalize, after interception is switched off, so
tracing pays nothing per I/O operation and the lookups are not traced
themselves. Per traced file it costs one ``stat`` and, on Lustre, one layout
lookup; ``statfs`` (a server round trip on NFS), the mount table and the
Lustre directory lookups are done once per mount or directory. Only facts the
node can answer are recorded, so they depend on the node: ``liblustreapi`` is
loaded at run time, and only once a file is really on Lustre, so DFTracer
builds and runs without Lustre. Files removed before finalize have no record.

Events can carry a ``relations`` object in their ``args`` (built with
``append_id("relations", <name>, <id>)``), e.g.
``"relations":{"used":["8f0de68c04eea0bf"]}``. It is kept even without
``DFTRACER_INC_METADATA``. Provenance (entities and their lineage) is written
this way by `moirai <https://github.com/llnl-asr/moirai>`_.

----------------------------------------
Lifecycle Events: "start" and "end"
----------------------------------------

Every process writes exactly one "start" event (emitted when the log file is
opened) and one "end" event (emitted at finalize), both with "cat":"dftracer"
and "type":1 (DFTRACER). "start" carries process-identifying metadata: the
resolved executable name and command line (hashed), the working directory
(hashed), the DFTracer build version, and the parent pid. "end" is the
trace's single self-description record: it reports how many events this
process wrote ("num_events"), the effective configuration, which
instrumentation layers are available in this build vs. which ones actually
produced an event this run, and any application-supplied metadata — all
folded into that one event rather than emitted as separate metadata events
per setting, so a trace's fixed-cost overhead doesn't grow with how much
there is to report. A real "end" event looks like this (formatted for
readability; on disk it is a single line):

.. code-block:: bash

    {
      "id": 10, "name": "end", "cat": "dftracer", "type": 1,
      "pid": 226346, "tid": 226346, "ts": 1785104602739106, "dur": 0, "ph": 1,
      "args": {
        "hhash": "8e0caa2d595b2a4d",
        "num_events": 9,
        "cfg": {
          "enable": 1, "metadata": 1, "core_affinity": 0,
          "time_metric": "US", "io": 1, "posix": 1, "stdio": 1,
          "compression": 1, "trace_all_files": 0, "tids": 1,
          "bind_signals": 0, "write_buffer_size": 16777216,
          "trace_interval_ms": 1000, "min_duration_ns": 0,
          "libuv_thread_count": 1,
          "aggregation_enable": 0, "aggregation_type": "FULL",
          "bind": 1,
          "log_file": "/tmp/final_check/t-fbd87939335a24cd-app.pfw.gz"
        },
        "build": {"mpi": 1, "hdf5": 1, "hip": 0, "cuda": 0, "finstrument": 0},
        "used": {"C_APP": 1, "LIBC_IO": 1},
        "app": {"batch_size": 32, "model": "resnet50"}
      }
    }

Five "args" keys partition this record by where a value came from. "cfg",
"build", "used", and "app" are nested JSON objects rather than a flat set of
dotted keys, so any one group can be sliced out at once with a tool like
``jq`` (e.g. ``jq '.args.cfg'``):

========  ===========================================================
 Key       Meaning
========  ===========================================================
 cfg       ``ConfigurationManager`` settings effective for this run
           (env var / YAML config, resolved), e.g. ``cfg.compression``,
           plus two facts resolved by ``DFTracerCore`` at runtime
           rather than settable via env/YAML: ``cfg.bind`` (whether
           GOTCHA interception was actually bound this run — see
           ``initialize_main`` vs. ``initialize_no_bind``) and
           ``cfg.log_file`` (the fully-resolved trace path: the
           configured prefix plus the hostname/exec hash, suffix, and
           extension)
 build     Layers this build was compiled with support for,
           regardless of whether they were used this run, e.g.
           ``build.mpi``, ``build.hdf5``, ``build.hip``, ``build.cuda``,
           ``build.finstrument``
 used      Layers (see `Event Types`_) that produced at least one
           event this run, plus any named sub-layer/integration
           reported via the ``mark_used``/``DFTRACER_C_MARK_USED``/
           ``DFTRACER_CPP_MARK_USED`` API — e.g. ``used.MPI``,
           ``used.PYTHON``, ``used.torch_profiler``, ``used.dynamo``,
           ``used.ai``. A key's absence here means that layer or
           integration never fired, not that it is unavailable.
           ``used.C_APP``/``used.CPP_APP`` are set as soon as
           ``DFTRACER_C_INIT``/``DFTRACER_CPP_INIT`` runs, so they
           appear even if the app never wraps an explicit region.
 app       Application-supplied metadata set via
           ``set_app_metadata_int``/``set_app_metadata_string``
           (C, C++, or Python), e.g. ``app.batch_size``; empty when
           the application never called that API
========  ===========================================================

In the example above, the run used the C API over POSIX/STDIO
(``used.C_APP``, ``used.LIBC_IO``) even though the build also supports MPI
and HDF5 (``build.mpi``, ``build.hdf5``) — those layers were compiled in but
never exercised, so they don't appear under "used". The app also recorded
its own ``batch_size``/``model`` metadata via ``set_app_metadata_int``/
``set_app_metadata_string``.

Each Python-side integration (PyTorch profiler, ``torch.compile``/dynamo, the
AI decorator framework) reports itself once, at its own initialization point,
rather than on every event it logs — all three log through the single
PYTHON ``type``, so without an explicit marker they'd be indistinguishable
under ``used.PYTHON``. Calling ``mark_used`` a second time for a name already
recorded is a cheap no-op (a single membership check), so it is safe to call
from a hot path if an integration has no natural one-time hook.

.. _`chrome tracing document`: https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview#heading=h.yr4qxyxotyw
