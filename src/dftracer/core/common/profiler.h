#ifndef DFTRACER_CORE_COMMON_PROFILER_H
#define DFTRACER_CORE_COMMON_PROFILER_H

#include <dftracer/core/dftracer_config.hpp>

/**
 * Self-profiling of dftracer's own pipeline: how long each step of getting an
 * event from the traced call into the trace takes, per event.
 *
 * Built only with -DDFTRACER_ENABLE_PROFILING=ON. Otherwise every
 * DFTRACER_PROFILE_* macro below expands to nothing and none of this file's
 * code exists in the library.
 *
 * Phases (Phase):
 *   capture[<layer>]  the interceptor or API region itself, per
 *                     TraceEventType (LIBC_IO for POSIX and STDIO, C_APP,
 *                     CPP_APP, MPI, HDF5, ...): filtering, the Metadata,
 *                     timestamps, DFTLogger::log. Counted once per event.
 *   filtered          intercepted calls that were not traced (is_traced said
 *                     no), so their cost is not hidden in capture.
 *   buffer            the buffer manager's own work: its lock (and the wait
 *                     for it), readiness checks, ring slot bookkeeping.
 *   aggregation       Aggregator: the rule check, folding into buckets and
 *                     collecting finished intervals.
 *   serialization     JSON line (in-process) or the binary ring record
 *                     (producer/consumer mode).
 *   compression       gzip of a full buffer (in-process only).
 *   store             fwrite of the compressed buffer (in-process) or the
 *                     slot and the copy into the shared-memory ring
 *                     (producer/consumer).
 *
 * Phases nest -- capture calls into the buffer manager, which serializes,
 * compresses and writes -- and each records only its *self* time (its own
 * time minus that of the phases inside it), so the phases add up to the
 * whole overhead and nothing is counted twice.
 *
 * Everything but the clock is decided at compile time: the phase and whether
 * a probe counts are template arguments, so a probe is two TSC reads and a
 * few adds into this thread's counters at a constant offset; no lookup, no
 * stack, no shared writes. Self time needs only one thread-local word: the
 * time spent in phases nested in the open one. Converting ticks to ns,
 * summing threads and printing happen once, at finalize, where
 * DFTLogger::finalize prints to stderr and appends CSV to
 * $DFTRACER_PROFILE_FILE when set.
 */
#ifdef DFTRACER_PROFILING_ENABLE

