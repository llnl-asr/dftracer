#ifndef DFTRACER_PAPI_COUNTERS_H
#define DFTRACER_PAPI_COUNTERS_H

#ifdef DFTRACER_DEBUG
#include <dftracer/core/dftracer_config_dbg.hpp>
#else
#include <dftracer/core/dftracer_config.hpp>
#endif

#ifdef DFTRACER_PAPI_TRACING_ENABLE

#include <dftracer/core/buffer/buffer.h>
#include <dftracer/core/function/generic_function.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <papi.h>
#include <sys/types.h>
#include <uv.h>

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dftracer {

// Periodic sampler for PAPI hardware counters.
//
// PAPI counters are process scoped, so this lives in the traced process
// alongside the HIP interceptor rather than in the dftracer_service daemon,
// which would only measure itself. Node-wide counting (PAPI_CPU_ATTACH) is not
// an option either: it needs perf_event_paranoid <= 0, which regular users do
// not get on HPC compute nodes.
//
// The sampler owns a single event set bound to the whole process with
// PAPI_attach(getpid()) plus PAPI_INHERIT_ALL, so one read yields counts across
// every application thread. That event set is created, read and destroyed on a
// dedicated background thread driven by a libuv timer -- the same shape
// dftracer_service uses for its telemetry collectors -- so DFTracerCore::log()
// does no PAPI work at all.
//
// Two constraints follow from the PAPI/perf_event behaviour measured on
// tuolumne (see counters.cpp for the details):
//   * inherit only covers threads created after PAPI_start, so the sampler must
//     start early in DFTracerCore::initialize();
//   * attaching to an individual thread id is NOT used, because reads of such
//     an event set return frozen garbage on this platform.
//
// Which counters to use is settled at build time: cmake/probes/papi_probe.c
// works out what this machine can program together and bakes the list into
// dftracer_config.hpp, so a run does no discovery at all. DFTRACER_PAPI_EVENTS
// overrides it. Counters the run host will not program are skipped when the
// event set is built.
class PAPICounterFunction : public dftracer::GenericFunction {
 private:
  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::shared_ptr<dftracer::BufferManager> buffer_manager;

  // Resolved counters, fixed once the library is initialized. Parallel arrays:
  // events[i] is written as the record's "name" and categories[i] as its "cat".
  std::vector<std::string> events;
  std::vector<std::string> categories;
  std::string event_list;

  std::atomic<bool> enabled;
  std::atomic<bool> library_ready;
  // Time-share the counters. Turned on when more counters were detected than
  // the CPU has slots, which is the only way to collect them all.
  bool multiplex_active;
  std::atomic<int> index;

  // Owned exclusively by the sampler thread.
  int event_set;
  std::vector<long long> last_values;
  std::vector<long long> current_values;
  ProcessID process_id;
  ThreadID thread_id;

  // libuv sampler thread. loop_ready tells finalize() the loop exists and can
  // be woken; it is set by the sampler and read by whoever finalizes.
  std::thread sampler_thread;
  uv_loop_t loop;
  uv_timer_t timer;
  uv_async_t stop_signal;
  std::atomic<bool> sampler_running;
  std::atomic<bool> loop_ready;
  // Lets initialize() block until the sampler is actually counting. Inherited
  // counters only cover threads created after PAPI_start, so returning to the
  // application before then would silently lose every worker thread it spawns.
  std::promise<void> sampler_ready;

  // Library setup, all on the sampler thread.
  bool initialize_library();
  bool bind_to_process(int event_set) const;
  bool start_counters();
  void stop_counters();

  void run_sampler();
  void emit_sample();

  static void on_timer(uv_timer_t *handle);
  static void on_stop(uv_async_t *handle);

  // Broad family a PAPI preset belongs to (CACHE, BRANCH, FLOP, ...), written
  // as the record's "cat".
  static std::string event_category(const std::string &event_name);

 public:
  PAPICounterFunction();
  ~PAPICounterFunction() override;

  void initialize() override;
  void finalize() override;
  bool is_enabled() const { return enabled.load(); }
  // Counters resolved for this machine; empty until the sampler has started.
  const std::vector<std::string> &active_events() const { return events; }
};

}  // namespace dftracer

#endif  // DFTRACER_PAPI_TRACING_ENABLE
#endif  // DFTRACER_PAPI_COUNTERS_H
