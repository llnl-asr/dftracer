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
      groups(),
      enabled(false),
      library_ready(false),
      index(0),
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

// The build-time probe emits "FAMILY:ev[,ev...][;FAMILY:...]". Families are
// decided there, next to the counter detection that produced them; here they
// only say how a sample is written out.
std::vector<PAPICounterFunction::CounterGroup>
PAPICounterFunction::parse_groups(const std::string &value) {
  std::vector<CounterGroup> parsed;
  size_t at = 0;
  while (at < value.size()) {
    size_t end = value.find(';', at);
    if (end == std::string::npos) end = value.size();
    std::string chunk = value.substr(at, end - at);
    at = end + 1;

    size_t colon = chunk.find(':');
    if (colon == std::string::npos) continue;
    CounterGroup group;
    group.category = trim_copy(chunk.substr(0, colon));
    if (group.category.empty()) continue;

    std::string members = chunk.substr(colon + 1);
    size_t member_at = 0;
    while (member_at < members.size()) {
      size_t comma = members.find(',', member_at);
      if (comma == std::string::npos) comma = members.size();
      auto name = trim_copy(members.substr(member_at, comma - member_at));
      member_at = comma + 1;
      if (!name.empty()) group.events.push_back(name);
    }
    if (!group.events.empty()) parsed.push_back(std::move(group));
  }
  return parsed;
}

// A preset PAPI computes by SUBTRACTING one native event from another --
// PAPI_BR_NTK is RETIRED_BRANCH_INSTRUCTIONS minus
// RETIRED_TAKEN_BRANCH_INSTRUCTIONS, for instance.
//
// Only the subtractive ones matter here. Multiplexing counts each native in a
// different time slice and scales it independently, so subtracting two ~1%
// estimates of two nearly equal large numbers leaves nothing but the error:
// measured on an MI300A, PAPI_BR_NTK reads 64000010 when its set fits the
// hardware and -191117 when the same set is multiplexed, a negative count of
// branches. A DERIVED_ADD preset such as PAPI_FP_INS is not affected in the
// same way -- summing two estimates keeps the same relative error and cannot
// change sign -- so those are kept and simply share the family's estimate
// quality.
static bool is_subtractive_derived_event(const std::string &name) {
  PAPI_event_info_t info;
  int code = 0;
  if (PAPI_event_name_to_code(const_cast<char *>(name.c_str()), &code) !=
      PAPI_OK) {
    return false;
  }
  if (PAPI_get_event_info(code, &info) != PAPI_OK) return false;
  if (info.count <= 1) return false;
  return strstr(info.derived, "SUB") != nullptr;
}

// How many of `events` the hardware will actually count together, found by
// adding them one at a time to a throwaway event set.
//
// Counting names against PAPI_num_hwctrs() is not the same question and gets
// the wrong answer in both directions: on an MI300A with 5 counters, six branch
// events fit in one set (they share natives) while a different four do not.
static size_t count_fitting(const std::vector<std::string> &events) {
  int set = PAPI_NULL;
  if (PAPI_create_eventset(&set) != PAPI_OK) return 0;
  if (PAPI_assign_eventset_component(set, 0) != PAPI_OK) {
    PAPI_destroy_eventset(&set);
    return 0;
  }
  size_t fitting = 0;
  for (const auto &name : events) {
    if (PAPI_add_named_event(set, name.c_str()) == PAPI_OK) fitting++;
  }
  PAPI_cleanup_eventset(set);
  PAPI_destroy_eventset(&set);
  return fitting;
}

