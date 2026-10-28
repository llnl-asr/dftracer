=====================================================
Design: producer/consumer tracing through the service
=====================================================

.. note::

   Design under review. Nothing here is implemented yet. Interfaces are
   sketches: names, ownership and threading are the decisions being made;
   exact signatures will settle during implementation.

Goal
====

Today every traced process serializes, aggregates, compresses and writes its own
trace inside ``BufferManager`` under a mutex, on the application's threads. The
producer/consumer (PC) mode moves that work out of the application and into
``dftracer_service``, one per node:

* **Producers (traced processes and their threads).** Each event is encoded
  into a compact binary record and committed into **one shared-memory ring per
  node**, owned by the service. Every process and thread produces
  independently and **lock-free**: one ``fetch_add`` reserves space, one
  release store commits it. The application never waits on JSON, aggregation,
  rule matching, compression or I/O.
* **Consumer (dftracer_service).** The ring is divided into fixed-size
  **blocks**, and workers drain completed blocks in parallel. A
  libuv-scheduled **staged pipeline** (drain, process, compress, write) keeps
  every stage busy at once, so a few service cores keep up with the whole node.
  Concurrency grows with backlog and shrinks when idle.
* **Block recovery by expiration.** A block holding an allocation that was never
  committed is recovered once its deadline passes. The deadline is twice the
  measured bad-write time. A crashed or stalled producer therefore never stalls
  the ring.
* **Aggregation and rules live in the service.** The service's configuration
  (``DFTRACER_ENABLE_AGGREGATION``, ``DFTRACER_AGGREGATION_TYPE``,
  ``DFTRACER_AGGREGATION_FILE``, compression, buffer sizes) applies to every
  client. The application's own aggregation settings are ignored in PC mode,
  with one warning at init.
* **One trace file per node.** Every client's events, and the service's own
  node-level telemetry, go through the same pipeline into
  ``<log_file>_<hostname>.pfw.gz``.
* Applications come and go; the service outlives them.

This is an **opt-in mode**, enabled with ``DFTRACER_ENABLE_PC_MODE=1`` (YAML:
``pc_mode``). The current in-process path is not removed or rewritten. It stays
the default, and it stays the fallback when the service cannot be reached. With
the option off, a traced process runs exactly the code it runs today and
produces the same bytes.

Architecture
============

::

   traced processes (any threads)        shared memory (one ring)            dftracer_service
   ──────────────────────────────        ────────────────────────            ──────────────────────────────────────────
   DFTLogger                              ┌──────┬──────┬──────┬────┐        EventLoop (loop thread = scheduler)
     │                                    │blk 0 │blk 1 │blk 2 │ .. │          timers: poll, controller, flush, expiry
   BufferManagerFactory                   │DRAIN │OWNED │FREE ◄┼─── head        │
     ├─ LocalBufferManager  (default)     └──────┴──────┴──────┴────┘          ▼
     └─ AsyncBufferManager  (PC mode)        ▲ CAS head: claim a whole block   S0 poll ─► S1 drain ─► S2 process ─► S3 compress ─► S4 write
          │ EventCodec encode               │ private fill, one publish store  (loop)   (pool,       (pool,         (pool)         (uv_fs_write)
          └─────────────────────────────────┘                                          per block)   per process)
                                                                                         │ demux       │ decode/aggregate
          service's own collectors ── AsyncBufferManager ── same ring                    │ by pid      │ JSON → open chunk
                                                                                         ▼             ▼
                                                                               block-ordered      shared open chunks ─► gzip members ─► node file
                                                                               stream FIFOs

Every arrow between stages is an ownership hand-off made on the loop thread.
Data-plane work runs on pool threads, each holding exclusive ownership of the
objects it was handed. So there are no locks in the pipeline, only bounded
queues and credits.

Producer side
=============

``BufferManager`` factory (``core/buffer/``)
---------------------------------------------

The switch is a factory on the buffer manager, the same shape as
``TelemetryCollector`` and ``TelemetryCollectorFactory`` in the service:

* ``BufferManager`` becomes the abstract interface. Its method set is
  today's public API, so call sites keep their signatures.
* ``LocalBufferManager`` is today's ``BufferManager`` code, moved verbatim:
  same aggregation, serialization, compression, ``STDIOWriter`` output and
  ``is_ready()`` guard. This is a rename plus ``override``. No logic changes.
* ``AsyncBufferManager`` is the PC-mode producer. It only pushes events into
  the ring. Chunk building, aggregation, compression and writing all happen in
  the service.
* ``BufferManagerFactory`` picks one. ``DFTLogger`` (today
  ``Singleton<BufferManager>::get_instance()``) and the telemetry collectors ask
  the factory instead. It keeps the chosen instance process-wide, as the
  singleton did.

.. code-block:: cpp

   class BufferManager {                 // interface; today's public API
    public:
     virtual ~BufferManager() = default;
     virtual int initialize(const char* filename, HashType hostname_hash) = 0;
     virtual int finalize(int index, ProcessID pid) = 0;
     virtual bool is_ready() const = 0;
     virtual void set_app_name(const char* name) = 0;
     virtual void set_rank(const int& rank) = 0;
     virtual void log_data_event(int index, ConstEventNameType name,
                                 ConstEventNameType category, TraceEventType type,
                                 TimeResolution start, TimeResolution duration,
                                 Metadata* metadata, ProcessID pid, ThreadID tid) = 0;
     virtual void log_counter_event(/* as today */) = 0;
     virtual void log_metadata_event(/* as today */) = 0;
     virtual void log_record(/* as today */) = 0;
   };

   class LocalBufferManager final : public BufferManager { /* today's code */ };

   class BufferManagerFactory {
    public:
     // pc_mode off (default): the LocalBufferManager singleton, as today.
     // pc_mode on: ServiceProbe first (see "Service check at init").
     //   READY -> AsyncBufferManager.
     //   else  -> exit if DFTRACER_PC_REQUIRE_SERVICE=1, otherwise warn
     //            once per node and return LocalBufferManager.
     static std::shared_ptr<BufferManager> get_instance();
   };

The factory attaches to the segment when the logger is constructed, so the
choice is made before ``initialize()``. A process never switches paths
mid-run. The existing ownership contract is kept: the buffer manager takes
ownership of ``metadata`` and frees it.

Service check at init
---------------------

In PC mode, whether the service is deployed must be known, and reported, before
the application does any work. ``BufferManagerFactory`` runs ``ServiceProbe``
as the very first PC-mode step. That happens when ``DFTracerCore`` builds the
logger, before any interceptor can emit an event and before ``MPI_Init``.

.. code-block:: cpp

   enum class ServiceStatus {
     READY,           // segment found, ABI matches, ACCEPTING, heartbeat fresh, pid alive
     NOT_FOUND,       // no segment under DFTRACER_SERVICE_SHM
     NO_PERMISSION,   // segment exists but cannot be opened (another uid?)
     ABI_MISMATCH,    // magic or abi_version differs (service/library version skew)
     NOT_ACCEPTING,   // state DRAINING or CLOSED (service shutting down)
     STALE,           // heartbeat older than DFTRACER_PC_HEARTBEAT_STALE_MS, or service pid dead
     CLIENTS_FULL,    // no free client slot
   };

   struct ServiceProbeResult {
     ServiceStatus status;
     std::string shm_name;
     pid_t service_pid;
     uint32_t abi_version;
     uint64_t ring_bytes;
     uint64_t heartbeat_age_ms;
     std::string to_string() const;   // one line, used for every report
   };

   class ServiceProbe {
    public:
     // Read-only: opens the segment, reads the header, closes it again. Never
     // creates anything. Shared by the factory and `dftracer_service check`.
     static ServiceProbeResult probe(const std::string& shm_name);
   };

