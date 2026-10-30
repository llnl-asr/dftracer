/* Event types through the C API, run with aggregation on. Checked by
 * test/check_event_types_trace.py. */
#include <dftracer/dftracer.h>
#include <stdint.h>

/* A project's own layer whose events are never aggregated. */
static const struct DFTracerEventType LINEAGE = {
    "lineage", DFTRACER_TRACE_TYPE_EXTERNAL, DF_DATA_EVENT, 0};
/* Metadata events are never aggregated: aggregatable = 0 on the C_APP layer
 * must leave C_APP data events aggregatable. */
static const struct DFTracerEventType NOTE_TYPE = {"C_APP", 2,
                                                   DF_METADATA_EVENT, 0};

int main(void) {
  int i;
  DFTRACER_C_INIT(NULL, NULL, NULL);
  {
    DFTRACER_C_TYPED_REGION_START(NOTE_TYPE, NOTE);
    DFTRACER_C_REGION_UPDATE_STR(NOTE, "text", "hello");
    DFTRACER_C_REGION_END(NOTE);
  }
  for (i = 0; i < 10; ++i) {
    DFTRACER_C_REGION_START(app);
    DFTRACER_C_REGION_END(app);
  }
  for (i = 0; i < 10; ++i) {
    DFTRACER_C_TYPED_REGION_START(LINEAGE, step);
    DFTRACER_C_REGION_UPDATE_INT(step, "i", i);
    update_metadata_append_id(data_step, "relations", "used",
                              (uint64_t)0xabcdef + (uint64_t)i);
    DFTRACER_C_REGION_END(step);
  }
  DFTRACER_C_FINI();
  return 0;
}
