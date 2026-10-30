#ifndef DFTRACER_BUFFER_MANAGER_H
#define DFTRACER_BUFFER_MANAGER_H
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/typedef.h>

#include <atomic>
#include <memory>
#include <string>
namespace dftracer {
/**
 * Where a process's events go. Two implementations, chosen once per process
 * by BufferManagerFactory:
 *
 *  * LocalBufferManager (local_buffer_manager.h): serialize to JSON lines,
 * aggregate, compress and write this process's own trace file. The default.
 *  * ServiceBufferManager (service_buffer_manager.h): encode each event in a
 *    compact binary form and push it into the shared-memory ring of the
 *    node's dftracer_service, which aggregates, compresses and writes for
 *    every process on the node (DFTRACER_ENABLE_PC_MODE=1).
 *
 * Every log_* call takes ownership of the Metadata it is given.
 */
class BufferManager {
 public:
  BufferManager() : app_name(), rank(-1) {}
  virtual ~BufferManager() {}

  // TWO-PHASE CONSTRUCTION GUARD.
  //
  // The constructor above deliberately does NOT wire `config` -- that happens
  // later, in initialize(). But BufferManagerFactory::get_instance()
  // publishes the pointer as soon as the object is constructed, so between
  // construction and initialize() this object is reachable with config ==
  // nullptr and rank == -1.
  //
  // Asynchronous producers do not respect that window. rocprofiler registers
  // through the exported rocprofiler_configure symbol at LOAD time -- before
  // dftracer initializes -- and flushes its buffers from its own thread pool.
  // A flush landing in the window called log_data_event() and dereferenced a
  // null config:
  //
  //   #0 BufferManager::log_data_event()   buffer.cpp: this->config->...
  //   #1 DFTLogger::log()
  //   #2 HIPFunction::tool_tracing_callback()
  //   #3 rocprofiler::buffer::flush()      librocprofiler-sdk
  //   #7 PTL::ThreadPool::execute_thread()
  //
  // DFTLogger::is_init is already true at that point (it is set once the
  // logger has *obtained* this singleton, not once this singleton is usable),
  // so no existing guard caught it. Measured on MI250X: clean at 1-2 ranks,
  // SIGSEGV 3/3 at >=3 ranks -- more ranks start GPU work sooner and fill
  // rocprofiler's buffers inside the window -- and invisible under gdb, whose
  // slowdown moves initialize() out from under the flush.
  //
  // `ready` closes both ends: false until initialize() has wired everything,
  // and false again from the start of finalize(), so a late flush during
  // teardown is equally harmless. Atomic because it is read from producer
  // threads dftracer does not own.
  bool inline is_ready() const { return ready.load(std::memory_order_acquire); }

  virtual void set_app_name(const char* name) { app_name = name; }

  inline const char* get_app_name() const {
    return app_name.empty() ? nullptr : app_name.c_str();
  }

  virtual void set_rank(const int& r) { rank = r; }

  /**
   * Write out everything this process holds that has not reached its trace
   * yet, and keep tracing. For exec(): a successful exec discards the
   * process's memory, a failed one returns and the process goes on logging.
   */
  virtual void flush() {}

  virtual int initialize(const char* filename, HashType hostname_hash) = 0;

  virtual int finalize(int index, ProcessID process_id) = 0;

  virtual void log_data_event(int index, ConstEventNameType event_name,
                              ConstEventNameType category, TraceEventType type,
                              TimeResolution start_time,
                              TimeResolution duration,
                              dftracer::Metadata* metadata,
                              ProcessID process_id, ThreadID tid) = 0;

  // Metadata record whose args are the fields (see JsonLines::record). Does
  // not take ownership of fields.
  virtual void log_record(ConstEventNameType record_name,
                          const dftracer::Metadata* fields, TraceEventType type,
                          ProcessID process_id, ThreadID tid) = 0;
  virtual void log_metadata_event(ConstEventNameType name,
                                  ConstEventNameType value,
                                  ConstEventNameType record_name,
                                  TraceEventType type, ProcessID process_id,
                                  ThreadID tid, bool is_string = true) = 0;

  virtual void log_counter_event(int index, ConstEventNameType name,
                                 ConstEventNameType category,
                                 TraceEventType type, TimeResolution start_time,
                                 ProcessID process_id, ThreadID thread_id,
                                 dftracer::Metadata* metadata) = 0;

 protected:
  std::string app_name;
  int rank;
  std::atomic<bool> ready{false};
};

/**
 * Picks the BufferManager of this process from the configuration, once, and
 * returns the same instance afterwards. Everything that logs reaches the
 * buffer manager through here, never through Singleton<...> of a concrete
 * class, so the choice is made in one place.
 */
class BufferManagerFactory {
 public:
  static std::shared_ptr<BufferManager> get_instance();
  // dftracer_service writes its own telemetry locally whatever the
  // environment says: it is the consumer of the ring, not a producer.
  static void force_local();
};
}  // namespace dftracer
#endif  // DFTRACER_BUFFER_MANAGER_H