// Bind an event set to the whole process: component first (PAPI_attach and
// PAPI_set_multiplex both fail with PAPI_ECMP otherwise), then attach to the
// thread-group leader, then ask for inheritance so threads the application
// spawns later are counted as well. Without PAPI_INHERIT_ALL only the main
// thread would be counted; attaching to individual thread ids is not an option
// because reads of such an event set return frozen garbage on this platform.
bool PAPICounterFunction::bind_to_process(int set, bool multiplex) const {
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

  if (multiplex) {
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

  // Which counters to use, and how they are grouped into families, is decided
  // at build time by cmake/probes/papi_probe.c and baked into
  // dftracer_config.hpp. DFTRACER_PAPI_EVENTS overrides it for a run.
  auto configured = normalize_events(config->papi_events);
  if (configured.empty()) {
    groups = parse_groups(DFTRACER_PAPI_DETECTED_EVENTS);
    DFTRACER_LOG_INFO(
        "PAPI: using the counters found for this machine at build time; set "
        "DFTRACER_PAPI_EVENTS to choose your own",
        "");
  } else {
    // An explicit list carries no family information; it becomes one family.
    // This is the way to get exact counts: name no more counters than the
    // machine can hold, and take one run per set you care about.
    CounterGroup group;
    group.category = "PAPI";
    group.events = configured;
    groups.assign(1, group);
    std::ostringstream chosen;
    for (size_t i = 0; i < configured.size(); ++i) {
      if (i > 0) chosen << ',';
      chosen << configured[i];
    }
    DFTRACER_LOG_INFO("PAPI: DFTRACER_PAPI_EVENTS selected %d counters: %s",
                      (int)configured.size(), chosen.str().c_str());
  }
  if (groups.empty()) {
    DFTRACER_LOG_WARN("PAPI tracing disabled: no counters configured", "");
    return false;
  }

  // Decide per family whether it has to be time-shared, by asking the hardware
  // rather than by counting names: a family fits only if every one of its
  // counters can be added to one event set.
  //
  // Time-sharing is not a preference here. The counters are attached to the
  // process with PAPI_INHERIT_ALL, and an event set can only be started while
  // it fits, so multiplexing is the only way to observe more counters than the
  // machine has registers in a single run. Rotating whole event sets instead
  // does not work: inheritance binds a set to the threads that exist when it
  // starts, so a set first started later reads essentially nothing (measured on
  // an MI300A: 1070 against 17,554,403,015 for a set started before the
  // threads).
  // First question, and the one that decides everything: does the whole
  // selection fit at once? Per-family fitting is not enough -- the registers
  // are shared by every started event set, so if the total exceeds them the
  // families beyond the budget fail to start at all and their counters are
  // simply missing from the trace.
  std::vector<std::string> everything;
  size_t total_events = 0;
  for (const auto &group : groups) {
    everything.insert(everything.end(), group.events.begin(),
                      group.events.end());
    total_events += group.events.size();
  }
  const bool all_fit = count_fitting(everything) == total_events;

  if (all_fit) {
    // Nothing is time-shared when everything fits, whatever was configured:
    // multiplexing a selection the hardware can hold outright would trade exact
    // counts for estimates and buy nothing.
    for (auto &group : groups) group.multiplexed = false;
    if (config->papi_multiplex) {
      DFTRACER_LOG_INFO(
          "PAPI: DFTRACER_PAPI_MULTIPLEX is set but all %d counters fit this "
          "machine, so they are counted exactly instead",
          (int)total_events);
    }
    DFTRACER_LOG_INFO("PAPI: %d counters fit the hardware; counts are exact",
                      (int)total_events);
    return true;
  }

  bool any_multiplexed = false;
  for (auto &group : groups) {
    const size_t fitting = count_fitting(group.events);
    group.multiplexed = fitting < group.events.size();

    if (group.multiplexed) {
      any_multiplexed = true;
      // Derived presets are dropped rather than time-shared: a scaled estimate
      // of each native makes their combination meaningless, not merely
      // imprecise. Select a set of counters that fits if you need them.
      std::vector<std::string> keep;
      for (const auto &event : group.events) {
        if (is_subtractive_derived_event(event)) {
          // ERROR, not WARN: a release build compiles WARN and INFO out
          // entirely, and silently dropping a counter the user asked for is
          // exactly the thing they must be told about.
          DFTRACER_LOG_ERROR(
              "PAPI counter %s is one native event subtracted from another "
              "and the %s family does not fit this machine; dropping it, "
              "because time-sharing the two would report impossible values "
              "such as a negative count",
              event.c_str(), group.category.c_str());
          continue;
        }
        keep.push_back(event);
      }
      group.events = std::move(keep);
    }
  }
  groups.erase(std::remove_if(groups.begin(), groups.end(),
                              [](const CounterGroup &group) {
                                return group.events.empty();
                              }),
               groups.end());
  if (groups.empty()) {
    DFTRACER_LOG_WARN("PAPI tracing disabled: no counters left to collect", "");
    return false;
  }

  if (any_multiplexed) {
    int retval = PAPI_multiplex_init();
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_multiplex_init", retval);
      for (auto &group : groups) group.multiplexed = false;
      any_multiplexed = false;
    }
  }

  // Say plainly that the numbers are estimates, and how to get exact ones. The
  // error is not a fixed small percentage: measured against an exact baseline
  // it was under 1% on a steady loop but 5-6% on a workload that alternates
  // phases, and biased in a consistent direction rather than noisy.
  if (any_multiplexed) {
    // ERROR for the same reason: this is the caveat that decides whether the
    // numbers in the trace can be quoted, and it has to survive a release
    // build, where DFTRACER_LOG_WARN is a no-op.
    DFTRACER_LOG_ERROR(
        "PAPI: %d counters were requested but this machine has %d hardware "
        "counters, so they are time-shared and every multiplexed reading is a "
        "scaled estimate (measured error 1%% to 6%%, worse on a workload with "
        "phases). For exact counts, name a set of counters that fits in "
        "DFTRACER_PAPI_EVENTS and take one run per set.",
        (int)total_events, DFTRACER_PAPI_HW_COUNTERS);
  }
  return true;
}

