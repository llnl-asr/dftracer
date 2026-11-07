// DFTRACER_MIN_DURATION_NS: events shorter than it are not traced. FAST
// regions do no work (well under a microsecond); SLOW regions spin for 2 ms.
// With the threshold at 1 ms only the slow ones, and dftracer's own events,
// are in the trace; without it, all of them (test/CMakeLists.txt checks
// both). Usage: test_min_duration
#include <dftracer/dftracer.h>
#include <stdint.h>
#include <time.h>

#ifndef ROUNDS
#define ROUNDS 20
#endif

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void fast_region(int i) {
  DFTRACER_C_REGION_START(fast_region);
  DFTRACER_C_REGION_UPDATE_INT(fast_region, "i", i);
  DFTRACER_C_REGION_END(fast_region);
}

static void slow_region(int i) {
  DFTRACER_C_REGION_START(slow_region);
  DFTRACER_C_REGION_UPDATE_INT(slow_region, "i", i);
  const uint64_t t0 = now_ns();
  while (now_ns() - t0 < 2000000ull) {
  }
  DFTRACER_C_REGION_END(slow_region);
}

int main(void) {
  DFTRACER_C_INIT(NULL, NULL, NULL);
  for (int i = 0; i < ROUNDS; ++i) {
    fast_region(i);
    slow_region(i);
  }
  DFTRACER_C_FINI();
  return 0;
}
