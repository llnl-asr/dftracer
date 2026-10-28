# Performance benchmarks. Enabled with DFTRACER_ENABLE_PERF_TESTS and labelled
# "perf": run them on an allocated compute node with `ctest -L perf`.

# Shared-memory event ring: producer/consumer bandwidth against STREAM.
add_executable(ring_bw ring_bw/ring_bw.cpp)
target_compile_options(ring_bw PRIVATE -O3 -march=native)
target_link_libraries(ring_bw pthread rt)

# Builds its own ring_bw and STREAM (fetched at a pinned checksum), sweeps
# producer counts and record sizes, and writes stream.csv, ring.csv and
# summary.csv into the work directory. Knobs: see run_ring_bw.sh.
set(test_name perf_ring_bw_vs_stream)
add_test(NAME ${test_name}
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ring_bw/run_ring_bw.sh
                 ${CMAKE_CURRENT_BINARY_DIR}/ring_bw_work)
set_tests_properties(${test_name} PROPERTIES LABELS perf TIMEOUT 7200)

# Per-call overhead of the C API without dftracer, in-process (aggregation on
# and off) and in producer/consumer mode, over thread counts and with and
# without metadata, 5 repetitions. Writes c_overhead.csv and summary.csv
# (added ns per call, and producer/consumer relative to in-process).
# Knobs: THREADS, OPS, REPS, META, FIRST_CPU (see run_c_overhead.sh).
set(test_name perf_c_overhead_pc_vs_local)
add_test(NAME ${test_name}
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/c_overhead/run_c_overhead.sh
                 ${CMAKE_BINARY_DIR} - ${CMAKE_CURRENT_BINARY_DIR}/c_overhead_work)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT
    LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}:$ENV{LD_LIBRARY_PATH}
    C_OVERHEAD_BASE=${CMAKE_BINARY_DIR}/bin/c_overhead_base
    C_OVERHEAD_DFT=${CMAKE_BINARY_DIR}/bin/c_overhead_dft)
set_tests_properties(${test_name} PROPERTIES LABELS perf TIMEOUT 7200)
