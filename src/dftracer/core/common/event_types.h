#ifndef DFTRACER_CORE_COMMON_EVENT_TYPES_H
#define DFTRACER_CORE_COMMON_EVENT_TYPES_H
#include <dftracer/core/common/enumeration.h>

#include <atomic>
#include <cstdint>

namespace dftracer {
// Whether each layer's events may be aggregated. A layer is aggregatable
// until an event type of data events on it says otherwise
// (dftracer_register_event_type); metadata events are never aggregated, so
// their types do not take part. Indexed by the uint8_t layer.
inline std::atomic<bool>* non_aggregatable_layers() {
  static std::atomic<bool> layers[256];
  return layers;
}

inline bool event_type_aggregatable(TraceEventType layer) {
  return !non_aggregatable_layers()[static_cast<uint8_t>(layer)].load(
      std::memory_order_relaxed);
}

inline void set_event_type_not_aggregatable(TraceEventType layer) {
  non_aggregatable_layers()[static_cast<uint8_t>(layer)].store(
      true, std::memory_order_relaxed);
}
}  // namespace dftracer
#endif  // DFTRACER_CORE_COMMON_EVENT_TYPES_H
