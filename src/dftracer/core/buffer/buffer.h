#ifndef DFTRACER_BUFFER_H
#define DFTRACER_BUFFER_H
#include <dftracer/core/common/logging.h>
#include <dftracer/core/compression/zlib_compression.h>
//
#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/writer/stdio_writer.h>

#include <any>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
namespace dftracer {
class BufferManager {
 public:
  BufferManager()
      : buffer(nullptr), buffer_pos(0), mtx(), app_name(), rank(-1) {}

  // TWO-PHASE CONSTRUCTION GUARD.
  //
  // The constructor above deliberately does NOT wire `config` -- that happens
  // later, in initialize(). But Singleton<BufferManager>::get_instance()
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
  ~BufferManager() {}

  void inline set_app_name(const char* name) { app_name = name; }

  inline const char* get_app_name() const {
    return app_name.empty() ? nullptr : app_name.c_str();
  }

  void inline set_rank(const int& r) { rank = r; }

  int initialize(const char* filename, HashType hostname_hash);

  int finalize(int index, ProcessID process_id);

  void log_data_event(int index, ConstEventNameType event_name,
                      ConstEventNameType category, TraceEventType type,
                      TimeResolution start_time, TimeResolution duration,
                      dftracer::Metadata* metadata, ProcessID process_id,
                      ThreadID tid);

  // Metadata record with named JSON fields (see JsonLines::record).
  void log_record(ConstEventNameType record_name, const char* fields,
                  TraceEventType type, ProcessID process_id, ThreadID tid);
  void log_metadata_event(ConstEventNameType name, ConstEventNameType value,
                          ConstEventNameType record_name, TraceEventType type,
                          ProcessID process_id, ThreadID tid,
                          bool is_string = true);

  void log_counter_event(int index, ConstEventNameType name,
                         ConstEventNameType category, TraceEventType type,
                         TimeResolution start_time, ProcessID process_id,
                         ThreadID thread_id, dftracer::Metadata* metadata);

 private:
  void compress_and_write_if_needed(size_t size, bool force = false);
  char* buffer;
  size_t buffer_pos;
  std::shared_mutex mtx;
  std::string app_name;
  int rank;
  std::atomic<bool> ready{false};

  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::shared_ptr<dftracer::JsonLines> serializer;
  std::shared_ptr<dftracer::ZlibCompression> compressor;
  std::shared_ptr<dftracer::STDIOWriter> writer;
  std::shared_ptr<dftracer::Aggregator> aggregator;
};
}  // namespace dftracer
#endif  // DFTRACER_BUFFER_H