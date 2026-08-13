#include <dftracer/core/common/logging.h>
#include <dftracer/service/telemetry/omnistat_collector.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace dftracer {

namespace {

// omnistat's exporter listens on 8001 and only accepts connections from
// localhost, which is where the service runs too, so there is nothing to
// configure: the address is always the loopback interface.
constexpr uint16_t kOmnistatPort = 8001;
// The exporter is on the loopback interface, so it either answers promptly or
// is not there. Keep the wait short: capture() holds a threadpool thread.
constexpr int kTimeoutSeconds = 2;

bool parse_double(const std::string &value, double &parsed) {
  if (value.empty()) return false;
  char *end = nullptr;
  parsed = std::strtod(value.c_str(), &end);
  return end != value.c_str() && *end == '\0';
}

}  // namespace

OmnistatTelemetryCollector::OmnistatTelemetryCollector() : active(true) {}

void OmnistatTelemetryCollector::initialize() { active = true; }

// Prometheus exposition format, one metric per line:
//   # HELP rocm_gpu_utilization_percentage ...
//   # TYPE rocm_gpu_utilization_percentage gauge
//   rocm_gpu_utilization_percentage{card="0"} 85.5
// Labels are kept verbatim: they carry the GPU a reading belongs to.
std::vector<OmnistatMetricSample> OmnistatTelemetryCollector::parse_exposition(
    const std::string &payload) {
  std::vector<OmnistatMetricSample> samples;
  size_t start = 0;
  while (start < payload.size()) {
    size_t end = payload.find('\n', start);
    if (end == std::string::npos) end = payload.size();
    std::string line = payload.substr(start, end - start);
    start = end + 1;

    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    // The value is whatever follows the last space.
    size_t space = line.rfind(' ');
    if (space == std::string::npos) continue;
    double value = 0.0;
    if (!parse_double(line.substr(space + 1), value)) continue;

    std::string key = line.substr(0, space);
    std::string labels;
    size_t brace = key.find('{');
    if (brace != std::string::npos) {
      size_t close = key.rfind('}');
      if (close != std::string::npos && close > brace) {
        labels = key.substr(brace + 1, close - brace - 1);
      }
      key = key.substr(0, brace);
    }
    if (key.empty()) continue;
    samples.push_back({key, value, labels});
  }
  return samples;
}

std::string OmnistatTelemetryCollector::scrape() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return std::string();

  struct timeval timeout{};
  timeout.tv_sec = kTimeoutSeconds;
  timeout.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  struct sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(kOmnistatPort);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, reinterpret_cast<struct sockaddr *>(&address),
              sizeof(address)) != 0) {
    close(fd);
    return std::string();
  }

  static const char kRequest[] =
      "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  if (send(fd, kRequest, sizeof(kRequest) - 1, MSG_NOSIGNAL) < 0) {
    close(fd);
    return std::string();
  }

  std::string response;
  char chunk[8192];
  ssize_t got = 0;
  while ((got = recv(fd, chunk, sizeof(chunk), 0)) > 0) {
    response.append(chunk, static_cast<size_t>(got));
  }
  close(fd);

  // Body only; the status line and headers matter no further than confirming
  // the request succeeded.
  size_t status_end = response.find("\r\n");
  if (response.rfind("HTTP/1.", 0) != 0 || status_end == std::string::npos ||
      response.find(" 200", 0) > status_end) {
    return std::string();
  }
  size_t body = response.find("\r\n\r\n");
  if (body == std::string::npos) return std::string();
  return response.substr(body + 4);
}

void OmnistatTelemetryCollector::capture(
    std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> /*logger*/, std::atomic<int> &index,
    TimeResolution timestamp) {
  if (!active) return;

  const std::string payload = scrape();
  if (payload.empty()) {
    // No exporter on this node. Stop trying rather than reconnecting forever.
    active = false;
    DFTRACER_LOG_INFO(
        "Omnistat exporter not reachable on port %d; GPU telemetry disabled",
        (int)kOmnistatPort);
    return;
  }

  for (const auto &sample : parse_exposition(payload)) {
    auto *metadata = new Metadata();
    metadata->insert_or_assign("value", sample.metric_value);
    if (!sample.labels.empty()) {
      metadata->insert_or_assign("labels", sample.labels);
    }
    // Node scoped, like the other service collectors: no pid/tid, because a
    // GPU-wide reading cannot be attributed to a process.
    int current_index = index.fetch_add(1, std::memory_order_relaxed);
    buffer_manager->log_counter_event(
        current_index, sample.metric_name.c_str(), "omnistat",
        TraceEventType::TRACE_TYPE_OMNISTAT, timestamp, 0, 0, metadata);
  }
}

void OmnistatTelemetryCollector::finalize() {}

}  // namespace dftracer
