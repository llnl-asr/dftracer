#ifndef DFTRACER_VARIORUM_TELEMETRY_COLLECTOR_H
#define DFTRACER_VARIORUM_TELEMETRY_COLLECTOR_H

#include <dftracer/service/telemetry/telemetry_interface.h>
#include <dftracer/service/telemetry/variorum_power_layout.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dftracer {

/**
 * @brief Samples node-level power through the variorum API.
 *
 * Power is a node-wide quantity: the RAPL/HSMP/OPAL registers variorum reads
 * report what the socket, memory and GPU domains draw in total, which cannot be
 * attributed to any one process. So it is collected here in the service, next
 * to the cpu/memory/io/network/omnistat collectors, rather than inside a traced
 * process, and written as counters with no pid/tid.
 *
 * Each tick calls variorum_get_power_json(), which returns a nested object
 * keyed by hostname. Its readings are grouped into the families variorum names
 * them for -- cpu, gpu, memory, network, node -- and each family becomes one
 * counter record whose category is that family, holding every socket and device
 * in it. So a sample is a handful of records saying what each part of the node
 * draws, rather than one record per socket-and-domain pair.
 *
 * Which families a node has is settled once, in initialize(), by taking a first
 * reading and working out where each of its keys belongs. A sample after that
 * is a parse, one lookup per reading and one record per family: no key is
 * picked apart twice. Nothing here holds a list of the families variorum can
 * report, so a machine DFTracer has never seen groups itself correctly.
 *
 * Reading power registers needs privilege on most machines: msr-safe or
 * CAP_SYS_RAWIO for RAPL, /dev/hsmp for AMD, OPAL for IBM. When variorum cannot
 * read them the collector goes quiet, the same way the omnistat collector does
 * when no exporter is running.
 */
class VariorumTelemetryCollector : public TelemetryCollector {
 public:
  VariorumTelemetryCollector();

  void initialize() override;
  void capture(std::shared_ptr<BufferManager> buffer_manager,
               std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
               TimeResolution timestamp) override;
  void finalize() override;
  std::string name() const override { return "variorum"; }

  /**
   * @brief Flatten a variorum power JSON payload into samples.
   *
   * Public so it can be tested without power registers or even without
   * variorum linked in. Non-numeric leaves and the "timestamp" field variorum
   * stamps its own readings with are skipped; the trace already timestamps the
   * counter record. The single hostname object every payload is wrapped in is
   * dropped, since the trace is already per node.
   *
   * Returns an empty vector for anything that is not parseable JSON.
   */
  static std::vector<VariorumPowerSample> parse_power_json(
      const std::string& payload);

  /**
   * @brief Group flattened readings by the family each one measures.
   *
   * The family comes from variorum's own naming: a path segment of the form
   * "power_<family>_watts" says what the reading is the power of, so "cpu",
   * "gpu", "mem" and "net" fall out of the payload rather than being a list
   * kept here that a new variorum release could outgrow. "mem" and "net" are
   * spelled out to "memory" and "network", and the plurals variorum uses in
   * some keys fold into the same family.
   *
   * A reading that is not watts but does name a device -- num_gpus_per_socket
   * is the one every GPU node reports -- joins that device's family, so
   * everything known about the GPUs arrives in the gpu record. Only a reading
   * that names no device at all is grouped under "other".
   *
   * Families come back in the order they first appear. Not what capture() calls
   * per sample -- it uses the table built once in initialize() -- but the same
   * rules, exposed so they can be tested directly.
   */
  static std::vector<VariorumPowerFamily> group_by_family(
      const std::vector<VariorumPowerSample>& samples);

 private:
  // Cleared once variorum reports it cannot read power on this machine, so an
  // unsupported or unprivileged node does not pay for a failing call on every
  // tick.
  bool active;

  // Where each flattened key goes, worked out once in initialize() from a first
  // reading: which family record it belongs in, and what it is called there.
  struct Placement {
    size_t family_index;
    std::string reading;
  };
  std::unordered_map<std::string, Placement> placements;

  // Family names in the order they were first seen, and the per-tick buffer of
  // readings for each. Cleared and refilled on every capture rather than
  // reallocated, since the set of families does not change under a running
  // service.
  std::vector<std::string> family_names;
  std::vector<std::vector<std::pair<std::string, double>>> family_readings;

  /** @brief variorum_get_power_json(). Empty on any failure. */
  std::string read_power();

  /**
   * @brief Add a key to the placement table, creating its family if new.
   *
   * Called for every key of the first reading, and again for any key a later
   * reading brings that the first did not have -- a device that was offline at
   * startup, say. So a key is still only ever picked apart once.
   */
  const Placement& place(const std::string& key);
};

}  // namespace dftracer

#endif  // DFTRACER_VARIORUM_TELEMETRY_COLLECTOR_H