**What happens with each result:**

* ``READY``: attach, then register. The process logs at ``INFO``:
  ``dftracer: PC mode -> dftracer_service pid=<p> shm=<name> ring=<size>``.
* Anything else:

  * With ``DFTRACER_PC_REQUIRE_SERVICE=1``, print the reason to ``stderr``
    and exit with a non-zero status (``DFTRACER_PC_EXIT_CODE``, default 78).
    The job fails fast, before it computes anything that would go untraced.
  * Otherwise (the default), print a ``WARNING`` with the reason and the fix,
    then fall back to ``LocalBufferManager``, as today. For example:
    ``dftracer: WARNING PC mode requested but dftracer_service is not running
    (shm /dftracer-1234 NOT_FOUND). Start it with `dftracer_service start`
    before the application. Events go to <log_file>-<hash>-spill.pfw.gz.``

**One report per node, not per rank.** Thousands of ranks starting at once
must not flood ``stderr``. On ``READY``, the first process to CAS
``announced`` in the segment header from 0 to 1 prints at ``WARN`` level so
the message shows by default; the others log only at ``INFO``. On failure
there is no segment to coordinate through. The first process to create
``/dev/shm/dftracer-<uid>.pcwarn`` with ``O_CREAT|O_EXCL`` prints the warning.
The rest stay quiet, and a later successful service start removes the marker.
``DFTRACER_PC_REQUIRE_SERVICE=1`` failures always print, because each process
exits.

**Checking before launch.** The same probe is available as a command, so job
scripts can confirm the service is up before ``srun`` or ``flux run``:

::

   dftracer_service check   # prints ServiceProbeResult; exit 0 if READY, else 1
   dftracer_service start   # returns only once the segment is ACCEPTING

Both read ``DFTRACER_LOG_FILE`` like the application; the service's ``.pid``
and ``.log`` sit next to the node trace, ``<log_file>_<host>.pfw.gz``
(:doc:`dftracer_service`).

``AsyncBufferManager`` (``core/buffer/async_buffer_manager.h``)
-------------------------------------------------------------------

.. code-block:: cpp

   class AsyncBufferManager final : public BufferManager {
    public:
     int initialize(const char* filename, HashType hostname_hash) override;
       // register in the client table, emit HELLO; ready = true
     int finalize(int index, ProcessID pid) override;
       // ready = false, emit GOODBYE, mark client EXITED, detach
     void set_app_name(const char* name) override;   // SET_APP_NAME record
     void set_rank(const int& rank) override;        // SET_RANK record
     void log_data_event(...) override;
       // size -> ring.reserve -> encode -> ring.commit -> delete metadata
    private:
     std::unique_ptr<ShmAttachment> shm_;
     RingProducer ring_;
     uint32_t client_slot_;
     FullPolicy full_policy_;             // WAIT(timeout_us) then DROP
     std::atomic<bool> ready_{false};
   };

   class RingProducer {
    public:
     struct Reservation { uint8_t* payload; RecordHeader* hdr; uint32_t epoch; };
     // false when the ring is full (the caller applies FullPolicy).
     bool reserve(size_t payload_bytes, RecordKind kind, Reservation& out);
     void commit(const Reservation& r);
   };

* **Encoding happens on the calling thread, straight into the reserved ring
  bytes:** size the record, reserve, encode, commit, with no intermediate
  buffer. This is the only work left on the application thread. It is a short
  walk over the event's ``Metadata`` and a copy of the strings.
* **Full ring.** No wait by default (``DFTRACER_PC_FULL_WAIT_US=0``): a full
  ring means the service is behind, and waiting would only hold the
  application to its pace. The thread spills, in-process, and keeps spilling
  without encoding for the ring for 1 ms before it tries the ring again. The
  event spills to the process's own
  ``<log_file>-<hash>-spill.pfw.gz``: a ``LocalBufferManager`` created on the
  first spill, buffering and compressing as in-process tracing does. Only what
  did not fit is there; start and end events stay in the ring. Spills are
  counted in the segment header, and an event is dropped (and counted) only if
  the spill file cannot be written. At stop the service appends every finished
  spill file of its clients (it learns each one's name from its ``HELLO``) to
  the node trace as it is -- the events are aggregated and serialized already
  -- and deletes it; a producer holds an ``flock`` on its spill file while it
  writes, so a file still being written is left in place. Control
  records (``HELLO``, ``GOODBYE``, app name, rank) have no spill file; they wait
  up to 5 s for room.
* **fork.** The child's ``reinitialize()`` registers a new client slot and sends
  its own ``HELLO``.
* **Service gone** (the segment no longer ``ACCEPTING``, or the service pid
  gone; checked at most every 100 ms): every later event spills, rather than
  going into a ring no one will drain. Events the service had already taken
  but not yet written die with it.

Shared memory layout (``core/ipc/shm_layout.h``)
================================================

Created by the service with ``boost::interprocess::shared_memory_object``
(``create_only`` after removing a stale segment) plus ``mapped_region``. The
content is plain POD plus ``std::atomic<uint64_t>`` and ``std::atomic<uint32_t>``,
which are lock-free and address-free on x86-64 and aarch64, so the layout does
not depend on where it is mapped. Boost containers and allocators are not used
inside the segment: nothing in it needs a heap.

Segment name: ``DFTRACER_SERVICE_SHM``, default ``/dftracer-<uid>``. Both sides
read it.

.. code-block:: cpp

   struct SegmentHeader {
     uint64_t magic;                   // "DFTRSHM1"
     uint32_t abi_version;             // bumped on any layout or record change
     uint32_t block_count;             // power of two
     uint32_t block_size;              // bytes, power of two (default 1 MiB)
     uint32_t client_slots;
     std::atomic<uint32_t> state;      // INITIALIZING, ACCEPTING, DRAINING, CLOSED
     pid_t    service_pid;
     uint64_t service_start_time;      // /proc/<pid>/stat field 22
     std::atomic<uint64_t> heartbeat_ns;
     std::atomic<uint32_t> announced;  // first client on the node prints the READY banner
     alignas(64) std::atomic<uint64_t> head;  // next block sequence to be claimed
   };

   struct alignas(64) BlockControl {
     std::atomic<uint64_t> armed;      // epoch the block is free for; the service bumps it on release
     std::atomic<uint64_t> claim_ns;   // set by the claiming producer: start of the expiry deadline
     alignas(64) std::atomic<uint64_t> committed;  // block_size once the owner publishes it
     // Service side only (single writer: the loop thread)
     uint32_t state;                   // FREE, OWNED, COMPLETE, DRAINING, EXPIRED, QUARANTINE
     uint32_t epoch;                   // current lap of this block
     uint64_t quarantine_until_ns;
   };

   struct ClientSlot {                 // registry; HELLO/GOODBYE travel in the ring
     std::atomic<uint32_t> state;      // FREE, ACTIVE, EXITED
     pid_t    pid;
     uint64_t start_time;              // defeats pid reuse
   };
   // Segment = SegmentHeader | ClientSlot[client_slots] | BlockControl[block_count]
   //           | uint8_t data[block_count][block_size]

Record header (16 bytes, records 16-byte aligned):

.. code-block:: cpp

   struct RecordHeader {
     // [63:32] epoch of the block when reserved | [31:8] length | [7:0] state
     // state: PAD (reserved, or padding), COMMITTED
     std::atomic<uint64_t> word;
     pid_t    pid;                     // written before word: lets recovery check liveness
     uint16_t kind;                    // RecordKind
     uint16_t flags;
   };

Ring protocol
-------------

Implemented in ``src/dftracer/core/ipc/`` (``shm_layout.h``,
``ring_producer.h``, ``shm_region.h``) and
``src/dftracer/service/pipeline/block_tracker.h``. The enum values and constants
live with the rest of dftracer's: ``ShmSegmentState``, ``ShmRecordState`` and
``ShmRecordKind`` in ``include/dftracer/core/common/enumeration.h``, and
``DFT_SHM_*`` in ``include/dftracer/core/common/constants.h``.

**Epochs.** Block sequence number ``s`` has epoch ``(s >> log2(block_count)) +
1``: its lap around the ring, plus one. The offset matters. A fresh segment is
all zeros, and with 0-based laps an unwritten header in the first lap read as
"written in this lap". The stress test caught exactly that.

**Producers allocate blocks, not records.** The first version allocated each
record with a ``fetch_add`` on the open block's ``alloc`` word and counted it
with a second ``fetch_add`` on ``committed``. That is lock-free but not
scalable: every record moved two shared cache lines between cores, so the whole
node got about one record per line transfer. On an MI300A node that was 84 M
records/s for one producer of 256 B records and a flat 17-20 M records/s in
total for anything from 2 to 23 producers. The same ceiling holds for every
design built on one shared reservation index (eBPF ringbuf, DPDK ``rte_ring``,
LCRQ/SCQ). Designs that scale keep the per-record path off shared lines
(LTTng per-CPU sub-buffers, NanoLog per-thread buffers). Block ownership does
that while keeping one ring:

**Claim** (producer, lock-free, once per block):

1. ``s = head`` (acquire), giving block ``s % block_count``.
2. If the block's ``armed`` is not ``epoch(s)``: either the service has not
   released it yet, or ``s`` is stale. Re-read ``head``. If it has not moved,
   the ring is full and ``FullPolicy`` applies; otherwise retry with the new
   value.
3. CAS ``head`` from ``s`` to ``s + 1``. ``head`` only grows, so success means
   this producer alone owns block ``s`` for this lap. Store ``claim_ns``.

**Reserve and commit** (inside the owned block, no shared writes): place the
record at the producer's private offset, write ``pid``/``kind`` and
``word = epoch | n | PAD``. ``commit`` stores ``word = epoch | n | COMMITTED``.
Both are plain (relaxed) stores to memory no other producer touches. A
reservation that is never committed stays ``PAD``.

**Publish** (once per block): when the next record does not fit, on
``flush()``, or from the producer's flush timer (``flush_if_older``,
``DFTRACER_PC_FLUSH_MS``), write one ``PAD`` over the unused tail and store
``committed = block_size`` (release). The producer then holds no block until
its next record. A producer never holds a block while waiting on a full ring,
so held blocks always drain.

