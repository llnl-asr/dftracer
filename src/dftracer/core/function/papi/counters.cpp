#include <dftracer/core/function/papi/counters.h>

#ifdef DFTRACER_PAPI_TRACING_ENABLE

#include <dftracer/core/common/singleton.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <sstream>
#include <unordered_set>

template <>
std::shared_ptr<dftracer::PAPICounterFunction>
    dftracer::Singleton<dftracer::PAPICounterFunction>::instance = nullptr;
template <>
bool dftracer::Singleton<
    dftracer::PAPICounterFunction>::stop_creating_instances = false;

namespace {

// PAPI needs distinct per-thread ids; gettid() is exactly that and is stable
// for the thread's lifetime.
unsigned long papi_thread_identifier() {
  return static_cast<unsigned long>(syscall(SYS_gettid));
}

void log_papi_status(const char *operation, int retval) {
  DFTRACER_LOG_WARN("PAPI operation %s failed with %d (%s)", operation, retval,
                    PAPI_strerror(retval));
}

std::string trim_copy(const std::string &value) {
  auto first = value.find_first_not_of(" \t\n\r");
  if (first == std::string::npos) return std::string();
  auto last = value.find_last_not_of(" \t\n\r");
  return value.substr(first, last - first + 1);
}

std::vector<std::string> normalize_events(
    const std::vector<std::string> &raw_events) {
  std::vector<std::string> normalized;
  std::unordered_set<std::string> seen;
  for (const auto &raw : raw_events) {
    auto event = trim_copy(raw);
    if (event.empty() || seen.count(event) > 0) continue;
    seen.insert(event);
    normalized.push_back(event);
  }
  return normalized;
}

std::vector<std::string> split_event_list(const std::string &value) {
  std::vector<std::string> items;
  std::string current;
  for (char ch : value) {
    if (ch == ',' || ch == ';') {
      auto trimmed = trim_copy(current);
      if (!trimmed.empty()) items.push_back(trimmed);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  auto trimmed = trim_copy(current);
  if (!trimmed.empty()) items.push_back(trimmed);
  return items;
}

// Counters the build-time probe (cmake/probes/papi_probe.c) found this machine
// could program together. Using them as the candidate list means a run does no
// preset enumeration at all in the common case.
std::vector<std::string> build_time_events() {
  return split_event_list(DFTRACER_PAPI_DETECTED_EVENTS);
}

}  // namespace

namespace dftracer {

PAPICounterFunction::PAPICounterFunction()
    : dftracer::GenericFunction(),
      config(
          dftracer::Singleton<dftracer::ConfigurationManager>::get_instance()),
      buffer_manager(
          dftracer::Singleton<dftracer::BufferManager>::get_instance()),
      events(),
      categories(),
      event_list(),
      enabled(false),
      library_ready(false),
      multiplex_active(false),
      index(0),
      event_set(PAPI_NULL),
      last_values(),
      current_values(),
      process_id(0),
      thread_id(0),
      sampler_thread(),
      sampler_running(false),
      loop_ready(false) {
  enabled.store(config != nullptr && config->papi_tracing);
  if (!enabled.load()) {
    DFTRACER_LOG_DEBUG("PAPI tracing runtime option disabled", "");
  }
}

PAPICounterFunction::~PAPICounterFunction() { finalize(); }

// Group a preset into a family for the record's "cat", so a reader can select
// e.g. every cache counter without knowing each preset name. Derived from the
// naming scheme of the PAPI preset table; longer prefixes are tested first, and
// anything unrecognised falls back to "PAPI" rather than being guessed at.
std::string PAPICounterFunction::event_category(const std::string &event_name) {
  struct Prefix {
    const char *prefix;
    const char *category;
  };
  static const Prefix kPrefixes[] = {
      // Cycles and instruction counts.
      {"PAPI_TOT_CYC", "CYCLE"},
      {"PAPI_REF_CYC", "CYCLE"},
      {"PAPI_TOT_INS", "INSTRUCTION"},
      {"PAPI_TOT_IIS", "INSTRUCTION"},
      {"PAPI_INT_INS", "INSTRUCTION"},
      {"PAPI_SYC_INS", "INSTRUCTION"},
      {"PAPI_HW_INT", "INTERRUPT"},
      // Floating point and vector. The FPU sub-counters (multiply, add,
      // divide, square root, FMA, inverse) are named per operation, so each
      // needs its own prefix.
      {"PAPI_FMA_INS", "FLOP"},
      {"PAPI_FML_INS", "FLOP"},
      {"PAPI_FAD_INS", "FLOP"},
      {"PAPI_FDV_INS", "FLOP"},
      {"PAPI_FSQ_INS", "FLOP"},
      {"PAPI_FNV_INS", "FLOP"},
      {"PAPI_FPU_IDL", "STALL"},
      {"PAPI_FP_STAL", "STALL"},
      {"PAPI_FP_", "FLOP"},
      {"PAPI_SP_", "FLOP"},
      {"PAPI_DP_", "FLOP"},
      {"PAPI_VEC_", "FLOP"},
      // Cache and coherency.
      {"PAPI_L1_", "CACHE"},
      {"PAPI_L2_", "CACHE"},
      {"PAPI_L3_", "CACHE"},
      {"PAPI_CA_", "COHERENCY"},
      {"PAPI_PRF_DM", "PREFETCH"},
      // Address translation.
      {"PAPI_TLB_", "TLB"},
      // Branches.
      {"PAPI_BTAC_M", "BRANCH"},
      {"PAPI_BRU_IDL", "STALL"},
      {"PAPI_BR_", "BRANCH"},
      // Loads, stores and memory stalls.
      {"PAPI_LST_INS", "MEMORY"},
      {"PAPI_LD_INS", "MEMORY"},
      {"PAPI_SR_INS", "MEMORY"},
      {"PAPI_MEM_SCY", "STALL"},
      {"PAPI_MEM_RCY", "STALL"},
      {"PAPI_MEM_WCY", "STALL"},
      {"PAPI_MEM_", "MEMORY"},
      {"PAPI_CSR_", "SYNC"},
      // Issue and completion stalls.
      {"PAPI_STL_", "STALL"},
      {"PAPI_FUL_", "STALL"},
      {"PAPI_RES_STL", "STALL"},
      {"PAPI_FXU_IDL", "STALL"},
      {"PAPI_LSU_IDL", "STALL"},
  };
  for (const auto &entry : kPrefixes) {
    if (event_name.rfind(entry.prefix, 0) == 0) return entry.category;
  }
  return "PAPI";
}

// Bind an event set to the whole process: component first (PAPI_attach and
// PAPI_set_multiplex both fail with PAPI_ECMP otherwise), then attach to the
// thread-group leader, then ask for inheritance so threads the application
// spawns later are counted as well. Without PAPI_INHERIT_ALL only the main
// thread would be counted; attaching to individual thread ids is not an option
// because reads of such an event set return frozen garbage on this platform.
bool PAPICounterFunction::bind_to_process(int set) const {
  int retval = PAPI_assign_eventset_component(set, 0);
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_assign_eventset_component", retval);
    return false;
  }

  retval = PAPI_attach(set, static_cast<unsigned long>(getpid()));
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_attach", retval);
    return false;
  }

  PAPI_option_t option;
  memset(&option, 0, sizeof(option));
  option.inherit.inherit = PAPI_INHERIT_ALL;
  option.inherit.eventset = set;
  retval = PAPI_set_opt(PAPI_INHERIT, &option);
  if (retval != PAPI_OK) {
    // Not fatal: counters still work, but they only cover the main thread.
    DFTRACER_LOG_WARN(
        "PAPI could not enable counter inheritance (%d, %s); counters will "
        "cover the main thread only",
        retval, PAPI_strerror(retval));
  }

  if (multiplex_active) {
    retval = PAPI_set_multiplex(set);
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_set_multiplex", retval);
    }
  }
  return true;
}

