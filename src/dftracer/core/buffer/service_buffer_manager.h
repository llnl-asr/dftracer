#ifndef DFTRACER_SERVICE_BUFFER_MANAGER_H
#define DFTRACER_SERVICE_BUFFER_MANAGER_H
#include <dftracer/core/buffer/buffer_manager.h>
#include <dftracer/core/buffer/local_buffer_manager.h>
#include <dftracer/core/ipc/event_codec.h>
#include <dftracer/core/ipc/ring_producer.h>
#include <dftracer/core/ipc/shm_region.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dftracer {

/**
 * The producer/consumer BufferManager (DFTRACER_ENABLE_PC_MODE=1). Each event
 * is encoded in binary (core/ipc/event_codec.h) on the calling thread and
 * pushed into the node's dftracer_service ring. Serialization to JSON,
 * aggregation, compression and writing all happen in the service, so this
 * class holds no trace buffer, no file and no process-wide lock.
 *
 * Every thread that logs gets its own Slot: a RingProducer, which owns one
 * ring block at a time and fills it without shared writes, and the scratch
 * buffer events are encoded into. A slot is guarded by a per-slot state word
 * that only its own thread touches on the fast path, so finalize() (and a
 * future flush timer) can publish a quiet thread's block without racing it.
 *
 * An event that does not get into the ring -- no service is accepting, or
 * the ring is full (for longer than DFTRACER_PC_FULL_WAIT_US, by default 0:
 * no waiting) -- spills to
 * a file of this process's own, <log_file>-<hash>-spill.pfw.gz, next to the
 * node trace: a LocalBufferManager, created on the first spill, that buffers,
 * compresses and writes them as in-process tracing does. The start and end
 * events go to the ring like any other, so the spill file holds only what did
 * not fit. An event is dropped only if the spill file cannot be written.
 * At stop, the service appends every finished spill file of its node to the
 * node trace and deletes it: its events are aggregated and serialized
 * already, as in-process tracing does.
 *
 * A producer also checks, every DFT_SHM_LIVENESS_CHECK_NS, that the service
 * still drains the ring; once it has crashed or stopped, every event goes to
 * the spill file rather than into a ring no one will read.
 */
class ServiceBufferManager : public BufferManager {
 public:
  ServiceBufferManager();
  ~ServiceBufferManager() override;

  int initialize(const char* filename, HashType hostname_hash) override;
  int finalize(int index, ProcessID process_id) override;

  // Events are in the ring already; publish every thread's block so the
  // service has them now rather than after it takes the blocks over.
  void flush() override;

  void set_app_name(const char* name) override;

  /**
   * The spill file of the process whose trace would be `trace_file`:
   * <log_file>-<hash>-<suffix>.pfw.gz -> <log_file>-<hash>-spill.pfw.gz, with
   * the suffixes DFTracerCore names traces with; any other name (the
   * service's own, <log_file>_<host>.pfw.gz) just gets -spill. The service
   * finds its clients' spill files with it.
   */
  static std::string spill_file_for(const std::string& trace_file);
  void set_rank(const int& r) override;

  void log_data_event(int index, ConstEventNameType event_name,
                      ConstEventNameType category, TraceEventType type,
                      TimeResolution start_time, TimeResolution duration,
                      dftracer::Metadata* metadata, ProcessID process_id,
                      ThreadID tid) override;
  void log_record(ConstEventNameType record_name,
                  const dftracer::Metadata* fields, TraceEventType type,
                  ProcessID process_id, ThreadID tid) override;
  void log_metadata_event(ConstEventNameType name, ConstEventNameType value,
                          ConstEventNameType record_name, TraceEventType type,
                          ProcessID process_id, ThreadID tid,
                          bool is_string = true) override;
  void log_counter_event(int index, ConstEventNameType name,
                         ConstEventNameType category, TraceEventType type,
                         TimeResolution start_time, ProcessID process_id,
                         ThreadID thread_id,
                         dftracer::Metadata* metadata) override;