**Complete** (service): blocks before ``head`` are claimed; one is complete
when ``committed == block_size``, one acquire load that also makes every
record in it visible. An earlier version walked the record headers instead, a
chain of dependent loads that capped the ring at about 5 M records/s; that walk
is now only a verification mode used by the tests.

**Free**: the service resets ``claim_ns`` and ``committed``, then stores
``armed = epoch + 1`` (release), arming the block for its next lap.

**Parallel drain** (``ParallelBlockClaimer``, ``service/pipeline/block_tracker.h``):
any number of service threads claim complete blocks in sequence order with one
CAS on a consumer-side cursor (once per block), then decode and release them in
any order. Out-of-order release is safe because producers claim strictly in
order: a block released early waits, armed, until ``head`` reaches it. The
published ``tail`` advances over released blocks using a per-slot mark of the
latest release; a slot may already carry the mark of a later lap, which
implies the earlier one was released. Measured on one MI300A node with 80-128
byte events, decode-and-release scales from 54-267 M events/s on one thread to
660-1460 M events/s on sixteen.

**Drain thread count** (``ConsumerScaler``, ``service/pipeline/consumer_scaler.h``):
the service starts ``DFTRACER_PC_CONSUMER_THREADS`` drain threads but runs
only as many as the load needs; the rest sleep on a condition variable. Every
``DFTRACER_PC_CONSUMER_TICK_MS`` drain thread 0, between blocks, samples the
outstanding work (complete blocks not yet claimed) and the arrival rate
(``head`` per tick), and sets

    threads = ceil((arrival rate + ready backlog / 100 ms) / (0.8 x per-thread rate))

where the per-thread rate is measured from the drain threads' own busy time,
so it follows the event mix. It scales up within one tick and down by one
thread after ten ticks in a row that wanted fewer, within
[``DFTRACER_PC_CONSUMER_MIN_THREADS``, ``DFTRACER_PC_CONSUMER_THREADS``].
With 256 B POSIX events and a 5-30-70-120-30-0 M events/s ramp it ran 1, 3, 6,
10-12, then back to 1 thread, with the ready backlog under 10% of the ring and
no drops; four fixed threads stalled producers at 65 M events/s.

**Sizing.** Each producer holds at most one block, so the ring needs many more
blocks than producers. The default is 1 GiB in 1 MiB blocks: 1024 blocks, of
which 64 producers hold at most 6% and 256 producers at most 25%. A 1 MiB
block holds about 3800 records of 256 B, so the shared ``head`` line is touched
once per ~3800 records.

**Cost.** A quiet producer's block holds back the release (not the draining) of
every later block until it is flushed, so the flush timer bounds both record
latency and how much of the ring a quiet producer can pin. A producer
descheduled while holding the oldest block does the same for as long as it is
off-CPU.

Taking over held blocks (implemented)
-------------------------------------

A producer holds its block until the block is full, so a quiet thread (rank 0
waiting in ``MPI_Wait``) or a dead process would otherwise keep it forever.
Because producers claim blocks strictly in order, that block would stop every
producer on the node once the ring laps it. What is implemented instead of
the expiry below:

* **Drain passes over held blocks.** ``ParallelBlockClaimer::claim`` takes
  the oldest *complete* block with one CAS on that block's ``committed`` word
  (``epoch << 32 | block_size``, plus a claimed bit); blocks still held are
  skipped, not waited for.
* **Owner word.** Each ``BlockControl`` has ``owner = epoch << 8 | state`` on
  its own cache line, plus ``owner_pid`` and ``owner_tid``. The producer holds
  the block ``IDLE`` between records and ``WRITING`` from reserve to commit
  (one uncontended CAS and one store per record), and ``PUBLISHED`` once it
  publishes.
* **Take-over.** Once a block is older than ``DFTRACER_PC_FLUSH_MS``, drain
  thread 0 CASes it ``IDLE -> TAKEN``, or ``WRITING -> TAKEN`` if
  ``kill(owner_pid, 0)`` says the producer is dead. It drains the committed
  records, stopping at the first header not of this lap and skipping a dead
  writer's uncommitted reservation by its length. A producer that finds its
  block ``TAKEN`` drops it and claims another. A record being written is never
  read, so there is no late-writer race and no quarantine.
* **Finalize** publishes every thread's block, waiting up to 100 ms for a
  thread that is inside a log call. The exec wrappers finalize before calling
  the real ``exec``, so an exec'ing process loses nothing, in either mode.

The expiry design below is kept for reference; it is superseded.

