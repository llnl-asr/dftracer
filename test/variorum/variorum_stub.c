/* Stand-ins for libvariorum, for check_variorum_runtime.sh: the service loads
 * libvariorum when it starts, so a test can hand it one of these instead of a
 * Variorum that needs power registers.
 *   VARIORUM_STUB_API_08  variorum_get_power_json(), as Variorum 0.8 has it,
 *                         returning a two-socket node with GPUs
 *   (otherwise)           only variorum_get_node_power_json(), as 0.6 and 0.7
 *                         name it, which the service must ignore */
#include <stdlib.h>
#include <string.h>

static const char kPower[] =
    "{\"testhost\": {\"timestamp\": 1755043200, \"power_node_watts\": 1024.5, "
    "\"socket_0\": {\"power_cpu_watts\": 105.25, \"power_mem_watts\": 22.5, "
    "\"power_gpu_watts\": 384.0}, "
    "\"socket_1\": {\"power_cpu_watts\": 98.75, \"power_mem_watts\": 21.0, "
    "\"power_gpu_watts\": 393.0}}}";

#ifdef VARIORUM_STUB_API_08
int variorum_get_power_json(char **out) {
  *out = strdup(kPower);
  return *out == NULL ? -1 : 0;
}
#else
int variorum_get_node_power_json(char **out) {
  *out = strdup(kPower);
  return *out == NULL ? -1 : 0;
}
#endif
