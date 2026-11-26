// Counts one rocp_sdk counter through PAPI against mock_rocprofiler_sdk.cpp
// and exits without PAPI_shutdown, as the traced application does. Exit code
// 0 when the visible GPU gave the mock's value.
#include <papi.h>
#include <stdio.h>

#define EXPECTED_VALUE 4242

int main(void) {
  int retval = PAPI_library_init(PAPI_VER_CURRENT);
  if (retval != PAPI_VER_CURRENT) {
    fprintf(stderr, "PAPI_library_init: %d\n", retval);
    return 1;
  }
  int set = PAPI_NULL;
  if (PAPI_create_eventset(&set) != PAPI_OK) return 1;
  retval = PAPI_add_named_event(set, "rocp_sdk:::SQ_WAVES:device=1");
  if (retval != PAPI_OK) {
    fprintf(stderr, "PAPI_add_named_event: %s\n", PAPI_strerror(retval));
    return 1;
  }
  retval = PAPI_start(set);
  if (retval != PAPI_OK) {
    fprintf(stderr, "PAPI_start: %s\n", PAPI_strerror(retval));
    return 1;
  }
  long long value = 0;
  retval = PAPI_read(set, &value);
  if (retval != PAPI_OK) {
    fprintf(stderr, "PAPI_read: %s\n", PAPI_strerror(retval));
    return 1;
  }
  printf("rocp_sdk:::SQ_WAVES:device=1 = %lld\n", value);
  if (value != EXPECTED_VALUE) {
    fprintf(stderr, "expected %d: the hidden GPU stopped the counting\n",
            EXPECTED_VALUE);
    return 1;
  }
  return 0;
}
