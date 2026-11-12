# What this machine has of each library a DFTracer module wraps, and so which
# module variant fits it. Used by the compose project to pick the archives
# that match the machine; it shares dftracer_module_variant with the main
# build, so the keys it computes are the ones the archives were named with.
#
#   dftracer_detect_module(<module> <found_var> <variant_var> <libraries_var>)
#
# <found_var> is TRUE when the library is here, <variant_var> the variant key
# a module built against it carries, <libraries_var> what to link for it.
# MPI_HOME / CMAKE_PREFIX_PATH, HDF5_ROOT, PAPI_PREFIX, ROCM_PATH and
# CUDA_HOME / DFTRACER_CUDA_PATH steer the searches as in the main build.
include_guard(GLOBAL)
include(DFTracerCompose)

set(_dftracer_detect_dir "${CMAKE_CURRENT_LIST_DIR}")

# MPI implementation and library version, from the same probe as the main
# build. Sets DFTRACER_MPI_IMPL_NAME / DFTRACER_MPI_PACKAGE_VERSION in the
# caller.
function(_dftracer_detect_mpi_impl)
  set(DFTRACER_MPI_IMPL_NAME "UNKNOWN" PARENT_SCOPE)
  set(DFTRACER_MPI_PACKAGE_VERSION "" PARENT_SCOPE)
  try_run(_run _compile
    "${CMAKE_BINARY_DIR}/dftracer_detect_mpi"
    "${_dftracer_detect_dir}/mpi_detect.c"
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${MPI_C_INCLUDE_DIRS}"
    LINK_LIBRARIES ${MPI_C_LIBRARIES}
    RUN_OUTPUT_VARIABLE _out)
  if(_compile AND "${_run}" STREQUAL "0")
    if(_out MATCHES "IMPL=([A-Z]+)")
      set(DFTRACER_MPI_IMPL_NAME "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
    if(_out MATCHES "VERSION=([0-9]+\\.[0-9]+\\.[0-9]+)")
      set(DFTRACER_MPI_PACKAGE_VERSION "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

# The parameters are prefixed: CMake functions see their callers' variables,
# and FindHDF5's _HDF5_parse_compile_line writes its results through
# ${libraries_var} and similar names it never defines, so a caller variable
# called libraries_var sends HDF5's library list there instead, and HDF5 is
# not found wherever it has to be searched for (Ubuntu's h5pcc, for one).
function(dftracer_detect_module _dftd_module _dftd_found_out _dftd_variant_out _dftd_libraries_out)
  set(module "${_dftd_module}")
  set(_found FALSE)
  set(_libraries "")
  list(APPEND CMAKE_MODULE_PATH "${_dftracer_detect_dir}")

  if(module STREQUAL "mpi" OR module STREQUAL "hdf5")
    find_package(MPI QUIET COMPONENTS C CXX)
    if(MPI_C_FOUND)
      _dftracer_detect_mpi_impl()
    endif()
  endif()

  if(module STREQUAL "mpi")
    if(MPI_C_FOUND)
      set(_found TRUE)
      set(_libraries ${MPI_CXX_LIBRARIES} ${MPI_C_LIBRARIES})
    endif()
  elseif(module STREQUAL "hdf5")
    set(HDF5_PREFER_PARALLEL TRUE)
    set(HDF5_NO_FIND_PACKAGE_CONFIG_FILE TRUE)
    find_package(HDF5 QUIET COMPONENTS C)
    if(HDF5_FOUND)
      set(_found TRUE)
      if(HDF5_C_LIBRARIES)
        set(_libraries ${HDF5_C_LIBRARIES})
      else()
        set(_libraries ${HDF5_LIBRARIES})
      endif()
      if(HDF5_IS_PARALLEL AND MPI_C_FOUND)
        list(APPEND _libraries ${MPI_C_LIBRARIES})
      endif()
    endif()
  elseif(module STREQUAL "papi")
    find_package(PAPI QUIET)
    if(PAPI_FOUND)
      set(_found TRUE)
      set(_libraries ${PAPI_LIBRARIES})
    endif()
  elseif(module STREQUAL "hwloc")
    find_package(HWLOC QUIET)
    if(HWLOC_FOUND)
      set(_found TRUE)
      set(_libraries ${HWLOC_LIBRARIES})
    endif()
  elseif(module STREQUAL "hip")
    if(DEFINED ENV{ROCM_PATH})
      list(APPEND CMAKE_PREFIX_PATH "$ENV{ROCM_PATH}")
    endif()
    find_package(rocprofiler-sdk QUIET)
    if(rocprofiler-sdk_FOUND AND TARGET rocprofiler-sdk::rocprofiler-sdk)
      set(_found TRUE)
      if(rocprofiler-sdk_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.?([0-9]*)")
        set(DFTRACER_ROCPROFILER_VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(DFTRACER_ROCPROFILER_VERSION_MINOR "${CMAKE_MATCH_2}")
      endif()
      dftracer_resolve_link_items(_libraries rocprofiler-sdk::rocprofiler-sdk)
    endif()
  elseif(module STREQUAL "cuda")
    find_package(CUPTI QUIET)
    if(CUPTI_FOUND)
      set(_found TRUE)
      set(_libraries ${CUPTI_LIBRARIES})
    endif()
  else()
    message(FATAL_ERROR "[DFTRACER] no detection for module '${module}'")
  endif()

  set(_variant "")
  if(_found)
    dftracer_module_variant(${module} _variant)
  endif()
  set(${_dftd_found_out} ${_found} PARENT_SCOPE)
  set(${_dftd_variant_out} "${_variant}" PARENT_SCOPE)
  set(${_dftd_libraries_out} "${_libraries}" PARENT_SCOPE)
endfunction()
