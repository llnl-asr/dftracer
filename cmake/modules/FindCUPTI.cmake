# FindCUPTI
# ---------
# Locates the CUDA toolkit and its CUPTI (CUDA Profiling Tools Interface)
# headers/library, which DFTracer uses to trace NVIDIA GPU activity.
#
# CUPTI ships inside the CUDA toolkit but its location moved: through CUDA 10.1
# it lives under <cuda>/extras/CUPTI, and from CUDA 10.2 onwards it is merged
# into the toolkit's own include/ and lib64/.  Both layouts are handled.
#
# Search order for the CUDA root:
#   1. DFTRACER_CUDA_PATH        (explicit DFTracer override)
#   2. CUDAToolkit_ROOT / CUDA_TOOLKIT_ROOT_DIR
#   3. CUDA_HOME / CUDA_PATH environment variables (set by `module load cuda`)
#   4. find_package(CUDAToolkit)
#   5. nvcc on PATH
#
# Sets:
#   CUPTI_FOUND
#   CUPTI_INCLUDE_DIRS   - CUPTI headers plus the toolkit include dir (cupti.h
#                          includes cuda.h, which lives in the latter on the
#                          split CUDA 10.1 layout)
#   CUPTI_LIBRARIES      - libcupti
#   CUPTI_LIBRARY_DIR    - directory holding libcupti (for rpath)
#   CUPTI_CUDA_ROOT      - the resolved CUDA toolkit root
#   CUPTI_CUDA_VERSION   - toolkit version as MAJOR.MINOR.PATCH, when known

set(_cupti_cuda_hints "")

if(DFTRACER_CUDA_PATH)
        list(APPEND _cupti_cuda_hints "${DFTRACER_CUDA_PATH}")
endif()

foreach(_cupti_var CUDAToolkit_ROOT CUDA_TOOLKIT_ROOT_DIR)
        if(DEFINED ${_cupti_var} AND NOT "${${_cupti_var}}" STREQUAL "")
                list(APPEND _cupti_cuda_hints "${${_cupti_var}}")
        endif()
endforeach()

foreach(_cupti_env CUDAToolkit_ROOT CUDA_HOME CUDA_PATH)
        if(DEFINED ENV{${_cupti_env}} AND NOT "$ENV{${_cupti_env}}" STREQUAL "")
                list(APPEND _cupti_cuda_hints "$ENV{${_cupti_env}}")
        endif()
endforeach()

# Only fall back to auto-discovery when nothing was pinned explicitly; an
# explicit DFTRACER_CUDA_PATH must not be silently overridden by a stray
# toolkit elsewhere on the system.
if(NOT DFTRACER_CUDA_PATH)
        find_package(CUDAToolkit QUIET)

        if(CUDAToolkit_FOUND)
                if(DEFINED CUDAToolkit_TARGET_DIR)
                        list(APPEND _cupti_cuda_hints "${CUDAToolkit_TARGET_DIR}")
                endif()

                if(DEFINED CUDAToolkit_BIN_DIR)
                        get_filename_component(_cupti_toolkit_root "${CUDAToolkit_BIN_DIR}" DIRECTORY)
                        list(APPEND _cupti_cuda_hints "${_cupti_toolkit_root}")
                endif()
        endif()

        find_program(_cupti_nvcc nvcc)

        if(_cupti_nvcc)
                get_filename_component(_cupti_nvcc_bin "${_cupti_nvcc}" DIRECTORY)
                get_filename_component(_cupti_nvcc_root "${_cupti_nvcc_bin}" DIRECTORY)
                list(APPEND _cupti_cuda_hints "${_cupti_nvcc_root}")
        endif()

        list(APPEND _cupti_cuda_hints /usr/local/cuda)
endif()

list(REMOVE_DUPLICATES _cupti_cuda_hints)

# Resolve the CUDA root to the first hint that actually contains CUPTI.
set(CUPTI_CUDA_ROOT "")
set(_cupti_include_hints "")
set(_cupti_library_hints "")

