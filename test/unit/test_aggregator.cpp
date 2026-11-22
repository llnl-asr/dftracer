#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/aggregator/rules.h>
#include <dftracer/core/common/event_draft.h>
#include <dftracer/core/ipc/event_codec.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "event_feed.h"

using namespace dftracer;

void test_rules_parsing() {
  std::cout << "=== Test: Rules Parsing ===\n" << std::endl;

  Rules rules;

  // Test adding different types of rules - use correct field names
  rules.addRule("cat == 'posix'");     // Use 'cat' not 'category'
  rules.addRule("name LIKE 'read*'");  // Use 'name' not 'event_name'
  rules.addRule("cat == 'posix' AND name == 'read'");
  rules.addRule("dur > 1000");

  std::cout << "✓ Rules parsing test passed\n" << std::endl;
}

void test_rules_evaluation() {
  std::cout << "=== Test: Rules Evaluation ===\n" << std::endl;

  Rules inclusion_rules;
  inclusion_rules.addRule("cat == 'posix'");  // Use 'cat' not 'category'

  Metadata metadata;
  metadata.insert_or_assign("test_key", std::string("test_value"));

  // Create AggregatedKey for testing
  AggregatedKey key("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 0, 0,
                    0, &metadata, nullptr, nullptr);

  // Test if rules match
  bool matches = inclusion_rules.satisfies(&key);
  DFT_CHECK(matches == true);

  // Test with different category
  AggregatedKey key2("stdio", "write", TraceEventType::TRACE_TYPE_LIBC_IO, 0, 0,
                     0, &metadata, nullptr, nullptr);
  bool matches2 = inclusion_rules.satisfies(&key2);
  DFT_CHECK(matches2 == false);

  auto check = [&](const char* rule, const AggregatedKey& k, bool expected) {
    Rules r;
    r.addRule(rule);
    DFT_CHECK(r.satisfies(&k) == expected);
  };
  check("name != 'read'", key, false);
  check("name != 'read'", key2, true);
  check("cat IN {'stdio','posix'}", key, true);
  check("cat IN {'stdio','mpi'}", key, false);
  check("name LIKE '*ea*'", key, true);
  check("name LIKE '*rit'", key2, false);
  check("cat == 5", key, false);
  check("tags.test_key == 'test_value'", key, true);
  check("tags.test_key LIKE 'test*'", key, true);
  check("tags.missing == ''", key, true);
  metadata.insert_or_assign("size", 4096);
  check("tags.size LIKE '4096'", key, true);
  AggregatedKey long_key("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 0,
                         1ULL << 32, 0, nullptr, nullptr, nullptr);
  check("dur < 100", long_key, false);
  check("dur > 100", long_key, true);

  std::cout << "✓ Rules evaluation test passed\n" << std::endl;
}

void test_like_pattern_matching() {
  std::cout << "=== Test: LIKE Pattern Matching ===\n" << std::endl;

  Rules rules;
  rules.addRule("cat LIKE 'pos*'");  // Use 'cat' not 'category'

  Metadata metadata;
  metadata.insert_or_assign("test_key", std::string("test_value"));

  AggregatedKey key("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 0, 0,
                    0, &metadata, nullptr, nullptr);

  bool matches = rules.satisfies(&key);
  DFT_CHECK(matches == true);

  AggregatedKey key2("stdio", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 0, 0,
                     0, &metadata, nullptr, nullptr);
  bool matches2 = rules.satisfies(&key2);
  DFT_CHECK(matches2 == false);

  std::cout << "✓ LIKE pattern matching test passed\n" << std::endl;
}

namespace {

constexpr ProcessID kPid = 1234;
char kHostname[] = "test_hash_12345";

// An Aggregator's configuration is read from the environment when its
// ConfigurationManager is built, so each test builds its own. Rules come from
// the aggregation file in a real run; here they are set on the configuration.
std::shared_ptr<ConfigurationManager> make_config(
    const char* type, const char* interval_ms, const char* inclusion = nullptr,
    const char* exclusion = nullptr) {
  setenv("DFTRACER_ENABLE", "1", 1);
  setenv("DFTRACER_ENABLE_AGGREGATION", "1", 1);
  setenv("DFTRACER_AGGREGATION_TYPE", type, 1);
  setenv("DFTRACER_TRACE_INTERVAL_MS", interval_ms, 1);
  auto config = std::make_shared<ConfigurationManager>();
  if (inclusion != nullptr)
    config->aggregation_inclusion_rules.push_back(inclusion);
  if (exclusion != nullptr)
    config->aggregation_exclusion_rules.push_back(exclusion);
  return config;
}

struct AggFeed {
  const ipc::DecodedEvent& make(
      const char* category, const char* name, TimeResolution ts,
      TimeResolution dur, ThreadID tid, const EventDraft& args,
      TraceEventType type = TraceEventType::TRACE_TYPE_LIBC_IO) {
    return feed.make(category, name, type, ts, dur, kPid, tid, &args);
  }
  Feed feed;
};

std::vector<std::string> drain(Aggregator& aggregator, bool all) {
  Rows rows;
  aggregator.drain(all, kPid, kHostname, rows);
  return rows.lines();
}

bool has(const std::string& line, const std::string& part) {
  return line.find(part) != std::string::npos;
}

// The one line that has every part.
const std::string& row(const std::vector<std::string>& lines,
                       const std::vector<std::string>& parts) {
  const std::string* found = nullptr;
  for (const auto& line : lines) {
    bool all = true;
    for (const auto& part : parts) all = all && has(line, part);
    if (!all) continue;
    DFT_CHECK(found == nullptr);
    found = &line;
  }
  DFT_CHECK(found != nullptr);
  return *found;
}

}  // namespace

