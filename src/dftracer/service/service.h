#ifndef DFTRACER_SERVER
#define DFTRACER_SERVER

#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/event_loop.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/service/common/datastructure.h>
#include <dftracer/service/telemetry/telemetry_factory.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace dftracer {

// Main service class for DFTracer server
class DFTracerService {
 public:
  // Constructor: initializes configuration, logger, and buffer manager
  DFTracerService() : index{0}, interval(1000), running(false) {
    auto conf =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    conf->metadata = true;
    conf->enable = true;
    conf->write_buffer_size = 16 * 1024 * 1024;
    interval = conf->trace_interval_ms;
    if (conf->log_file.empty()) {
      throw std::runtime_error(
          "Configuration error: Please set the log_file prefix in the "
          "configuration.");
    } else {
      // Get hostname and append to log_file for uniqueness
      char hostname[256] = {0};
      if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
        throw std::runtime_error("Failed to get hostname for log_file.");
      }
      hostname[sizeof(hostname) - 1] = '\0';
      conf->log_file += std::string("_") + hostname;
      // Add file extension based on compression setting
      if (conf->compression) {
        conf->log_file += ".pfw.gz";
      } else {
        conf->log_file += ".pfw";
      }
      // Delete the file if it exists
      if (FILE* f = fopen(conf->log_file.c_str(), "r")) {
        fclose(f);
        if (remove(conf->log_file.c_str()) != 0) {
          throw std::runtime_error("Failed to delete existing log file: " +
                                   conf->log_file);
        }
      }
      logger = DFT_LOGGER_INIT();
      buffer_manager =
          dftracer::Singleton<dftracer::BufferManager>::get_instance();
      auto hostname_hash = logger->get_hash(hostname);
      this->buffer_manager->initialize(conf->log_file.c_str(), hostname_hash);

      // Initialize telemetry collectors
      telemetry_collectors = TelemetryCollectorFactory::create_all();
      for (auto& collector : telemetry_collectors) {
        collector->initialize();
      }
    }
  }

  // Destructor: ensures the service is stopped and resources are cleaned up
  ~DFTracerService() { stop(); }

  // Starts the shared event loop and samples every collector on it
  void start() {
    if (running.load(std::memory_order_relaxed)) {
      return;
    }
    if (finalized) {
      throw std::runtime_error(
          "DFTracerService is single-use after stop/finalize.");
    }

    auto conf =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    auto libuv_threads = std::to_string(conf->libuv_thread_count);
    setenv("UV_THREADPOOL_SIZE", libuv_threads.c_str(), 1);

    event_loop = dftracer::Singleton<dftracer::EventLoop>::get_instance();
    if (event_loop == nullptr) {
      throw std::runtime_error(
          "DFTracerService could not obtain the event loop.");
    }

    collector_tasks.clear();
    collector_tasks.reserve(telemetry_collectors.size());
    for (auto& collector : telemetry_collectors) {
      auto task = std::make_unique<CollectorTask>();
      task->service = this;
      task->collector = collector.get();
      collector_tasks.push_back(std::move(task));
    }

    running = true;

    // Job schedulers (Flux, Slurm) send SIGTERM, not SIGINT, when cancelling a
    // job or cgroup, so without both a cancellation would kill the daemon
    // without flushing and compressing its trace buffer.
    event_loop->add_signal(SIGINT, [this]() { request_stop(); });
    event_loop->add_signal(SIGTERM, [this]() { request_stop(); });

    // One timer per collector: a collector that is slow to read does not push
    // the others off their interval. First tick immediately, so even a run that
    // lasts less than one interval carries a sample of the node.
    for (auto& task : collector_tasks) {
      CollectorTask* raw = task.get();
      event_loop->add_timer(interval, 0,
                            [this, raw]() { queue_capture(*raw); });
    }

    // The service's main thread has nothing else to do, so it drives the loop.
    event_loop->run();
    finalize_service();
  }

  // Stops capture loop and finalizes service resources
  void stop() {
    request_stop();
    finalize_service();
  }

 private:
  struct CollectorTask {
    DFTracerService* service = nullptr;
    TelemetryCollector* collector = nullptr;
    // Set while a capture of this collector is on the threadpool, so a tick
    // that arrives before the last one finished is dropped rather than queuing
    // a second read of the same source.
    std::atomic<bool> in_flight{false};
  };

  std::shared_ptr<DFTLogger> logger;  // Logger instance
  std::atomic<int> index;     // Event index counter across libuv worker threads
  unsigned int interval;      // Interval between metric collections (ms)
  std::atomic<bool> running;  // Flag to control metric capture
  bool stop_requested = false;
  bool finalized = false;
  std::shared_ptr<dftracer::EventLoop> event_loop;
  std::shared_ptr<dftracer::BufferManager> buffer_manager;  // Buffer manager
  std::vector<std::unique_ptr<TelemetryCollector>>
      telemetry_collectors;  // Telemetry collectors for system metrics
  std::vector<std::unique_ptr<CollectorTask>> collector_tasks;

  void queue_capture(CollectorTask& task) {
    if (!running.load(std::memory_order_relaxed)) {
      return;
    }
    if (task.in_flight.exchange(true, std::memory_order_acq_rel)) {
      return;
    }

    CollectorTask* raw = &task;
    bool queued = event_loop->queue_work(
        [this, raw]() {
          if (!running.load(std::memory_order_relaxed)) return;
          TimeResolution time = logger->get_time();
          raw->collector->capture(buffer_manager, logger, index, time);
        },
        [raw]() { raw->in_flight.store(false, std::memory_order_release); });
    if (!queued) {
      task.in_flight.store(false, std::memory_order_release);
    }
  }

  void request_stop() {
    running.store(false, std::memory_order_relaxed);
    if (stop_requested) {
      return;
    }
    stop_requested = true;
    // Closes the timers and signal handlers, so the loop falls out once the
    // captures already on the threadpool have finished. Nothing is abandoned
    // mid-write.
    if (event_loop != nullptr) {
      event_loop->stop();
    }
  }

  void finalize_service() {
    if (finalized) {
      return;
    }

    for (auto& collector : telemetry_collectors) {
      collector->finalize();
    }

    this->buffer_manager->finalize(index.load(std::memory_order_relaxed), true);
    if (auto conf =
            dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
        conf && !conf->compression) {
      std::string cmd = "gzip -f " + conf->log_file;
      int ret = system(cmd.c_str());
      if (ret != 0) {
        fprintf(stderr, "Warning: gzip compression failed for %s\n",
                conf->log_file.c_str());
      }
    }

    collector_tasks.clear();
    finalized = true;
  }
};
}  // namespace dftracer

#endif  // DFTRACER_SERVER
