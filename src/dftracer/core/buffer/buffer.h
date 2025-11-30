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
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
namespace dftracer {
class BufferManager {
 public:
  BufferManager()
      : buffer(nullptr), buffer_pos(0), mtx(), app_name(), rank(-1) {}
  ~BufferManager() {}

  void inline set_app_name(const char* name) { app_name = name; }

  inline const char* get_app_name() const {
    return app_name.empty() ? nullptr : app_name.c_str();
  }

  void inline set_rank(const int& r) { rank = r; }

  int initialize(const char* filename, HashType hostname_hash);

  int finalize(int index, ProcessID process_id, bool end_sym = false);

  void log_data_event(int index, ConstEventNameType event_name,
                      ConstEventNameType category, TimeResolution start_time,
                      TimeResolution duration, dftracer::Metadata* metadata,
                      ProcessID process_id, ThreadID tid);

  void log_metadata_event(ConstEventNameType name, ConstEventNameType value,
                          ConstEventNameType ph, ProcessID process_id,
                          ThreadID tid, bool is_string = true);

  void log_counter_event(int index, ConstEventNameType name,
                         ConstEventNameType category, TimeResolution start_time,
                         ProcessID process_id, ThreadID thread_id,
                         dftracer::Metadata* metadata);

 private:
  void compress_and_write_if_needed(size_t size, bool force = false);
  char* buffer;
  size_t buffer_pos;
  std::shared_mutex mtx;
  std::string app_name;
  int rank;

  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::shared_ptr<dftracer::JsonLines> serializer;
  std::shared_ptr<dftracer::ZlibCompression> compressor;
  std::shared_ptr<dftracer::STDIOWriter> writer;
  std::shared_ptr<dftracer::Aggregator> aggregator;
};
}  // namespace dftracer
#endif  // DFTRACER_BUFFER_H