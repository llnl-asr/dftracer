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

# Everything below that says what is supported is read from the sources this
# build compiles and the brahma headers it compiles against, so the generated
# script has no version tables of its own to keep in sync.
set(_bi_src "${CMAKE_CURRENT_LIST_DIR}/../../src/dftracer/core")

# MPI compat interfaces. A family is traced only when brahma ships the
# interface (include/brahma/interface/<kind>_<slug>_compat.h) and DFTracer has
# a tracer for it (src/dftracer/core/brahma/<kind>_<slug>_compat.cpp, the files
# the core library globs). Families on one side only are reported so a brahma
# that is newer than the generated tracers is visible.
function(_dftracer_bi_compat_slugs out kind dir ext)
    set(_slugs "")
    file(GLOB _files "${dir}/${kind}_*_compat.${ext}")
    foreach(_f ${_files})
        get_filename_component(_n "${_f}" NAME_WE)
        string(REGEX REPLACE "^${kind}_(.*)_compat$" "\\1" _slug "${_n}")
        list(APPEND _slugs "${_slug}")
    endforeach()
    list(SORT _slugs)
    set(${out} "${_slugs}" PARENT_SCOPE)
endfunction()

set(_bi_generated_from "")
set(_bi_brahma_dir "")
if(_bi_brahma_hdr)
    get_filename_component(_bi_brahma_dir "${_bi_brahma_hdr}" DIRECTORY)
    set(_bi_agg "${_bi_brahma_dir}/interface/mpi_compat.h")
    if(EXISTS "${_bi_agg}")
        file(STRINGS "${_bi_agg}" _bi_lines REGEX "^///   [a-z]+ [0-9.]+$")
        foreach(_l ${_bi_lines})
            string(REGEX REPLACE "^///   " "" _l "${_l}")
            list(APPEND _bi_generated_from "${_l}")
        endforeach()
    endif()
endif()

# The family brahma compiles in for this MPI: same rule as the gate in each
# brahma compat header (implementation, major, minor of BRAHMA_MPI_VERSION).
set(_bi_active_slug "")
if(_bi_b_mpi_impl AND _bi_b_mpi_ver GREATER 0)
    string(TOLOWER "${_bi_b_mpi_impl}" _bi_impl_lc)
    math(EXPR _bi_major "${_bi_b_mpi_ver} / 100000")
    math(EXPR _bi_minor "(${_bi_b_mpi_ver} / 100) % 1000")
    set(_bi_active_slug "${_bi_impl_lc}_${_bi_major}_${_bi_minor}")
endif()
_dftracer_bi_set(MPI_ACTIVE_SLUG "${_bi_active_slug}")

foreach(_kind mpi mpiio)
    string(TOUPPER "${_kind}" _K)
    set(_brahma "")
    if(_bi_brahma_dir)
        _dftracer_bi_compat_slugs(_brahma ${_kind} "${_bi_brahma_dir}/interface" h)
    endif()
    _dftracer_bi_compat_slugs(_tracers ${_kind} "${_bi_src}/brahma" cpp)
    set(_both "")
    set(_brahma_only "")
    set(_tracer_only "${_tracers}")
    foreach(_s ${_brahma})
        if(_s IN_LIST _tracers)
            list(APPEND _both "${_s}")
            list(REMOVE_ITEM _tracer_only "${_s}")
        else()
            list(APPEND _brahma_only "${_s}")
        endif()
    endforeach()
    _dftracer_bi_set(${_K}_COMPAT_FAMILIES "${_both}")
    _dftracer_bi_set(${_K}_COMPAT_BRAHMA_ONLY "${_brahma_only}")
    _dftracer_bi_set(${_K}_COMPAT_TRACER_ONLY "${_tracer_only}")
endforeach()
_dftracer_bi_set(MPI_GENERATED_FROM "${_bi_generated_from}")

