#include <dftracer/core/common/singleton.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/service/telemetry/cpu_collector.h>
#include <dftracer/service/telemetry/io_collector.h>
#include <dftracer/service/telemetry/memory_collector.h>
#include <dftracer/service/telemetry/network_collector.h>
#include <dftracer/service/telemetry/omnistat_collector.h>
#include <dftracer/service/telemetry/telemetry_factory.h>
#include <dftracer/service/telemetry/variorum_collector.h>

#include <algorithm>
#include <stdexcept>

namespace dftracer {

std::unique_ptr<TelemetryCollector> TelemetryCollectorFactory::create(
    const std::string& type) {
  if (type == "cpu") {
    return std::make_unique<CPUTelemetryCollector>();
  } else if (type == "memory") {
    return std::make_unique<MemoryTelemetryCollector>();
  } else if (type == "io") {
    return std::make_unique<IOTelemetryCollector>();
  } else if (type == "network") {
    return std::make_unique<NetworkTelemetryCollector>();
  } else if (type == "omnistat") {
    return std::make_unique<OmnistatTelemetryCollector>();
  } else if (type == "variorum") {
    return std::make_unique<VariorumTelemetryCollector>();
  } else {
    throw std::invalid_argument("Unknown telemetry collector type: " + type);
  }
}

std::vector<std::unique_ptr<TelemetryCollector>>
TelemetryCollectorFactory::create_all() {
  std::vector<std::unique_ptr<TelemetryCollector>> collectors;
  collectors.push_back(std::make_unique<CPUTelemetryCollector>());
  collectors.push_back(std::make_unique<MemoryTelemetryCollector>());
  collectors.push_back(std::make_unique<IOTelemetryCollector>());
  collectors.push_back(std::make_unique<NetworkTelemetryCollector>());
  collectors.push_back(std::make_unique<OmnistatTelemetryCollector>());
  collectors.push_back(std::make_unique<VariorumTelemetryCollector>());
  return collectors;
}

std::vector<std::string> TelemetryCollectorFactory::get_supported_types() {
  return {"cpu", "memory", "io", "network", "omnistat", "variorum"};
}

bool TelemetryCollectorFactory::is_supported(const std::string& type) {
  auto supported = get_supported_types();
  return std::find(supported.begin(), supported.end(), type) != supported.end();
}

}  // namespace dftracer