Block recovery by expiration
----------------------------

A block whose allocations are not all committed is held, not skipped, until its
**deadline** has passed:

* **Bad-write time.** The loop thread records, for every block, the hold time
  ``t_complete - claim_ns``. This is the time from the block being claimed
  until its owner published it. ``T_bad`` is the p99.9 of that lag over a sliding
  window, clamped to ``[DFTRACER_PC_BAD_WRITE_MIN_US,
  DFTRACER_PC_BAD_WRITE_MAX_US]``. It can also be fixed through config.
* **Deadline.** ``claim_ns + 2 × T_bad``, and never earlier than
  ``DFTRACER_PC_FLUSH_MS`` after the claim, since a live producer may
  legitimately hold a block that long. If the ``pid`` of the owner is already
  dead (per the client table and ``kill(pid, 0)`` plus its
  start time), the block expires immediately, without waiting.
* **Salvage.** An ``EXPIRED`` block is drained like any other, record by
  record. ``COMMITTED`` records are kept. A ``PAD`` record of this epoch (a
  reservation not yet committed) is skipped using its length and counted as
  lost. If a header does not carry this
  epoch at all, the producer died before writing it, so its length is unknown.
  The rest of that block is dropped and counted. Loss is bounded to one block
  and is recorded in the trace as a per-block loss record.
* **Quarantine.** An expired block is not returned to ``FREE`` straight away.
  It waits another ``2 × T_bad`` (``QUARANTINE``) so that a producer that was
  late but alive is very likely finished before the memory is reused.

**Late but alive producers.** No timeout can make this case impossible: a
thread stopped with SIGSTOP, inside a debugger or swapped out can resume at any
time. The design makes the case *detected and bounded*:

* Its commit store carries the old epoch. Under the new lap that header reads
  as not committed, so it is never taken as valid data.
* Its late publish (``committed = block_size``) must not count toward the next
  lap, or that lap's block could look complete while its new owner is still
  writing. To be settled when recovery is implemented: store the epoch with
  it (``committed = epoch << 32 | block_size``) and accept only the current
  epoch.
* Before each publish, and every few records, the producer re-checks the
  block's ``armed`` epoch. If the block has been taken from it, it abandons the
  block and counts the records.
  The remaining window is the payload copy itself, about 100 ns, on top of the
  quarantine.
* ``DFTRACER_PC_RECORD_CRC=1`` (off by default) adds a CRC32C to every record.
  It is computed with the hardware instruction on x86 and aarch64. The consumer
  then also drops any record whose payload a late writer overwrote.

**Why a single ring.** Every producer claims the next free block from the same
pool, so a busy producer simply takes more blocks; no lane can sit idle while
another overflows. The only shared write is the ``head`` CAS, once per block.

Records (``core/ipc/event_codec.h``)
====================================

One record per buffer-manager call, plus control records. Every record carries
its producer's ``pid`` in the header, which S1 uses to route it to that
process's stream.

==================  ============================================================
Kind                Payload
==================  ============================================================
``HELLO``           hostname hash, the log file path the client would have used
                    (recorded as metadata only), client slot
``SET_APP_NAME``    string
``SET_RANK``        int
``DATA``            index, type, start, duration, tid, event name, category,
                    metadata
``COUNTER``         index, type, start, tid, name, category, metadata
``METADATA``        name, value, record name, type, tid, is_string
``RECORD``          record name, fields, type, tid
``GOODBYE``         final index, dropped-event count
``PAD``             none (block tail filler)
==================  ============================================================

Encoding is deliberately simple for now. Strings are length-prefixed copies;
the producer's ``const char*`` cannot cross processes. ``Metadata`` is encoded
as TLV: ``{key, MetadataType, value-tag, value}``. The value tags cover exactly
the types in ``DFTRACER_FOR_EACH_NUMERIC_TYPE`` and
``DFTRACER_FOR_EACH_STRING_TYPE``, plus ``RawJson``. An unknown ``std::any``
type is encoded as a string marker and counted, never dropped silently. The
service never rebuilds a ``Metadata`` from it: DATA and COUNTER records are
written and aggregated straight from the decoded record
(`Metadata in the service`_). A faster producer-side layout is deferred
(`Deferred: faster record encoding`_).

A record larger than a block, such as a huge ``RawJson`` or metadata blob, is
rejected and counted. ``block_size`` defaults to 1 MiB, well above any normal
event.

.. code-block:: cpp

   class EventCodec {
    public:
     static size_t data_size(ConstEventNameType name, ConstEventNameType cat,
                             const Metadata* md);
     static void encode_data(uint8_t* dst, int index, ConstEventNameType name,
                             ConstEventNameType cat, TraceEventType type,
                             TimeResolution start, TimeResolution dur,
                             const Metadata* md, ThreadID tid);
     // ... one size/encode pair per RecordKind
   };

   class EventDecoder {          // service side
    public:
     // Replays every record in [begin, end) onto stream. Returns records decoded.
     size_t replay(const uint8_t* begin, const uint8_t* end, Stream& stream);
   };

Consumer side: the staged pipeline
==================================

**As implemented: run to completion on the drain threads.** Instead of the
libuv stages below, each drain thread (``RingSink``) handles a block from
start to finish:

1. claim it, or take it over;
2. replay its records into the ``LocalBufferManager`` of the producer thread
   that wrote them (``ClientTable``: one lane per ``(pid, tid)``);
3. write into the drain thread's chunk, which goes out as one gzip member when
   full or older than ``DFTRACER_PC_FLUSH_MS``;
4. release the block.

The lane runs in attached mode, with its own aggregator and serializer and no
file of its own. Aggregation keys include the thread id, so lanes aggregate
exactly as the process would. Each member is written at an offset reserved
with one ``fetch_add`` (``NodeTraceFile``); members in any order form a valid
``.pfw.gz``. ``ConsumerScaler`` sizes the drain threads by busy time, which now
includes JSON, aggregation and gzip, so one controller covers the whole
pipeline. Processes are finished, with their open aggregates written, 2 x
``DFTRACER_PC_FLUSH_MS`` after their ``GOODBYE``, or after they die without
one.

The service writes its own telemetry into its ring like any client, so the
node file holds both. The internal metadata setting travels per event
(``EventHead::flags``). Traces are always gzip-compressed and always carry
metadata; neither is a user setting any more. The node file is always a
sequence of independent gzip members of whole lines, which readers can
decompress in parallel (``test/pc/check_gzip_members.py`` checks this). Aggregation settings are the service's, so start it
with the application's.

Measured on a login node, 16 processes x 2 threads x 200k events with one
metadata key:

* aggregation off: about 15 us of service CPU per event (JSON plus gzip
  level 1);
* aggregation on: about 0.6 us per event.

All 6.4 M events arrived, none malformed.

Design goal
-----------

As few service cores as possible should keep up with the whole node. That
takes two kinds of overlap:

* **Depth (pipelining).** Each stage hands its output to the next and
  immediately takes new input. While block *n* is being written, *n+1* is
  being compressed, *n+2* processed and *n+3* drained.
* **Width (parallelism).** Every stage that does not need ordering runs as many
  tasks at once as it has credits. Only per-process processing is serialized,
  because aggregation state is per process.

Stages
------

=====  ===============  ================  ===============================================  =======================
Stage  Name             Runs on           Work                                             Concurrency rule
=====  ===============  ================  ===============================================  =======================
S0     poll             loop thread       check claimed blocks for completion or           n/a (cheap)
                                          expiry; dispatch S1