bool PAPICounterFunction::initialize_library() {
  int retval = PAPI_library_init(PAPI_VER_CURRENT);
  if (retval != PAPI_VER_CURRENT) {
    DFTRACER_LOG_WARN("PAPI_library_init failed with %d (%s)", retval,
                      PAPI_strerror(retval));
    return false;
  }

  retval = PAPI_thread_init(papi_thread_identifier);
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_thread_init", retval);
    return false;
  }

  // Which counters to use is decided at build time by cmake/probes/papi_probe.c
  // and baked into dftracer_config.hpp, so nothing is enumerated here.
  // DFTRACER_PAPI_EVENTS overrides it for a specific run.
  auto configured = normalize_events(config->papi_events);
  events =
      configured.empty() ? normalize_events(build_time_events()) : configured;
  if (events.empty()) {
    DFTRACER_LOG_WARN("PAPI tracing disabled: no counters configured", "");
    return false;
  }

  // More counters than the CPU has slots can only be collected by time-sharing
  // them. Values then become scaled estimates rather than exact counts, so this
  // is only turned on when it is the difference between collecting a counter
  // and dropping it.
  int hw_counters = DFTRACER_PAPI_HW_COUNTERS;
  multiplex_active =
      config->papi_multiplex ||
      (hw_counters > 0 && static_cast<int>(events.size()) > hw_counters);
  if (multiplex_active) {
    int retval = PAPI_multiplex_init();
    if (retval != PAPI_OK) {
      multiplex_active = false;
      log_papi_status("PAPI_multiplex_init", retval);
    } else if (!config->papi_multiplex) {
      DFTRACER_LOG_INFO(
          "PAPI multiplexing enabled: %d counters requested but only %d "
          "hardware slots; values are scaled estimates",
          static_cast<int>(events.size()), hw_counters);
    }
  }
  return true;
}

