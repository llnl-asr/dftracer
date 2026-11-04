# Bake everything configure time found into a standalone `dftracer_install_info`
# script. Include this after all dependency detection has run. The generated
# script only needs python3's standard library, so it works without importing
# dftracer, without the MPI/HDF5/PAPI runtimes loaded, and on a relocated tree.

# Escape a value so it can sit between double quotes in the generated script.
function(_dftracer_bi_set name)
    set(_v "${ARGN}")
    string(REPLACE "\\" "\\\\" _v "${_v}")
    string(REPLACE "\"" "\\\"" _v "${_v}")
    set(DFTRACER_BI_${name} "${_v}" PARENT_SCOPE)
endfunction()

function(_dftracer_bi_onoff name value)
    if(value)
        set(DFTRACER_BI_${name} "True" PARENT_SCOPE)
    else()
        set(DFTRACER_BI_${name} "False" PARENT_SCOPE)
    endif()
endfunction()

# brahma bakes its own MPI/HDF5 version numbers into brahma_config.hpp; those,
# not the library versions, select which interceptors are compiled.
set(_bi_brahma_hdr "")
foreach(_d ${BRAHMA_INCLUDE_DIRS})
    if(EXISTS "${_d}/brahma/brahma_config.hpp")
        set(_bi_brahma_hdr "${_d}/brahma/brahma_config.hpp")
        break()
    endif()
endforeach()
set(_bi_b_mpi_ver 0)
set(_bi_b_hdf5_ver 0)
set(_bi_b_mpi_impl "")
set(_bi_b_mpi 0)
set(_bi_b_hdf5 0)
if(_bi_brahma_hdr)
    file(READ "${_bi_brahma_hdr}" _bi_txt)
    if(_bi_txt MATCHES "\n#define BRAHMA_MPI_VERSION ([0-9]+)")
        set(_bi_b_mpi_ver ${CMAKE_MATCH_1})
    endif()
    if(_bi_txt MATCHES "\n#define BRAHMA_HDF5_VERSION ([0-9]+)")
        set(_bi_b_hdf5_ver ${CMAKE_MATCH_1})
    endif()
    if(_bi_txt MATCHES "\n#define BRAHMA_MPI_IMPL_NAME \"([A-Z]+)\"")
        set(_bi_b_mpi_impl "${CMAKE_MATCH_1}")
    endif()
    if(_bi_txt MATCHES "\n#define BRAHMA_ENABLE_MPI 1")
        set(_bi_b_mpi 1)
    endif()
    if(_bi_txt MATCHES "\n#define BRAHMA_ENABLE_HDF5 1")
        set(_bi_b_hdf5 1)
    endif()
endif()

