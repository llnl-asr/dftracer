// The hwloc module's hook into the core (see common/module_hooks.h): the
// cores this process is bound to, for the "core_affinity" metadata event.
// Empty without hwloc, so the hook stays undefined and no affinity is
// recorded.
#include <dftracer/core/common/logging.h>

#ifdef DFTRACER_HWLOC_ENABLE
#include <dftracer/core/common/module_hooks.h>
#include <hwloc.h>

#include <mutex>

namespace {
// Loaded on first use and kept for the life of the process, as the logger
// did when it owned the topology.
hwloc_topology_t& topology() {
  static hwloc_topology_t topo;
  static std::once_flag once;
  std::call_once(once, [] {
    hwloc_topology_init(&topo);  // initialization
    hwloc_topology_load(topo);   // actual detection
  });
  return topo;
}
}  // namespace

extern "C" void dftracer_module_hwloc_core_affinity(
    std::vector<unsigned>* cores) {
  hwloc_cpuset_t set = hwloc_bitmap_alloc();
  hwloc_get_cpubind(topology(), set, HWLOC_CPUBIND_PROCESS);
  for (int id = hwloc_bitmap_first(set); id != -1;
       id = hwloc_bitmap_next(set, id)) {
    cores->push_back(id);
  }
  hwloc_bitmap_free(set);
}
#endif  // DFTRACER_HWLOC_ENABLE
