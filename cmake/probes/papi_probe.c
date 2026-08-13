/* Build-time probe: finds every PAPI counter this machine can actually count,
 * so the answer can be baked into dftracer_config.hpp instead of being
 * rediscovered on every run.
 *
 * No counter is singled out. The probe walks the whole preset table and keeps
 * each one the CPU implements and PAPI will program, which is the only way to
 * be complete: which presets exist varies enormously between CPUs -- and even
 * between PAPI versions on the SAME CPU. On an AMD MI300A node, PAPI 7.2.0.2
 * programs 30 presets (including L1/L2 cache, TLB, branch and FP/FMA/vector
 * counters) while PAPI 7.0.1.2 manages only 19, so any hand-picked list would
 * mostly miss.
 *
 * "Available" (PAPI_event_info_t.count > 0) is necessary but not sufficient --
 * a preset can be listed and still refuse to be programmed -- so every
 * candidate is also added to a real event set on its own.
 *
 * Prints three TAGGED lines to stdout:
 *   DFTRACER_PAPI_EVENTS=<every countable counter, comma separated>
 *   DFTRACER_PAPI_HWCTRS=<number of hardware counter slots>
 *   DFTRACER_PAPI_FITTING=<how many fit in one event set without multiplexing>
 *
 * The tags matter. CMake's try_run() captures stdout and stderr MERGED into one
 * variable, and libraries pulled in transitively can write to stderr during
 * PAPI_library_init -- on a ROCm system, rocprofiler-register emits glog
 * warnings ("Device N could not be locked for profiling ... SYS_PERFMON") when
 * the process lacks perf capability, which is normal on a login node. Parsing
 * this stream by LINE POSITION silently baked those warnings into
 * dftracer_config.hpp as the counter list. Tagging lets CMake pick out exactly
 * the three values it needs and ignore anything else on the stream.
 *
 * Exits non-zero if PAPI is unusable here, in which case CMake falls back to a
 * portable default.
 */
#include <papi.h>
#include <stdio.h>
#include <string.h>

/* The preset table is ~108 entries; leave room for every one of them. */
#define MAX_EVENTS 256

int main(void) {
  char countable[MAX_EVENTS][PAPI_MAX_STR_LEN];
  int num_countable = 0;
  int code, rv, i;

  if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT) {
    fprintf(stderr, "papi_probe: PAPI_library_init failed\n");
    return 1;
  }

  /* Every preset the CPU implements and PAPI will program. */
  code = 0 | PAPI_PRESET_MASK;
  rv = PAPI_enum_event(&code, PAPI_ENUM_FIRST);
  while (rv == PAPI_OK && num_countable < MAX_EVENTS) {
    PAPI_event_info_t info;
    if (PAPI_get_event_info(code, &info) == PAPI_OK && info.count > 0 &&
        info.symbol[0] != '\0') {
      int set = PAPI_NULL;
      if (PAPI_create_eventset(&set) == PAPI_OK) {
        PAPI_assign_eventset_component(set, 0);
        if (PAPI_add_named_event(set, info.symbol) == PAPI_OK) {
          strncpy(countable[num_countable], info.symbol, PAPI_MAX_STR_LEN - 1);
          countable[num_countable][PAPI_MAX_STR_LEN - 1] = '\0';
          num_countable++;
        }
        PAPI_cleanup_eventset(set);
        PAPI_destroy_eventset(&set);
      }
    }
    rv = PAPI_enum_event(&code, PAPI_PRESET_ENUM_AVAIL);
  }

  if (num_countable == 0) {
    fprintf(stderr, "papi_probe: no counter could be programmed\n");
    return 1;
  }

  printf("DFTRACER_PAPI_EVENTS=");
  for (i = 0; i < num_countable; ++i) {
    if (i > 0) printf(",");
    printf("%s", countable[i]);
  }
  /* PAPI_num_hwctrs, not PAPI_num_counters: the latter is high-level API
   * that some PAPI builds do not declare, and a probe that fails to compile
   * would silently fall back to the portable default counter list. */
  printf("\nDFTRACER_PAPI_HWCTRS=%d\n", PAPI_num_hwctrs());

  /* How many fit at once. Counting more than this needs multiplexing, which
   * DFTracer enables for itself; reported so the build log shows the gap. */
  {
    int set = PAPI_NULL;
    int fitting = 0;
    if (PAPI_create_eventset(&set) == PAPI_OK) {
      PAPI_assign_eventset_component(set, 0);
      for (i = 0; i < num_countable; ++i) {
        if (PAPI_add_named_event(set, countable[i]) == PAPI_OK) fitting++;
      }
      PAPI_cleanup_eventset(set);
      PAPI_destroy_eventset(&set);
    }
    printf("DFTRACER_PAPI_FITTING=%d\n", fitting);
  }
  return 0;
}