// One event set per family, so a family's counters are read at one instant.
bool PAPICounterFunction::start_counters() {
  std::vector<CounterGroup> started;
  for (auto &group : groups) {
    int retval = PAPI_create_eventset(&group.event_set);
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_create_eventset", retval);
      group.event_set = PAPI_NULL;
      continue;
    }
    if (!bind_to_process(group.event_set, group.multiplexed)) {
      PAPI_destroy_eventset(&group.event_set);
      group.event_set = PAPI_NULL;
      continue;
    }

    std::vector<std::string> added;
    for (const auto &event_name : group.events) {
      retval = PAPI_add_named_event(group.event_set, event_name.c_str());
      if (retval != PAPI_OK) {
        DFTRACER_LOG_WARN("PAPI counter %s cannot be counted here (%d, %s)",
                          event_name.c_str(), retval, PAPI_strerror(retval));
        continue;
      }
      added.push_back(event_name);
    }
    group.events = std::move(added);
    if (group.events.empty()) {
      PAPI_cleanup_eventset(group.event_set);
      PAPI_destroy_eventset(&group.event_set);
      group.event_set = PAPI_NULL;
      continue;
    }

    group.last_values.assign(group.events.size(), 0);
    group.current_values.assign(group.events.size(), 0);

    retval = PAPI_start(group.event_set);
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_start", retval);
      PAPI_cleanup_eventset(group.event_set);
      PAPI_destroy_eventset(&group.event_set);
      group.event_set = PAPI_NULL;
      continue;
    }
    started.push_back(std::move(group));
  }
  groups = std::move(started);

  if (groups.empty()) {
    DFTRACER_LOG_WARN(
        "PAPI tracing disabled: none of the configured counters can be counted "
        "on this machine",
        "");
    return false;
  }

  std::ostringstream summary;
  int total = 0;
  for (size_t g = 0; g < groups.size(); ++g) {
    if (g > 0) summary << ' ';
    summary << groups[g].category << '(' << groups[g].events.size()
            << (groups[g].multiplexed ? ",mux" : "") << ')';
    total += static_cast<int>(groups[g].events.size());
  }
  DFTRACER_LOG_INFO("PAPI tracing enabled with %d counters in %d families: %s",
                    total, static_cast<int>(groups.size()),
                    summary.str().c_str());
  return true;
}

