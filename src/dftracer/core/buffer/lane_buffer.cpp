#include <dftracer/core/buffer/lane_buffer.h>
#include <dftracer/core/common/profiler.h>
#include <dftracer/core/common/singleton.h>

namespace dftracer {

LaneBuffer::LaneBuffer(HashType hostname_hash)
    : config_(Singleton<ConfigurationManager>::get_instance()) {
  serializer_.initialize(nullptr, hostname_hash);
  if (config_->aggregation_enable)
    aggregator_ = std::make_unique<Aggregator>(config_);
}

void LaneBuffer::attach(char* chunk, size_t used, size_t capacity,
                        ChunkSink* sink) {
  chunk_ = chunk;
  used_ = used;
  capacity_ = capacity;
  sink_ = sink;
}

size_t LaneBuffer::detach() {
  const size_t used = used_;
  chunk_ = nullptr;
  used_ = 0;
  sink_ = nullptr;
  return used;
}

void LaneBuffer::wrote(size_t size) {
  if (used_ + size > capacity_ && sink_ != nullptr) {
    sink_->flush(chunk_, used_ + size);
    used_ = 0;
  } else {
    used_ += size;
  }
}

void LaneBuffer::log_packed_event(const ipc::DecodedEvent& event, bool counter,
                                  ShapeLines* lines, size_t shape) {
  if (finished_) return;
  if (counter) {
    wrote(serializer_.counter(chunk_ + used_, event));
    return;
  }
  if (aggregator_ != nullptr && aggregator_->wants(event, app_name(), &rank_)) {
    if (aggregator_->aggregate(event)) drain(false, event.head.process_id);
    return;
  }
  wrote(lines != nullptr
            ? serializer_.data(chunk_ + used_, event, *lines, shape)
            : serializer_.data(chunk_ + used_, event));
}

void LaneBuffer::log_metadata_event(ConstEventNameType name,
                                    ConstEventNameType value,
                                    ConstEventNameType record_name,
                                    TraceEventType type, ProcessID process_id,
                                    ThreadID tid, bool is_string) {
  if (finished_) return;
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(BUFFER));
  wrote(serializer_.metadata(chunk_ + used_, name, value, record_name, type,
                             process_id, tid, is_string));
}

void LaneBuffer::log_record(ConstEventNameType record_name, const char* fields,
                            TraceEventType type, ProcessID process_id,
                            ThreadID tid) {
  if (finished_) return;
  DFTRACER_PROFILE_SCOPE(DFTRACER_PROFILE_PHASE(BUFFER));
  wrote(serializer_.record(chunk_ + used_, record_name, fields, type,
                           process_id, tid));
}

void LaneBuffer::finalize(ProcessID process_id) {
  if (finished_) return;
  finished_ = true;
  if (chunk_ == nullptr || aggregator_ == nullptr) return;
  DFTRACER_PROFILE_SCOPE_UNCOUNTED(DFTRACER_PROFILE_PHASE(AGGREGATION));
  drain(true, process_id);
}

void LaneBuffer::drain(bool all, ProcessID process_id) {
  // One row at a time, each into the chunk, so a long drain moves on to a
  // fresh chunk instead of running past this one's slack.
  struct ChunkRows : Aggregator::RowSink {
    explicit ChunkRows(LaneBuffer& b) : self(b) {}
    char* row() override { return self.chunk_ + self.used_; }
    void wrote(size_t size) override { self.wrote(size); }
    LaneBuffer& self;
  } rows(*this);
  aggregator_->drain(all, process_id, serializer_.hostname(), rows);
}
}  // namespace dftracer
