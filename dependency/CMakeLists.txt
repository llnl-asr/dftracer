include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/modules/dftracer-utils.cmake)
dftracer_install_external_project(cpp-logger 2.0.0 "CPP_LOGGER" https://github.com/llnl-asr/cpp-logger.git e2f126e9fe71f7f49ad4c8b69c45e6508e1ad907 ${CMAKE_INSTALL_PREFIX} "")

# Build Brahma with MPI and/or HDF5 support based on dftracer options.
#
# DFTRACER_BRAHMA_VARIANTS=ON (default) builds brahma once per part of
# DFTracer that links it, each static so they combine in libdftracer_core:
#   <prefix>                          no MPI, no HDF5: the core links this one
#   <prefix>/brahma-variants/mpi      MPI only: the MPI module links this one
#   <prefix>/brahma-variants/hdf5     HDF5 only: the HDF5 module links this one
# OFF builds the single shared brahma with every interface, as before.
option(DFTRACER_BRAHMA_VARIANTS
       "Build a static brahma for the core and one per MPI/HDF5 module" ON)
# Keep BRAHMA's core targets/install rules enabled in the currently pinned branch.
set(_brahma_common_args "-DBRAHMA_BUILD_DEPENDENCIES=ON")
set(_brahma_mpi_args "")
set(_brahma_hdf5_args "")

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
  list(APPEND _brahma_mpi_args "-DBRAHMA_BUILD_WITH_MPI=ON")

  # Forward detected/overridden MPI implementation and numeric version to brahma.
  # BRAHMA_MPI_VERSION must be the library version (e.g. 400106 for OpenMPI 4.1.6),
  # not the MPI standard version (3.1 → 300100) that brahma would compute on its own.
  if(_dftracer_dep_mpi_impl AND NOT "${_dftracer_dep_mpi_impl}" STREQUAL "UNKNOWN")
    list(APPEND _brahma_mpi_args "-DBRAHMA_MPI_IMPL=${_dftracer_dep_mpi_impl}")
    message(STATUS "[${PROJECT_NAME}] Forwarding BRAHMA_MPI_IMPL=${_dftracer_dep_mpi_impl} to brahma")
  endif()
  # A version given by the user wins over the probe, and is the only source of
  # one when DFTRACER_MPI_IMPL skips the probe.
  if(BRAHMA_MPI_VERSION)
    set(_dftracer_dep_brahma_mpi_version "${BRAHMA_MPI_VERSION}")
  endif()
  if(_dftracer_dep_brahma_mpi_version)
    list(APPEND _brahma_mpi_args "-DBRAHMA_MPI_VERSION=${_dftracer_dep_brahma_mpi_version}")
    message(STATUS "[${PROJECT_NAME}] Forwarding BRAHMA_MPI_VERSION=${_dftracer_dep_brahma_mpi_version} to brahma")
  endif()
endif()

if(DFTRACER_ENABLE_HDF5)
  list(APPEND _brahma_hdf5_args "-DBRAHMA_BUILD_WITH_HDF5=ON")

  # Spack's HDF5 config package can report its library as the local CMake
  # target "hdf5-shared".  That target cannot be carried across Brahma's
  # ExternalProject boundary and is otherwise emitted as the invalid linker
  # flag -lhdf5-shared.  Use CMake's FindHDF5 module in both projects so the
  # selected Spack installation is represented by concrete library paths.
  set(HDF5_PREFER_PARALLEL TRUE)
  set(HDF5_NO_FIND_PACKAGE_CONFIG_FILE TRUE)
  list(APPEND _brahma_hdf5_args
    "-DHDF5_PREFER_PARALLEL=TRUE" "-DHDF5_NO_FIND_PACKAGE_CONFIG_FILE=TRUE")

  # Forward HDF5_ROOT so brahma's find_package(HDF5) resolves to the same install
  if(DEFINED HDF5_ROOT AND NOT "${HDF5_ROOT}" STREQUAL "")
    list(APPEND _brahma_hdf5_args "-DHDF5_ROOT=${HDF5_ROOT}")
  endif()


  # Detect HDF5 version here and forward BRAHMA_HDF5_VERSION = major*100000 + minor*100 + patch.
  # Without this, brahma runs its own find_package(HDF5) which may pick a different install
  # or compute the wrong version, enabling the wrong set of intercepted functions.
  find_package(HDF5 REQUIRED COMPONENTS C)
  # Capture exact library paths to forward to brahma so it uses the same HDF5.
  set(_dftracer_dep_hdf5_include_dirs "${HDF5_INCLUDE_DIRS}")
  set(_dftracer_dep_hdf5_libraries    "${HDF5_LIBRARIES}")
  # A parallel HDF5's headers include mpi.h, and the h5pcc of a CMake-built
  # HDF5 does not report MPI's paths (the MPI wrapper supplies them). Brahma's
  # HDF5 variant is built with MPI off, so hand it MPI's paths with HDF5's.
  if(HDF5_IS_PARALLEL)
    if(NOT MPI_FOUND)
      find_package(MPI QUIET COMPONENTS C)
    endif()
    if(MPI_C_FOUND)
      list(APPEND _dftracer_dep_hdf5_include_dirs ${MPI_C_INCLUDE_DIRS})
      list(APPEND _dftracer_dep_hdf5_libraries ${MPI_C_LIBRARIES})
    else()
      message(WARNING "[${PROJECT_NAME}] HDF5 is parallel but MPI was not found; brahma's HDF5 interface may not compile")
    endif()
  endif()
  set(_dftracer_dep_hdf5_version_str  "${HDF5_VERSION}")
  set(_dftracer_dep_hdf5_version "")
  if(HDF5_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    math(EXPR _dftracer_dep_hdf5_version
         "${CMAKE_MATCH_1} * 100000 + ${CMAKE_MATCH_2} * 100 + ${CMAKE_MATCH_3}")
  endif()
  if(_dftracer_dep_hdf5_version)
    list(APPEND _brahma_hdf5_args "-DBRAHMA_HDF5_VERSION=${_dftracer_dep_hdf5_version}")
    message(STATUS "[${PROJECT_NAME}] dependency: HDF5 version=${HDF5_VERSION} brahma_version=${_dftracer_dep_hdf5_version}")
    message(STATUS "[${PROJECT_NAME}] Forwarding BRAHMA_HDF5_VERSION=${_dftracer_dep_hdf5_version} to brahma")
  endif()
endif()

if(DFTRACER_GENERATE_INTERFACES)
  list(APPEND _brahma_common_args "-DBRAHMA_GENERATE_INTERFACES=ON")

  # Forward Clang_DIR / LLVM_DIR to brahma so its find_package(Clang) does not
  # accidentally pick up an unrelated LLVM install on the system (e.g., a
  # version whose dev libraries are missing on CI runners).
  if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
    list(APPEND _brahma_common_args "-DClang_DIR=${Clang_DIR}")
  endif()

  if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
    list(APPEND _brahma_common_args "-DLLVM_DIR=${LLVM_DIR}")
  endif()
endif()

# Build the CMAKE_CACHE_ARGS list that passes the exact MPI and HDF5 library
# paths found above to brahma, so brahma skips its own find_package calls and
# uses the same installations without any risk of mismatch.
# CMAKE_CACHE_ARGS (not CMAKE_ARGS) is used here because these variables can be
# semicolon-separated CMake lists, which CMAKE_ARGS would split into extra args.
set(_brahma_mpi_cache_args "")
set(_brahma_hdf5_cache_args "")
if(_dftracer_dep_mpi_c_include_dirs)
  list(APPEND _brahma_mpi_cache_args
    "-DBRAHMA_MPI_C_INCLUDE_DIRS:STRING=${_dftracer_dep_mpi_c_include_dirs}")
endif()
if(_dftracer_dep_mpi_c_libraries)
  list(APPEND _brahma_mpi_cache_args
    "-DBRAHMA_MPI_C_LIBRARIES:STRING=${_dftracer_dep_mpi_c_libraries}")
endif()
if(_dftracer_dep_hdf5_include_dirs)
  list(APPEND _brahma_hdf5_cache_args
    "-DBRAHMA_HDF5_INCLUDE_DIRS:STRING=${_dftracer_dep_hdf5_include_dirs}")
endif()
if(_dftracer_dep_hdf5_libraries)
  list(APPEND _brahma_hdf5_cache_args
    "-DBRAHMA_HDF5_LIBRARIES:STRING=${_dftracer_dep_hdf5_libraries}")
endif()
if(_dftracer_dep_hdf5_version_str)
  list(APPEND _brahma_hdf5_cache_args
    "-DBRAHMA_HDF5_VERSION_STRING:STRING=${_dftracer_dep_hdf5_version_str}")
endif()

# Forward CMAKE_PREFIX_PATH in case HDF5 was found via a prefix path. It is a
# list that holds this project's own binary dir, so it must stay out of
# CMAKE_ARGS: split there, brahma would configure against this project's cache.
if(DFTRACER_ENABLE_HDF5 AND NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
  list(APPEND _brahma_hdf5_cache_args
    "-DCMAKE_PREFIX_PATH:STRING=${CMAKE_PREFIX_PATH}")
endif()

# Which brahma to build: the release below, or another (a fork, a local clone, a
# commit not yet released) for testing dftracer against it.
set(DFTRACER_BRAHMA_GIT_REPOSITORY "https://github.com/llnl-asr/brahma.git" CACHE STRING
    "Git repository brahma is built from")
set(DFTRACER_BRAHMA_GIT_TAG "f1f4bc4d7ea36ebef715a851220322b23ba8dbc9" CACHE STRING "Tag, branch or commit of brahma to build")
set(_brahma_variants_dir "${CMAKE_INSTALL_PREFIX}/brahma-variants")
find_package(brahma 5.1.1 QUIET)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/modules/DFTracerBrahmaCheck.cmake)
if(brahma_FOUND AND (DFTRACER_ENABLE_MPI OR DFTRACER_ENABLE_DYNAMIC_DETECTION))
  # An earlier install (e.g. the same pip prefix) can hold an older brahma with
  # the same package version; build the pinned one over it instead. With
  # variants, the MPI interface is in the MPI variant, not the core's brahma.
  if(DFTRACER_BRAHMA_VARIANTS)
    set(_brahma_mpi_include_dirs "${_brahma_variants_dir}/mpi/include")
  else()
    set(_brahma_mpi_include_dirs ${BRAHMA_INCLUDE_DIRS})
  endif()
  dftracer_brahma_missing_features(_brahma_missing ${_brahma_mpi_include_dirs})
  if(_brahma_missing)
    message(STATUS "[${PROJECT_NAME}] brahma at ${brahma_DIR} lacks ${_brahma_missing}; "
                   "building ${DFTRACER_BRAHMA_GIT_TAG} instead")
    set(brahma_FOUND FALSE)
  endif()
endif()
if(brahma_FOUND AND DFTRACER_BRAHMA_VARIANTS AND DFTRACER_ENABLE_HDF5
   AND NOT EXISTS "${_brahma_variants_dir}/hdf5/include/brahma/interface/hdf5.h")
  set(brahma_FOUND FALSE)
endif()

# dftracer_add_brahma(<name> <install prefix> <CMAKE_ARGS list> <CMAKE_CACHE_ARGS list>)
function(dftracer_add_brahma name prefix args cache_args)
  ExternalProject_Add(
    ${name}
    PREFIX ${CMAKE_BINARY_DIR}
    GIT_REPOSITORY ${DFTRACER_BRAHMA_GIT_REPOSITORY}
    GIT_TAG ${DFTRACER_BRAHMA_GIT_TAG}
    TIMEOUT 10
    CMAKE_ARGS
      "-DCMAKE_INSTALL_PREFIX=${prefix}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      ${args}
    CMAKE_CACHE_ARGS
      ${cache_args}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel
    # --prefix for the same reason as dftracer_install_external_project: a stale
    # sub-build keeps its cached CMAKE_INSTALL_PREFIX when configure is skipped.
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --prefix ${prefix}
    LOG_DOWNLOAD ON
  )
  if(TARGET cpp-logger)
    add_dependencies(${name} cpp-logger)
  endif()
endfunction()

if(brahma_FOUND)
  message(STATUS "[${PROJECT_NAME}] found brahma already installed")
else()
  if(DFTRACER_BRAHMA_VARIANTS)
    set(_brahma_static "-DBUILD_SHARED_LIBS=OFF")
    dftracer_add_brahma(brahma ${CMAKE_INSTALL_PREFIX}
      "${_brahma_common_args};${_brahma_static};-DBRAHMA_BUILD_WITH_MPI=OFF;-DBRAHMA_BUILD_WITH_HDF5=OFF"
      "")
    if(_brahma_mpi_args)
      dftracer_add_brahma(brahma_mpi ${_brahma_variants_dir}/mpi
        "${_brahma_common_args};${_brahma_static};${_brahma_mpi_args};-DBRAHMA_BUILD_WITH_HDF5=OFF"
        "${_brahma_mpi_cache_args}")
      add_dependencies(brahma_mpi brahma)
    endif()
    if(_brahma_hdf5_args)
      dftracer_add_brahma(brahma_hdf5 ${_brahma_variants_dir}/hdf5
        "${_brahma_common_args};${_brahma_static};-DBRAHMA_BUILD_WITH_MPI=OFF;${_brahma_hdf5_args}"
        "${_brahma_hdf5_cache_args}")
      add_dependencies(brahma_hdf5 brahma)
    endif()
  else()
    if(_brahma_mpi_args)
      set(_brahma_with_mpi ${_brahma_mpi_args})
    else()
      set(_brahma_with_mpi "-DBRAHMA_BUILD_WITH_MPI=OFF")
    endif()
    dftracer_add_brahma(brahma ${CMAKE_INSTALL_PREFIX}
      "${_brahma_common_args};${_brahma_with_mpi};${_brahma_hdf5_args}"
      "${_brahma_mpi_cache_args};${_brahma_hdf5_cache_args}")
  endif()
  include_directories(${CMAKE_INSTALL_PREFIX}/include)
  link_directories(${CMAKE_INSTALL_PREFIX}/lib)
  link_directories(${CMAKE_INSTALL_PREFIX}/lib64)
  include_directories(${CMAKE_BINARY_DIR}/src/brahma/include)
endif()
if(TARGET brahma AND TARGET cpp-logger)
  add_dependencies(brahma cpp-logger)
endif()

# At least the release DFTracer pins (0.6.3, built below otherwise). 0.6.2,
# which RHEL 8 ships, has the same soname but a different ABI (node_data's
# empty_scalar is a variable there, a function from 0.6.3): a composable
# install's core built from source against it then loads the 0.6.3 the wheel
# bundles and fails with an undefined symbol. The version goes to
# dftracer_install_external_project, whose own find_package decides whether
# to build it.
dftracer_install_external_project(yaml-cpp 0.6.3 "YAML_CPP" https://github.com/jbeder/yaml-cpp.git yaml-cpp-0.6.3 ${CMAKE_INSTALL_PREFIX} "-DYAML_CPP_BUILD_TESTS=OFF;-DYAML_BUILD_SHARED_LIBS=ON;-DCMAKE_POLICY_VERSION_MINIMUM=3.5")

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
# libdeflate compresses the gzip members of every trace.
dftracer_install_external_project(libdeflate 1.9 "libdeflate" https://github.com/ebiggers/libdeflate.git v1.24 ${CMAKE_INSTALL_PREFIX} "-DLIBDEFLATE_BUILD_STATIC_LIB=OFF;-DLIBDEFLATE_BUILD_GZIP=OFF;-DLIBDEFLATE_BUILD_TESTS=OFF;-DCMAKE_INSTALL_LIBDIR=lib64")

# Boost: only headers are needed (Boost.Interprocess maps the shared-memory
# event ring between traced processes and dftracer_service), so the release
# tarball is unpacked and its boost/ directory copied into the prefix -- no b2
# build and no recursive-submodule clone. Kept last in this file: the tarball
# has no GIT_TAG, and scripts/wheel/manifest.py pairs each ExternalProject_Add
# with the next GIT_TAG it finds. The wheel build takes Boost from the system
# package manager instead (scripts/wheel/build_deps.sh).
find_package(Boost 1.66 QUIET)
if(NOT Boost_FOUND)
  ExternalProject_Add(
    boost_headers
    PREFIX ${CMAKE_BINARY_DIR}
    URL https://archives.boost.io/release/1.83.0/source/boost_1_83_0.tar.gz
    URL_HASH SHA256=c0685b68dd44cc46574cce86c4e17c0f611b15e195be9848dfd0769a0a207628
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/boost ${CMAKE_INSTALL_PREFIX}/include/boost
    LOG_DOWNLOAD ON
  )
endif()
