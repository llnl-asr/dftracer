#include <dftracer/service/telemetry/variorum_collector.h>

#include <iostream>
#include <string>
#include <vector>

#include "check.h"

using dftracer::VariorumPowerSample;
using dftracer::VariorumTelemetryCollector;

namespace {

const VariorumPowerSample* find_sample(
    const std::vector<VariorumPowerSample>& samples, const std::string& name) {
  for (const auto& sample : samples) {
    if (sample.metric_name == name) return &sample;
  }
  return nullptr;
}

// The shape variorum_get_power_json() returns on a two-socket node with GPUs:
// one object per host, then one per socket, with the node total alongside them.
const char* kTwoSocketPower =
    "{\"tuolumne1002\": {"
    "\"timestamp\": 1755043200, "
    "\"power_node_watts\": 1024.5, "
    "\"socket_0\": {"
    "\"power_cpu_watts\": 105.25, "
    "\"power_mem_watts\": 22.5, "
    "\"power_gpu_watts\": 384.0"
    "}, "
    "\"socket_1\": {"
    "\"power_cpu_watts\": 98.75, "
    "\"power_mem_watts\": 21.0, "
    "\"power_gpu_watts\": 393.0"
    "}"
    "}}";

void test_flattens_nested_power() {
  std::cout << "Testing variorum power JSON flattening..." << std::endl;
  auto samples = VariorumTelemetryCollector::parse_power_json(kTwoSocketPower);

  // Node total plus three domains on each of two sockets. The timestamp is not
  // one of them: the counter record carries the trace's own.
  DFT_CHECK(samples.size() == 7);
  DFT_CHECK(find_sample(samples, "timestamp") == nullptr);

  const auto* node = find_sample(samples, "power_node_watts");
  DFT_CHECK(node != nullptr);
  DFT_CHECK(node->metric_value == 1024.5);

  // Nesting becomes a dotted name, so each socket's domains stay distinct.
  const auto* cpu0 = find_sample(samples, "socket_0.power_cpu_watts");
  DFT_CHECK(cpu0 != nullptr);
  DFT_CHECK(cpu0->metric_value == 105.25);

  const auto* gpu1 = find_sample(samples, "socket_1.power_gpu_watts");
  DFT_CHECK(gpu1 != nullptr);
  DFT_CHECK(gpu1->metric_value == 393.0);

  // The hostname wrapper is dropped: the trace is already per node.
  for (const auto& sample : samples) {
    DFT_CHECK(sample.metric_name.find("tuolumne1002") == std::string::npos);
  }

  std::cout << "✓ variorum power JSON flattening passed" << std::endl;
}

// Captured verbatim from variorum_get_power_json() on an MI300A node
// (tuolumne, variorum v0.8.0 built with VARIORUM_WITH_AMD_GPU). Worth pinning
// as it is: the per-device object under power_gpu_watts is a level deeper than
// the documented format, and the readings are what a real APU package draws.
const char* kMI300APower =
    "{\n"
    "    \"tuolumne1002\": {\n"
    "        \"timestamp\": 1786640700079974,\n"
    "        \"num_gpus_per_socket\": 1,\n"
    "        \"socket_0\": {\n"
    "            \"power_gpu_watts\": {\n"
    "                \"GPU_0\": 134.0\n"
    "            }\n"
    "        },\n"
    "        \"socket_1\": {\n"
    "            \"power_gpu_watts\": {\n"
    "                \"GPU_1\": 134.0\n"
    "            }\n"
    "        },\n"
    "        \"socket_2\": {\n"
    "            \"power_gpu_watts\": {\n"
    "                \"GPU_2\": 131.0\n"
    "            }\n"
    "        },\n"
    "        \"socket_3\": {\n"
    "            \"power_gpu_watts\": {\n"
    "                \"GPU_3\": 131.0\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}";

void test_real_mi300a_payload() {
  std::cout << "Testing variorum MI300A payload..." << std::endl;
  auto samples = VariorumTelemetryCollector::parse_power_json(kMI300APower);

  // One reading per socket, plus the GPU count. The timestamp is dropped.
  DFT_CHECK(samples.size() == 5);
  DFT_CHECK(find_sample(samples, "timestamp") == nullptr);

  const auto* gpu0 = find_sample(samples, "socket_0.power_gpu_watts.GPU_0");
  DFT_CHECK(gpu0 != nullptr);
  DFT_CHECK(gpu0->metric_value == 134.0);

  const auto* gpu3 = find_sample(samples, "socket_3.power_gpu_watts.GPU_3");
  DFT_CHECK(gpu3 != nullptr);
  DFT_CHECK(gpu3->metric_value == 131.0);

  // Whitespace and newlines in variorum's pretty-printed output are not part
  // of any name.
  const auto* count = find_sample(samples, "num_gpus_per_socket");
  DFT_CHECK(count != nullptr);
  DFT_CHECK(count->metric_value == 1.0);

  std::cout << "✓ variorum MI300A payload passed" << std::endl;
}

void test_keeps_names_without_a_host_wrapper() {
  std::cout << "Testing variorum payload with no host wrapper..." << std::endl;
  // Two top-level entries, so no single wrapper to strip and the full paths
  // are kept rather than a level being silently lost.
  auto samples = VariorumTelemetryCollector::parse_power_json(
      "{\"nodeA\":{\"power_node_watts\":10.0},"
      "\"nodeB\":{\"power_node_watts\":20.0}}");

  DFT_CHECK(samples.size() == 2);
  DFT_CHECK(find_sample(samples, "nodeA.power_node_watts") != nullptr);
  DFT_CHECK(find_sample(samples, "nodeB.power_node_watts") != nullptr);

  // A flat payload needs no stripping at all.
  auto flat =
      VariorumTelemetryCollector::parse_power_json("{\"power_node_watts\":5}");
  DFT_CHECK(flat.size() == 1);
  DFT_CHECK(flat[0].metric_name == "power_node_watts");
  DFT_CHECK(flat[0].metric_value == 5.0);

  std::cout << "✓ variorum payload with no host wrapper passed" << std::endl;
}

void test_skips_non_numeric_leaves() {
  std::cout << "Testing variorum non-numeric leaves..." << std::endl;
  // A counter value has to be a number, so strings, booleans and nulls are
  // parsed to keep the walk in step and then dropped.
  auto samples = VariorumTelemetryCollector::parse_power_json(
      "{\"host\":{"
      "\"hostname\":\"tuolumne1002\","
      "\"capped\":true,"
      "\"power_gpu_watts\":null,"
      "\"power_node_watts\":42.5"
      "}}");

  DFT_CHECK(samples.size() == 1);
  DFT_CHECK(samples[0].metric_name == "power_node_watts");
  DFT_CHECK(samples[0].metric_value == 42.5);

  std::cout << "✓ variorum non-numeric leaves passed" << std::endl;
}

void test_number_forms() {
  std::cout << "Testing variorum number forms..." << std::endl;
  auto samples = VariorumTelemetryCollector::parse_power_json(
      "{\"h\":{\"a\":-1.5,\"b\":2e3,\"c\":0,\"d\":1.0e-2}}");

  DFT_CHECK(samples.size() == 4);
  DFT_CHECK(find_sample(samples, "a")->metric_value == -1.5);
  DFT_CHECK(find_sample(samples, "b")->metric_value == 2000.0);
  DFT_CHECK(find_sample(samples, "c")->metric_value == 0.0);
  DFT_CHECK(find_sample(samples, "d")->metric_value == 0.01);

  std::cout << "✓ variorum number forms passed" << std::endl;
}

void test_arrays_are_indexed() {
  std::cout << "Testing variorum array handling..." << std::endl;
  auto samples = VariorumTelemetryCollector::parse_power_json(
      "{\"h\":{\"gpus\":[10.0,20.0,30.0]}}");

  DFT_CHECK(samples.size() == 3);
  DFT_CHECK(find_sample(samples, "gpus.0")->metric_value == 10.0);
  DFT_CHECK(find_sample(samples, "gpus.2")->metric_value == 30.0);

  std::cout << "✓ variorum array handling passed" << std::endl;
}

void test_rejects_malformed_payloads() {
  std::cout << "Testing variorum malformed payloads..." << std::endl;
  // Half a reading looks like a whole one once it is in the trace, so a
  // payload that does not parse end to end yields nothing at all.
  DFT_CHECK(VariorumTelemetryCollector::parse_power_json(
                "{\"h\":{\"power_node_watts\":1.0")
                .empty());
  DFT_CHECK(VariorumTelemetryCollector::parse_power_json("{\"h\":}").empty());
  DFT_CHECK(VariorumTelemetryCollector::parse_power_json("not json").empty());
  DFT_CHECK(VariorumTelemetryCollector::parse_power_json("{\"a\":1}{\"b\":2}")
                .empty());
  // NaN and Infinity are not JSON, and strtod would otherwise accept them.
  DFT_CHECK(
      VariorumTelemetryCollector::parse_power_json("{\"a\":NaN}").empty());
  DFT_CHECK(
      VariorumTelemetryCollector::parse_power_json("{\"a\":Infinity}").empty());

  std::cout << "✓ variorum malformed payloads passed" << std::endl;
}

void test_empty_payload() {
  std::cout << "Testing variorum empty payload..." << std::endl;
  DFT_CHECK(VariorumTelemetryCollector::parse_power_json("").empty());
  DFT_CHECK(VariorumTelemetryCollector::parse_power_json("{}").empty());
  DFT_CHECK(VariorumTelemetryCollector::parse_power_json("{\"h\":{}}").empty());
  std::cout << "✓ variorum empty payload passed" << std::endl;
}

const dftracer::VariorumPowerFamily* find_family(
    const std::vector<dftracer::VariorumPowerFamily>& families,
    const std::string& name) {
  for (const auto& family : families) {
    if (family.family == name) return &family;
  }
  return nullptr;
}

double reading_of(const dftracer::VariorumPowerFamily& family,
                  const std::string& name) {
  for (const auto& reading : family.readings) {
    if (reading.first == name) return reading.second;
  }
  DFT_CHECK(false);
  return 0.0;
}

void test_groups_by_family() {
  std::cout << "Testing variorum family grouping..." << std::endl;
  auto families = VariorumTelemetryCollector::group_by_family(
      VariorumTelemetryCollector::parse_power_json(kTwoSocketPower));

  // One family per kind of thing measured, not one per socket.
  DFT_CHECK(families.size() == 4);

  const auto* node = find_family(families, "node");
  DFT_CHECK(node != nullptr);
  // Nothing distinguishes a node-wide reading within its own family.
  DFT_CHECK(node->readings.size() == 1);
  DFT_CHECK(reading_of(*node, "total") == 1024.5);

  // Both sockets land in the same cpu record, keyed by what tells them apart.
  const auto* cpu = find_family(families, "cpu");
  DFT_CHECK(cpu != nullptr);
  DFT_CHECK(cpu->readings.size() == 2);
  DFT_CHECK(reading_of(*cpu, "socket_0") == 105.25);
  DFT_CHECK(reading_of(*cpu, "socket_1") == 98.75);

  // variorum's abbreviations are spelled out.
  const auto* memory = find_family(families, "memory");
  DFT_CHECK(memory != nullptr);
  DFT_CHECK(reading_of(*memory, "socket_0") == 22.5);

  const auto* gpu = find_family(families, "gpu");
  DFT_CHECK(gpu != nullptr);
  DFT_CHECK(reading_of(*gpu, "socket_1") == 393.0);

  // "power_<family>_watts" is not repeated inside the family that it named.
  for (const auto& family : families) {
    for (const auto& reading : family.readings) {
      DFT_CHECK(reading.first.find("power_") == std::string::npos);
    }
  }

  std::cout << "✓ variorum family grouping passed" << std::endl;
}

void test_groups_mi300a_payload() {
  std::cout << "Testing variorum MI300A family grouping..." << std::endl;
  auto families = VariorumTelemetryCollector::group_by_family(
      VariorumTelemetryCollector::parse_power_json(kMI300APower));

  // Everything this payload reports is about the GPUs, so it is one record:
  // the four device readings plus the topology count that describes them.
  DFT_CHECK(families.size() == 1);

  const auto* gpu = find_family(families, "gpu");
  DFT_CHECK(gpu != nullptr);
  DFT_CHECK(gpu->readings.size() == 5);
  DFT_CHECK(reading_of(*gpu, "socket_0.GPU_0") == 134.0);
  DFT_CHECK(reading_of(*gpu, "socket_3.GPU_3") == 131.0);
  // Named for the device it counts, so it belongs with it rather than in a
  // bucket of its own. Its full key is kept: no segment was consumed to find
  // the family, so trimming one would lose what the number means.
  DFT_CHECK(reading_of(*gpu, "num_gpus_per_socket") == 1.0);

  std::cout << "✓ variorum MI300A family grouping passed" << std::endl;
}

void test_family_edge_cases() {
  std::cout << "Testing variorum family edge cases..." << std::endl;
  DFT_CHECK(VariorumTelemetryCollector::group_by_family({}).empty());

  // A family variorum grows later needs no change here: it is read out of the
  // key rather than matched against a list.
  auto future = VariorumTelemetryCollector::group_by_family(
      {{"socket_0.power_accelerator_watts", 7.5}});
  DFT_CHECK(future.size() == 1);
  DFT_CHECK(future[0].family == "accelerator");
  DFT_CHECK(reading_of(future[0], "socket_0") == 7.5);

  // A key that only looks like the watts pattern still lands in the family it
  // names, and one that names nothing at all falls through to "other".
  auto mixed = VariorumTelemetryCollector::group_by_family(
      {{"power_cpu", 2.0}, {"watts", 3.0}});
  DFT_CHECK(mixed.size() == 2);
  DFT_CHECK(find_family(mixed, "cpu") != nullptr);
  DFT_CHECK(reading_of(*find_family(mixed, "cpu"), "power_cpu") == 2.0);
  DFT_CHECK(reading_of(*find_family(mixed, "other"), "watts") == 3.0);

  // Plural and abbreviated spellings fold into one family rather than
  // splitting the devices they describe across several records.
  auto plurals = VariorumTelemetryCollector::group_by_family(
      {{"socket_0.power_mem_watts", 1.0},
       {"num_memory_devices", 2.0},
       {"socket_0.power_gpu_watts.GPU_0", 3.0},
       {"num_gpus_per_socket", 4.0}});
  DFT_CHECK(plurals.size() == 2);
  DFT_CHECK(find_family(plurals, "memory")->readings.size() == 2);
  DFT_CHECK(find_family(plurals, "gpu")->readings.size() == 2);

  std::cout << "✓ variorum family edge cases passed" << std::endl;
}

void test_placement_is_stable_across_samples() {
  std::cout << "Testing variorum placement stability..." << std::endl;
  // capture() groups through a table built once in initialize();
  // group_by_family applies the same rules directly. The two must agree, or a
  // trace would be grouped differently from what the tests above pin down.
  // Re-placing the same keys must also be idempotent: a second reading must not
  // invent a family.
  const auto samples =
      VariorumTelemetryCollector::parse_power_json(kTwoSocketPower);

  auto first = VariorumTelemetryCollector::group_by_family(samples);
  auto second = VariorumTelemetryCollector::group_by_family(samples);
  DFT_CHECK(first.size() == second.size());
  for (size_t index = 0; index < first.size(); index++) {
    DFT_CHECK(first[index].family == second[index].family);
    DFT_CHECK(first[index].readings.size() == second[index].readings.size());
    for (size_t reading = 0; reading < first[index].readings.size();
         reading++) {
      DFT_CHECK(first[index].readings[reading].first ==
                second[index].readings[reading].first);
      DFT_CHECK(first[index].readings[reading].second ==
                second[index].readings[reading].second);
    }
  }

  // Every reading reaches exactly one family, so nothing is lost or counted
  // twice by the grouping.
  size_t placed = 0;
  for (const auto& family : first) placed += family.readings.size();
  DFT_CHECK(placed == samples.size());

  std::cout << "✓ variorum placement stability passed" << std::endl;
}

void test_collector_is_registered() {
  std::cout << "Testing variorum collector identity..." << std::endl;
  VariorumTelemetryCollector collector;
  DFT_CHECK(collector.name() == "variorum");
  // A build with no variorum, or a node with no readable power domain, must be
  // a no-op rather than an error: initialize/capture/finalize still run.
  collector.initialize();
  collector.finalize();
  std::cout << "✓ variorum collector identity passed" << std::endl;
}

}  // namespace

int main() {
  std::cout << "=== Running Variorum Unit Tests ===" << std::endl;
  try {
    test_flattens_nested_power();
    test_real_mi300a_payload();
    test_keeps_names_without_a_host_wrapper();
    test_skips_non_numeric_leaves();
    test_number_forms();
    test_arrays_are_indexed();
    test_rejects_malformed_payloads();
    test_empty_payload();
    test_groups_by_family();
    test_groups_mi300a_payload();
    test_family_edge_cases();
    test_placement_is_stable_across_samples();
    test_collector_is_registered();
    std::cout << "\n✓ All Variorum tests passed!" << std::endl;
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Variorum test failed: " << ex.what() << std::endl;
    return 1;
  }
}
