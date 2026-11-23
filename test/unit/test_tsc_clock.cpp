#include <dftracer/core/utils/tsc_clock.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

using dftracer::TscClock;

static uint64_t wall_us() {
  struct timeval tv{};
  gettimeofday(&tv, nullptr);
  return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL +
         static_cast<uint64_t>(tv.tv_usec);
}

static int fail(const char* what, long long a, long long b) {
  std::fprintf(stderr, "FAIL %s: %lld %lld\n", what, a, b);
  return 1;
}

static int check(TscClockMode mode) {
  TscClock::start(mode);
  uint64_t last = 0;
  long long worst = 0;
  const uint64_t until = wall_us() + 50000;
  while (wall_us() < until) {
    const uint64_t before = wall_us();
    const uint64_t t = TscClock::now_us();
    const uint64_t after = wall_us();
    if (t < last) return fail("went back", t, last);
    last = t;
    const long long lo = static_cast<long long>(before) - 50;
    const long long hi = static_cast<long long>(after) + 50;
    const long long v = static_cast<long long>(t);
    if (v < lo || v > hi) return fail("off the wall clock", v, before);
    const long long d = v < static_cast<long long>(before)
                            ? static_cast<long long>(before) - v
                            : (v > static_cast<long long>(after)
                                   ? v - static_cast<long long>(after)
                                   : 0);
    if (d > worst) worst = d;
  }
  std::printf("mode=%s enabled=%d worst=%lldus\n", to_string(mode).c_str(),
              TscClock::enabled(), worst);
  return 0;
}

// A reading taken on one thread after another thread's reading may come a
// little before it. Under valgrind one thread alone strays up to about 10 us,
// so the bound is the one check() uses.
static int check_threads() {
  std::atomic<uint64_t> first{0};
  std::atomic<int> turn{0};
  int bad = 0;
  std::thread other([&] {
    for (int i = 0; i < 2000; ++i) {
      while (turn.load(std::memory_order_acquire) != 1)
        std::this_thread::yield();
      first.store(TscClock::now_us(), std::memory_order_relaxed);
      turn.store(2, std::memory_order_release);
    }
  });
  for (int i = 0; i < 2000; ++i) {
    turn.store(1, std::memory_order_release);
    while (turn.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    const uint64_t a = first.load(std::memory_order_relaxed);
    const uint64_t b = TscClock::now_us();
    if (b + 50 < a) bad = fail("thread order", a, b);
  }
  other.join();
  return bad;
}

int main() {
  if (check(TscClockMode::TSC_CLOCK_OFF) != 0) return EXIT_FAILURE;
  if (TscClock::enabled()) return fail("enabled when not wanted", 1, 0);
  if (check(TscClockMode::TSC_CLOCK_AUTO) != 0) return EXIT_FAILURE;
  if (!TscClock::enabled())
    std::printf("auto: clock source is not tsc, so clock_gettime\n");
  if (check_threads() != 0) return EXIT_FAILURE;
  // FORCE runs the counter path on any x86 machine, such as a CI VM whose
  // clock source is not tsc.
  if (check(TscClockMode::TSC_CLOCK_FORCE) != 0) return EXIT_FAILURE;
#if defined(__x86_64__) || defined(__i386__)
  if (!TscClock::enabled()) return fail("not enabled when forced", 0, 1);
#endif
  if (check_threads() != 0) return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
