/* Typed values, lists and metadata records through the C region API.
 * Checked by test/check_args_trace.py. */
#include <dftracer/dftracer.h>
#include <stdint.h>

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;
  DFTRACER_C_INIT(NULL, NULL, NULL);
  {
    DFTRACER_C_REGION_START(typed);
    DFTRACER_C_REGION_UPDATE_INT64(typed, "i64", -9007199254740993LL);
    DFTRACER_C_REGION_UPDATE_UINT64(typed, "u64", UINT64_MAX);
    DFTRACER_C_REGION_UPDATE_DOUBLE(typed, "f64", 2.5);
    DFTRACER_C_REGION_UPDATE_INT(typed, "flag", 1);
    DFTRACER_C_REGION_END(typed);
  }
  {
    DFTRACER_C_REGION_START(lists);
    DFTRACER_C_REGION_APPEND_STR(lists, "files", NULL, "a.h5");
    DFTRACER_C_REGION_APPEND_STR(lists, "files", NULL, "b \"q\".h5");
    DFTRACER_C_REGION_APPEND_INT(lists, "ids", NULL, 1);
    DFTRACER_C_REGION_APPEND_INT(lists, "ids", NULL, -2);
    DFTRACER_C_REGION_APPEND_STR(lists, "links", "in", "x");
    DFTRACER_C_REGION_APPEND_STR(lists, "links", "out", "y");
    DFTRACER_C_REGION_APPEND_STR(lists, "links", "out", "z");
    /* files is a plain list: an object-of-lists append to it is ignored. */
    DFTRACER_C_REGION_APPEND_STR(lists, "files", "bad", "ignored");
    DFTRACER_C_REGION_END(lists);
  }
  {
    DFTRACER_C_RECORD_START(SAMPLE);
    DFTRACER_C_REGION_UPDATE_STR(SAMPLE, "id", "s1");
    DFTRACER_C_REGION_UPDATE_INT(SAMPLE, "n", 3);
    DFTRACER_C_REGION_UPDATE_DOUBLE(SAMPLE, "ratio", 0.5);
    DFTRACER_C_REGION_APPEND_STR(SAMPLE, "tags", NULL, "x");
    DFTRACER_C_REGION_APPEND_STR(SAMPLE, "tags", NULL, "y");
    /* A field set again keeps the last value. */
    DFTRACER_C_REGION_UPDATE_INT(SAMPLE, "n", 4);
    DFTRACER_C_RECORD_END(SAMPLE);
  }
  DFTRACER_C_METADATA(plain, "plain_key", "plain_value");
  /* In-process mode keeps the trace of a process with at least 5 events. */
  for (int i = 0; i < 5; ++i) {
    DFTRACER_C_REGION_START(pad);
    DFTRACER_C_REGION_END(pad);
  }
  DFTRACER_C_FINI();
  return 0;
}