void test_aggregator_basic() {
  std::cout << "=== Test: Aggregator Basic Functionality ===\n" << std::endl;

  Aggregator aggregator(make_config("FULL", "1000"));
  AggFeed feed;
  EventDraft args;
  args.set("test", std::string("value"));
  const auto& event = feed.make("posix", "read", 1000000, 5000, 1, args);

  // In FULL mode, all events should be aggregated
  int rank = 0;
  DFT_CHECK(aggregator.should_aggregate(event, "app", &rank));
  // The first event opens the first interval: nothing is complete yet.
  DFT_CHECK(!aggregator.aggregate(event));

  const auto lines = drain(aggregator, true);
  DFT_CHECK(lines.size() == 1);
  DFT_CHECK(has(lines[0], "\"name\":\"read\",\"cat\":\"posix\""));
  DFT_CHECK(has(lines[0], "\"ph\":3"));
  DFT_CHECK(has(lines[0], "\"pid\":1234,\"tid\":1"));
  DFT_CHECK(has(lines[0], "\"ts\":1000000"));
  DFT_CHECK(has(lines[0], "\"test\":\"value\""));
  DFT_CHECK(has(lines[0], "\"dft_cnt\":1"));
  DFT_CHECK(has(lines[0], "\"dur\":5000"));
  DFT_CHECK(drain(aggregator, true).empty());

  std::cout << "✓ Aggregator basic test passed\n" << std::endl;
}

void test_aggregator_selective() {
  std::cout << "=== Test: Aggregator Selective Mode ===\n" << std::endl;

  Aggregator aggregator(make_config("SELECTIVE", "1000", "cat == 'posix'"));
  AggFeed feed;
  EventDraft args;
  args.set("test", std::string("value"));
  int rank = 0;

  // Matches the inclusion rule
  DFT_CHECK(aggregator.should_aggregate(
      feed.make("posix", "read", 1000000, 5000, 1, args), "app", &rank));
  // Does not match it
  DFT_CHECK(!aggregator.should_aggregate(
      feed.make("stdio", "write", 1000000, 3000, 1, args), "app", &rank));

  std::cout << "✓ Aggregator selective mode test passed\n" << std::endl;
}

void test_aggregator_with_metadata() {
  std::cout << "=== Test: Aggregator with Additional Metadata ===\n"
            << std::endl;

  Aggregator aggregator(make_config("FULL", "1000"));
  AggFeed feed;
  const ThreadID tid = 12345;

  EventDraft first;
  first.set("bytes_read", static_cast<int64_t>(1024), MetadataType::MT_VALUE);
  first.set("io_count", static_cast<int>(5), MetadataType::MT_VALUE);
  first.set("offset", static_cast<size_t>(2048), MetadataType::MT_VALUE);
  first.set("error_code", static_cast<int>(0), MetadataType::MT_VALUE);
  aggregator.aggregate(feed.make("posix", "read", 1500000, 5000, tid, first));

  // Another event in the same interval
  EventDraft second;
  second.set("bytes_read", static_cast<int64_t>(2048), MetadataType::MT_VALUE);
  second.set("io_count", static_cast<int>(3), MetadataType::MT_VALUE);
  DFT_CHECK(!aggregator.aggregate(
      feed.make("posix", "read", 1500500, 3000, tid, second)));

  const auto lines = drain(aggregator, true);
  DFT_CHECK(lines.size() == 1);
  const std::string& line = lines[0];
  DFT_CHECK(has(line, "\"ts\":1000000"));
  DFT_CHECK(has(line, "\"dft_cnt\":2"));
  DFT_CHECK(has(line, "\"dur_sum\":8000"));
  DFT_CHECK(has(line, "\"dur_min\":3000") && has(line, "\"dur_max\":5000"));
  DFT_CHECK(has(line, "\"bytes_read_sum\":3072"));
  DFT_CHECK(has(line, "\"bytes_read_min\":1024"));
  DFT_CHECK(has(line, "\"bytes_read_max\":2048"));
  DFT_CHECK(has(line, "\"io_count_sum\":8"));
  DFT_CHECK(has(line, "\"io_count_min\":3") && has(line, "\"io_count_max\":5"));
  // Seen once: written as itself
  DFT_CHECK(has(line, "\"offset\":2048"));
  DFT_CHECK(has(line, "\"error_code\":0"));

  std::cout << "✓ Aggregator with metadata test passed\n" << std::endl;
}