S1     drain            pool              copy one ``COMPLETE``/``EXPIRED`` block into a   any number of blocks
                                          ``RawBatch``, then demux its records by pid      in parallel
                                          into per-process slices
S2     process          pool              decode, aggregate and apply rules, write JSON    1 per process (order);
                                          lines into an *open chunk*                       N processes in parallel
S3     compress         pool              gzip a sealed chunk into its own member,         capped by credits only
                                          using a ``thread_local`` z_stream
S4     write            libuv fs (pool)   ``uv_fs_write`` at an offset reserved on the     several in flight
                                          loop thread
=====  ===============  ================  ===============================================  =======================

**Ordering.** Blocks are drained out of order and in parallel. The loop thread
releases each block's per-process slices into the stream FIFOs in
**block-sequence order**, holding back any that complete early. Each process
therefore sees its own events in the order they were reserved, while S1 stays
fully parallel. A block waiting on its deadline holds back only later slices,
for at most ``2 × T_bad``.

**Streams.** A stream is one client process, keyed by pid and client slot. It
has its own ``LocalBufferManager`` (aggregation intervals, ``app_name``,
``rank``) and a bounded FIFO of slices (``DFTRACER_PC_STREAM_QUEUE``). A full
FIFO stops S1 dispatch, and the ring then fills, so backpressure reaches the
producers.

**Single node file, shared open chunks.** S2 does not write into per-stream
buffers. On dispatch, the loop thread hands each S2 task an *open chunk* from a
small pool shared by all streams, preferring partly filled ones. When the task
returns, the loop thread either seals the chunk (at ``write_buffer_size``, or
when it is older than ``DFTRACER_PC_FLUSH_MS``) and passes it to S3, or returns
it to the open pool.

**Why any order is valid.** ``GzipCompression::compress`` (libdeflate, which is
one-shot) turns each call's buffer into one complete gzip member. So every
sealed chunk is a self-contained gzip member, and members concatenated in any
order form a valid
``.pfw.gz``. S3 and S4 can therefore run fully in parallel, with S4 writing
each member at its own reserved offset.

Scheduling and elasticity
-------------------------

libuv's threadpool size is fixed once, from ``UV_THREADPOOL_SIZE``, the first
time the pool is used. It cannot grow or shrink afterwards, but idle pool
threads block on a condition variable and use no CPU. The elastic resource is
therefore **the number of tasks in flight**, and the scheduler is the loop
thread:

* **One budget, many stages.** ``target`` is the number of pool tasks allowed
  in flight in total, between ``DFTRACER_PC_MIN_WORKERS`` and
  ``DFTRACER_PC_MAX_WORKERS``. The pool is sized to ``max + fs_slots``, so
  ``uv_fs_write``, which shares the pool, can never be starved.
* **Downstream first.** When a slot frees, the loop thread dispatches the ready
  work closest to the disk first: S4, then S3, then S2, then S1. Data already
  in memory leaves before more is pulled in. This bounds memory and latency
  without a separate controller for each stage.
* **The controller** (a timer, adaptive 1–50 ms) samples ring backlog
  (``head`` minus the oldest non-free block) and the depth of every stage
  queue, then lets the ``ScalingPolicy`` set ``target``. Scale-up is fast and
  scale-down slow (hysteresis), in the spirit of the hpc-aio controller. When
  idle, polling backs off to 50 ms and ``target`` falls to the minimum.
* **Memory is bounded** by fixed pools of ``RawBatch``, open-chunk and
  sealed-chunk buffers. When a pool is empty its upstream stage is not
  dispatched, so backpressure travels to the ring.

Classes (``service/pipeline/``)
-------------------------------

.. code-block:: cpp

   class ShmSegment {                       // the service owns the segment
    public:
     static std::unique_ptr<ShmSegment> create(const std::string& name, uint32_t blocks,
                                               uint32_t block_size, uint32_t client_slots);
     ~ShmSegment();                         // shared_memory_object::remove
     SegmentHeader& header();
     BlockControl& block(uint64_t seq);
     uint8_t* data(uint64_t seq);
     ClientSlot& client(uint32_t i);
     void set_state(SegmentState s);
     void beat();                           // heartbeat_ns = now
   };

   class BlockTracker {                     // loop thread; the service half of the ring protocol
    public:
     void poll();                           // OWNED -> COMPLETE | EXPIRED
     bool next_ready(uint64_t& seq);        // COMPLETE/EXPIRED blocks not yet dispatched
     void on_drained(uint64_t seq);         // -> FREE, or QUARANTINE if it expired
     void release_quarantined();
     uint64_t backlog_blocks() const;
    private:
     BadWriteEstimator bad_write_;          // sliding p99.9 of commit lag -> T_bad
   };

   class BlockDrainer {                     // S1 body, pool thread
    public:
     // Copies the block, walks its records, and builds per-pid slices.
     // Salvage mode applies the epoch / uncommitted / CRC rules above.
     void drain(const ShmSegment& seg, uint64_t seq, bool salvage,
                RawBatch& out, SliceIndex& slices, LossStats& loss);
   };

   template <typename T>
   class FixedPool {                        // RawBatch, Chunk: preallocated, loop-thread owned
    public:
     T* acquire();                          // nullptr when exhausted -> stage not dispatched
     void release(T*);
   };

   class Stream {                           // one per client process
    public:
     void on_hello(const HelloRecord&);     // builds LocalBufferManager(components)
     LocalBufferManager& buffer();
     std::deque<Slice>& pending();          // bounded FIFO between S1 and S2, block-ordered
     bool busy;                             // an S2 task is in flight for this stream
     void on_goodbye(const GoodbyeRecord&); // flush aggregates, emit per-stream summary
     void on_vanished();                    // pid dead without GOODBYE: same, plus a truncated marker
   };

   class StreamTable;                       // keyed by (pid, client slot); loop thread only

   class Pipeline {                         // the scheduler; every method runs on the loop thread
    public:
     Pipeline(EventLoop& loop, ShmSegment& seg, BlockTracker& blocks,
              StreamTable& streams, NodeTraceFile& out,
              std::unique_ptr<ScalingPolicy> policy);
     void on_poll();                        // S0
     void on_controller_tick();             // sample, then policy -> target
     void on_flush_tick();                  // seal open chunks older than flush_ms
     void drain_all();                      // shutdown: run until the ring and all queues are empty
    private:
     void dispatch();                       // fill free slots, downstream first
     void on_done(Task&);                   // after_work: hand-off, reorder by block seq, dispatch()
     uint32_t in_flight_ = 0, target_ = 0;
   };

   class ScalingPolicy {                    // pluggable
    public:
     virtual ~ScalingPolicy() = default;
     struct Sample { uint64_t backlog_blocks; uint32_t q_drain, q_process,
                     q_compress, q_write; uint32_t in_flight; };
     virtual uint32_t target(const Sample& s) = 0;
   };
   class HysteresisPolicy final : public ScalingPolicy { /* high/low water, up fast, down slow */ };

   class NodeTraceFile {                    // loop thread only
    public:
     int open(const std::string& path);     // <log_file>_<hostname>.pfw.gz
     void write(Chunk* c, std::function<void()> done);
       // offset = next_offset; next_offset += c->len; uv_fs_write(..., offset)
     void close();                          // after all writes complete; fsync
   };

   class ClientReaper {                     // loop timer, ~1 s
     // An ACTIVE client slot whose pid is dead (or whose start time
     // mismatches): Stream::on_vanished once its last slice is processed,
     // then the slot -> FREE. An EXITED slot -> FREE once its GOODBYE is
     // processed.
   };

