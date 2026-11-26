// Counts rocp_sdk counters through PAPI against mock_rocprofiler_sdk.cpp, where
// GPU 0 is hidden and GPUs 1 and 2 are visible, and exits without
// PAPI_shutdown, as the traced application does. Exit code 0 when PAPI counts
// only the visible GPUs.
#include <papi.h>
#include <stdio.h>
#include <string.h>

#define GPU1_VALUE 4242
#define GPU2_VALUE 4343

// Exported by the PAPI that cmake/modules/papi-patches builds.
int dftracer_papi_rocp_sdk_devices(int *devices, int max_devices);

static int add(int set, const char *name) {
  int retval = PAPI_add_named_event(set, name);
  if (retval != PAPI_OK)
    fprintf(stderr, "PAPI_add_named_event %s: %s\n", name,
            PAPI_strerror(retval));
  return retval;
}

int main(void) {
  int retval = PAPI_library_init(PAPI_VER_CURRENT);
  if (retval != PAPI_VER_CURRENT) {
    fprintf(stderr, "PAPI_library_init: %d\n", retval);
    return 1;
  }

  // Without device=, the lowest visible GPU, not the hidden GPU 0.
  int code = 0;
  if (PAPI_event_name_to_code("rocp_sdk:::SQ_WAVES", &code) != PAPI_OK) {
    fprintf(stderr, "rocp_sdk:::SQ_WAVES is not known\n");
    return 1;
  }
  char name[PAPI_MAX_STR_LEN];
  if (PAPI_event_code_to_name(code, name) != PAPI_OK) return 1;
  printf("rocp_sdk:::SQ_WAVES is %s\n", name);
  if (strstr(name, ":device=1") == NULL) {
    fprintf(stderr, "expected the default device to be GPU 1\n");
    return 1;
  }

  // A hidden GPU would read 0 without an error.
  if (PAPI_event_name_to_code("rocp_sdk:::SQ_WAVES:device=0", &code) ==
      PAPI_OK) {
    fprintf(stderr, "device=0 is hidden but PAPI accepts it\n");
    return 1;
  }

  int devices[8];
  int count = dftracer_papi_rocp_sdk_devices(devices, 8);
  if (count != 2 || devices[0] != 1 || devices[1] != 2) {
    fprintf(stderr, "visible GPUs: %d, expected 1 and 2\n", count);
    return 1;
  }

  // The qualifier list, as papi_native_avail walks it, ends and offers a
  // visible GPU as the default device.
  int cid = PAPI_get_component_index("rocp_sdk");
  int listed = 0;
  code = 0 | PAPI_NATIVE_MASK;
  if (PAPI_enum_cmp_event(&code, PAPI_ENUM_FIRST, cid) == PAPI_OK) {
    do {
      int qualifier = code;
      while (PAPI_enum_cmp_event(&qualifier, PAPI_NTV_ENUM_DEFAULT_QUALIFIERS,
                                 cid) == PAPI_OK) {
        if (++listed > 100) {
          fprintf(stderr, "the rocp_sdk qualifier list does not end\n");
          return 1;
        }
        if (PAPI_event_code_to_name(qualifier, name) != PAPI_OK) return 1;
        if (strstr(name, "device=") != NULL &&
            strstr(name, ":device=1") == NULL) {
          fprintf(stderr, "qualifier %s does not default to GPU 1\n", name);
          return 1;
        }
      }
    } while (PAPI_enum_cmp_event(&code, PAPI_ENUM_EVENTS, cid) == PAPI_OK);
  }
  if (listed == 0) {
    fprintf(stderr, "rocp_sdk lists no qualifier\n");
    return 1;
  }

  int set = PAPI_NULL;
  if (PAPI_create_eventset(&set) != PAPI_OK) return 1;
  if (add(set, "rocp_sdk:::SQ_WAVES:device=1") != PAPI_OK) return 1;
  if (add(set, "rocp_sdk:::SQ_WAVES:device=2") != PAPI_OK) return 1;
  // Each base counter on each visible GPU fits in one event set.
  if (add(set, "rocp_sdk:::GRBM_COUNT:device=1") != PAPI_OK) return 1;
  if (add(set, "rocp_sdk:::GRBM_COUNT:device=2") != PAPI_OK) return 1;
  retval = PAPI_start(set);
  if (retval != PAPI_OK) {
    fprintf(stderr, "PAPI_start: %s\n", PAPI_strerror(retval));
    return 1;
  }
  long long values[4] = {0, 0, 0, 0};
  retval = PAPI_read(set, values);
  if (retval != PAPI_OK) {
    fprintf(stderr, "PAPI_read: %s\n", PAPI_strerror(retval));
    return 1;
  }
  printf("device=1 = %lld, device=2 = %lld\n", values[0], values[1]);
  if (values[0] != GPU1_VALUE || values[1] != GPU2_VALUE) {
    fprintf(stderr, "expected %d and %d: the hidden GPU stopped the counting\n",
            GPU1_VALUE, GPU2_VALUE);
    return 1;
  }
  return 0;
}
