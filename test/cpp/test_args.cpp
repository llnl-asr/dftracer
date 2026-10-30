// Typed values, lists and metadata records through the C++ region API.
// Checked by test/check_args_trace.py.
#include <dftracer/dftracer.h>

#include <cstdint>
#include <limits>

int main() {
  DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
  {
    DFTRACER_CPP_REGION(typed);
    DFTRACER_CPP_REGION_UPDATE(typed, "i64", int64_t{-9007199254740993LL});
    DFTRACER_CPP_REGION_UPDATE(typed, "u64",
                               std::numeric_limits<uint64_t>::max());
    DFTRACER_CPP_REGION_UPDATE(typed, "f64", 2.5);
    DFTRACER_CPP_REGION_UPDATE(typed, "flag", true);
  }
  {
    DFTRACER_CPP_REGION(lists);
    DFTRACER_CPP_REGION_APPEND(lists, "files", "a.h5");
    DFTRACER_CPP_REGION_APPEND(lists, "files", "b \"q\".h5");
    DFTRACER_CPP_REGION_APPEND(lists, "ids", int64_t{1});
    DFTRACER_CPP_REGION_APPEND(lists, "ids", int64_t{-2});
    DFTRACER_CPP_REGION_APPEND(lists, "links", "in", "x");
    DFTRACER_CPP_REGION_APPEND(lists, "links", "out", "y");
    DFTRACER_CPP_REGION_APPEND(lists, "links", "out", "z");
    // files is a plain list: an object-of-lists append to it is ignored.
    DFTRACER_CPP_REGION_APPEND(lists, "files", "bad", "ignored");
  }
  {
    DFTRACER_CPP_RECORD(SAMPLE);
    DFTRACER_CPP_REGION_UPDATE(SAMPLE, "id", "s1");
    DFTRACER_CPP_REGION_UPDATE(SAMPLE, "n", 3);
    DFTRACER_CPP_REGION_UPDATE(SAMPLE, "ratio", 0.5);
    DFTRACER_CPP_REGION_APPEND(SAMPLE, "tags", "x");
    DFTRACER_CPP_REGION_APPEND(SAMPLE, "tags", "y");
    // A field set again keeps the last value.
    DFTRACER_CPP_REGION_UPDATE(SAMPLE, "n", 4);
  }
  DFTRACER_CPP_METADATA(plain, "plain_key", "plain_value");
  // In-process mode keeps the trace of a process with at least 5 events.
  for (int i = 0; i < 5; ++i) {
    DFTRACER_CPP_REGION(pad);
  }
  DFTRACER_CPP_FINI();
  return 0;
}