void PAPICounterFunction::stop_counters() {
  for (auto &group : groups) {
    if (group.event_set == PAPI_NULL) continue;
    PAPI_stop(group.event_set, group.current_values.data());
    PAPI_cleanup_eventset(group.event_set);
    PAPI_destroy_eventset(&group.event_set);
    group.event_set = PAPI_NULL;
  }
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

  uint64_t interval_ms = static_cast<uint64_t>(config->papi_sample_interval_ms);
  if (interval_ms == 0) interval_ms = 1000;

  event_loop = dftracer::Singleton<dftracer::EventLoop>::get_instance();
  if (event_loop == nullptr ||
      !event_loop->add_timer(interval_ms, interval_ms,
                             [this]() { this->emit_sample(); })) {
    DFTRACER_LOG_WARN("PAPI sampler could not arm its timer on the event loop",
                      "");
    stop_counters();
    library_ready.store(false);
    sampler_running.store(false);
    notify_ready();
    PAPI_unregister_thread();
    return;
  }
  loop_ready.store(true);

  // Counting is live and the loop is armed: the application may proceed.
  notify_ready();

  // This thread created the event set, so it has to be the one that runs the
  // loop: PAPI only lets the creating thread read and tear an event set down.
  event_loop->run();

  // Final read so the last interval is not lost, then tear down on the same
  // thread, still as PAPI requires.
  emit_sample();
  stop_counters();
  library_ready.store(false);
  loop_ready.store(false);

  PAPI_unregister_thread();
}

void PAPICounterFunction::emit_sample() {
  if (!library_ready.load()) return;

  TimeResolution now = logger->get_time();

  // One record per family, carrying all of that family's counters. Writing a
  // record per counter instead is most of what a PAPI trace costs: here 17
  // counters become 4 records a sample rather than 17.
  for (auto &group : groups) {
    if (group.event_set == PAPI_NULL) continue;

    int retval = PAPI_read(group.event_set, group.current_values.data());
    if (retval != PAPI_OK) {
      log_papi_status("PAPI_read", retval);
      continue;
    }

    auto metadata = new Metadata();
    for (size_t idx = 0; idx < group.events.size(); ++idx) {
      long long value = group.current_values[idx];
      metadata->insert_or_assign(group.events[idx], value);
      metadata->insert_or_assign(group.events[idx] + "_delta",
                                 value - group.last_values[idx]);
    }
    // Only an oversized family is time-shared, so this says per record whether
    // its values are exact counts or scaled estimates.
    metadata->insert_or_assign("multiplex", group.multiplexed ? 1 : 0);

    // process_id/thread_id are passed explicitly so the sample is attributed to
    // the traced process rather than to the sampler thread.
    int current_index = index.fetch_add(1, std::memory_order_relaxed);
    buffer_manager->log_counter_event(current_index, group.category.c_str(),
                                      "papi", TraceEventType::TRACE_TYPE_PAPI,
                                      now, process_id, thread_id, metadata);

    group.last_values = group.current_values;
  }
}

void PAPICounterFunction::finalize() {
  if (!sampler_running.exchange(false)) return;

  // Wake the loop so it can drain, emit a last sample and tear its event set
  // down. If the loop never came up, the sampler is already on its way out.
  if (loop_ready.load() && event_loop != nullptr) {
    event_loop->stop();
  }
  if (sampler_thread.joinable()) {
    sampler_thread.join();
  }
  DFTRACER_LOG_INFO("PAPI sampler stopped", "");
}

}  // namespace dftracer

#endif  // DFTRACER_PAPI_TRACING_ENABLE