#include <dftracer/core/common/enumeration.h>
#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <vector>
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace dftracer {
namespace profiling {

// One capture phase per TraceEventType, then the shared phases.
enum Phase : unsigned {
  PHASE_CAPTURE = 0,
  PHASE_FILTERED = TraceEventType::TRACE_TYPE_MAX,
  PHASE_BUFFER,
  PHASE_AGGREGATION,
  PHASE_SERIALIZATION,
  PHASE_COMPRESSION,
  PHASE_STORE,
  PHASE_MAX
};

constexpr unsigned capture(TraceEventType type) {
  return type < TraceEventType::TRACE_TYPE_MAX
             ? PHASE_CAPTURE + static_cast<unsigned>(type)
             : PHASE_CAPTURE;
}

inline const char* phase_name(unsigned phase) {
  switch (phase) {
    case PHASE_FILTERED:
      return "filtered";
    case PHASE_BUFFER:
      return "buffer";
    case PHASE_AGGREGATION:
      return "aggregation";
    case PHASE_SERIALIZATION:
      return "serialization";
    case PHASE_COMPRESSION:
      return "compression";
    case PHASE_STORE:
      return "store";
    default:
      return "capture";
  }
}

// A cheap, monotonic tick: the TSC on x86 (a few ns), nanoseconds elsewhere.
// Converted to ns at report time against steady_clock.
inline uint64_t ticks() {
#if defined(__x86_64__) || defined(__i386__)
  return __rdtsc();
#else
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
#endif
}

inline uint64_t steady_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

/**
 * One thread's counters. Plain data, zero-initialized with the thread, so
 * reaching it is a TLS offset with no init guard. Written only by its thread;
 * report() reads the others' racily, which at worst misses an add in flight.
 */
struct Counters {
  uint64_t ticks[PHASE_MAX];
  uint64_t count[PHASE_MAX];
  uint64_t child;  // ticks of the phases nested in the open one
  int state;       // 0 new, 1 registered, 2 retired (thread exiting)
};

// The default TLS model: initial-exec would make a dlopen'd libdftracer_core
// (Python) fail to load (see df_gettid in posix_bypass.cpp).
inline thread_local Counters tl_counters;

struct Totals {
  uint64_t ticks[PHASE_MAX] = {};
  uint64_t count[PHASE_MAX] = {};
};

// Every thread's counters, plus the sums of threads that have exited.
class Registry {
 public:
  Registry() : origin_ticks_(ticks()), origin_ns_(steady_ns()) {
    pthread_key_create(&exit_key_, &Registry::on_thread_exit);
  }

  void add(Counters* c) {
    std::lock_guard<std::mutex> lock(mtx_);
    live_.push_back(c);
    c->state = 1;
    // Any non-null value: the key only exists to run on_thread_exit.
    pthread_setspecific(exit_key_, c);
  }
  Totals sum() {
    std::lock_guard<std::mutex> lock(mtx_);
    Totals t = retired_;
    for (Counters* c : live_) fold(t, *c);
    return t;
  }
  // ns per tick, measured over the life of the process (at least 10 ms).
  double ns_per_tick() {
#if defined(__x86_64__) || defined(__i386__)
    uint64_t t1 = ticks(), n1 = steady_ns();
    while (n1 - origin_ns_ < 10000000ULL) {
      t1 = ticks();
      n1 = steady_ns();
    }
    return static_cast<double>(n1 - origin_ns_) /
           static_cast<double>(t1 - origin_ticks_);
#else
    return 1.0;
#endif
  }
  // In a fork()ed child only the forking thread exists, and what the parent
  // measured is the parent's to report.
  void reset_in_child() {
    new (&mtx_) std::mutex();
    live_.clear();
    retired_ = Totals();
    reported_ = false;
    Counters& self = tl_counters;
    const int state = self.state;
    self = Counters();
    if (state == 1) {
      live_.push_back(&self);
      self.state = 1;
    }
  }
  bool claim_report() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (reported_) return false;
    reported_ = true;
    return true;
  }

 private:
  static void fold(Totals& t, const Counters& c) {
    for (unsigned p = 0; p < PHASE_MAX; ++p) {
      t.ticks[p] += c.ticks[p];
      t.count[p] += c.count[p];
    }
  }
  static void on_thread_exit(void* arg);

  std::mutex mtx_;
  std::vector<Counters*> live_;
  Totals retired_;
  pthread_key_t exit_key_;
  uint64_t origin_ticks_;
  uint64_t origin_ns_;
  bool reported_ = false;

  friend Registry& registry();
};

inline void after_fork_child();

// Never destroyed: threads and atexit handlers may log after static
// destruction has begun.
inline Registry& registry() {
  static Registry* r = [] {
    auto* reg = new Registry();
    pthread_atfork(nullptr, nullptr, after_fork_child);
    return reg;
  }();
  return *r;
}

// At thread exit: fold its counters into retired_ and forget it. The thread
// may still log afterwards (other TLS destructors); those adds are dropped.
inline void Registry::on_thread_exit(void* arg) {
  auto* c = static_cast<Counters*>(arg);
  Registry& reg = registry();
  std::lock_guard<std::mutex> lock(reg.mtx_);
  fold(reg.retired_, *c);
  for (auto it = reg.live_.begin(); it != reg.live_.end(); ++it) {
    if (*it == c) {
      reg.live_.erase(it);
      break;
    }
  }
  c->state = 2;
}

inline void after_fork_child() { registry().reset_in_child(); }

__attribute__((noinline, cold)) inline void register_thread() {
  if (tl_counters.state == 0) registry().add(&tl_counters);
}

/** The start of an open phase. */
struct Frame {
  uint64_t start;
  uint64_t saved_child;
};

inline void begin(Frame& f) {
  Counters& c = tl_counters;
  if (__builtin_expect(c.state == 0, 0)) register_thread();
  f.saved_child = c.child;
  c.child = 0;
  f.start = ticks();
}

/** Closes `f` as phase P, adding its self time (and one occurrence if
 * Count). P is a compile-time constant: a fixed offset into the counters. */
template <unsigned P, bool Count>
inline void end(Frame& f) {
  static_assert(P < PHASE_MAX, "unknown profiling phase");
  const uint64_t inclusive = ticks() - f.start;
  Counters& c = tl_counters;
  c.ticks[P] += inclusive - c.child;
  if constexpr (Count) ++c.count[P];
  c.child = f.saved_child + inclusive;
}

/** For the few places whose layer is only known at run time (API regions of
 * a registered event type): the same, with the phase as an index. */
template <bool Count>
inline void end_dynamic(Frame& f, unsigned phase) {
  const uint64_t inclusive = ticks() - f.start;
  Counters& c = tl_counters;
  if (phase >= PHASE_MAX) phase = PHASE_CAPTURE;
  c.ticks[phase] += inclusive - c.child;
  if constexpr (Count) ++c.count[phase];
  c.child = f.saved_child + inclusive;
}

template <unsigned P, bool Count>
class Scope {
 public:
  Scope() { begin(frame_); }
  ~Scope() { end<P, Count>(frame_); }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  Frame frame_;
};

template <bool Count>
class DynamicScope {
 public:
  explicit DynamicScope(unsigned phase) : phase_(phase) { begin(frame_); }
  ~DynamicScope() { end_dynamic<Count>(frame_, phase_); }
  DynamicScope(const DynamicScope&) = delete;
  DynamicScope& operator=(const DynamicScope&) = delete;

 private:
  Frame frame_;
  unsigned phase_;
};

/**
 * Prints the totals (once per process). `mode` names the buffer manager,
 * "local" or "pc". With $DFTRACER_PROFILE_FILE set, also appends one CSV row
 * per phase: pid,mode,phase,layer,count,total_ns,avg_ns,ns_per_event.
 */
inline void report(const char* mode) {
  Registry& reg = registry();
  if (!reg.claim_report()) return;
  const double ns_tick = reg.ns_per_tick();
  // What one empty probe costs, so readers can discount it. Run against a
  // copy of this thread's counters, so it lands in no reported phase.
  double probe_ns = 0;
  {
    const Counters saved = tl_counters;
    constexpr int kProbe = 10000;
    const uint64_t t0 = ticks();
    for (int i = 0; i < kProbe; ++i) {
      Frame f;
      begin(f);
      end<PHASE_STORE, true>(f);
    }
    probe_ns = static_cast<double>(ticks() - t0) * ns_tick / kProbe;
    tl_counters = saved;
  }
  Totals t = reg.sum();
  uint64_t events = 0;
  for (unsigned p = PHASE_CAPTURE; p < PHASE_FILTERED; ++p)
    events += t.count[p];
  const int pid = static_cast<int>(getpid());
  FILE* csv = nullptr;
  if (const char* path = std::getenv("DFTRACER_PROFILE_FILE")) {
    if (path[0] != '\0') csv = std::fopen(path, "a");
  }
  std::fprintf(stderr,
               "[DFTRACER PROFILE] pid %d mode %s: %llu events, %.1f ns per "
               "probe (included below)\n",
               pid, mode, static_cast<unsigned long long>(events), probe_ns);
  std::fprintf(stderr, "[DFTRACER PROFILE] %-26s %12s %14s %12s %14s\n",
               "phase", "count", "total_ms", "avg_ns", "ns_per_event");
  double sum_per_event = 0;
  for (unsigned p = 0; p < PHASE_MAX; ++p) {
    if (t.ticks[p] == 0 && t.count[p] == 0) continue;
    const double total_ns = static_cast<double>(t.ticks[p]) * ns_tick;
    const double avg_ns =
        t.count[p] ? total_ns / static_cast<double>(t.count[p]) : 0.0;
    const double per_event =
        events ? total_ns / static_cast<double>(events) : 0.0;
    if (p != PHASE_FILTERED) sum_per_event += per_event;
    char label[64];
    const char* layer = "";
    if (p < PHASE_FILTERED) {
      layer = to_string(static_cast<TraceEventType>(p));
      std::snprintf(label, sizeof(label), "capture[%s]", layer);
    } else {
      std::snprintf(label, sizeof(label), "%s", phase_name(p));
    }
    std::fprintf(stderr,
                 "[DFTRACER PROFILE] %-26s %12llu %14.3f %12.1f %14.1f\n",
                 label, static_cast<unsigned long long>(t.count[p]),
                 total_ns / 1e6, avg_ns, per_event);
    if (csv != nullptr)
      std::fprintf(csv, "%d,%s,%s,%s,%llu,%.0f,%.1f,%.1f\n", pid, mode,
                   phase_name(p), layer,
                   static_cast<unsigned long long>(t.count[p]), total_ns,
                   avg_ns, per_event);
  }
  std::fprintf(stderr, "[DFTRACER PROFILE] %-26s %12s %14s %12s %14.1f\n",
               "total (excl. filtered)", "", "", "", sum_per_event);
  if (csv != nullptr) std::fclose(csv);
}

}  // namespace profiling
}  // namespace dftracer

