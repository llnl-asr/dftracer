#include <dftracer/dftracer.h>

#include <papi.h>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool verify_papi_runtime() {
  int retval = PAPI_library_init(PAPI_VER_CURRENT);
  if (retval != PAPI_VER_CURRENT) {
    std::fprintf(stderr, "PAPI_library_init failed: %d (%s)\n", retval,
                 PAPI_strerror(retval));
    return false;
  }

  const PAPI_hw_info_t *hwinfo = PAPI_get_hardware_info();
  if (hwinfo == nullptr) {
    std::fprintf(stderr, "PAPI_get_hardware_info returned null\n");
    return false;
  }

  int counters = PAPI_num_counters();
  if (counters <= 0) {
    std::fprintf(stderr,
                 "PAPI runtime is available but exposes %d hardware counters on this node; cannot validate DFTracer PAPI sampling here.\n",
                 counters);
    return false;
  }

  return true;
}

void compute_chunk(std::vector<double> &buffer, int step, int inner_loops) {
  DFTRACER_CPP_FUNCTION();

  DFTracer chunk("compute_chunk", CPP_LOG_CATEGORY, DF_DATA_EVENT);
  chunk.update("step", step);
  chunk.update("inner_loops", inner_loops);

  char label[32];
  std::snprintf(label, sizeof(label), "chunk_%03d", step);
  chunk.update("label", label);

  volatile double guard = 0.0;
  for (int loop = 0; loop < inner_loops; ++loop) {
    for (size_t idx = 0; idx < buffer.size(); ++idx) {
      double value = buffer[idx];
      value = value * 1.0000001 + static_cast<double>((idx % 97) + step + loop);
      value -= static_cast<double>(idx % 13) * 0.125;
      buffer[idx] = value;
      guard += value;
    }
  }

  if (guard < 0.0) {
    std::fprintf(stderr, "guard=%f\n", static_cast<double>(guard));
  }

  usleep(1000);
}

int reduce_checksum(const std::vector<double> &buffer, int step) {
  DFTRACER_CPP_FUNCTION();

  DFTracer summary("checksum", CPP_LOG_CATEGORY, DF_DATA_EVENT);
  summary.update("step", step);

  long long checksum = 0;
  for (size_t idx = 0; idx < buffer.size(); idx += 1024) {
    checksum += static_cast<long long>(buffer[idx]);
  }
  int folded = static_cast<int>(checksum % 2147483629LL);
  summary.update("checksum_mod", folded);
  return folded;
}

}  // namespace

int main(int argc, char **argv) {
  std::string trace_prefix = (argc > 1) ? argv[1] : "./traces/papi-single";
  int steps = (argc > 2) ? std::atoi(argv[2]) : 12;
  size_t elements =
      (argc > 3) ? static_cast<size_t>(std::strtoull(argv[3], nullptr, 10))
                 : static_cast<size_t>(1 << 20);
  int inner_loops = (argc > 4) ? std::atoi(argv[4]) : 8;

  if (steps <= 0 || elements == 0 || inner_loops <= 0) {
    std::fprintf(stderr,
                 "usage: %s [trace_prefix] [steps>0] [elements>0] [inner_loops>0]\n",
                 argv[0]);
    return 1;
  }

  if (!verify_papi_runtime()) {
    return 2;
  }

  DFTRACER_CPP_INIT_NO_BIND(trace_prefix.c_str(), nullptr, nullptr);

  {
    DFTracer setup("single_node_setup", CPP_LOG_CATEGORY, DF_DATA_EVENT);
    setup.update("steps", steps);
    setup.update("elements", static_cast<int>(elements > static_cast<size_t>(2147483647)
                                                   ? 2147483647
                                                   : elements));
    setup.update("inner_loops", inner_loops);
  }

  std::vector<double> buffer(elements, 1.0);
  int checksum = 0;
  for (int step = 0; step < steps; ++step) {
    compute_chunk(buffer, step, inner_loops);
    checksum ^= reduce_checksum(buffer, step);
  }

  {
    DFTracer final_region("single_node_finalize", CPP_LOG_CATEGORY,
                          DF_DATA_EVENT);
    final_region.update("final_checksum", checksum);
  }

  DFTRACER_CPP_FINI();
  return 0;
}