  /**
   * Publish the block of every thread that is not inside a log call right now
   * and has held its block for at least `max_age_ns`. For a periodic timer;
   * finalize() calls it with 0.
   */
  void flush_idle(uint64_t max_age_ns);

  /**
   * Publish every thread's block, waiting up to `wait_ns` for a thread that
   * is inside a log call. finalize() uses it: nothing logged before it may
   * be left in a block only the service's take-over would find.
   */
  void flush_all(uint64_t wait_ns);

  // Records pushed and dropped so far, summed over every thread, and the
  // events spilled to the spill file.
  uint64_t pushed();
  uint64_t dropped();
  uint64_t spilled() const { return spilled_.load(std::memory_order_relaxed); }

 private:
  using Slot = ServiceSlot;
  struct ThreadRef;
  static thread_local ThreadRef tl_ref;
  // Set once the calling thread's tl_ref has been destroyed. A plain bool, so
  // it stays readable afterwards: exit handlers (the preload finalizer's
  // "end" event, atexit, static destructors) still log on that thread.
  static thread_local bool tl_exited;

  Slot* acquire_slot();
  Slot* register_slot();
  // A slot added to slots_, not tied to any thread.
  std::shared_ptr<Slot> create_slot();
  void release_slot(Slot* slot);
  // Copies the encoded bytes in slot->scratch into the ring as one record,
  // waiting up to wait_ns for room. False if the ring had none in time.
  bool push(Slot* slot, ShmRecordKind kind, uint64_t wait_ns);
  // Encode with `encode(ByteWriter&)` and push, on the calling thread's slot.
  // False if the record is not in the ring.
  template <typename Encode>
  bool emit(ShmRecordKind kind, Encode&& encode);
  // emit() for a control record (hello, goodbye, app name, rank): no spill
  // file can hold one, so it waits longer, and a loss is counted apart.
  template <typename Encode>
  void emit_control(ShmRecordKind kind, Encode&& encode);
  // An event the ring did not take: `log(LocalBufferManager&)` logs it to the
  // spill file. Returns false (and counts a drop) if there is none.
  template <typename Log>
  bool spill(Log&& log);
  // Counts a record that is in neither the ring nor the spill file.
  void count_drop();
  // False once the service no longer drains the ring (stopped or died).
  bool service_alive();
  // The spill file's manager, created on first use; nullptr if it cannot be.
  LocalBufferManager* spill_manager();
  // Drops every slot inherited across fork(): their blocks belong to the
  // parent, which publishes them itself.
  void abandon_slots();

  std::unique_ptr<ipc::ShmRegion> region_;
  std::string log_file_;
  std::string hostname_hash_;
  int owner_pid_ = 0;
  uint64_t full_wait_ns_ = 0;
  uint8_t event_flags_ = 0;  // ShmEventFlags of every event this process sends
  std::atomic<uint64_t> generation_{1};

  std::mutex slots_mtx_;
  std::vector<std::shared_ptr<Slot>> slots_;
  // Shared by threads whose own slot is gone (tl_exited); held under
  // orphan_mtx_ from acquire_slot() to release_slot().
  std::mutex orphan_mtx_;
  std::shared_ptr<Slot> orphan_;

  // Pushes of slots that have been retired (thread exit, fork), and every
  // record that reached neither the ring nor the spill file.
  std::atomic<uint64_t> retired_pushed_{0};
  std::atomic<uint64_t> retired_dropped_{0};

  // Spill-over (see the class comment). spill_ready_ is set once spill_ is
  // usable; spill_mtx_ guards creating it.
  std::mutex spill_mtx_;
  std::unique_ptr<LocalBufferManager> spill_;
  std::atomic<bool> spill_ready_{false};
  bool spill_failed_ = false;
  int spill_lock_fd_ = -1;  // flock held while the spill file is written
  std::string spill_file_;
  std::atomic<uint64_t> spilled_{0};
  std::atomic<uint64_t> control_dropped_{0};
  std::atomic<bool> service_gone_{false};
  std::atomic<uint64_t> liveness_checked_ns_{0};
};

}  // namespace dftracer
#endif  // DFTRACER_SERVICE_BUFFER_MANAGER_H