_dftracer_bi_set(VERSION "${DFTRACER_PACKAGE_VERSION}")
_dftracer_bi_set(PREFIX "${CMAKE_INSTALL_PREFIX}")
_dftracer_bi_set(BUILD_TYPE "${CMAKE_BUILD_TYPE}")
_dftracer_bi_set(CMAKE_VERSION "${CMAKE_VERSION}")
_dftracer_bi_set(C_COMPILER "${CMAKE_C_COMPILER}")
_dftracer_bi_set(C_COMPILER_ID "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
_dftracer_bi_set(CXX_COMPILER "${CMAKE_CXX_COMPILER}")
_dftracer_bi_set(CXX_COMPILER_ID "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
_dftracer_bi_set(LIBC "${DFTRACER_LIBC_SO_PATH}")

# What the user asked for vs. what the build ended up enabling.
foreach(_o MPI HDF5 FTRACING HIP_TRACING CUDA_TRACING PAPI_TRACING VARIORUM DYNAMIC_DETECTION)
    _dftracer_bi_onoff(REQ_${_o} "${DFTRACER_ENABLE_${_o}}")
endforeach()
_dftracer_bi_onoff(ON_MPI "${DFTRACER_MPI_ENABLE}")
_dftracer_bi_onoff(ON_HDF5 "${DFTRACER_HDF5_ENABLE}")
_dftracer_bi_onoff(ON_FTRACING "${DFTRACER_ENABLE_FTRACING}")
_dftracer_bi_onoff(ON_HIP_TRACING "${DFTRACER_HIP_TRACING_ENABLE}")
_dftracer_bi_onoff(ON_CUDA_TRACING "${DFTRACER_CUDA_TRACING_ENABLE}")
_dftracer_bi_onoff(ON_PAPI_TRACING "${DFTRACER_PAPI_TRACING_ENABLE}")
_dftracer_bi_onoff(ON_VARIORUM "${DFTRACER_VARIORUM_ENABLE}")
_dftracer_bi_onoff(ON_HWLOC "${DFTRACER_HWLOC_ENABLE}")

# MPI
_dftracer_bi_set(MPI_IMPL "${DFTRACER_MPI_IMPL_NAME}")
_dftracer_bi_set(MPI_VERSION "${DFTRACER_MPI_PACKAGE_VERSION}")
_dftracer_bi_set(MPI_C_COMPILER "${MPI_C_COMPILER}")
_dftracer_bi_set(MPI_CXX_COMPILER "${MPI_CXX_COMPILER}")
_dftracer_bi_set(MPI_INCLUDE_DIRS "${MPI_CXX_INCLUDE_DIRS}")
_dftracer_bi_set(MPI_LIBRARIES "${MPI_CXX_LIBRARIES}")
_dftracer_bi_set(BRAHMA_MPI_IMPL "${_bi_b_mpi_impl}")
_dftracer_bi_onoff(BRAHMA_MPI "${_bi_b_mpi}")
_dftracer_bi_set(BRAHMA_MPI_VERSION "${_bi_b_mpi_ver}")

# HDF5
_dftracer_bi_set(HDF5_VERSION "${HDF5_VERSION}")
_dftracer_bi_set(HDF5_INCLUDE_DIRS "${HDF5_INCLUDE_DIRS}")
_dftracer_bi_set(HDF5_LIBRARIES "${HDF5_C_LIBRARIES}")
_dftracer_bi_onoff(BRAHMA_HDF5 "${_bi_b_hdf5}")
_dftracer_bi_set(BRAHMA_HDF5_VERSION "${_bi_b_hdf5_ver}")

# PAPI
_dftracer_bi_set(PAPI_INCLUDE_DIRS "${PAPI_INCLUDE_DIRS}")
_dftracer_bi_set(PAPI_LIBRARIES "${PAPI_LIBRARIES}")
_dftracer_bi_set(PAPI_EVENTS "${DFTRACER_PAPI_DETECTED_EVENTS}")
_dftracer_bi_set(PAPI_HW "${DFTRACER_PAPI_HW_COUNTERS}")
_dftracer_bi_set(PAPI_FIT "${DFTRACER_PAPI_FITTING_COUNTERS}")

# GPU / power / topology
_dftracer_bi_set(ROCPROFILER_INCLUDE "${rocprofiler-sdk_INCLUDE_DIR}")
_dftracer_bi_set(ROCPROFILER_VERSION "${DFTRACER_ROCPROFILER_VERSION_MAJOR}.${DFTRACER_ROCPROFILER_VERSION_MINOR}.${DFTRACER_ROCPROFILER_VERSION_PATCH}")
_dftracer_bi_set(CUDA_VERSION "${DFTRACER_CUDA_VERSION_MAJOR}.${DFTRACER_CUDA_VERSION_MINOR}.${DFTRACER_CUDA_VERSION_PATCH}")
_dftracer_bi_set(CUPTI_INCLUDE "${CUPTI_INCLUDE_DIR}")
_dftracer_bi_set(VARIORUM_INCLUDE_DIRS "${VARIORUM_INCLUDE_DIRS}")
_dftracer_bi_set(VARIORUM_LIBRARIES "${VARIORUM_LIBRARIES}")
_dftracer_bi_set(HWLOC_INCLUDE_DIRS "${HWLOC_INCLUDE_DIRS}")
_dftracer_bi_set(HWLOC_LIBRARIES "${HWLOC_LIBRARIES}")

# Dependencies and runtime layout
_dftracer_bi_set(BRAHMA_DIR "${brahma_DIR}")
_dftracer_bi_set(BRAHMA_INCLUDE_DIRS "${BRAHMA_INCLUDE_DIRS}")
_dftracer_bi_set(PC_RING_BLOCKS "${DFTRACER_PC_RING_BLOCKS}")
_dftracer_bi_set(PC_RING_BLOCK_SIZE "${DFTRACER_PC_RING_BLOCK_SIZE}")

if(NOT CMAKE_INSTALL_BINDIR)
    set(CMAKE_INSTALL_BINDIR "bin")
endif()
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/../../script/dftracer_install_info.in"
    "${CMAKE_CURRENT_BINARY_DIR}/dftracer_install_info" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/dftracer_install_info"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
