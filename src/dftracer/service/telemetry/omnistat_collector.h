#ifndef DFTRACER_OMNISTAT_TELEMETRY_COLLECTOR_H
#define DFTRACER_OMNISTAT_TELEMETRY_COLLECTOR_H

#include <dftracer/service/telemetry/telemetry_interface.h>

#include <string>
#include <vector>

namespace dftracer {

/**
 * @brief One omnistat metric reading, as scraped from the node's exporter.
 */
struct OmnistatMetricSample {
  std::string metric_name;
  double metric_value;
  // Prometheus labels verbatim, e.g. card="0". Carries the GPU the reading
  // belongs to.
  std::string labels;
};

/**
 * @brief Samples node-level AMD GPU telemetry from the local omnistat exporter.
 *
 * omnistat runs a Prometheus exporter on every node (port 8001 by default,
 * restricted to localhost) reporting per-GPU utilization, HBM usage, power,
 * temperature and clocks. Those are device-wide quantities shared by every
 * process on the GPU, so they are node scoped and belong here in the service
 * rather than being sampled inside a traced process.
 *
 * Works like the cpu/memory/io/network collectors: nothing to configure and
 * nothing to enable, just a reading taken on each timer tick and written
 * straight into the trace as counters, and every metric the exporter serves is
 * taken. capture() runs on a libuv threadpool thread, not the loop thread, so
 * the scrape cannot stall the service.
 *
 * If the exporter is not running the collector goes quiet after the first
 * attempt, the same way a missing /proc file would.
 */
class OmnistatTelemetryCollector : public TelemetryCollector {
 public:
  OmnistatTelemetryCollector();

  void initialize() override;
  void capture(std::shared_ptr<BufferManager> buffer_manager,
               std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
               TimeResolution timestamp) override;
  void finalize() override;
  std::string name() const override { return "omnistat"; }

  /**
   * @brief Parse a Prometheus exposition payload into samples.
   *
   * Public so it can be tested without a live exporter. Comment lines (HELP,
   * TYPE) and non-numeric values are skipped.
   */
  static std::vector<OmnistatMetricSample> parse_exposition(
      const std::string& payload);

 private:
  // Cleared once the exporter is found missing, so a node without omnistat does
  // not pay for a connection attempt on every tick.
  bool active;

  /** @brief GET /metrics from the exporter. Empty on any failure. */
  std::string scrape();
};

}  // namespace dftracer

#endif  // DFTRACER_OMNISTAT_TELEMETRY_COLLECTOR_H
