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

/* Family a preset belongs to, from the naming scheme of the PAPI preset table.
 * Counters are grouped by family so a sample can be written as one record per
 * family instead of one per counter, which is most of the trace size. Longer
 * prefixes must come first. Keep in step with the docs in counters.h. */
struct papi_family {
  const char *prefix;
  const char *category;
};
static const struct papi_family kFamilies[] = {
    {"PAPI_TOT_CYC", "CYCLE"},
    {"PAPI_REF_CYC", "CYCLE"},
    {"PAPI_TOT_INS", "INSTRUCTION"},
    {"PAPI_TOT_IIS", "INSTRUCTION"},
    {"PAPI_INT_INS", "INSTRUCTION"},
    {"PAPI_SYC_INS", "INSTRUCTION"},
    {"PAPI_HW_INT", "INTERRUPT"},
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
    {"PAPI_L1_", "CACHE"},
    {"PAPI_L2_", "CACHE"},
    {"PAPI_L3_", "CACHE"},
    {"PAPI_CA_", "COHERENCY"},
    {"PAPI_PRF_DM", "PREFETCH"},
    {"PAPI_TLB_", "TLB"},
    {"PAPI_BTAC_M", "BRANCH"},
    {"PAPI_BRU_IDL", "STALL"},
    {"PAPI_BR_", "BRANCH"},
    {"PAPI_LST_INS", "MEMORY"},
    {"PAPI_LD_INS", "MEMORY"},
    {"PAPI_SR_INS", "MEMORY"},
    {"PAPI_MEM_SCY", "STALL"},
    {"PAPI_MEM_RCY", "STALL"},
    {"PAPI_MEM_WCY", "STALL"},
    {"PAPI_MEM_", "MEMORY"},
    {"PAPI_CSR_", "SYNC"},
    {"PAPI_STL_", "STALL"},
    {"PAPI_FUL_", "STALL"},
    {"PAPI_RES_STL", "STALL"},
    {"PAPI_FXU_IDL", "STALL"},
    {"PAPI_LSU_IDL", "STALL"},
};
static const int kNumFamilies = (int)(sizeof(kFamilies) / sizeof(kFamilies[0]));

static const char *family_of(const char *event) {
  int i;
  for (i = 0; i < kNumFamilies; ++i) {
    size_t n = strlen(kFamilies[i].prefix);
    if (strncmp(event, kFamilies[i].prefix, n) == 0)
      return kFamilies[i].category;
  }
  return "PAPI";
}

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
  /* Group by family: "CYCLE:PAPI_TOT_CYC;BRANCH:PAPI_BR_CN,PAPI_BR_MSP;...".
   * Each group becomes one event set at run time and one record per sample. */
  {
    const char *emitted[MAX_EVENTS];
    int num_emitted = 0;
    int first_group = 1;
    for (i = 0; i < num_countable; ++i) {
      const char *family = family_of(countable[i]);
      int seen = 0, j;
      for (j = 0; j < num_emitted; ++j) {
        if (strcmp(emitted[j], family) == 0) {
          seen = 1;
          break;
        }
      }
      if (seen) continue;
      emitted[num_emitted++] = family;

      if (!first_group) printf(";");
      first_group = 0;
      printf("%s:", family);
      {
        int first_member = 1;
        for (j = 0; j < num_countable; ++j) {
          if (strcmp(family_of(countable[j]), family) != 0) continue;
          if (!first_member) printf(",");
          first_member = 0;
          printf("%s", countable[j]);
        }
      }
    }
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