# HDF5: every [lo, hi) BRAHMA_HDF5_VERSION range any wrapper in hdf5.cpp is
# compiled for. The script merges them into the versions that trace anything.
set(_bi_hdf5_ranges "")
if(EXISTS "${_bi_src}/brahma/hdf5.cpp")
    file(READ "${_bi_src}/brahma/hdf5.cpp" _bi_txt)
    string(REGEX MATCHALL
        "BRAHMA_HDF5_VERSION >= [0-9]+ && BRAHMA_HDF5_VERSION < [0-9]+"
        _bi_pairs "${_bi_txt}")
    list(REMOVE_DUPLICATES _bi_pairs)
    foreach(_p ${_bi_pairs})
        string(REGEX REPLACE ".*>= ([0-9]+) .*< ([0-9]+)" "\\1:\\2" _p "${_p}")
        list(APPEND _bi_hdf5_ranges "${_p}")
    endforeach()
endif()
_dftracer_bi_set(HDF5_RANGES "${_bi_hdf5_ranges}")

# CUDA: the CUPTI struct bands (DFTRACER_GET_VERSION thresholds) in
# cuda/intercept.cpp, and the toolkit headers its "Verified against the
# headers of CUDA ..." comment lists.
set(_bi_cuda_bands "")
set(_bi_cuda_verified "")
set(_bi_f "${_bi_src}/function/cuda/intercept.cpp")
if(EXISTS "${_bi_f}")
    file(READ "${_bi_f}" _bi_txt)
    string(REGEX MATCHALL
        "DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION\\([0-9]+, [0-9]+, [0-9]+\\)"
        _bi_m "${_bi_txt}")
    foreach(_g ${_bi_m})
        string(REGEX REPLACE ".*\\(([0-9]+), ([0-9]+), ([0-9]+)\\)" "\\1.\\2" _g "${_g}")
        list(APPEND _bi_cuda_bands "${_g}")
    endforeach()
    list(REMOVE_DUPLICATES _bi_cuda_bands)
    file(STRINGS "${_bi_f}" _bi_lines)
    set(_in FALSE)
    set(_bi_txt "")
    foreach(_l IN LISTS _bi_lines)
        if(_l MATCHES "Verified against the headers of CUDA")
            set(_in TRUE)
        endif()
        if(_in)
            string(APPEND _bi_txt " ${_l}")
            if(_l MATCHES "\\.[ \t]*$" OR NOT _l MATCHES "^[ \t]*//")
                break()
            endif()
        endif()
    endforeach()
    string(REGEX REPLACE ".*headers of CUDA" "" _bi_txt "${_bi_txt}")
    string(REGEX REPLACE "[ \t]*//[ \t]*" " " _bi_txt "${_bi_txt}")
    string(REGEX REPLACE "\\.[ \t]*$" "" _bi_txt "${_bi_txt}")
    string(STRIP "${_bi_txt}" _bi_cuda_verified)
endif()
_dftracer_bi_set(CUDA_BANDS "${_bi_cuda_bands}")
_dftracer_bi_set(CUDA_VERIFIED "${_bi_cuda_verified}")

# ROCm: every rocprofiler-sdk API version hip/intercept.cpp branches on.
set(_bi_rocm_gates "")
file(GLOB _bi_hip_files "${_bi_src}/function/hip/*.cpp" "${_bi_src}/function/hip/*.h")
foreach(_f ${_bi_hip_files})
    file(READ "${_f}" _bi_txt)
    string(REGEX MATCHALL
        "DFTRACER_ROCPROFILER_API_VERSION [<>=]+ DFTRACER_GET_VERSION\\([0-9]+, [0-9]+, [0-9]+\\)"
        _bi_m "${_bi_txt}")
    foreach(_g ${_bi_m})
        string(REGEX REPLACE ".*\\(([0-9]+), ([0-9]+), ([0-9]+)\\)" "\\1.\\2.\\3" _g "${_g}")
        list(APPEND _bi_rocm_gates "${_g}")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _bi_rocm_gates)
list(SORT _bi_rocm_gates COMPARE NATURAL)
_dftracer_bi_set(ROCM_GATES "${_bi_rocm_gates}")

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
