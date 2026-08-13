#include <dftracer/core/common/enumeration.h>

#include <cstring>
#include <iostream>
#include <set>
#include <string>

#include "check.h"

// The "type" column is part of the on-disk trace format. These tests pin the
// wire values so an accidental renumbering (inserting a type in the middle of
// the enum rather than appending) fails here instead of silently misreading
// every trace written by an older DFTracer.
void test_wire_values_are_pinned() {
  std::cout << "=== Test: TraceEventType wire values ===\n" << std::endl;

  DFT_CHECK(TRACE_TYPE_UNKNOWN == 0);
  DFT_CHECK(TRACE_TYPE_DFTRACER == 1);
  DFT_CHECK(TRACE_TYPE_C_APP == 2);
  DFT_CHECK(TRACE_TYPE_LIBC_IO == 3);
  DFT_CHECK(TRACE_TYPE_HIP == 4);
  DFT_CHECK(TRACE_TYPE_HDF5 == 5);
  DFT_CHECK(TRACE_TYPE_PYTHON == 6);
  DFT_CHECK(TRACE_TYPE_PSUTIL == 7);
  DFT_CHECK(TRACE_TYPE_FINSTRUMENT == 8);
  DFT_CHECK(TRACE_TYPE_CPP_APP == 9);
  DFT_CHECK(TRACE_TYPE_MPI == 10);
  DFT_CHECK(TRACE_TYPE_PAPI == 11);
  DFT_CHECK(TRACE_TYPE_OMNISTAT == 12);
  DFT_CHECK(TRACE_TYPE_VARIORUM == 13);

  // MAX is a sentinel one past the last real type; it is never serialized.
  DFT_CHECK(TRACE_TYPE_MAX == 14);

  std::cout << "✓ Wire values pinned\n" << std::endl;
}

void test_to_string_is_total_and_unique() {
  std::cout << "=== Test: TraceEventType to_string ===\n" << std::endl;

  DFT_CHECK(std::string(to_string(TRACE_TYPE_UNKNOWN)) == "UNKNOWN");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_DFTRACER)) == "DFTRACER");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_C_APP)) == "C_APP");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_LIBC_IO)) == "LIBC_IO");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_HIP)) == "HIP");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_HDF5)) == "HDF5");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_PYTHON)) == "PYTHON");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_PSUTIL)) == "PSUTIL");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_FINSTRUMENT)) == "FINSTRUMENT");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_CPP_APP)) == "CPP_APP");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_MPI)) == "MPI");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_PAPI)) == "PAPI");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_OMNISTAT)) == "OMNISTAT");
  DFT_CHECK(std::string(to_string(TRACE_TYPE_VARIORUM)) == "VARIORUM");

  // Every real type must map to a distinct name, so a name can be used to
  // identify a type without ambiguity.
  std::set<std::string> names;
  for (int i = 0; i < TRACE_TYPE_MAX; ++i) {
    names.insert(to_string(static_cast<TraceEventType>(i)));
  }
  DFT_CHECK(names.size() == static_cast<size_t>(TRACE_TYPE_MAX));

  std::cout << "✓ to_string total and unique\n" << std::endl;
}

void test_convert_rejects_out_of_range() {
  std::cout << "=== Test: TraceEventType convert ===\n" << std::endl;

  TraceEventType type;

  // Every in-range value round-trips.
  for (int i = 0; i < TRACE_TYPE_MAX; ++i) {
    convert(i, type);
    DFT_CHECK(type == static_cast<TraceEventType>(i));
  }

  // Out-of-range values degrade to UNKNOWN rather than producing a bogus type.
  // This is what lets an older reader survive a trace from a newer DFTracer.
  convert(static_cast<int>(TRACE_TYPE_MAX), type);
  DFT_CHECK(type == TRACE_TYPE_UNKNOWN);

  convert(999, type);
  DFT_CHECK(type == TRACE_TYPE_UNKNOWN);

  convert(-1, type);
  DFT_CHECK(type == TRACE_TYPE_UNKNOWN);

  std::cout << "✓ convert clamps to UNKNOWN\n" << std::endl;
}

// Same contract for the "ph" column.
void test_phase_wire_values_are_pinned() {
  std::cout << "=== Test: TracePhaseType wire values ===\n" << std::endl;

  DFT_CHECK(TRACE_PHASE_UNKNOWN == 0);
  DFT_CHECK(TRACE_PHASE_COMPLETE == 1);
  DFT_CHECK(TRACE_PHASE_COUNTER == 2);
  DFT_CHECK(TRACE_PHASE_AGGREGATED == 3);
  DFT_CHECK(TRACE_PHASE_METADATA == 4);
  DFT_CHECK(TRACE_PHASE_MAX == 5);

  // Counters and aggregated records are distinct; they used to share "C".
  DFT_CHECK(TRACE_PHASE_COUNTER != TRACE_PHASE_AGGREGATED);

  std::cout << "\u2713 Phase wire values pinned\n" << std::endl;
}

void test_phase_to_string_and_convert() {
  std::cout << "=== Test: TracePhaseType to_string and convert ===\n"
            << std::endl;

  DFT_CHECK(std::string(to_string(TRACE_PHASE_UNKNOWN)) == "UNKNOWN");
  DFT_CHECK(std::string(to_string(TRACE_PHASE_COMPLETE)) == "COMPLETE");
  DFT_CHECK(std::string(to_string(TRACE_PHASE_COUNTER)) == "COUNTER");
  DFT_CHECK(std::string(to_string(TRACE_PHASE_AGGREGATED)) == "AGGREGATED");
  DFT_CHECK(std::string(to_string(TRACE_PHASE_METADATA)) == "METADATA");

  std::set<std::string> names;
  for (int i = 0; i < TRACE_PHASE_MAX; ++i) {
    names.insert(to_string(static_cast<TracePhaseType>(i)));
  }
  DFT_CHECK(names.size() == static_cast<size_t>(TRACE_PHASE_MAX));

  TracePhaseType phase;
  for (int i = 0; i < TRACE_PHASE_MAX; ++i) {
    convert(i, phase);
    DFT_CHECK(phase == static_cast<TracePhaseType>(i));
  }
  convert(static_cast<int>(TRACE_PHASE_MAX), phase);
  DFT_CHECK(phase == TRACE_PHASE_UNKNOWN);
  convert(-1, phase);
  DFT_CHECK(phase == TRACE_PHASE_UNKNOWN);

  std::cout << "\u2713 Phase to_string and convert\n" << std::endl;
}

int main() {
  std::cout << "\n=== Running Trace Enum Unit Tests ===\n" << std::endl;
  try {
    test_wire_values_are_pinned();
    test_to_string_is_total_and_unique();
    test_convert_rejects_out_of_range();
    test_phase_wire_values_are_pinned();
    test_phase_to_string_and_convert();
    std::cout << "\n=== All Trace Enum Tests Passed ===\n" << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
