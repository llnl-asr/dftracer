// Event types through the C++ API, run with aggregation on. Checked by
// test/check_event_types_trace.py.
#include <dftracer/dftracer.h>

#include <cstdint>

// A project's own layer whose events are never aggregated.
struct Lineage : dftracer::EventType {
  static constexpr const char* category = "lineage";
  static constexpr TraceEventType layer =
      static_cast<TraceEventType>(DFTRACER_TRACE_TYPE_EXTERNAL);
  static constexpr bool aggregatable = false;
};
// Metadata events are never aggregated: aggregatable = false on the CPP_APP
// layer must leave CPP_APP data events aggregatable.
struct Note : dftracer::EventType {
  static constexpr int kind = DF_METADATA_EVENT;
  static constexpr bool aggregatable = false;
};

int main() {
  DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
  {
    DFTRACER_CPP_TYPED_REGION(Note, NOTE);
    DFTRACER_CPP_REGION_UPDATE(NOTE, "text", "hello");
  }
  for (int i = 0; i < 10; ++i) {
    DFTRACER_CPP_REGION(app);
  }
  for (int i = 0; i < 10; ++i) {
    DFTRACER_CPP_TYPED_REGION(Lineage, step);
    DFTRACER_CPP_REGION_UPDATE(step, "i", i);
    profiler_step.append_id("relations", "used", uint64_t{0xabcdef} + i);
  }
  DFTRACER_CPP_FINI();
  return 0;
}
