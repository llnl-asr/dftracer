#include <mpi.h>

#include <dftracer/dftracer.h>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void rank_compute(std::vector<double> &buffer, int step, int loops, int rank,
                  int world_size) {
  DFTRACER_CPP_FUNCTION();

  DFTracer region("rank_compute", CPP_LOG_CATEGORY, DF_DATA_EVENT);
  region.update("step", step);
  region.update("rank", rank);
  region.update("world_size", world_size);
  region.update("loops", loops);

  volatile double guard = 0.0;
  for (int loop = 0; loop < loops; ++loop) {
    for (size_t idx = 0; idx < buffer.size(); ++idx) {
      double value = buffer[idx];
      value += static_cast<double>((rank + 1) * (step + 1));
      value *= 1.0 + (static_cast<double>((idx % 31) + 1) / 1000000.0);
      value -= static_cast<double>(loop % 7) * 0.25;
      buffer[idx] = value;
      guard += value;
    }
  }

  if (guard < 0.0) {
    std::fprintf(stderr, "guard=%f\n", static_cast<double>(guard));
  }
}

double collective_reduce(double local_value, int step, int rank, int world_size) {
  DFTRACER_CPP_FUNCTION();

  DFTracer region("allreduce_phase", CPP_LOG_CATEGORY, DF_DATA_EVENT);
  region.update("step", step);
  region.update("rank", rank);
  region.update("world_size", world_size);

  double global_value = 0.0;
  MPI_Allreduce(&local_value, &global_value, 1, MPI_DOUBLE, MPI_SUM,
                MPI_COMM_WORLD);
  return global_value;
}

int fold_local_checksum(const std::vector<double> &buffer, int rank, int step) {
  DFTRACER_CPP_FUNCTION();

  DFTracer region("local_checksum", CPP_LOG_CATEGORY, DF_DATA_EVENT);
  region.update("step", step);
  region.update("rank", rank);

  long long checksum = 0;
  for (size_t idx = 0; idx < buffer.size(); idx += 1024) {
    checksum += static_cast<long long>(buffer[idx]);
  }
  int folded = static_cast<int>(checksum % 2147483629LL);
  region.update("checksum_mod", folded);
  return folded;
}

}  // namespace

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);

  int rank = 0;
  int world_size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  std::string trace_prefix = (argc > 1) ? argv[1] : "./traces/papi-mpi";
  int steps = (argc > 2) ? std::atoi(argv[2]) : 10;
  size_t elements_per_rank =
      (argc > 3) ? static_cast<size_t>(std::strtoull(argv[3], nullptr, 10))
                 : static_cast<size_t>(1 << 18);
  int loops = (argc > 4) ? std::atoi(argv[4]) : 6;

  if (steps <= 0 || elements_per_rank == 0 || loops <= 0) {
    if (rank == 0) {
      std::fprintf(stderr,
                   "usage: %s [trace_prefix] [steps>0] [elements_per_rank>0] [loops>0]\n",
                   argv[0]);
    }
    MPI_Finalize();
    return 1;
  }

  char rank_trace_prefix[1024];
  std::snprintf(rank_trace_prefix, sizeof(rank_trace_prefix), "%s-rank-%04d-pid-%d",
                trace_prefix.c_str(), rank, static_cast<int>(getpid()));

  DFTRACER_CPP_INIT_NO_BIND(rank_trace_prefix, nullptr, nullptr);

  {
    DFTracer setup("mpi_setup", CPP_LOG_CATEGORY, DF_DATA_EVENT);
    setup.update("rank", rank);
    setup.update("world_size", world_size);
    setup.update("steps", steps);
    setup.update("loops", loops);
    setup.update("elements_per_rank",
                 static_cast<int>(elements_per_rank > static_cast<size_t>(2147483647)
                                      ? 2147483647
                                      : elements_per_rank));
  }

  std::vector<double> buffer(elements_per_rank,
                             static_cast<double>(rank + 1));
  double global_accumulator = 0.0;
  int folded_checksum = 0;

  for (int step = 0; step < steps; ++step) {
    rank_compute(buffer, step, loops, rank, world_size);

    int local_checksum = fold_local_checksum(buffer, rank, step);
    double global_value =
        collective_reduce(static_cast<double>(local_checksum), step, rank,
                          world_size);
    global_accumulator += global_value;

    {
      DFTracer step_summary("mpi_step_summary", CPP_LOG_CATEGORY,
                            DF_DATA_EVENT);
      step_summary.update("step", step);
      step_summary.update("rank", rank);
      step_summary.update("local_checksum", local_checksum);
      step_summary.update("global_checksum_mod",
                          static_cast<int>(static_cast<long long>(global_value) %
                                           2147483629LL));
    }

    folded_checksum ^= local_checksum;
    MPI_Barrier(MPI_COMM_WORLD);
  }

  {
    DFTracer final_region("mpi_finalize", CPP_LOG_CATEGORY, DF_DATA_EVENT);
    final_region.update("rank", rank);
    final_region.update("folded_checksum", folded_checksum);
    final_region.update("global_accum_mod",
                        static_cast<int>(static_cast<long long>(global_accumulator) %
                                         2147483629LL));
  }

  DFTRACER_CPP_FINI();
  MPI_Finalize();
  return 0;
}
