#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/service/telemetry/variorum_collector.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dftracer/core/dftracer_config.hpp>
#include <string>
#include <utility>
#include <vector>

namespace dftracer {

namespace {
// Variorum is found when the service starts, not when it is built, so every
// build -- a wheel built without it included -- reads node power on a
// machine that has it. The one call the collector makes is
// variorum_get_power_json(), which Variorum 0.8 introduced (0.6 and 0.7 call
// it variorum_get_node_power_json, with another layout); a library without it
// counts as no Variorum. DFTRACER_VARIORUM_LIBRARY names the library;
// otherwise libvariorum.so is looked up as any library is (LD_LIBRARY_PATH,
// the service's RUNPATH, which a build that provisioned Variorum points at
// it, and the system's).
using variorum_get_power_json_fn = int (*)(char **);

enum class ProbeExit : int {
  kRead = 0,
  kNoCall = 3,
  kCallFailed = 4,
  kLibraryExited = 5,
};
constexpr int kProbeTimeoutMs = 10000;
constexpr int kProbePollMs = 20;

void exit_from_library() { _exit(static_cast<int>(ProbeExit::kLibraryExited)); }

[[noreturn]] void run_probe(const char *library) {
  // Registered last, so it runs first: a library that calls exit() ends the
  // child here, before the service's own exit handlers and destructors run.
  atexit(exit_from_library);
  void *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
  auto call = handle == nullptr ? nullptr
                                : reinterpret_cast<variorum_get_power_json_fn>(
                                      dlsym(handle, "variorum_get_power_json"));
  if (call == nullptr) _exit(static_cast<int>(ProbeExit::kNoCall));
  char *payload = nullptr;
  _exit(static_cast<int>(call(&payload) == 0 ? ProbeExit::kRead
                                             : ProbeExit::kCallFailed));
}

// Variorum ends the process with exit() on a topology it does not handle
// (corona's login nodes: "hwloc reports the number of threads (2) mod the
// number of cores (8) is not zero"), so its first call runs in a child. The
// service loads it only if that call returned: a crash, an exit or a hang
// there turns power telemetry off instead of taking the service down.
bool variorum_survives(const char *library) {
  fflush(nullptr);
  const pid_t pid = fork();
  if (pid < 0) {
    DFTRACER_LOG_ERROR(
        "Cannot fork to check Variorum (%s): %s; power telemetry disabled",
        library, strerror(errno));
    return false;
  }
  if (pid == 0) run_probe(library);

  int status = 0;
  for (int waited_ms = 0;; waited_ms += kProbePollMs) {
    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) break;
    if (done < 0 && errno != EINTR) {
      DFTRACER_LOG_ERROR("Lost the Variorum check (%s): %s", library,
                         strerror(errno));
      return false;
    }
    if (waited_ms >= kProbeTimeoutMs) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      DFTRACER_LOG_ERROR(
          "Variorum (%s) did not return from variorum_get_power_json within "
          "%d s; power telemetry disabled",
          library, kProbeTimeoutMs / 1000);
      return false;
    }
    const struct timespec pause = {0, kProbePollMs * 1000000L};
    nanosleep(&pause, nullptr);
  }

  if (WIFSIGNALED(status)) {
    DFTRACER_LOG_ERROR(
        "Variorum (%s) was killed by signal %d in variorum_get_power_json; "
        "power telemetry disabled",
        library, WTERMSIG(status));
    return false;
  }
  switch (static_cast<ProbeExit>(WEXITSTATUS(status))) {
    case ProbeExit::kRead:
    case ProbeExit::kNoCall:
    case ProbeExit::kCallFailed:
      // Safe to load here; initialize() reports a missing or failing call.
      return true;
    case ProbeExit::kLibraryExited:
      break;
  }
  DFTRACER_LOG_ERROR(
      "Variorum (%s) ended the process in variorum_get_power_json (its "
      "message is above); power telemetry disabled",
      library);
  return false;
}

variorum_get_power_json_fn variorum_get_power_json_symbol() {
  static const variorum_get_power_json_fn fn = []() {
    const char *named = std::getenv("DFTRACER_VARIORUM_LIBRARY");
    const char *library =
        (named != nullptr && named[0] != '\0') ? named : "libvariorum.so";
    if (!variorum_survives(library))
      return static_cast<variorum_get_power_json_fn>(nullptr);
    void *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
      DFTRACER_LOG_INFO("Variorum not loaded (%s): %s", library, dlerror());
      return static_cast<variorum_get_power_json_fn>(nullptr);
    }
    auto symbol = reinterpret_cast<variorum_get_power_json_fn>(
        dlsym(handle, "variorum_get_power_json"));
    if (symbol == nullptr) {
      DFTRACER_LOG_INFO(
          "%s has no variorum_get_power_json; Variorum 0.8 or later is needed",
          library);
    }
    return symbol;
  }();
  return fn;
}
}  // namespace

std::vector<VariorumPowerSample> VariorumTelemetryCollector::parse_power_json(
    const std::string &payload) {
  return variorum_layout::flatten_power_json(payload);
}