#define DFTRACER_PROFILE_CAT2(a, b) a##b
#define DFTRACER_PROFILE_CAT(a, b) DFTRACER_PROFILE_CAT2(a, b)
#define DFTRACER_PROFILE_NAME DFTRACER_PROFILE_CAT(dftracer_profile_, __LINE__)
// Phases as compile-time constants.
#define DFTRACER_PROFILE_CAPTURE(type) (::dftracer::profiling::capture(type))
#define DFTRACER_PROFILE_PHASE(name) (::dftracer::profiling::PHASE_##name)
// Times the rest of the enclosing block as `phase`; counts one occurrence.
#define DFTRACER_PROFILE_SCOPE(phase) \
  ::dftracer::profiling::Scope<phase, true> DFTRACER_PROFILE_NAME
// The same without counting: one more slice of an occurrence counted
// elsewhere.
#define DFTRACER_PROFILE_SCOPE_UNCOUNTED(phase) \
  ::dftracer::profiling::Scope<phase, false> DFTRACER_PROFILE_NAME
// A phase only known at run time (a registered event type's layer).
#define DFTRACER_PROFILE_SCOPE_DYNAMIC(phase, count) \
  ::dftracer::profiling::DynamicScope<count> DFTRACER_PROFILE_NAME(phase)
