#ifndef DFTRACER_SERVER
#define DFTRACER_SERVER

#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/service/common/datastructure.h>
#include <dftracer/service/telemetry/telemetry_factory.h>
#include <unistd.h>
#include <uv.h>

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

  // Starts the libuv event loop for periodic metric collection
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

    collector_tasks.clear();
    collector_tasks.reserve(telemetry_collectors.size());

    for (auto& collector : telemetry_collectors) {
      auto task = std::make_unique<CollectorTask>();
      task->service = this;
      task->collector = collector.get();
      collector_tasks.push_back(std::move(task));
    }

    pending_work_count.store(0, std::memory_order_relaxed);
    stop_requested = false;
    running = true;

    if (!loop_initialized) {
      uv_loop_init(&loop);
      loop_initialized = true;
    }

    signal_handle.data = this;
    uv_signal_init(&loop, &signal_handle);
    uv_signal_start(&signal_handle, DFTracerService::on_signal, SIGINT);

    // Also treat SIGTERM as a graceful-shutdown request: job schedulers
    // (Flux, Slurm) send SIGTERM (not SIGINT) when cancelling a job/cgroup,
    // so without this handler a job cancellation kills the daemon without
    // flushing/compressing its trace buffer.
    sigterm_handle.data = this;
    uv_signal_init(&loop, &sigterm_handle);
    uv_signal_start(&sigterm_handle, DFTracerService::on_signal, SIGTERM);

    for (auto& task : collector_tasks) {
      task->timer.data = task.get();
      task->work_req.data = task.get();
      uv_timer_init(&loop, &task->timer);
      uv_timer_start(&task->timer, DFTracerService::on_collector_tick, 0,
                     interval);
    }

    uv_run(&loop, UV_RUN_DEFAULT);
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
    uv_timer_t timer;
    uv_work_t work_req;
    bool in_flight = false;
  };

  std::shared_ptr<DFTLogger> logger;  // Logger instance
  std::atomic<int> index;     // Event index counter across libuv worker threads
  unsigned int interval;      // Interval between metric collections (ms)
  std::atomic<bool> running;  // Flag to control metric capture
  uv_loop_t loop;                 // Single libuv event loop
  uv_signal_t signal_handle;      // Signal handler for SIGINT
  uv_signal_t sigterm_handle;     // Signal handler for SIGTERM
  bool loop_initialized = false;
  bool stop_requested = false;
  bool finalized = false;
  std::atomic<int> pending_work_count{0};
  std::shared_ptr<dftracer::BufferManager> buffer_manager;  // Buffer manager
  std::vector<std::unique_ptr<TelemetryCollector>>
      telemetry_collectors;  // Telemetry collectors for system metrics
  std::vector<std::unique_ptr<CollectorTask>> collector_tasks;

  static void on_signal(uv_signal_t* handle, int signum) {
    if ((signum != SIGINT && signum != SIGTERM) || handle == nullptr ||
        handle->data == nullptr) {
      return;
    }
    auto* service = static_cast<DFTracerService*>(handle->data);
    service->request_stop();
  }

  static void on_collector_tick(uv_timer_t* handle) {
    if (handle == nullptr || handle->data == nullptr) {
      return;
    }
    auto* task = static_cast<CollectorTask*>(handle->data);
    if (task->service == nullptr || task->collector == nullptr) {
      return;
    }
    task->service->queue_capture(*task);
  }

  static void on_capture_work(uv_work_t* req) {
    if (req == nullptr || req->data == nullptr) {
      return;
    }
    auto* task = static_cast<CollectorTask*>(req->data);
    if (task->service == nullptr || task->collector == nullptr) {
      return;
    }
    if (!task->service->running.load(std::memory_order_relaxed)) {
      return;
    }
    TimeResolution time = task->service->logger->get_time();
    task->collector->capture(task->service->buffer_manager,
                             task->service->logger, task->service->index, time);
  }

  static void on_capture_after_work(uv_work_t* req, int /*status*/) {
    if (req == nullptr || req->data == nullptr) {
      return;
    }
    auto* task = static_cast<CollectorTask*>(req->data);
    task->in_flight = false;

    auto* service = task->service;
    if (service == nullptr) {
      return;
    }

    int remaining =
        service->pending_work_count.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (!service->running.load(std::memory_order_relaxed) && remaining == 0) {
      service->stop_loop_if_idle();
    }
  }

  void queue_capture(CollectorTask& task) {
    if (!running.load(std::memory_order_relaxed) || task.in_flight) {
      return;
    }
    int ret =
        uv_queue_work(&loop, &task.work_req, DFTracerService::on_capture_work,
                      DFTracerService::on_capture_after_work);
    if (ret == 0) {
      task.in_flight = true;
      pending_work_count.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void stop_loop_if_idle() {
    if (running.load(std::memory_order_relaxed)) {
      return;
    }
    if (pending_work_count.load(std::memory_order_relaxed) != 0) {
      return;
    }
    if (!loop_initialized) {
      return;
    }
    uv_stop(&loop);
  }

  void close_runtime_handles() {
    if (!loop_initialized) {
      return;
    }

    uv_signal_stop(&signal_handle);
    uv_close(reinterpret_cast<uv_handle_t*>(&signal_handle), nullptr);

    uv_signal_stop(&sigterm_handle);
    uv_close(reinterpret_cast<uv_handle_t*>(&sigterm_handle), nullptr);

    for (auto& task : collector_tasks) {
      uv_timer_stop(&task->timer);
      uv_close(reinterpret_cast<uv_handle_t*>(&task->timer), nullptr);
      task->in_flight = false;
    }
  }

  void request_stop() {
    bool was_running = running.exchange(false, std::memory_order_relaxed);
    if (!was_running && stop_requested) {
      return;
    }
    if (!stop_requested) {
      close_runtime_handles();
      stop_requested = true;
    }
    stop_loop_if_idle();
  }

  void finalize_service() {
    if (finalized) {
      return;
    }

    if (loop_initialized) {
      while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_DEFAULT);
      }
      uv_loop_close(&loop);
      loop_initialized = false;
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