// Build the event set and start counting. Counters that this machine will not
// program -- a different CPU from the build host, or one counter slot too many
// -- are skipped rather than failing the run.
bool PAPICounterFunction::start_counters() {
  int retval = PAPI_create_eventset(&event_set);
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_create_eventset", retval);
    event_set = PAPI_NULL;
    return false;
  }
  if (!bind_to_process(event_set)) {
    PAPI_destroy_eventset(&event_set);
    event_set = PAPI_NULL;
    return false;
  }

  std::vector<std::string> added;
  for (const auto &event_name : events) {
    retval = PAPI_add_named_event(event_set, event_name.c_str());
    if (retval != PAPI_OK) {
      DFTRACER_LOG_WARN("PAPI counter %s cannot be counted here (%d, %s)",
                        event_name.c_str(), retval, PAPI_strerror(retval));
      continue;
    }
    added.push_back(event_name);
  }
  events = std::move(added);
  if (events.empty()) {
    DFTRACER_LOG_WARN(
        "PAPI tracing disabled: none of the configured counters can be counted "
        "on this machine",
        "");
    PAPI_cleanup_eventset(event_set);
    PAPI_destroy_eventset(&event_set);
    event_set = PAPI_NULL;
    return false;
  }

  categories.clear();
  std::ostringstream names;
  for (size_t idx = 0; idx < events.size(); ++idx) {
    categories.push_back(event_category(events[idx]));
    if (idx > 0) names << ',';
    names << events[idx];
  }
  event_list = names.str();

  last_values.assign(events.size(), 0);
  current_values.assign(events.size(), 0);

  retval = PAPI_start(event_set);
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_start", retval);
    PAPI_cleanup_eventset(event_set);
    PAPI_destroy_eventset(&event_set);
    event_set = PAPI_NULL;
    return false;
  }

  DFTRACER_LOG_INFO("PAPI tracing enabled with %d counters: %s",
                    static_cast<int>(events.size()), event_list.c_str());
  return true;
}

void PAPICounterFunction::stop_counters() {
  if (event_set == PAPI_NULL) return;
  PAPI_stop(event_set, current_values.data());
  PAPI_cleanup_eventset(event_set);
  PAPI_destroy_eventset(&event_set);
  event_set = PAPI_NULL;
}

void PAPICounterFunction::initialize() {
  if (!enabled.load()) return;
  if (sampler_running.load()) return;

  if (buffer_manager == nullptr) {
    buffer_manager =
        dftracer::Singleton<dftracer::BufferManager>::get_instance();
  }
  if (buffer_manager == nullptr || logger == nullptr) {
    enabled.store(false);
    DFTRACER_LOG_WARN("PAPI tracing disabled: tracing buffers unavailable", "");
    return;
  }

  // Counters are attributed to the process; the thread column carries the
  // thread-group leader, which is the thread the event set is attached to.
  process_id = static_cast<ProcessID>(getpid());
  thread_id = static_cast<ThreadID>(getpid());

  sampler_ready = std::promise<void>();
  auto counting = sampler_ready.get_future();
  sampler_running.store(true);
  sampler_thread = std::thread([this]() { this->run_sampler(); });

  // Counter discovery probes every preset the machine offers and takes a few
  // milliseconds. Threads the application starts in that window would not be
  // inherited by the event set, so hold it here until counting has begun (or
  // definitively failed). The timeout only guards against a wedged PAPI.
  if (counting.wait_for(std::chrono::seconds(30)) ==
      std::future_status::timeout) {
    DFTRACER_LOG_WARN(
        "PAPI sampler did not start counting in time; counters may miss "
        "threads started meanwhile",
        "");
  }
}