Aggregation in the service: reusing ``LocalBufferManager`` (additive only)
--------------------------------------------------------------------------

S2 runs one ``LocalBufferManager`` per producer thread of a client process
(a *lane*), so aggregation gives the rows the process would have produced
itself. METADATA and RECORD records go through its ordinary ``log_*`` calls.
DATA and COUNTER records go through ``log_packed_event`` and the
``PackedAggregator``, which work on the decoded record instead of a
``Metadata`` (`Metadata in the service`_). Today's components are process-wide
singletons. Three additions make that possible while leaving the default path
untouched:

1. **A second constructor.** ``LocalBufferManager(BufferManagerComponents)``
   takes its own serializer and aggregator, plus a reference to the shared
   rules. The default constructor and ``initialize()`` keep using the
   singletons exactly as today.
2. **Rules parsed once, shared read-only.** The service loads its aggregation
   config and rule file at startup into one
   ``std::shared_ptr<const AggregationRules>``. Each stream's ``Aggregator``
   holds only its own interval and bucket state. ``app_name`` and ``rank``
   come from ``SET_APP_NAME`` and ``SET_RANK``, so rules see the same keys they
   would in-process.
3. **An attached chunk instead of an owned buffer.** In the service a
   ``LocalBufferManager`` does not own its buffer. S2 calls
   ``attach_chunk(Chunk*)`` before replaying a slice and ``detach_chunk()``
   after it. When the chunk fills mid-slice, ``compress_and_write_if_needed``
   hands it back through the ``ChunkSink`` and receives a fresh one. That
   branch runs only when a sink is attached. Otherwise the existing
   compress-in-place and ``STDIOWriter`` code runs, unmodified.

.. code-block:: cpp

   struct BufferManagerComponents {
     std::shared_ptr<const ConfigurationManager> config;   // the service's
     std::shared_ptr<const AggregationRules> rules;        // shared, parsed once
     std::unique_ptr<Aggregator> aggregator;               // per stream
     std::unique_ptr<JsonLines> serializer;                // per stream
   };

   class ChunkSink {                        // implemented by the S2 task
    public:
     virtual ~ChunkSink() = default;
     virtual Chunk* exchange(Chunk* full) = 0;   // seal full, return a fresh one
   };

Metadata in the service
-----------------------

``Metadata`` (an ``unordered_map`` of ``std::string`` to ``std::any``) is the
API that interceptors and users write to, and the in-process path keeps it.
The service does not need it. It already holds every event as a decoded
record: a fixed head, plus ``DecodedValue`` entries that are typed (tag),
classified (``MetadataType``) and whose strings are views into the ring.
Rebuilding a ``Metadata`` from that cost a heap map, a ``std::string`` per
key, a ``std::any`` per value (a heap copy for strings) and a hash per key,
for every event. The serializer and aggregator then recovered each value's
type by trying ``any_cast`` type by type. Measured on a saturated 64-process
node (10 us events, 256 B records), the service spent about 20 us of CPU per
event.

The service path uses its own structures instead:

* **Plain events** (``core/serialization/packed_json.h``). ``data`` and
  ``counter`` write the line that ``JsonLines::data`` and
  ``JsonLines::counter`` write for the same event, straight from the decoded
  record into the chunk. There is no ``Metadata``, no ``stringstream`` and no
  allocation. Numbers print as ``JsonLines`` prints the decoder's types:
  64-bit integers, and ``double`` as ``ostream`` prints it (``%g``). Strings
  are written unescaped, as ``JsonLines`` writes them. Field order is the
  record's; JSON does not fix it, and ``JsonLines`` follows its
  ``unordered_map``. Without ``DFTRACER_INC_METADATA`` only the ``relations``
  arg survives.