void test_aggregator_exclusion_rules() {
  std::cout << "=== Test: Aggregator with Exclusion Rules ===\n" << std::endl;

  Aggregator aggregator(
      make_config("SELECTIVE", "1000", "cat == 'posix'", "name == 'stat'"));
  AggFeed feed;
  EventDraft args;
  args.set("test", std::string("value"));
  int rank = 0;

  // Matches inclusion, not excluded
  DFT_CHECK(aggregator.should_aggregate(
      feed.make("posix", "read", 1000000, 5000, 1, args), "app", &rank));
  // Matches inclusion but also matches exclusion
  DFT_CHECK(!aggregator.should_aggregate(
      feed.make("posix", "stat", 1000000, 3000, 1, args), "app", &rank));
  // Matches neither
  DFT_CHECK(!aggregator.should_aggregate(
      feed.make("stdio", "read", 1000000, 3000, 1, args), "app", &rank));

  std::cout << "✓ Aggregator exclusion rules test passed\n" << std::endl;
}

void test_aggregator_time_intervals() {
  std::cout << "=== Test: Aggregator Time Interval Processing ===\n"
            << std::endl;

  Aggregator aggregator(make_config("FULL", "500"));  // 500ms intervals
  AggFeed feed;
  EventDraft args;
  args.set("file", std::string("/tmp/test.txt"));

  // Events in different time intervals
  DFT_CHECK(!aggregator.aggregate(
      feed.make("posix", "read", 500000, 1000, 1, args)));  // 500ms
  DFT_CHECK(aggregator.aggregate(
      feed.make("posix", "read", 1200000, 2000, 1, args)));  // 1200ms
  DFT_CHECK(aggregator.aggregate(
      feed.make("posix", "write", 1700000, 3000, 1, args)));  // 1700ms
  DFT_CHECK(aggregator.aggregate(
      feed.make("posix", "read", 2500000, 1500, 1, args)));  // 2500ms
  // An event in an interval already open does not open a later one.
  DFT_CHECK(!aggregator.aggregate(
      feed.make("posix", "read", 2600000, 1500, 1, args)));

  // Only the previous intervals, not the current one
  const auto partial = drain(aggregator, false);
  DFT_CHECK(partial.size() == 3);
  DFT_CHECK(has(row(partial, {"\"name\":\"read\"", "\"ts\":500000,"}),
                "\"dur\":1000"));
  DFT_CHECK(has(row(partial, {"\"name\":\"read\"", "\"ts\":1000000,"}),
                "\"dur\":2000"));
  DFT_CHECK(has(row(partial, {"\"name\":\"write\"", "\"ts\":1500000,"}),
                "\"dur\":3000"));

  // Then all that remains
  const auto rest = drain(aggregator, true);
  DFT_CHECK(rest.size() == 1);
  DFT_CHECK(has(rest[0], "\"ts\":2500000,"));
  DFT_CHECK(has(rest[0], "\"dft_cnt\":2"));
  DFT_CHECK(has(rest[0], "\"dur_sum\":3000"));
  DFT_CHECK(drain(aggregator, true).empty());

  std::cout << "✓ Aggregator time interval test passed\n" << std::endl;
}

void test_aggregator_finalize() {
  std::cout << "=== Test: Aggregator Finalize ===\n" << std::endl;

  AggFeed feed;
  EventDraft args;
  args.set("count", static_cast<int>(42));
  {
    // Destroyed with events in it: nothing leaks.
    Aggregator aggregator(make_config("FULL", "1000"));
    aggregator.aggregate(feed.make("posix", "read", 1000000, 5000, 1, args));
  }

  // Everything is written at the end, with the rows of the open interval.
  Aggregator aggregator(make_config("FULL", "1000"));
  aggregator.aggregate(feed.make("posix", "read", 1000000, 5000, 1, args));
  DFT_CHECK(drain(aggregator, false).empty());
  const auto lines = drain(aggregator, true);
  DFT_CHECK(lines.size() == 1);
  DFT_CHECK(has(lines[0], "\"count\":42"));
  DFT_CHECK(drain(aggregator, true).empty());

  std::cout << "✓ Aggregator finalize test passed\n" << std::endl;
}