// Body of the sampler thread: a libuv loop whose timer fires every
// papi_sample_interval_ms, mirroring how dftracer_service drives its telemetry
// collectors. Every PAPI call for the real event set happens on this thread,
// which is what PAPI requires of the thread that created it.
void PAPICounterFunction::run_sampler() {
  // initialize() waits on this; release it on every exit path, successful or
  // not, so a PAPI failure never blocks application start-up.
  bool notified = false;
  auto notify_ready = [&]() {
    if (!notified) {
      notified = true;
      sampler_ready.set_value();
    }
  };

  // The library has to be up before this thread can register with it;
  // PAPI_register_thread fails with PAPI_ENOINIT the other way round.
  if (!initialize_library()) {
    enabled.store(false);
    sampler_running.store(false);
    notify_ready();
    return;
  }

  // Not fatal: this thread called PAPI_library_init, so PAPI already knows it
  // and may report the registration as redundant.
  int retval = PAPI_register_thread();
  if (retval != PAPI_OK) {
    DFTRACER_LOG_DEBUG("PAPI_register_thread returned %d (%s)", retval,
                       PAPI_strerror(retval));
  }

  if (!start_counters()) {
    enabled.store(false);
    sampler_running.store(false);
    notify_ready();
    PAPI_unregister_thread();
    return;
  }
  library_ready.store(true);

  if (uv_loop_init(&loop) != 0) {
    DFTRACER_LOG_WARN("PAPI sampler could not initialize its libuv loop", "");
    stop_counters();
    library_ready.store(false);
    sampler_running.store(false);
    notify_ready();
    PAPI_unregister_thread();
    return;
  }

  timer.data = this;
  stop_signal.data = this;
  uv_timer_init(&loop, &timer);
  uv_async_init(&loop, &stop_signal, PAPICounterFunction::on_stop);

  uint64_t interval_ms = static_cast<uint64_t>(config->papi_sample_interval_ms);
  if (interval_ms == 0) interval_ms = 1000;
  uv_timer_start(&timer, PAPICounterFunction::on_timer, interval_ms,
                 interval_ms);
  loop_ready.store(true);

  // Counting is live and the loop is armed: the application may proceed.
  notify_ready();

  uv_run(&loop, UV_RUN_DEFAULT);

  // Final read so the last interval is not lost, then tear down on the same
  // thread that created the event set, as PAPI requires.
  emit_sample();
  stop_counters();
  library_ready.store(false);

  uv_close(reinterpret_cast<uv_handle_t *>(&timer), nullptr);
  uv_close(reinterpret_cast<uv_handle_t *>(&stop_signal), nullptr);
  uv_run(&loop, UV_RUN_DEFAULT);
  uv_loop_close(&loop);
  loop_ready.store(false);

  PAPI_unregister_thread();
}

void PAPICounterFunction::on_timer(uv_timer_t *handle) {
  if (handle == nullptr || handle->data == nullptr) return;
  static_cast<PAPICounterFunction *>(handle->data)->emit_sample();
}

void PAPICounterFunction::on_stop(uv_async_t *handle) {
  if (handle == nullptr || handle->data == nullptr) return;
  auto *self = static_cast<PAPICounterFunction *>(handle->data);
  uv_timer_stop(&self->timer);
  uv_stop(&self->loop);
}

void PAPICounterFunction::emit_sample() {
  if (!library_ready.load() || event_set == PAPI_NULL) return;

  int retval = PAPI_read(event_set, current_values.data());
  if (retval != PAPI_OK) {
    log_papi_status("PAPI_read", retval);
    return;
  }

  TimeResolution now = logger->get_time();

  // One record per counter: "name" is the counter, "cat" its family, and the
  // reading is carried in args. process_id/thread_id are passed explicitly so
  // the sample is attributed to the traced process, not to the sampler thread.
  for (size_t idx = 0; idx < events.size(); ++idx) {
    long long value = current_values[idx];
    long long delta = value - last_values[idx];

    auto metadata = new Metadata();
    metadata->insert_or_assign("value", value);
    metadata->insert_or_assign("delta", delta);
    // Multiplexed readings are extrapolated from a time slice, so mark them:
    // they are estimates and can even move backwards.
    metadata->insert_or_assign("multiplex", multiplex_active ? 1 : 0);

    int current_index = index.fetch_add(1, std::memory_order_relaxed);
    buffer_manager->log_counter_event(
        current_index, events[idx].c_str(), categories[idx].c_str(),
        TraceEventType::TRACE_TYPE_PAPI, now, process_id, thread_id, metadata);
  }

  last_values = current_values;
}

void PAPICounterFunction::finalize() {
  if (!sampler_running.exchange(false)) return;

  // Wake the loop so it can drain, emit a last sample and tear its event set
  // down. If the loop never came up, the sampler is already on its way out.
  if (loop_ready.load()) {
    uv_async_send(&stop_signal);
  }
  if (sampler_thread.joinable()) {
    sampler_thread.join();
  }
  DFTRACER_LOG_INFO("PAPI sampler stopped", "");
}

}  // namespace dftracer

#endif  // DFTRACER_PAPI_TRACING_ENABLE