foreach(_cupti_root IN LISTS _cupti_cuda_hints)
        if(EXISTS "${_cupti_root}/extras/CUPTI/include/cupti.h")
                set(CUPTI_CUDA_ROOT "${_cupti_root}")
                set(_cupti_include_hints "${_cupti_root}/extras/CUPTI/include")
                set(_cupti_library_hints
                        "${_cupti_root}/extras/CUPTI/lib64"
                        "${_cupti_root}/extras/CUPTI/lib"
                        "${_cupti_root}/lib64")
                break()
        elseif(EXISTS "${_cupti_root}/include/cupti.h")
                set(CUPTI_CUDA_ROOT "${_cupti_root}")
                set(_cupti_include_hints "${_cupti_root}/include")
                set(_cupti_library_hints "${_cupti_root}/lib64" "${_cupti_root}/lib")
                break()
        endif()
endforeach()

find_path(CUPTI_INCLUDE_DIR
        NAMES cupti.h
        HINTS ${_cupti_include_hints}
        NO_DEFAULT_PATH)

find_library(CUPTI_LIBRARY
        NAMES cupti
        HINTS ${_cupti_library_hints}
        NO_DEFAULT_PATH)

# The CUDA runtime, needed by anything that calls cudaMalloc/cudaMemcpy (the
# CUPTI test program does).  Not required for the tracing backend itself, which
# only links libcupti.
find_library(CUPTI_CUDART_LIBRARY
        NAMES cudart
        HINTS ${_cupti_library_hints} "${CUPTI_CUDA_ROOT}/lib64" "${CUPTI_CUDA_ROOT}/lib"
        NO_DEFAULT_PATH)

# cupti.h does `#include <cuda.h>`, which on the CUDA 10.1 split layout is not
# in the CUPTI include dir. Always carry the toolkit include dir alongside.
set(CUPTI_CUDA_INCLUDE_DIR "")

if(CUPTI_CUDA_ROOT AND EXISTS "${CUPTI_CUDA_ROOT}/include/cuda.h")
        set(CUPTI_CUDA_INCLUDE_DIR "${CUPTI_CUDA_ROOT}/include")
endif()

# Read the toolkit version out of cuda.h rather than trusting the directory
# name, which is not a reliable version source across installs.
set(CUPTI_CUDA_VERSION "")

if(CUPTI_CUDA_INCLUDE_DIR AND EXISTS "${CUPTI_CUDA_INCLUDE_DIR}/cuda.h")
        file(READ "${CUPTI_CUDA_INCLUDE_DIR}/cuda.h" _cupti_cuda_h)
        string(REGEX MATCH "#define[ \t]+CUDA_VERSION[ \t]+([0-9]+)" _cupti_ver_match "${_cupti_cuda_h}")

        if(CMAKE_MATCH_1)
                # CUDA_VERSION is encoded as MAJOR*1000 + MINOR*10 + PATCH.
                math(EXPR _cupti_major "${CMAKE_MATCH_1} / 1000")
                math(EXPR _cupti_minor "(${CMAKE_MATCH_1} % 1000) / 10")
                math(EXPR _cupti_patch "${CMAKE_MATCH_1} % 10")
                set(CUPTI_CUDA_VERSION "${_cupti_major}.${_cupti_minor}.${_cupti_patch}")
        endif()
endif()

if(NOT CUPTI_CUDA_VERSION AND CUDAToolkit_FOUND AND CUDAToolkit_VERSION)
        set(CUPTI_CUDA_VERSION "${CUDAToolkit_VERSION}")
endif()

set(CUPTI_INCLUDE_DIRS "")

if(CUPTI_INCLUDE_DIR)
        list(APPEND CUPTI_INCLUDE_DIRS "${CUPTI_INCLUDE_DIR}")
endif()

if(CUPTI_CUDA_INCLUDE_DIR)
        list(APPEND CUPTI_INCLUDE_DIRS "${CUPTI_CUDA_INCLUDE_DIR}")
endif()

list(REMOVE_DUPLICATES CUPTI_INCLUDE_DIRS)
set(CUPTI_LIBRARIES "${CUPTI_LIBRARY}")

if(CUPTI_LIBRARY)
        get_filename_component(CUPTI_LIBRARY_DIR "${CUPTI_LIBRARY}" DIRECTORY)
endif()

if(CUPTI_CUDART_LIBRARY)
        get_filename_component(CUPTI_CUDART_LIBRARY_DIR "${CUPTI_CUDART_LIBRARY}" DIRECTORY)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CUPTI
        REQUIRED_VARS CUPTI_LIBRARY CUPTI_INCLUDE_DIR
        VERSION_VAR CUPTI_CUDA_VERSION)

mark_as_advanced(CUPTI_INCLUDE_DIR CUPTI_LIBRARY CUPTI_CUDART_LIBRARY)