std::vector<VariorumPowerFamily> VariorumTelemetryCollector::group_by_family(
    const std::vector<VariorumPowerSample> &samples) {
  std::vector<VariorumPowerFamily> families;

  for (const auto &sample : samples) {
    const VariorumKeyPlacement placement =
        variorum_layout::place_key(sample.metric_name);

    // Linear scan: a node has a handful of families, so first-seen order is
    // worth more here than a lookup structure.
    VariorumPowerFamily *target = nullptr;
    for (auto &existing : families) {
      if (existing.family == placement.family) {
        target = &existing;
        break;
      }
    }
    if (target == nullptr) {
      families.push_back({placement.family, {}});
      target = &families.back();
    }
    target->readings.emplace_back(placement.reading, sample.metric_value);
  }

  return families;
}

VariorumTelemetryCollector::VariorumTelemetryCollector() : active(false) {}

const VariorumTelemetryCollector::Placement &VariorumTelemetryCollector::place(
    const std::string &key) {
  auto existing = placements.find(key);
  if (existing != placements.end()) return existing->second;

  const VariorumKeyPlacement resolved = variorum_layout::place_key(key);

  size_t family_index = family_names.size();
  for (size_t index = 0; index < family_names.size(); index++) {
    if (family_names[index] == resolved.family) {
      family_index = index;
      break;
    }
  }
  if (family_index == family_names.size()) {
    family_names.push_back(resolved.family);
    family_readings.emplace_back();
  }

  return placements.emplace(key, Placement{family_index, resolved.reading})
      .first->second;
}

void VariorumTelemetryCollector::initialize() {
  placements.clear();
  family_names.clear();
  family_readings.clear();

  auto conf =
      dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
  if (conf != nullptr && !conf->variorum_power) {
    active = false;
    DFTRACER_LOG_INFO("Variorum power telemetry disabled by configuration");
    return;
  }

  if (variorum_get_power_json_symbol() == nullptr) {
    // No Variorum 0.8 here: nothing to read, and capture() is a no-op.
    active = false;
    return;
  }

  // Take one reading now to settle what this node reports, so a sample costs a
  // parse and a lookup per reading rather than working the families out again
  // every tick. It also surfaces an unsupported or unprivileged machine at
  // startup instead of on the first timer tick.
  const std::string payload = read_power();
  if (payload.empty()) {
    // Either the architecture has no power support in variorum or the registers
    // are not readable from here (msr-safe, /dev/hsmp, OPAL). Neither changes
    // while the service runs, so do not ask again.
    active = false;
    DFTRACER_LOG_INFO(
        "Variorum could not read node power on this machine; power telemetry "
        "disabled");
    return;
  }

  const auto samples = parse_power_json(payload);
  if (samples.empty()) {
    active = false;
    DFTRACER_LOG_INFO(
        "Variorum returned no usable power readings; power telemetry disabled");
    return;
  }

  for (const auto &sample : samples) {
    place(sample.metric_name);
  }
  active = true;
  DFTRACER_LOG_INFO("Variorum power telemetry active with %d families",
                    (int)family_names.size());
}

std::string VariorumTelemetryCollector::read_power() {
  const auto get_power_json = variorum_get_power_json_symbol();
  if (get_power_json == nullptr) return std::string();
  // variorum mallocs the string and hands ownership over.
  char *payload = nullptr;
  if (get_power_json(&payload) != 0 || payload == nullptr) {
    if (payload != nullptr) free(payload);
    return std::string();
  }
  std::string result(payload);
  free(payload);
  return result;
}

void VariorumTelemetryCollector::capture(
    std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> /*logger*/, std::atomic<int> &index,
    TimeResolution timestamp) {
  if (!active) return;

  const std::string payload = read_power();
  if (payload.empty()) return;

  const auto samples = parse_power_json(payload);
  if (samples.empty()) return;

  for (auto &readings : family_readings) {
    readings.clear();
  }

  for (const auto &sample : samples) {
    // Known keys -- every key of the first reading -- cost one lookup. A key
    // the node did not have at startup is placed then, once, and is a lookup
    // from the next tick on.
    const Placement &placement = place(sample.metric_name);
    family_readings[placement.family_index].emplace_back(placement.reading,
                                                         sample.metric_value);
  }

  // One record per family, the way the PAPI sampler writes one per counter
  // family: the category says what the power is of (cpu, gpu, memory, network,
  // node) and the record carries every socket and device in that family. Node
  // scoped, so no pid/tid, because the power a socket draws cannot be
  // attributed to a process.
  for (size_t family = 0; family < family_names.size(); family++) {
    // A family whose devices all dropped out of this reading has nothing to
    // report; writing an empty record would look like a reading of zero.
    if (family_readings[family].empty()) continue;

    auto *metadata = new Metadata();
    for (const auto &reading : family_readings[family]) {
      metadata->insert_or_assign(reading.first, reading.second);
    }

    int current_index = index.fetch_add(1, std::memory_order_relaxed);
    buffer_manager->log_counter_event(
        current_index, "power", family_names[family].c_str(),
        TraceEventType::TRACE_TYPE_VARIORUM, timestamp, 0, 0, metadata);
  }
}

void VariorumTelemetryCollector::finalize() { active = false; }

}  // namespace dftracer
