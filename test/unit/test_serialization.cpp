#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/common/event_draft.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/ipc/event_codec.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "check.h"
#include "event_feed.h"

using namespace dftracer;

namespace {

// The rows of one event, aggregated alone.
std::string aggregated_row(const ipc::DecodedEvent& event) {
  Aggregator aggregator(Singleton<ConfigurationManager>::get_instance());
  aggregator.aggregate(event);
  Rows rows;
  aggregator.drain(true, 1234, const_cast<char*>("test_hash_12345"), rows);
  return rows.text();
}

}  // namespace

void test_initialize_serialization() {
  std::cout << "=== Test: Initialize Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();

  char buffer[1024];
  HashType hostname_hash = const_cast<char*>("test_hash_12345");

  // Nothing is written up front: the trace is bare JSON lines, so that jq and
  // other line-oriented tools can read it without stripping a wrapper array.
  std::memset(buffer, 'x', sizeof(buffer));
  size_t size = serializer->initialize(buffer, hostname_hash);

  DFT_CHECK(size == 0);
  DFT_CHECK(buffer[0] == 'x');

  std::cout << "✓ Initialize serialization test passed\n" << std::endl;
}

void test_data_event_serialization() {
  std::cout << "=== Test: Data Event Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();

  char buffer[4096];
  size_t index = 0;

  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  serializer->initialize(buffer, hostname_hash);

  ConstEventNameType category = "posix";
  ConstEventNameType event_name = "read";
  TimeResolution start_time = 2000000;
  TimeResolution duration = 3000;
  ProcessID process_id = 1234;
  ThreadID thread_id = 1;

  EventDraft args;
  args.set("file_name", std::string("/tmp/test.dat"));
  args.set("file_size", static_cast<size_t>(1024));
  args.set("file_offset", static_cast<size_t>(0));

  Feed feed;
  size_t size = serializer->data(
      buffer,
      feed.make(category, event_name, TraceEventType::TRACE_TYPE_LIBC_IO,
                start_time, duration, process_id, thread_id, &args,
                static_cast<int>(index)));

  DFT_CHECK(size > 0);
  std::string result(buffer, size);
  DFT_CHECK(result.find("read") != std::string::npos);
  DFT_CHECK(result.find("posix") != std::string::npos);
  // The layer that produced the event is carried in "type"; "cat" is unchanged.
  DFT_CHECK(result.find("\"cat\":\"posix\",\"type\":3") != std::string::npos);

  std::cout << "✓ Data event serialization test passed\n" << std::endl;
}

void test_aggregated_serialization() {
  std::cout << "=== Test: Aggregated Data Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();

  char buffer[8192];

  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  serializer->initialize(buffer, hostname_hash);

  EventDraft args1, args2;
  args1.set("test", std::string("value1"));
  args2.set("test", std::string("value2"));

  Feed feed;
  const std::string row1 = aggregated_row(
      feed.make("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 1000000,
                5000, 1234, 1, &args1));
  DFT_CHECK(!row1.empty());
  DFT_CHECK(row1.find("read") != std::string::npos);
  DFT_CHECK(row1.find("\"test\":\"value1\"") != std::string::npos);

  const std::string row2 = aggregated_row(
      feed.make("posix", "write", TraceEventType::TRACE_TYPE_LIBC_IO, 1000000,
                2000, 1234, 1, &args2));
  DFT_CHECK(!row2.empty());
  DFT_CHECK(row2.find("write") != std::string::npos);
  DFT_CHECK(row2.find("\"test\":\"value2\"") != std::string::npos);

  std::cout << "✓ Aggregated serialization test passed\n" << std::endl;
}