// A step that does not fit a block (the two halves of an interceptor).
#define DFTRACER_PROFILE_BEGIN(frame) \
  ::dftracer::profiling::Frame frame; \
  ::dftracer::profiling::begin(frame)
#define DFTRACER_PROFILE_END(frame, phase, count) \
  ::dftracer::profiling::end<phase, count>(frame)
// Closes `frame` as `phase` (uncounted) if `traced`, else as one filtered call.
#define DFTRACER_PROFILE_END_TRACED(frame, traced, phase)                    \
  if (traced) {                                                              \
    ::dftracer::profiling::end<phase, false>(frame);                         \
  } else {                                                                   \
    ::dftracer::profiling::end<::dftracer::profiling::PHASE_FILTERED, true>( \
        frame);                                                              \
  }
#define DFTRACER_PROFILE_REPORT(mode) ::dftracer::profiling::report(mode)

#else  // !DFTRACER_PROFILING_ENABLE

#define DFTRACER_PROFILE_SCOPE(phase)
#define DFTRACER_PROFILE_SCOPE_UNCOUNTED(phase)
#define DFTRACER_PROFILE_SCOPE_DYNAMIC(phase, count)
#define DFTRACER_PROFILE_BEGIN(frame)
#define DFTRACER_PROFILE_END(frame, phase, count)
#define DFTRACER_PROFILE_END_TRACED(frame, traced, phase)
#define DFTRACER_PROFILE_REPORT(mode)

#endif  // DFTRACER_PROFILING_ENABLE
#endif  // DFTRACER_CORE_COMMON_PROFILER_H