void test_aggregator_multiple_threads() {
  std::cout << "=== Test: Aggregator with Multiple Thread IDs ===\n"
            << std::endl;

  Aggregator aggregator(make_config("FULL", "1000"));
  AggFeed feed;
  EventDraft args;
  args.set("file", std::string("/data/file.bin"));

  aggregator.aggregate(feed.make("posix", "read", 1000000, 5000, 100, args));
  aggregator.aggregate(feed.make("posix", "read", 1000500, 3000, 200, args));
  aggregator.aggregate(feed.make("posix", "write", 1001000, 7000, 100, args));
  aggregator.aggregate(feed.make("posix", "write", 1001500, 4000, 300, args));
  // The same thread and name as the first: one bucket
  aggregator.aggregate(feed.make("posix", "read", 1002000, 1000, 100, args));

  const auto lines = drain(aggregator, true);
  DFT_CHECK(lines.size() == 4);
  const auto& r100 = row(lines, {"\"name\":\"read\"", "\"tid\":100"});
  DFT_CHECK(has(r100, "\"dft_cnt\":2") && has(r100, "\"dur_sum\":6000"));
  const auto& r200 = row(lines, {"\"name\":\"read\"", "\"tid\":200"});
  DFT_CHECK(has(r200, "\"dft_cnt\":1") && has(r200, "\"dur\":3000"));
  const auto& w100 = row(lines, {"\"name\":\"write\"", "\"tid\":100"});
  DFT_CHECK(has(w100, "\"dft_cnt\":1") && has(w100, "\"dur\":7000"));
  const auto& w300 = row(lines, {"\"name\":\"write\"", "\"tid\":300"});
  DFT_CHECK(has(w300, "\"dft_cnt\":1") && has(w300, "\"dur\":4000"));

  std::cout << "✓ Aggregator multiple threads test passed\n" << std::endl;
}

// MT_KEY fields are part of the aggregation key: events that differ in one are
// aggregated apart, and each bucket keeps its MT_KEY fields (not its values)
// for the aggregated record. MT_VALUE fields are folded into the bucket.
void test_aggregator_mt_keys() {
  std::cout << "=== Test: Aggregator MT_KEY Fields Split Buckets ===\n"
            << std::endl;

  Aggregator aggregator(make_config("FULL", "1000"));
  AggFeed feed;
  auto log = [&](const char* name, const char* fhash, int64_t size,
                 TimeResolution ts) {
    EventDraft args;
    if (fhash != nullptr)
      args.set("fhash", std::string(fhash), MetadataType::MT_KEY);
    args.set("size", size, MetadataType::MT_VALUE);
    aggregator.aggregate(feed.make("posix", name, ts, 10, 7, args));
  };
  const TimeResolution t = 7000000;
  log("read", "111", 100, t);
  log("read", "222", 200, t + 1);
  log("read", "111", 300, t + 2);
  log("write", nullptr, 5, t + 3);
  log("write", nullptr, 6, t + 4);

  const auto lines = drain(aggregator, true);
  DFT_CHECK(lines.size() == 3);
  const auto& r111 = row(lines, {"\"name\":\"read\"", "\"fhash\":\"111\""});
  DFT_CHECK(has(r111, "\"dft_cnt\":2") && has(r111, "\"size_sum\":400") &&
            has(r111, "\"size_min\":100") && has(r111, "\"size_max\":300"));
  DFT_CHECK(has(r111, "\"dur_sum\":20"));
  const auto& r222 = row(lines, {"\"name\":\"read\"", "\"fhash\":\"222\""});
  DFT_CHECK(has(r222, "\"dft_cnt\":1") && has(r222, "\"size\":200"));
  const auto& write = row(lines, {"\"name\":\"write\""});
  DFT_CHECK(!has(write, "fhash"));
  DFT_CHECK(has(write, "\"dft_cnt\":2") && has(write, "\"size_sum\":11") &&
            has(write, "\"size_min\":5") && has(write, "\"size_max\":6"));
  // The values are folded, never keys
  DFT_CHECK(!has(r111, "\"size\":"));

  std::cout << "✓ Aggregator MT_KEY test passed\n" << std::endl;
}

int main() {
  std::cout << "\n=== Running Aggregator Unit Tests ===\n" << std::endl;

  try {
    test_rules_parsing();
    test_rules_evaluation();
    test_like_pattern_matching();
    test_aggregator_basic();
    test_aggregator_selective();
    test_aggregator_with_metadata();
    test_aggregator_exclusion_rules();
    test_aggregator_time_intervals();
    test_aggregator_finalize();
    test_aggregator_multiple_threads();
    test_aggregator_mt_keys();

    std::cout << "\n=== All Aggregator Tests Passed ===\n" << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
