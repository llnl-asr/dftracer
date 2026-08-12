/* Build-time probe: works out which PAPI counters this machine can actually
 * count together, so the answer can be baked into dftracer_config.hpp instead
 * of being rediscovered on every run.
 *
 * Enumerating the available presets is not enough on its own: a CPU typically
 * exposes far more presets than it has counter slots, and some presets cannot
 * be programmed alongside others. So each candidate is added to a real event
 * set and kept only if it sticks.
 *
 * Prints one line to stdout: the surviving counters, comma separated, followed
 * by a line with the number of hardware counters. Exits non-zero if PAPI is
 * unusable here, in which case CMake falls back to a portable default.
 */
#include <papi.h>
#include <stdio.h>
#include <string.h>

#define MAX_EVENTS 64

/* Counters worth a scarce hardware slot, most useful first. Keep in step with
 * kPreferredEvents in src/dftracer/core/function/papi/counters.cpp. */
static const char *kPreferred[] = {
    "PAPI_TOT_CYC", "PAPI_TOT_INS", "PAPI_LST_INS", "PAPI_FP_OPS",
    "PAPI_L1_DCM",  "PAPI_L2_TCM",  "PAPI_L3_TCM",  "PAPI_BR_MSP",
    "PAPI_TLB_DM",  "PAPI_LD_INS",  "PAPI_SR_INS",  "PAPI_BR_INS",
};
static const int kNumPreferred = (int)(sizeof(kPreferred) / sizeof(kPreferred[0]));

int main(void) {
  char candidates[MAX_EVENTS][PAPI_MAX_STR_LEN];
  int num_candidates = 0;
  int i;

  if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT) {
    fprintf(stderr, "papi_probe: PAPI_library_init failed\n");
    return 1;
  }

  /* Preferred counters first ... */
  for (i = 0; i < kNumPreferred && num_candidates < MAX_EVENTS; ++i) {
    if (PAPI_query_named_event(kPreferred[i]) == PAPI_OK) {
      strncpy(candidates[num_candidates], kPreferred[i], PAPI_MAX_STR_LEN - 1);
      candidates[num_candidates][PAPI_MAX_STR_LEN - 1] = '\0';
      num_candidates++;
    }
  }

  /* ... then whatever else this CPU reports as available. */
  {
    int code = 0 | PAPI_PRESET_MASK;
    int rv = PAPI_enum_event(&code, PAPI_ENUM_FIRST);
    while (rv == PAPI_OK && num_candidates < MAX_EVENTS) {
      PAPI_event_info_t info;
      if (PAPI_get_event_info(code, &info) == PAPI_OK && info.count > 0 &&
          info.symbol[0] != '\0') {
        int seen = 0;
        for (i = 0; i < num_candidates; ++i) {
          if (strcmp(candidates[i], info.symbol) == 0) { seen = 1; break; }
        }
        if (!seen) {
          strncpy(candidates[num_candidates], info.symbol, PAPI_MAX_STR_LEN - 1);
          candidates[num_candidates][PAPI_MAX_STR_LEN - 1] = '\0';
          num_candidates++;
        }
      }
      rv = PAPI_enum_event(&code, PAPI_PRESET_ENUM_AVAIL);
    }
  }

  /* Keep only the ones that can be programmed at the same time. */
  {
    int set = PAPI_NULL;
    int kept = 0;
    if (PAPI_create_eventset(&set) != PAPI_OK) {
      fprintf(stderr, "papi_probe: PAPI_create_eventset failed\n");
      return 1;
    }
    if (PAPI_assign_eventset_component(set, 0) != PAPI_OK) {
      fprintf(stderr, "papi_probe: PAPI_assign_eventset_component failed\n");
      return 1;
    }
    for (i = 0; i < num_candidates; ++i) {
      if (PAPI_add_named_event(set, candidates[i]) != PAPI_OK) continue;
      if (kept > 0) printf(",");
      printf("%s", candidates[i]);
      kept++;
    }
    printf("\n%d\n", PAPI_num_counters());
    PAPI_cleanup_eventset(set);
    PAPI_destroy_eventset(&set);
    if (kept == 0) {
      fprintf(stderr, "papi_probe: no counter could be programmed\n");
      return 1;
    }
  }
  return 0;
}