void test_type_column_serialization() {
  std::cout << "=== Test: Type Column Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();
  char buffer[4096];
  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  serializer->initialize(buffer, hostname_hash);

  // A data event carries the type it was logged with, not a fixed value.
  Feed feed;
  size_t size = serializer->data(
      buffer, feed.make("POSIX", "read", TraceEventType::TRACE_TYPE_HDF5, 1000,
                        10, 1234, 1, nullptr));
  DFT_CHECK(std::string(buffer, size).find("\"type\":5") != std::string::npos);
  DFT_CHECK(std::string(buffer, size).find("\"ph\":1") != std::string::npos);

  // Counter events carry it too.
  serializer->counter(buffer, 0, "memory", "sys",
                      TraceEventType::TRACE_TYPE_PSUTIL, 1000, 1234, 1,
                      nullptr);
  {
    std::string result(buffer);
    DFT_CHECK(result.find("\"type\":7") != std::string::npos);
    DFT_CHECK(result.find("\"ph\":2") != std::string::npos);
  }

  // Metadata events are attributed to their caller rather than always being
  // tagged as internal, so metadata logged from Python reads as PYTHON.
  serializer->metadata(buffer, "epoch", "1", "CM",
                       TraceEventType::TRACE_TYPE_PYTHON, 1234, 1, false);
  {
    std::string result(buffer);
    DFT_CHECK(result.find("\"type\":6") != std::string::npos);
    DFT_CHECK(result.find("\"ph\":4") != std::string::npos);
  }

  // Internal bookkeeping stays DFTRACER.
  serializer->metadata(buffer, "corona211", "51242", "HH",
                       TraceEventType::TRACE_TYPE_DFTRACER, 1234, 1, true);
  DFT_CHECK(std::string(buffer).find("\"type\":1") != std::string::npos);

  std::cout << "\u2713 Type column serialization test passed\n" << std::endl;
}

void test_aggregated_preserves_type() {
  std::cout << "=== Test: Aggregated Preserves Type ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();
  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  serializer->initialize(nullptr, hostname_hash);

  EventDraft args;
  args.set("test", std::string("value"));
  Feed feed;
  const std::string out = aggregated_row(
      feed.make("MPIIO", "MPI_File_read", TraceEventType::TRACE_TYPE_MPI,
                1000000, 5000, 1234, 1, &args));
  DFT_CHECK(!out.empty());
  // The aggregated record keeps the type of the layer it came from, and is
  // marked AGGREGATED rather than COUNTER so the two can be told apart.
  DFT_CHECK(out.find("\"type\":10") != std::string::npos);
  DFT_CHECK(out.find("\"ph\":3") != std::string::npos);
  DFT_CHECK(out.find("\"ph\":2") == std::string::npos);

  std::cout << "\u2713 Aggregated type test passed\n" << std::endl;
}

void test_multiple_events() {
  std::cout << "=== Test: Multiple Events Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();

  char buffer[8192];
  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  size_t total_size = serializer->initialize(buffer, hostname_hash);

  // Serialize multiple data events
  for (size_t i = 0; i < 3; ++i) {
    EventDraft args;
    args.set("iteration", static_cast<size_t>(i));

    Feed feed;
    total_size += serializer->data(
        buffer + total_size,
        feed.make("posix", "open", TraceEventType::TRACE_TYPE_LIBC_IO,
                  1000000 + i * 10000, 5000, 1234, 1, &args,
                  static_cast<int>(i)));
  }

  // Verify buffer contains multiple events
  std::string result(buffer);
  DFT_CHECK(result.find("open") != std::string::npos);
  DFT_CHECK(total_size > 0);

  // No trailing bracket: every byte written is part of a JSON line.
  DFT_CHECK(buffer[total_size - 1] == '\n');

  std::cout << "✓ Multiple events serialization test passed\n" << std::endl;
}

int main() {
  std::cout << "\n=== Running Serialization Unit Tests ===\n" << std::endl;

  try {
    test_initialize_serialization();
    test_data_event_serialization();
    test_aggregated_serialization();
    test_type_column_serialization();
    test_aggregated_preserves_type();
    test_multiple_events();

    std::cout << "\n=== All Serialization Tests Passed ===\n" << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
