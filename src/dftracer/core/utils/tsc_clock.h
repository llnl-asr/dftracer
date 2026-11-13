#ifndef DFTRACER_UTILS_TSC_CLOCK_H
#define DFTRACER_UTILS_TSC_CLOCK_H

#include <dftracer/core/utils/posix_bypass.h>
#include <fcntl.h>
#include <time.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define DFTRACER_TSC_CLOCK_SUPPORTED 1
#endif

namespace dftracer {

/**
 * Wall-clock time in microseconds, read from the CPU's time-stamp counter
 * when the kernel itself uses it as its clock source (so it runs at a fixed
 * rate and agrees across cores), else from the realtime clock. A counter read
 * takes about a third of a clock_gettime call.
 *
 * Each thread anchors to CLOCK_REALTIME once per millisecond of counter time,
 * so an error in the counter rate never adds up past that window. The rate is
 * measured over the first 2 ms after start(); until then every read is a
 * clock_gettime call. A thread's readings never go back. Two threads can
 * disagree by a few tens of ns (more under valgrind), so a reading on one
 * thread can round to one microsecond after a later reading on another.
 */
class TscClock {
 public:
  /** Uses the counter when `wanted` and the kernel's clock source is tsc.
   * Call once, before the threads that read the clock start. */
  static void start(bool wanted) {
    rate_.store(0, std::memory_order_relaxed);
    enabled_ = wanted && kernel_uses_tsc();
#ifdef DFTRACER_TSC_CLOCK_SUPPORTED
    if (enabled_) start_ns_ = sample(&start_tsc_);
#endif
  }

  /** Whether reads come from the counter once its rate is known. */
  static bool enabled() { return enabled_; }

  static uint64_t now_us() {
#ifdef DFTRACER_TSC_CLOCK_SUPPORTED
    const double ns_per_tick = rate_.load(std::memory_order_relaxed);
    if (__builtin_expect(ns_per_tick > 0, 1)) {
      Anchor& a = anchor();
      const uint64_t tsc = __rdtsc();
      uint64_t t;
      if (tsc - a.tsc < a.window) {
        // Signed: within the window the change is small, and x86 converts
        // signed integers to and from double in one instruction each.
        t = a.ns + static_cast<uint64_t>(static_cast<int64_t>(
                       static_cast<double>(static_cast<int64_t>(tsc - a.tsc)) *
                       ns_per_tick));
      } else {
        t = sample(&a.tsc);
        a.ns = t;
        a.window = static_cast<uint64_t>(kAnchorNs / ns_per_tick);
      }
      if (t < a.last) t = a.last;
      a.last = t;
      return t / 1000;
    }
    if (enabled_) return calibrate() / 1000;
#endif
    return wall_ns() / 1000;
  }

 private:
  static constexpr double kAnchorNs = 1e6;
  static constexpr uint64_t kCalibrateNs = 2000000;
  static constexpr uint64_t kTightTicks = 1000;

  struct Anchor {
    uint64_t ns = 0;
    uint64_t tsc = 0;
    uint64_t window = 0;  // 0: anchor on the next read
    uint64_t last = 0;
  };
  static Anchor& anchor() {
    thread_local Anchor a;
    return a;
  }

  static uint64_t wall_ns() {
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
  }

#ifdef DFTRACER_TSC_CLOCK_SUPPORTED
  // A thread paused between the counter read and the clock read would shift
  // every reading of the next window, so keep the tightest of a few tries.
  static __attribute__((noinline)) uint64_t sample(uint64_t* tsc) {
    uint64_t best = ~0ULL, ns = 0;
    for (int i = 0; i < 4 && best > kTightTicks; ++i) {
      const uint64_t before = __rdtsc();
      const uint64_t t = wall_ns();
      const uint64_t gap = __rdtsc() - before;
      if (gap < best) {
        best = gap;
        ns = t;
        *tsc = before + gap / 2;
      }
    }
    return ns;
  }

  static __attribute__((noinline)) uint64_t calibrate() {
    uint64_t tsc = 0;
    const uint64_t t = sample(&tsc);
    if (t - start_ns_ >= kCalibrateNs && tsc > start_tsc_)
      rate_.store(static_cast<double>(t - start_ns_) /
                      static_cast<double>(tsc - start_tsc_),
                  std::memory_order_relaxed);
    return t;
  }
#endif

  static bool kernel_uses_tsc() {
#ifdef DFTRACER_TSC_CLOCK_SUPPORTED
    auto& posix = POSIXBypass::get_instance();
    posix.initialize();
    const int fd = posix.open(
        "/sys/devices/system/clocksource/clocksource0/current_clocksource",
        O_RDONLY);
    if (fd < 0) return false;
    char name[16] = {};
    const ssize_t n = posix.read(fd, name, sizeof(name) - 1);
    posix.close(fd);
    return n >= 3 && std::strncmp(name, "tsc", 3) == 0 &&
           (n == 3 || name[3] == '\n');
#else
    return false;
#endif
  }

  inline static std::atomic<double> rate_{0};  // nanoseconds per tick
  inline static bool enabled_ = false;
  inline static uint64_t start_ns_ = 0;
  inline static uint64_t start_tsc_ = 0;
};

}  // namespace dftracer
#endif  // DFTRACER_UTILS_TSC_CLOCK_H
