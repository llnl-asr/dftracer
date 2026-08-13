Standalone PAPI tracing samples

This directory provides two standalone C++ test programs and a plain Makefile:

- `papi_single_node.cpp`: non-MPI, single-node counter tracing
- `papi_mpi.cpp`: MPI rank-local compute plus collectives for scaling runs

Build assumptions:

- `libdftracer_core.so` is already built or installed elsewhere
- the DFTracer public headers are available
- the `cpp-logger` headers used by the public DFTracer headers are available
- `mpicxx` is available for the MPI sample

Key Makefile variables:

- `DFTRACER_INCLUDEDIR`: include directory containing `dftracer/dftracer.h`
- `CPP_LOGGER_INCLUDEDIR`: include directory containing `cpp-logger/logger.h`
- `DFTRACER_LIBDIR`: library directory containing `libdftracer_core.so`
- `MPI_LAUNCH`: launch command for the MPI sample, default `mpirun -np 4`

Examples:

```bash
make print-config
make syntax-check CPP_LOGGER_INCLUDEDIR=/path/to/include
make all \
  DFTRACER_INCLUDEDIR=/path/to/include \
  CPP_LOGGER_INCLUDEDIR=/path/to/include \
  DFTRACER_LIBDIR=/path/to/lib64

make run-single \
  DFTRACER_INCLUDEDIR=/path/to/include \
  CPP_LOGGER_INCLUDEDIR=/path/to/include \
  DFTRACER_LIBDIR=/path/to/lib64

make run-mpi \
  DFTRACER_INCLUDEDIR=/path/to/include \
  CPP_LOGGER_INCLUDEDIR=/path/to/include \
  DFTRACER_LIBDIR=/path/to/lib64 \
  MPI_LAUNCH='mpirun -np 8'
```

Runtime behavior:

- Both programs call `DFTRACER_CPP_INIT_NO_BIND(...)` so they exercise the new PAPI tracing path without requiring GOTCHA I/O interception.
- The Makefile run targets enable:
  - `DFTRACER_ENABLE=1`
  - `DFTRACER_INC_METADATA=1`
  - `DFTRACER_ENABLE_PAPI_TRACING=1`
  - `DFTRACER_PAPI_SAMPLE_INTERVAL_MS=0`
- The default counters are `PAPI_TOT_CYC,PAPI_TOT_INS`, but `DFTRACER_PAPI_EVENTS` can override them.

Trace files are written under `./traces` by default.