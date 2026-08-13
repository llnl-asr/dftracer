#include <dftracer/service/telemetry/omnistat_collector.h>

#include <iostream>
#include <string>
#include <vector>

#include "check.h"

using dftracer::OmnistatMetricSample;
using dftracer::OmnistatTelemetryCollector;

namespace {

const OmnistatMetricSample* find_sample(
    const std::vector<OmnistatMetricSample>& samples, const std::string& name) {
  for (const auto& sample : samples) {
    if (sample.metric_name == name) return &sample;
  }
  return nullptr;
}

// A trimmed but otherwise faithful slice of what omnistat's exporter serves on
// http://127.0.0.1:8001/metrics.
const char* kExposition =
    "# HELP rocm_gpu_utilization_percentage GPU use (%)\n"
    "# TYPE rocm_gpu_utilization_percentage gauge\n"
    "rocm_gpu_utilization_percentage{card=\"0\"} 85.5\n"
    "rocm_gpu_utilization_percentage{card=\"1\"} 79\n"
    "# HELP rocm_average_socket_power_watts Average power (W)\n"
    "rocm_average_socket_power_watts{card=\"0\"} 420.1\n"
    "rocm_vram_used_percentage{card=\"0\",location=\"HBM\"} 12.25\n"
    "no_labels_metric 7\n";

void test_parses_metrics_and_labels() {
  std::cout << "Testing omnistat exposition parsing..." << std::endl;
  auto samples = OmnistatTelemetryCollector::parse_exposition(kExposition);

  // Five metric lines; HELP/TYPE comments must not become samples.
  DFT_CHECK(samples.size() == 5);

  const auto* util = find_sample(samples, "rocm_gpu_utilization_percentage");
  DFT_CHECK(util != nullptr);
  DFT_CHECK(util->metric_value == 85.5);
  DFT_CHECK(util->labels == "card=\"0\"");

  const auto* power = find_sample(samples, "rocm_average_socket_power_watts");
  DFT_CHECK(power != nullptr);
  DFT_CHECK(power->metric_value == 420.1);

  // Several labels are kept verbatim, braces stripped.
  const auto* vram = find_sample(samples, "rocm_vram_used_percentage");
  DFT_CHECK(vram != nullptr);
  DFT_CHECK(vram->labels == "card=\"0\",location=\"HBM\"");

  // A metric without labels is still a sample.
  const auto* plain = find_sample(samples, "no_labels_metric");
  DFT_CHECK(plain != nullptr);
  DFT_CHECK(plain->metric_value == 7.0);
  DFT_CHECK(plain->labels.empty());

  std::cout << "✓ omnistat exposition parsing passed" << std::endl;
}

void test_every_gpu_reported_separately() {
  std::cout << "Testing omnistat per-GPU samples..." << std::endl;
  auto samples = OmnistatTelemetryCollector::parse_exposition(kExposition);

  // The same metric appears once per card and both readings survive: a node has
  // several GPUs and each is its own series.
  int utilization_samples = 0;
  for (const auto& sample : samples) {
    if (sample.metric_name == "rocm_gpu_utilization_percentage") {
      utilization_samples++;
    }
  }
  DFT_CHECK(utilization_samples == 2);
  std::cout << "✓ omnistat per-GPU samples passed" << std::endl;
}

void test_rejects_malformed_lines() {
  std::cout << "Testing omnistat malformed line handling..." << std::endl;
  auto samples = OmnistatTelemetryCollector::parse_exposition(
      "# comment only\n"
      "good_metric 1.5\n"
      "bad_value_metric not_a_number\n"
      "no_value_metric\n"
      "\n"
      "trailing_ws_metric{card=\"0\"} 2.5\r\n");

  // Only the two well-formed lines: a non-numeric value and a line with no
  // value at all are skipped rather than becoming zeroes.
  DFT_CHECK(samples.size() == 2);
  DFT_CHECK(find_sample(samples, "good_metric") != nullptr);
  DFT_CHECK(find_sample(samples, "bad_value_metric") == nullptr);
  DFT_CHECK(find_sample(samples, "no_value_metric") == nullptr);

  // CRLF is stripped, so the value still parses.
  const auto* crlf = find_sample(samples, "trailing_ws_metric");
  DFT_CHECK(crlf != nullptr);
  DFT_CHECK(crlf->metric_value == 2.5);

  std::cout << "✓ omnistat malformed line handling passed" << std::endl;
}

void test_empty_payload() {
  std::cout << "Testing omnistat empty payload..." << std::endl;
  DFT_CHECK(OmnistatTelemetryCollector::parse_exposition("").empty());
  DFT_CHECK(OmnistatTelemetryCollector::parse_exposition("# only a comment\n")
                .empty());
  std::cout << "✓ omnistat empty payload passed" << std::endl;
}

}  // namespace

int main() {
  std::cout << "=== Running Omnistat Unit Tests ===" << std::endl;
  try {
    test_parses_metrics_and_labels();
    test_every_gpu_reported_separately();
    test_rejects_malformed_lines();
    test_empty_payload();
    std::cout << "\n✓ All Omnistat tests passed!" << std::endl;
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Omnistat test failed: " << ex.what() << std::endl;
    return 1;
  }
}
