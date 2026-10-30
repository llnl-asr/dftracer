#ifndef DFTRACER_SERIALIZATION_JSON_LINE_H
#define DFTRACER_SERIALIZATION_JSON_LINE_H

#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <any>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <typeinfo>
#include <unordered_map>

// The longest line any serializer writes, newline included.
static constexpr size_t DFTRACER_SERIALIZATION_EVENT_MAX = 16 * 1024;

namespace dftracer {
class JsonLines {
  bool include_metadata;
  HashType hostname_hash;
  // Shared layout for the timestamped, duration-less records: counters and
  // aggregated events differ only in their phase. Takes ownership of metadata
  // (convert_metadata() frees a non-empty map, series() an empty one).
  size_t series(char* buffer, int index, ConstEventNameType event_name,
                ConstEventNameType category, TraceEventType type,
                TracePhaseType phase, TimeResolution start_time,
                ProcessID process_id, ThreadID thread_id,
                dftracer::Metadata* metadata);

 public:
  JsonLines();
  // Writes the metadata as "key":value,... into meta_stream and frees it.
  // Returns whether there was any.
  static bool convert_metadata(Metadata* metadata,
                               std::stringstream& meta_stream);
  // The service serializes for many processes, each with its own setting.
  void set_include_metadata(bool include) { include_metadata = include; }
  bool includes_metadata() const { return include_metadata; }
  HashType hostname() const { return hostname_hash; }
  size_t initialize(char* buffer, HashType hostname_hash);
  size_t data(char* buffer, int index, ConstEventNameType event_name,
              ConstEventNameType category, TraceEventType type,
              TimeResolution start_time, TimeResolution duration,
              dftracer::Metadata* metadata, ProcessID process_id, ThreadID tid);
  // record_name is the metadata record kind (PR, FH, HH, SH, CM), not a phase.
  size_t metadata(char* buffer, ConstEventNameType name,
                  ConstEventNameType value, ConstEventNameType record_name,
                  TraceEventType type, ProcessID process_id, ThreadID thread_id,
                  bool is_string = true);
  // A metadata record whose args are named fields (entity records EH/ET/ER).
  // `fields` is the inside of the args object after hhash, e.g.
  // "\"id\":\"8f0d...\",\"type\":\"x\",\"store\":3" -- the caller guarantees it
  // is valid JSON (entity fields are sanitized to a JSON-safe charset).
  size_t record(char* buffer, ConstEventNameType record_name,
                const char* fields, TraceEventType type, ProcessID process_id,
                ThreadID thread_id);
  // The same from a record's Metadata, written straight into the line.
  size_t record(char* buffer, ConstEventNameType record_name,
                const Metadata* fields, TraceEventType type,
                ProcessID process_id, ThreadID thread_id);
  size_t counter(char* buffer, int index, ConstEventNameType name,
                 ConstEventNameType category, TraceEventType type,
                 TimeResolution start_time, ProcessID process_id,
                 ThreadID thread_id, dftracer::Metadata* metadata);
  // Where aggregated rows go, one at a time: row() is where the next row is
  // written (room for DFTRACER_SERIALIZATION_EVENT_MAX bytes), wrote() is told
  // its size. A buffer manager flushes in wrote() when it fills up, since an
  // interval can hold more rows than any fixed slack.
  struct RowSink {
    virtual ~RowSink() = default;
    virtual char* row() = 0;
    virtual void wrote(size_t size) = 0;
  };
  void aggregated(RowSink& rows, int index, ProcessID process_id,
                  dftracer::AggregatedDataType& data);
  // All rows contiguously into buffer, which must have room for them.
  size_t aggregated(char* buffer, int index, ProcessID process_id,
                    dftracer::AggregatedDataType& data);
};
}  // namespace dftracer

#endif  // DFTRACER_SERIALIZATION_JSON_LINE_H