* **Aggregation** (``core/aggregator/packed_aggregator.h``). An event's
  metadata is two sets:

  - *Keys*: its ``MT_KEY`` fields. They are part of the bucket, together with
    category, name, type, thread and interval. Events that differ in a key
    are aggregated apart, and each row carries its bucket's keys.
  - *Values*: its ``MT_VALUE`` fields. They are folded into the bucket: count,
    sum, min and max of a number, and the count of a string (which only adds
    to the row's ``dft_cnt``, as in ``Aggregator``). Other fields are not
    aggregated.

  ``PackedAggregator`` holds, per lane:

  - a ``StringTable`` of categories, event names and key names (bounded by
    the code);
  - per interval, a vector of buckets. A bucket's key fields sit in one flat
    vector, as ``{key id, tag, 8-byte value}``, where a string's value is an
    id in the interval's own ``StringTable``, freed with the interval.
    Lookup is by a 64-bit signature over (category, name, type, thread, the
    key fields in key-id order), followed by an exact comparison;
  - per bucket, plain accumulators ``{key id, tag, count, sum, min, max}``
    for ``dur`` and the values.

  Folding an event is therefore a few table lookups and integer updates, with
  no allocation once its strings are known. Intervals (``trace_interval_ms``
  in the configured time unit), the closing of earlier intervals when a later
  one opens, and ``SELECTIVE`` rules are ``Aggregator``'s. Rules read
  ``tags.<key>`` through a ``TagSource`` on the decoded record, with the same
  strings ``Metadata::getTagValue`` gives. Each row is written as
  ``JsonLines::aggregated`` writes it: the keys, ``dft_cnt``, and per value
  either ``key`` (one event) or ``key_sum``, ``key_min`` and ``key_max``. Rows
  go into the chunk one at a time, so a long drain moves on to a fresh chunk.

``Aggregator`` follows the same key semantics. It looks up a bucket with the
event's key, and stores a key that owns a copy of the event's ``MT_KEY``
fields (nullptr when there are none), which ``release_aggregated_data``
frees. ``AggregatedKey`` equality and hash look at the ``MT_KEY`` fields
alone, so a live key (all of an event's metadata) matches a stored one. Note
that ``DFT_LOGGER_UPDATE`` without a type stores ``MT_KEY``, so such fields
(``fhash``, the fd fields, ``path``, ``pid``, and ``ret`` in some calls) split
buckets.

The service's own events
------------------------

Collectors keep their ``capture(BufferManager&, ...)`` signature. The service
creates an ``AsyncBufferManager`` attached to its own segment, as a client
like any other, and passes it to the collectors. Node-level telemetry is then
reserved, committed, drained, processed, compressed and written by exactly the
same ring and pipeline, into the same node file, as a stream with the service's
pid. Collector captures keep running as pool tasks and count against
``target``.

``DFTracerService`` lifecycle
-----------------------------

It keeps its current shape: daemonize, PID file, signals, the shared
``EventLoop``. Startup creates the segment, then the self
``AsyncBufferManager``, then the pipeline and its timers.

Shutdown on SIGINT or SIGTERM:

1. Set the segment to ``DRAINING``, so no new clients register.
2. Stop the collectors and send ``GOODBYE`` on the self stream.
3. Flush the service's own producers' blocks, then ``drain_all``, bounded by
   ``DFTRACER_PC_DRAIN_TIMEOUT_MS``. Blocks still incomplete when the bound is
   reached are salvaged. Every stream is finalized; one without a ``GOODBYE``
   is marked truncated.
4. Seal all open chunks, wait for S3 and S4, then ``fsync`` and close the node
   file. Set ``CLOSED`` and remove the segment.

Failure handling
================

=========================================  =========================================
Failure                                    Handling
=========================================  =========================================
Producer dies holding a block, mid-header  Its pid is dead, so the block expires
                                           immediately. The rest of that block is
                                           lost and counted. The ring keeps moving.
Producer dies mid-payload (still ``PAD``)  Expires immediately. That record is
                                           skipped by its length; the records
                                           before it are kept.
Producer stalls but is alive               The block expires after ``2 × T_bad``
                                           and is salvaged, then quarantined for
                                           another ``2 × T_bad``. The late commit
                                           is ignored (old epoch). A late payload
                                           write is caught by the pre-write epoch
                                           check, or by the CRC when enabled.
pid reused by an unrelated process         Start-time mismatch in the client table:
                                           treated as dead.
Service killed                             Clients see ``CLOSED`` or a stale
                                           heartbeat and drop with a count. Members
                                           already written are a valid gzip prefix.
                                           A new service removes the stale segment
                                           at start.
=========================================  =========================================

Configuration
=============

==========================================  ======================  ===========================================
Variable                                    Default                 Meaning
==========================================  ======================  ===========================================
``DFTRACER_ENABLE_PC_MODE``                 0                       1 = push raw events to the service
``DFTRACER_SERVICE_SHM``                    ``/dftracer-<uid>``     segment name (both sides)
``DFTRACER_PC_REQUIRE_SERVICE``             0                       1 = exit at init if the service is not READY
``DFTRACER_PC_EXIT_CODE``                   78                      exit status when the requirement fails
``DFTRACER_PC_HEARTBEAT_STALE_MS``          5000                    heartbeat age that counts as STALE
``DFTRACER_PC_BLOCK_SIZE``                  1 MiB                   block size (power of two)
``DFTRACER_PC_BLOCKS``                      1024                    blocks in the ring (1 GiB total)
``DFTRACER_PC_CLIENT_SLOTS``                1024                    max concurrently registered processes
``DFTRACER_PC_FULL_WAIT_US``                0                       producer wait on a full ring before spilling (0: spill at once)
``DFTRACER_PC_ROLLOVER_MB``                 1024                    compressed MiB per node trace file before rolling over to ``-<n>``
``DFTRACER_PC_CONSUMER_THREADS``            16                      most service threads draining the ring
``DFTRACER_PC_CONSUMER_MIN_THREADS``        1                       fewest service threads draining the ring
``DFTRACER_PC_CONSUMER_TICK_MS``            10                      how often the drain thread count is resized
``DFTRACER_PC_BAD_WRITE_MIN_US``            200                     floor for ``T_bad``
``DFTRACER_PC_BAD_WRITE_MAX_US``            100000                  ceiling for ``T_bad``
``DFTRACER_PC_BAD_WRITE_US``                (measured)              fix ``T_bad`` instead of measuring it
``DFTRACER_PC_RECORD_CRC``                  0                       1 = CRC32C on every record
``DFTRACER_PC_MIN_WORKERS``                 1                       floor on pool tasks in flight
``DFTRACER_PC_MAX_WORKERS``                 cores/4                 ceiling on pool tasks in flight
``DFTRACER_PC_STREAM_QUEUE``                8                       slices buffered per stream
``DFTRACER_PC_FLUSH_MS``                    1000                    take over a held block; write a partial chunk
``DFTRACER_PC_COMPRESSION_LEVEL``           1                       gzip level of the node trace (speed over ratio)
``DFTRACER_PC_DRAIN_TIMEOUT_MS``            10000                   shutdown drain bound
==========================================  ======================  ===========================================

``/dev/shm`` is tmpfs and counts against node memory. The default ring reserves
1 GiB of address space, but only pages that are touched get committed. Keep
``DFTRACER_PC_BLOCKS`` at least 4 × the most producers expected on a node,
since each holds one block while filling it.

Build: Boost dependency
=======================

Boost.Interprocess is header-only, and only the service and
``AsyncBufferManager`` need it. Resolution follows the yaml-cpp pattern in
``dependency/CMakeLists.txt``:

1. ``find_package(Boost 1.66 QUIET)``, headers only. Tuolumne's system Boost
   1.66 satisfies this.
2. If it is not found, ``dftracer_install_external_project`` fetches a pinned
   Boost release **tarball** and installs only the ``boost/`` headers into
   ``${CMAKE_INSTALL_PREFIX}/include``. There is no ``b2`` build and no
   recursive-submodule git clone.
3. Add the pin to ``dependency/manifest.txt`` so the wheel build and
   ``scripts/wheel/manifest.py`` stay in sync.

The new code is gated by ``DFTRACER_ENABLE_PC_SERVICE`` (on when the service is
built).

Testing
=======

**Same events in both modes (implemented).** Every CTest test that writes a
trace gets a ``pc_<test>`` twin, labelled ``pc``. The twin
(``test/pc/run_pc_twin.sh``):

1. runs the test's command in-process;
2. runs it again through a private ``dftracer_service`` with the same
   configuration;
3. checks with ``test/pc/compare_traces.py`` that both traced the same
   events.

The check compares name, category, type, phase and args as multisets, the
same set of event ids, and the same total aggregated count. Pids, tids,
timestamps and durations are ignored.

Two kinds of process are reported but not compared, because the in-process
run has no trace of them:

* processes that never finalized;
* processes with fewer than 5 events, whose trace file ``STDIOWriter``
  deletes.

``test/pc/run_pc_golden.sh`` does the same for a multi-threaded load with
aggregation off and on.

Correctness (``test/unit/ipc/``, in regular CI)
------------------------------------------------

* **Ring.** P producer threads and P producer *processes* (fork). Every record
  arrives exactly once with an intact payload: each record carries a sequence
  number and a checksum. Covers block overflow and ``PAD``, head advance under
  contention, wrap across laps, ring full with ``WAIT`` and ``DROP``, and
  flush of a quiet producer's block.
* **Block recovery.** Inject each failure at a controlled point: die right
  after claiming a block, die mid-payload, and stall past the deadline then resume.
  Check that expiry fires at ``2 × T_bad`` (or immediately for a dead pid),
  that salvage keeps every committed record, that loss is counted exactly, that
  a late commit is ignored, and that, with CRC on, a late payload write is
  caught.
* **Bad-write estimator.** Synthetic commit-lag distributions give the expected
  p99.9 and respect the clamps.
* **Codec round trip.** Every ``MetadataType`` and every ``std::any`` value
  tag, ``RawJson``, empty and maximum-length strings, and an oversize record
  rejected.
* **Packed path** (``test/unit/test_packed.cpp``). For the same events,
  ``packed_json`` writes the lines ``JsonLines`` writes (args compared as
  sets), with metadata included and not, for data and counter events. For
  the same events, ``PackedAggregator`` writes the rows that ``Aggregator``
  plus ``JsonLines::aggregated`` write: several keys, threads and intervals,
  number and string values, an ``MT_IGNORE`` field, and a single-event
  bucket.
* **Aggregation keys** (``test/unit/test_aggregator.cpp``). Events that differ
  in an ``MT_KEY`` field are aggregated apart, each bucket keeps its keys and
  not its values, and events without keys share one bucket.
* **Golden equivalence.** The same event script runs through
  ``LocalBufferManager`` (per-process file) and through ``AsyncBufferManager``
  → service (node file), with the same aggregation config and rules. The node
  file is filtered by pid, then the decompressed, line-sorted output must be
  identical, with aggregation on and off.
* **Default path unchanged.** The existing suite runs with the option unset and
  passes with no changes to expected outputs.
* **Service check.** Each ``ServiceStatus`` is produced on purpose: no
  segment, a segment from another uid, a wrong ABI, a ``DRAINING`` service, a
  stale heartbeat, a killed service pid, and a full client table. Check the
  fallback (warning plus a local trace), ``REQUIRE_SERVICE`` (exit code, no
  trace), one warning across 64 concurrently starting processes, the exit codes
  of ``dftracer_service check``, and that ``start`` returns only once the
  segment is ``ACCEPTING``.
* **Self telemetry.** Collector events appear in the node file under the
  service pid, interleaved with client events.

Ring bandwidth versus node memory (``test/perf/ring_bw``, ctest label ``perf``)
--------------------------------------------------------------------------------

The goal: production and consumption bandwidth of the ring close to the node's
memory bandwidth, as measured by STREAM on the same node.

**Baseline.** McCalpin's ``stream.c``, fetched at a pinned revision and
checksum. It is compiled ``-O3 -fopenmp`` with ``STREAM_ARRAY_SIZE`` set to
at least 4× the last-level cache, read from ``/sys/devices/system/cpu``. The
harness runs it with ``OMP_NUM_THREADS`` and ``OMP_PROC_BIND=close`` set to the
same core count and pinning the ring benchmark uses, and parses the **Copy**
rate (Triad is also recorded).

**Like-for-like accounting.** STREAM Copy counts bytes read plus bytes written.
Both halves of the ring are copy kernels, so they are counted the same way:

* *Production BW* = 2 × payload bytes committed per second. The producer copies
  a pre-built source record into the ring.
* *Consumption BW* = 2 × payload bytes drained per second. S1 copies blocks out
  into ``RawBatch`` es.

**Modes**, each swept over record size (64 B to 32 KiB), producers (1 to all
cores, as threads and as processes), block size, and ring size
(cache-resident, and DRAM-resident with the ring ≥ 4× LLC):

1. *Produce-only.* The consumer frees complete blocks without copying them.
   This isolates reserve and commit, and above all the ``fetch_add`` contention
   on the head block.
2. *Consume-only.* The ring is pre-filled, then drained by 1 to N parallel S1
   tasks.
3. *End-to-end.* Both run concurrently, in separate processes, through the real
   segment.
4. *NUMA placement.* Producers and consumers on the same domain, then across
   domains.

**Pass criteria** (configurable, reported as a percentage of STREAM Copy at the
same thread count):

* DRAM-resident, records ≥ 1 KiB: production and consumption each ≥ 80% of
  STREAM Copy.
* Small records (≤ 256 B): bounded by the head-block ``fetch_add``, not by
  bandwidth. Aggregate records/s across all producers is reported and checked
  against a floor. This is the number that decides open question 2.

Service efficiency (``test/perf/service_tput``, label ``perf``)
---------------------------------------------------------------

This checks that a few service cores keep up with the whole node:

* Synthetic producers on every remaining core emit a realistic POSIX-like
  event mix at increasing rates. For each rate, find the smallest
  ``MAX_WORKERS`` at which ring backlog stays bounded and nothing is dropped.
* Report events/s per service core, and CPU time per event for each stage
  (S1–S4). The per-stage numbers show the next bottleneck: serialization,
  compression, or the write path.
* Verify elasticity: a burst raises ``in_flight`` toward ``max``, and idle
  returns it to ``min`` within the down-tick window.

All benchmarks write CSV, one row per configuration, with the STREAM numbers in
the header. They are not part of default CI, because shared CI nodes give
noisy bandwidth. They run on allocated nodes, Tuolumne first.

Deferred: faster record encoding
================================

The service side no longer rebuilds a ``Metadata`` (`Metadata in the
service`_). What follows is the producer side, kept out of the first version
on purpose. Revisit it once the rest of the system is stable and the
service-efficiency numbers show that encoding on the application thread
matters:

* **Flat fixed-layout record:** a 64-byte header with fields written in place,
  plus inline 16-byte typed metadata slots. No encode pass, and records about
  half the size.
* **Per-process string arena in shared memory:** names, categories and
  metadata keys become 4-byte offsets. Literals are keyed by pointer, dynamic
  strings by content hash.
* **``EventBuilder``:** hot interceptors write straight into the reserved
  record, skipping the heap-allocated ``Metadata``.

Layout of new code
==================

::

   src/dftracer/core/buffer/      buffer_manager.h (interface), local_buffer_manager.{h,cpp} (today's code),
                                  async_buffer_manager.{h,cpp}, buffer_manager_factory.{h,cpp},
                                  chunk_sink.h
   src/dftracer/core/ipc/         shm_layout.h, ring_producer.h, shm_attachment.{h,cpp},
                                  service_probe.{h,cpp}, event_codec.{h,cpp}
   src/dftracer/service/pipeline/ shm_segment, block_tracker, bad_write_estimator, block_drainer,
                                  stream, stream_table, pipeline, tasks.h, fixed_pool.h,
                                  scaling_policy.h, node_trace_file, client_reaper
   test/unit/ipc/                 ring, recovery, estimator, codec, golden, self_telemetry
   test/perf/ring_bw/             ring_bw.cpp, stream fetch + run_ring_bw.sh
   test/perf/service_tput/        service_tput.cpp, run_service_tput.sh

Implementation order
====================

1. ``BufferManager`` interface, ``LocalBufferManager`` (today's code moved
   verbatim), ``BufferManagerFactory`` and the ``DFTRACER_ENABLE_PC_MODE``
   option, default off. The existing tests must pass untouched.
2. ``shm_layout``, ``RingProducer``, ``BlockTracker``, ``BlockDrainer`` (no
   recovery yet). Ring correctness tests, and the **ring bandwidth benchmark
   against STREAM**. Tune here, before anything is built on top.
3. Block recovery: ``BadWriteEstimator``, expiry, salvage, quarantine, the
   optional CRC. Recovery tests.
4. ``EventCodec``/``EventDecoder`` plus round-trip tests.
5. ``ShmSegment``, the client table, ``ServiceProbe`` (the init check, the
   per-node report, ``REQUIRE_SERVICE``, ``dftracer_service check``), and
   ``AsyncBufferManager`` (HELLO and GOODBYE).
6. Pipeline with a fixed ``target``: streams, block-ordered slices, S1–S4,
   shared open chunks, ``NodeTraceFile``. Golden equivalence test.
7. The service's own telemetry through the ring.
8. ``ScalingPolicy`` and controller; service-efficiency benchmark.
9. ``ClientReaper``, shutdown drain.
10. Docs (``dftracer_service.rst``), the config table, and the Boost
    dependency in the wheel manifest.

Open questions
==============

1. **Fallback when the service dies mid-run.** Drop and count, or switch to a
   local file with a ``.failover`` suffix?
2. **Out-of-order release.** A producer descheduled while holding the oldest
   block pins the release of every later block. If that shows up, the service
   could release complete blocks out of order through a free list instead of
   ``head`` order. Recommendation: measure on a loaded node first.
3. **Wake-up.** The design uses adaptive timer polling (1–50 ms). A shared
   futex doorbell (``FUTEX_WAKE`` on a word in the segment, waited on by one
   service thread that calls ``uv_async_send``) would cut idle latency.
   Recommendation: polling first, the doorbell only if measurements call for it.
4. **Node file name and rotation.** ``<log_file>_<hostname>.pfw.gz`` for the
   service's lifetime, or rotate per job or per size so that long-lived
   services don't produce one ever-growing file?
