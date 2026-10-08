include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/modules/dftracer-utils.cmake)
dftracer_install_external_project(cpp-logger 2.0.0 "CPP_LOGGER" https://github.com/llnl-asr/cpp-logger.git v0.0.8 ${CMAKE_INSTALL_PREFIX} "")

# Build Brahma with MPI and/or HDF5 support based on dftracer options
# Keep BRAHMA's core targets/install rules enabled in the currently pinned branch.
set(BRAHMA_CONFIGURE_ARGS "-DBRAHMA_BUILD_DEPENDENCIES=ON")

# Determine effective brahma MPI toggle from availability and detect implementation.
set(_dftracer_enable_brahma_mpi OFF)
set(_dftracer_dep_mpi_impl "")
if(DFTRACER_ENABLE_MPI)
  find_package(MPI QUIET COMPONENTS C)
  if(MPI_FOUND OR TARGET MPI::MPI_C)
    set(_dftracer_enable_brahma_mpi ON)
    # Capture exact library paths to forward to brahma so it uses the same MPI.
    set(_dftracer_dep_mpi_c_include_dirs "${MPI_C_INCLUDE_DIRS}")
    set(_dftracer_dep_mpi_c_libraries    "${MPI_C_LIBRARIES}")

    # Resolve MPI implementation: honour user override, then run C probe.
    if(DFTRACER_MPI_IMPL)
      set(_dftracer_dep_mpi_impl "${DFTRACER_MPI_IMPL}")
      message(STATUS "[${PROJECT_NAME}] dependency: MPI implementation overridden: ${_dftracer_dep_mpi_impl}")
    else()
      # Auto-detect via compiled C probe (reads preprocessor macros from mpi.h)
      try_run(
        _dep_mpi_run_result
        _dep_mpi_compile_result
        "${CMAKE_BINARY_DIR}/dftracer_dep_mpi_detect"
        "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/tests/mpi_detect.c"
        CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${MPI_C_INCLUDE_DIRS}"
        LINK_LIBRARIES ${MPI_C_LIBRARIES}
        COMPILE_OUTPUT_VARIABLE _dep_mpi_compile_out
        RUN_OUTPUT_VARIABLE _dep_mpi_detect_out
      )
      if(_dep_mpi_compile_result AND
         NOT "${_dep_mpi_run_result}" STREQUAL "FAILED_TO_RUN")
        if(_dep_mpi_detect_out MATCHES "IMPL=([A-Z]+)")
          set(_dftracer_dep_mpi_impl "${CMAKE_MATCH_1}")
        endif()
        # Parse library version and compute BRAHMA_MPI_VERSION = major*100000 + minor*100 + patch.
        # Brahma uses this numeric version to decide which GOTCHA MPI bindings to generate.
        # Without forwarding it, brahma falls back to the MPI *standard* version (e.g. 3.1 → 300100)
        # which is outside its supported OpenMPI range [400106, 400200), causing no MPI interception.
        set(_dftracer_dep_brahma_mpi_version "")
        if(_dep_mpi_detect_out MATCHES "VERSION=([0-9]+)\\.([0-9]+)\\.([0-9]+)")
          math(EXPR _dftracer_dep_brahma_mpi_version
               "${CMAKE_MATCH_1} * 100000 + ${CMAKE_MATCH_2} * 100 + ${CMAKE_MATCH_3}")
        endif()
        message(STATUS "[${PROJECT_NAME}] dependency: MPI C probe: impl=${_dftracer_dep_mpi_impl} brahma_version=${_dftracer_dep_brahma_mpi_version}")
      else()
        message(WARNING "[${PROJECT_NAME}] dependency: MPI C probe did not run; brahma will auto-detect impl independently")
      endif()
    endif()
  else()
    message(WARNING "[${PROJECT_NAME}] DFTRACER_ENABLE_MPI=ON but MPI was not found. Forcing BRAHMA_BUILD_WITH_MPI=OFF")
  endif()
endif()

if(_dftracer_enable_brahma_mpi)
  if(NOT "${BRAHMA_CONFIGURE_ARGS}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";")
  endif()
  string(APPEND BRAHMA_CONFIGURE_ARGS "-DBRAHMA_BUILD_WITH_MPI=ON")

  # Forward detected/overridden MPI implementation and numeric version to brahma.
  # BRAHMA_MPI_VERSION must be the library version (e.g. 400106 for OpenMPI 4.1.6),
  # not the MPI standard version (3.1 → 300100) that brahma would compute on its own.
  if(_dftracer_dep_mpi_impl AND NOT "${_dftracer_dep_mpi_impl}" STREQUAL "UNKNOWN")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DBRAHMA_MPI_IMPL=${_dftracer_dep_mpi_impl}")
    message(STATUS "[${PROJECT_NAME}] Forwarding BRAHMA_MPI_IMPL=${_dftracer_dep_mpi_impl} to brahma")
  endif()
  if(_dftracer_dep_brahma_mpi_version)
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DBRAHMA_MPI_VERSION=${_dftracer_dep_brahma_mpi_version}")
    message(STATUS "[${PROJECT_NAME}] Forwarding BRAHMA_MPI_VERSION=${_dftracer_dep_brahma_mpi_version} to brahma")
  endif()
else()
  if(NOT "${BRAHMA_CONFIGURE_ARGS}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";")
  endif()
  string(APPEND BRAHMA_CONFIGURE_ARGS "-DBRAHMA_BUILD_WITH_MPI=OFF")
endif()

if(DFTRACER_ENABLE_HDF5)
  if(NOT "${BRAHMA_CONFIGURE_ARGS}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";")
  endif()

  string(APPEND BRAHMA_CONFIGURE_ARGS "-DBRAHMA_BUILD_WITH_HDF5=ON")

  # Spack's HDF5 config package can report its library as the local CMake
  # target "hdf5-shared".  That target cannot be carried across Brahma's
  # ExternalProject boundary and is otherwise emitted as the invalid linker
  # flag -lhdf5-shared.  Use CMake's FindHDF5 module in both projects so the
  # selected Spack installation is represented by concrete library paths.
  set(HDF5_PREFER_PARALLEL TRUE)
  set(HDF5_NO_FIND_PACKAGE_CONFIG_FILE TRUE)
  string(APPEND BRAHMA_CONFIGURE_ARGS
    ";-DHDF5_PREFER_PARALLEL=TRUE;-DHDF5_NO_FIND_PACKAGE_CONFIG_FILE=TRUE")

  # Forward HDF5_ROOT so brahma's find_package(HDF5) resolves to the same install
  if(DEFINED HDF5_ROOT AND NOT "${HDF5_ROOT}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DHDF5_ROOT=${HDF5_ROOT}")
  endif()

  # Forward CMAKE_PREFIX_PATH in case HDF5 was found via a prefix path
  if(DEFINED CMAKE_PREFIX_PATH AND NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
  endif()

  # Detect HDF5 version here and forward BRAHMA_HDF5_VERSION = major*100000 + minor*100 + patch.
  # Without this, brahma runs its own find_package(HDF5) which may pick a different install
  # or compute the wrong version, enabling the wrong set of intercepted functions.
  find_package(HDF5 REQUIRED COMPONENTS C)
  # Capture exact library paths to forward to brahma so it uses the same HDF5.
  set(_dftracer_dep_hdf5_include_dirs "${HDF5_INCLUDE_DIRS}")
  set(_dftracer_dep_hdf5_libraries    "${HDF5_LIBRARIES}")
  set(_dftracer_dep_hdf5_version_str  "${HDF5_VERSION}")
  set(_dftracer_dep_hdf5_version "")
  if(HDF5_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    math(EXPR _dftracer_dep_hdf5_version
         "${CMAKE_MATCH_1} * 100000 + ${CMAKE_MATCH_2} * 100 + ${CMAKE_MATCH_3}")
  endif()
  if(_dftracer_dep_hdf5_version)
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DBRAHMA_HDF5_VERSION=${_dftracer_dep_hdf5_version}")
    message(STATUS "[${PROJECT_NAME}] dependency: HDF5 version=${HDF5_VERSION} brahma_version=${_dftracer_dep_hdf5_version}")
    message(STATUS "[${PROJECT_NAME}] Forwarding BRAHMA_HDF5_VERSION=${_dftracer_dep_hdf5_version} to brahma")
  endif()
endif()

if(DFTRACER_GENERATE_INTERFACES)
  if(NOT "${BRAHMA_CONFIGURE_ARGS}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";")
  endif()

  string(APPEND BRAHMA_CONFIGURE_ARGS "-DBRAHMA_GENERATE_INTERFACES=ON")

  # Forward Clang_DIR / LLVM_DIR to brahma so its find_package(Clang) does not
  # accidentally pick up an unrelated LLVM install on the system (e.g., a
  # version whose dev libraries are missing on CI runners).
  if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DClang_DIR=${Clang_DIR}")
  endif()

  if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DLLVM_DIR=${LLVM_DIR}")
  endif()
endif()

# Build the CMAKE_CACHE_ARGS list that passes the exact MPI and HDF5 library
# paths found above to brahma, so brahma skips its own find_package calls and
# uses the same installations without any risk of mismatch.
# CMAKE_CACHE_ARGS (not CMAKE_ARGS) is used here because these variables can be
# semicolon-separated CMake lists, which CMAKE_ARGS would split into extra args.
set(_brahma_cmake_cache_args "")
if(_dftracer_dep_mpi_c_include_dirs)
  list(APPEND _brahma_cmake_cache_args
    "-DBRAHMA_MPI_C_INCLUDE_DIRS:STRING=${_dftracer_dep_mpi_c_include_dirs}")
endif()
if(_dftracer_dep_mpi_c_libraries)
  list(APPEND _brahma_cmake_cache_args
    "-DBRAHMA_MPI_C_LIBRARIES:STRING=${_dftracer_dep_mpi_c_libraries}")
endif()
if(_dftracer_dep_hdf5_include_dirs)
  list(APPEND _brahma_cmake_cache_args
    "-DBRAHMA_HDF5_INCLUDE_DIRS:STRING=${_dftracer_dep_hdf5_include_dirs}")
endif()
if(_dftracer_dep_hdf5_libraries)
  list(APPEND _brahma_cmake_cache_args
    "-DBRAHMA_HDF5_LIBRARIES:STRING=${_dftracer_dep_hdf5_libraries}")
endif()
if(_dftracer_dep_hdf5_version_str)
  list(APPEND _brahma_cmake_cache_args
    "-DBRAHMA_HDF5_VERSION_STRING:STRING=${_dftracer_dep_hdf5_version_str}")
endif()

find_package(brahma 5.1.0 QUIET)
if(brahma_FOUND)
  message(STATUS "[${PROJECT_NAME}] found brahma already installed")
else()
  ExternalProject_Add(
    brahma
    PREFIX ${CMAKE_BINARY_DIR}
    GIT_REPOSITORY https://github.com/llnl-asr/brahma.git
    # TODO(rayandrew): replace this commit with the brahma release tag before the
    # dftracer release, and update dependency/manifest.txt to match.
    GIT_TAG c86a1338697265d8c29d74b17718290e30de7fbf
    TIMEOUT 10
    PATCH_COMMAND ${CMAKE_COMMAND}
      -DSOURCE_DIR=<SOURCE_DIR>
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/patches/brahma_version_override.cmake"
    CMAKE_ARGS
      "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      ${BRAHMA_CONFIGURE_ARGS}
    CMAKE_CACHE_ARGS
      ${_brahma_cmake_cache_args}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel
    # --prefix for the same reason as dftracer_install_external_project: a stale
    # sub-build keeps its cached CMAKE_INSTALL_PREFIX when configure is skipped.
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --prefix ${CMAKE_INSTALL_PREFIX}
    LOG_DOWNLOAD ON
  )
  include_directories(${CMAKE_INSTALL_PREFIX}/include)
  link_directories(${CMAKE_INSTALL_PREFIX}/lib)
  link_directories(${CMAKE_INSTALL_PREFIX}/lib64)
  include_directories(${CMAKE_BINARY_DIR}/src/brahma/include)
endif()
if(TARGET brahma AND TARGET cpp-logger)
  add_dependencies(brahma cpp-logger)
endif()

find_package(yaml-cpp QUIET)
dftracer_install_external_project(yaml-cpp "" "YAML_CPP" https://github.com/jbeder/yaml-cpp.git yaml-cpp-0.6.3 ${CMAKE_INSTALL_PREFIX} "-DYAML_CPP_BUILD_TESTS=OFF;-DYAML_BUILD_SHARED_LIBS=ON;-DCMAKE_POLICY_VERSION_MINIMUM=3.5")

if(NOT ${yaml-cpp_FOUND})
  if(TARGET yaml-cpp AND TARGET brahma)
    add_dependencies(yaml-cpp brahma)
  endif()

  if(TARGET yaml-cpp AND TARGET cpp-logger)
    add_dependencies(yaml-cpp cpp-logger)
  endif()
endif()

dftracer_install_external_project(pybind11 2.12.0 "pybind11" https://github.com/pybind/pybind11.git v2.12.0 ${CMAKE_INSTALL_PREFIX} "-DPYBIND11_TEST=OFF")

dftracer_install_external_project(libuv 1.52.1 "libuv" https://github.com/libuv/libuv.git v1.52.1 ${CMAKE_INSTALL_PREFIX} "-DLIBUV_BUILD_TESTS=OFF")
