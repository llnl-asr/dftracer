#ifndef DFTRACER_BUFFER_LANE_BUFFER_H
#define DFTRACER_BUFFER_LANE_BUFFER_H
#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/buffer/chunk_sink.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/ipc/event_codec.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <memory>
#include <string>

namespace dftracer {
/**
 * dftracer_service's state for one producer thread: it serializes and
 * aggregates the thread's records as the process would have, into the chunk
 * of whichever service thread replays them. No file and no compression of
 * its own; a full chunk goes to the ChunkSink. Not thread-safe: the caller
 * holds the lane's lock.
 */
class LaneBuffer {
 public:
  explicit LaneBuffer(HashType hostname_hash);

  /**
   * Serialize into `chunk` from offset `used`; at `capacity` hand it to
   * `sink` and start it over. The chunk needs DFTRACER_SERIALIZATION_EVENT_MAX
   * bytes of slack beyond `capacity` for the record that crosses it.
   */
  void attach(char* chunk, size_t used, size_t capacity, ChunkSink* sink);
  /** Stop writing into the chunk; returns the bytes used. */
  size_t detach();

  void set_app_name(const char* name) { app_name_ = name; }
  void set_rank(int rank) { rank_ = rank; }

  /** A DATA (or, with `counter`, COUNTER) record, written or aggregated.
   * `event` is only read. */
  void log_packed_event(const ipc::DecodedEvent& event, bool counter);
  void log_metadata_event(ConstEventNameType name, ConstEventNameType value,
                          ConstEventNameType record_name, TraceEventType type,
                          ProcessID process_id, ThreadID tid, bool is_string);
  /** A record whose fields are already JSON. */
  void log_record(ConstEventNameType record_name, const char* fields,
                  TraceEventType type, ProcessID process_id, ThreadID tid);
  /** Writes every open aggregation interval. Records after it are dropped. */
  void finalize(ProcessID process_id);

 private:
  void wrote(size_t size);
  void drain(bool all, ProcessID process_id);
  const char* app_name() const {
    return app_name_.empty() ? nullptr : app_name_.c_str();
  }

  std::shared_ptr<ConfigurationManager> config_;
  JsonLines serializer_;
  std::unique_ptr<Aggregator> aggregator_;
  std::string app_name_;
  int rank_ = -1;
  char* chunk_ = nullptr;
  size_t used_ = 0;
  size_t capacity_ = 0;
  ChunkSink* sink_ = nullptr;
  bool finished_ = false;
};
}  // namespace dftracer
#endif  // DFTRACER_BUFFER_LANE_BUFFER_H
