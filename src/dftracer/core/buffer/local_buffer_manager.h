#ifndef DFTRACER_LOCAL_BUFFER_MANAGER_H
#define DFTRACER_LOCAL_BUFFER_MANAGER_H
#include <dftracer/core/buffer/buffer_manager.h>
#include <dftracer/core/buffer/chunk_sink.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/compression/gzip_compression.h>
//
#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/aggregator/packed_aggregator.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/writer/stdio_writer.h>

#include <any>
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
namespace dftracer {
// The default BufferManager: this process serializes, aggregates, compresses
// and writes its own trace file.
//
// dftracer_service also runs one per producer thread in producer/consumer
// mode, constructed with its own aggregator and serializer and *attached* to
// the chunk of whichever service thread is replaying that producer's records:
// the same serialization and aggregation as in the process, but no file and
// no compression of its own; a full chunk goes to the ChunkSink. There its
// DATA and COUNTER records come in through log_packed_event and are written
// and aggregated straight from the decoded record (PackedAggregator), without
// a heap Metadata.
class LocalBufferManager : public BufferManager {
 public:
  LocalBufferManager() : buffer(nullptr), buffer_pos(0), mtx() {}

  LocalBufferManager(std::shared_ptr<dftracer::Aggregator> own_aggregator,
                     std::shared_ptr<dftracer::JsonLines> own_serializer)
      : buffer(nullptr),
        buffer_pos(0),
        mtx(),
        serializer(std::move(own_serializer)),
        aggregator(std::move(own_aggregator)),
        attached_mode(true) {}

  /**
   * Attached mode: serialize into `chunk` from offset `used`; at `capacity`
   * hand it to `sink` and start it over. The chunk needs slack beyond
   * `capacity` for the record that crosses it, as the owned buffer has.
   */
  void attach(char* chunk, size_t used, size_t capacity, ChunkSink* sink);
  /** Attached mode: stop writing into the chunk; returns the bytes used. */
  size_t detach();

  ~LocalBufferManager() override {}

  int initialize(const char* filename, HashType hostname_hash) override;

  int finalize(int index, ProcessID process_id) override;

  // Compress and write the buffer and every open aggregate, then fflush.
  void flush() override;

  void log_data_event(int index, ConstEventNameType event_name,
                      ConstEventNameType category, TraceEventType type,
                      TimeResolution start_time, TimeResolution duration,
                      dftracer::Metadata* metadata, ProcessID process_id,
                      ThreadID tid) override;

  // Metadata record whose args are the fields (see JsonLines::record).
  void log_record(ConstEventNameType record_name,
                  const dftracer::Metadata* fields, TraceEventType type,
                  ProcessID process_id, ThreadID tid) override;
  // The same with the fields already JSON (records from the ring).
  void log_record(ConstEventNameType record_name, const char* fields,
                  TraceEventType type, ProcessID process_id, ThreadID tid);
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
   * Attached mode: a DATA (or, with `counter`, COUNTER) record replayed by
   * the service, written or aggregated as log_data_event and
   * log_counter_event would its Metadata form. `event` is only read.
   */
  void log_packed_event(const ipc::DecodedEvent& event, bool counter);

 private:
  void compress_and_write_if_needed(size_t size, bool force = false);
  // Writes the complete packed intervals (or all) into the chunk.
  void drain_packed(bool all, ProcessID process_id);
  // Writes the rows of `data` into the buffer one at a time, flushing it as it
  // fills: an interval can hold more rows than the buffer's slack.
  void write_aggregations(dftracer::AggregatedDataType& data, int index,
                          ProcessID process_id);
  char* buffer;
  size_t buffer_pos;
  std::shared_mutex mtx;

  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::shared_ptr<dftracer::JsonLines> serializer;
  std::shared_ptr<dftracer::GzipCompression> compressor;
  std::shared_ptr<dftracer::STDIOWriter> writer;
  std::shared_ptr<dftracer::Aggregator> aggregator;
  std::unique_ptr<dftracer::PackedAggregator> packed_aggregator;
  // Attached mode only.
  bool attached_mode = false;
  ChunkSink* sink = nullptr;
  size_t capacity = 0;
};
}  // namespace dftracer
#endif  // DFTRACER_LOCAL_BUFFER_MANAGER_H