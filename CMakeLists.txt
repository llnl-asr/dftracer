cmake_minimum_required(VERSION 3.24)

# ------------------------------------------------------------------------------
# Version information - Read from VERSION and PACKAGE_VERSION files
# ------------------------------------------------------------------------------
# Read library version from VERSION file (e.g., 4.0.0)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DFTRACER_LIBRARY_VERSION_STRING)
string(STRIP "${DFTRACER_LIBRARY_VERSION_STRING}" DFTRACER_LIBRARY_VERSION_STRING)

# Parse library version components
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ ${DFTRACER_LIBRARY_VERSION_STRING})
set(DFTRACER_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(DFTRACER_VERSION_MINOR "${CMAKE_MATCH_2}")
set(DFTRACER_VERSION_PATCH "${CMAKE_MATCH_3}")
set(DFTRACER_LIBRARY_VERSION "${DFTRACER_VERSION_MAJOR}.${DFTRACER_VERSION_MINOR}.${DFTRACER_VERSION_PATCH}")

# Read Python package version from PACKAGE_VERSION file (e.g., 2.0.2)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/PACKAGE_VERSION" DFTRACER_PACKAGE_VERSION_STRING)
string(STRIP "${DFTRACER_PACKAGE_VERSION_STRING}" DFTRACER_PACKAGE_VERSION_STRING)

# Parse package version components
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ ${DFTRACER_PACKAGE_VERSION_STRING})
set(DFTRACER_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(DFTRACER_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_2}")
set(DFTRACER_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_3}")

# Set package version as tuple string (used by setuptools-scm and Python bindings)
set(DFTRACER_PACKAGE_VERSION "(${DFTRACER_PACKAGE_VERSION_MAJOR}, ${DFTRACER_PACKAGE_VERSION_MINOR}, ${DFTRACER_PACKAGE_VERSION_PATCH})")

# Set legacy package variables (for backward compatibility)
set(DFTRACER_PACKAGE ${PROJECT_NAME})
set(DFTRACER_PACKAGE_NAME ${PROJECT_NAME})
set(DFTRACER_PACKAGE_VERSION_FULL "${DFTRACER_PACKAGE_VERSION_MAJOR}.${DFTRACER_PACKAGE_VERSION_MINOR}.${DFTRACER_PACKAGE_VERSION_PATCH}")
set(DFTRACER_PACKAGE_VERSION_SHORT "${DFTRACER_PACKAGE_VERSION_MAJOR}.${DFTRACER_PACKAGE_VERSION_MINOR}")
set(DFTRACER_PACKAGE_STRING "${DFTRACER_PACKAGE_NAME} ${DFTRACER_PACKAGE_VERSION_FULL}")
set(DFTRACER_PACKAGE_TARNAME "${DFTRACER_PACKAGE}")

project(dftracer LANGUAGES C CXX)

# Convenience defines
string(TOUPPER "${PROJECT_NAME}" UPPER_PROJECT_NAME)
string(TOLOWER "${PROJECT_NAME}" LOWER_PROJECT_NAME)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-empty-body -Wno-format-extra-args")

# ------------------------------------------------------------------------------
# Internal Includes for header and libraries
# ------------------------------------------------------------------------------
# Get installation directories -- these get used in various places;
# best to just make them available
option(DFTRACER_LIBDIR_AS_LIB OFF)

if(NOT DFTRACER_LIBDIR_AS_LIB)
        include(GNUInstallDirs)
endif()

#
if(CMAKE_INSTALL_LIBDIR)
        message(STATUS "CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR}")
        set(DFTRACER_LIBDIR ${CMAKE_INSTALL_LIBDIR})
        set(DFTRACER_INSTALL_INCLUDE_DIR
                ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR})
        set(DFTRACER_INSTALL_LIB_DIR
                ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
        set(DFTRACER_INSTALL_DOCDIR
                ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DOCDIR})
        set(DFTRACER_INSTALL_SYSCONFDIR
                ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}/modulefiles)
        set(DFTRACER_INSTALL_BINFDIR
                ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
else()
        set(DFTRACER_LIBDIR "lib")
        set(DFTRACER_INSTALL_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
        set(DFTRACER_INSTALL_LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib")
        set(DFTRACER_INSTALL_DOCDIR "${CMAKE_INSTALL_PREFIX}/doc")
        set(DFTRACER_INSTALL_SYSCONFDIR "${CMAKE_INSTALL_PREFIX}/etc/modulefiles")
        set(DFTRACER_INSTALL_BINARYDIR "${CMAKE_INSTALL_PREFIX}/bin")
        message(STATUS "DFTRACER_LIBDIR set to ${DFTRACER_LIBDIR}")
endif()

# ------------------------------------------------------------------------------
# Internal Paths for cmake libraries and Setup install and output Directories
# ------------------------------------------------------------------------------
# This sets where to look for dependent libraries
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_BINARY_DIR} ${CMAKE_INSTALL_PREFIX})

# Add ROCM_PATH to CMAKE_PREFIX_PATH so find_package(rocprofiler-sdk) works
# when a rocm module is loaded (sets ROCM_PATH) but not CMAKE_PREFIX_PATH.
if(DEFINED ENV{ROCM_PATH} AND NOT "$ENV{ROCM_PATH}" STREQUAL "")
        list(PREPEND CMAKE_PREFIX_PATH "$ENV{ROCM_PATH}")
endif()

# This sets where to look for dependent library's cmake files
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR}/share/cmake)

# ------------------------------------------------------------------------------
if(NOT DFTRACER_EXTERNALLY_CONFIGURED)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin CACHE PATH "Single Directory for all Executables.")
        set(CMAKE_INCLUDE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/include CACHE PATH "Store the headers.")
        set(EXECUTABLE_OUTPUT_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR} CACHE PATH "Single Directory for all Libraries")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR} CACHE PATH "Single Directory for all static libraries.")
endif()

include_directories(${CMAKE_BINARY_DIR}/include)
link_directories(${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# -----------------------------------------------------------------------------
# Targets built within this project are exported at Install time for use
# by other projects.
# -----------------------------------------------------------------------------
if(NOT DFTRACER_EXPORTED_TARGETS)
        set(DFTRACER_EXPORTED_TARGETS "dftracer-targets")
endif()

include(dftracer-utils)

# ------------------------------------------------------------------------------
# Build options
# ------------------------------------------------------------------------------
# option(ENABLE_DFTRACER_DEBUG "Include debugging prints and logging" OFF) # This is not needed as we have CMAKE_BUILD_TYPE
# This is verbose, maybe an alternate might help simplify
# option(BUILD_URPC "Build DFTRACER's URPC code" OFF)
# option(ENABLE_PERFFLOW "Build with PerfFlow Aspect support" OFF)
# option(ENABLE_UCX_DTL "Build DFTRACER's UCX data transport layer" OFF)

# This option is off by default because non-developers should not use
# this option under normal circumstances.
option(DFTRACER_WARNINGS_AS_ERRORS "Build with warnings promoted to errors." OFF)
mark_as_advanced(DFTRACER_WARNINGS_AS_ERRORS)

option(DFTRACER_USE_CLANG_LIBCXX OFF)

if(DFTRACER_USE_CLANG_LIBCXX)
        set(GCC_TOOLCHAIN "")
endif(DFTRACER_USE_CLANG_LIBCXX)

# Options
option(DFTRACER_ENABLE_FTRACING "Enable Function Tracing" OFF)

if(DFTRACER_ENABLE_FTRACING)
        set(DFTRACER_FTRACING_ENABLE 1)
        set(DFTRACER_FUNCTION_FLAGS "-g" "-finstrument-functions" "-Wl,-E" "-fvisibility=default")
else()
        set(DFTRACER_FUNCTION_FLAGS)
endif()

option(DFTRACER_BUILD_PYTHON_BINDINGS "Build python bindings." ON)
set(DFTRACER_PYTHON_EXE "python3" CACHE STRING "Python executable to use for building.")
set(DFTRACER_PYTHON_SITE $ENV{DFTRACER_PYTHON_SITE} CACHE STRING "Python site packages to use for building.")

if(NOT DFTRACER_PYTHON_EXEC STREQUAL "OFF")
        set(PYTHON_EXECUTABLE ${DFTRACER_PYTHON_EXEC})
endif()

if(DFTRACER_BUILD_PYTHON_BINDINGS)
        if(DFTRACER_PYTHON_SITE)
                set(PYTHON_SITE_PACKAGES "${DFTRACER_PYTHON_SITE}")
                set(MAIN_PYTHON_SITE_PACKAGES ${PYTHON_SITE_PACKAGES})
        else()
                set(PYTHON_SITE_PACKAGES ${CMAKE_INSTALL_PREFIX})
                execute_process(COMMAND ${DFTRACER_PYTHON_EXE} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())" OUTPUT_VARIABLE MAIN_PYTHON_SITE_PACKAGES OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()

        # If using a virtual environment, also expose runtime binaries in venv/bin.
        get_filename_component(_dftracer_py_ver_dir "${MAIN_PYTHON_SITE_PACKAGES}" DIRECTORY)
        get_filename_component(_dftracer_py_lib_dir "${_dftracer_py_ver_dir}" DIRECTORY)
        get_filename_component(_dftracer_py_prefix_dir "${_dftracer_py_lib_dir}" DIRECTORY)
        if(EXISTS "${_dftracer_py_prefix_dir}/pyvenv.cfg")
                set(DFTRACER_VENV_BIN_DIR "${_dftracer_py_prefix_dir}/bin")
        else()
                set(DFTRACER_VENV_BIN_DIR "")
        endif()

        # Discover dependency package configs from the python prefix without
        # globally injecting its header tree into every target include path.
        list(APPEND CMAKE_PREFIX_PATH "${PYTHON_SITE_PACKAGES}/dftracer")
        link_directories(${PYTHON_SITE_PACKAGES}/dftracer/lib)
endif()

# Check optional dependencies
option(DFTRACER_DISABLE_HWLOC "Disable HWLOC" ON)

if(NOT DFTRACER_DISABLE_HWLOC)
        set(DFTRACER_HWLOC_ENABLE 1)
endif()

option(DFTRACER_ENABLE_HIP_TRACING "Enable HIP Tracing" OFF)

# rocprofiler-sdk version defaults (used by generated config header)
set(DFTRACER_ROCPROFILER_VERSION_MAJOR 0)
set(DFTRACER_ROCPROFILER_VERSION_MINOR 0)
set(DFTRACER_ROCPROFILER_VERSION_PATCH 0)

if(DFTRACER_ENABLE_HIP_TRACING)
        set(DFTRACER_HIP_TRACING_ENABLE 1)
endif()

option(DFTRACER_ENABLE_MPI "Enable MPI" OFF)

if(DFTRACER_ENABLE_MPI)
        set(DFTRACER_MPI_ENABLE 1)
endif()

set(DFTRACER_MPI_IMPL "" CACHE STRING
    "Override detected MPI implementation (OPENMPI, MPICH, CRAYMPICH, MVAPICH, INTELMPI). Empty = auto-detect via C probe.")
set_property(CACHE DFTRACER_MPI_IMPL PROPERTY STRINGS "" OPENMPI MPICH CRAYMPICH MVAPICH INTELMPI UNKNOWN)

option(DFTRACER_ENABLE_HDF5 "Enable HDF5" OFF)

if(DFTRACER_ENABLE_HDF5)
        set(DFTRACER_HDF5_ENABLE 1)
endif()

option(DFTRACER_ENABLE_DYNAMIC_DETECTION "Enable Dynamic Detection" OFF)
option(DFTRACER_GENERATE_INTERFACES "Generate Brahma and DFTracer interfaces from discovered headers" OFF)

function(dftracer_set_rocprofiler_version_from_string version_string)
        if("${version_string}" MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
                set(DFTRACER_ROCPROFILER_VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
                set(DFTRACER_ROCPROFILER_VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
                set(DFTRACER_ROCPROFILER_VERSION_PATCH "${CMAKE_MATCH_3}" PARENT_SCOPE)
                set(DFTRACER_ROCPROFILER_VERSION_FOUND TRUE PARENT_SCOPE)
        endif()
endfunction()

function(dftracer_set_rocprofiler_version_from_header include_dir)
        if(NOT include_dir)
                return()
        endif()

        set(_dftracer_rocprofiler_version_header "${include_dir}/rocprofiler-sdk/version.h")
        if(NOT EXISTS "${_dftracer_rocprofiler_version_header}")
                return()
        endif()

        file(READ "${_dftracer_rocprofiler_version_header}" _dftracer_rocprofiler_version_header_text)
        string(REGEX MATCH "#define[ \t]+ROCPROFILER_VERSION_MAJOR[ \t]+([0-9]+)" _dftracer_rocprofiler_major_match "${_dftracer_rocprofiler_version_header_text}")
        set(_dftracer_rocprofiler_major "${CMAKE_MATCH_1}")
        string(REGEX MATCH "#define[ \t]+ROCPROFILER_VERSION_MINOR[ \t]+([0-9]+)" _dftracer_rocprofiler_minor_match "${_dftracer_rocprofiler_version_header_text}")
        set(_dftracer_rocprofiler_minor "${CMAKE_MATCH_1}")
        string(REGEX MATCH "#define[ \t]+ROCPROFILER_VERSION_PATCH[ \t]+([0-9]+)" _dftracer_rocprofiler_patch_match "${_dftracer_rocprofiler_version_header_text}")
        set(_dftracer_rocprofiler_patch "${CMAKE_MATCH_1}")

        if(_dftracer_rocprofiler_major MATCHES "^[0-9]+$" AND
           _dftracer_rocprofiler_minor MATCHES "^[0-9]+$" AND
           _dftracer_rocprofiler_patch MATCHES "^[0-9]+$")
                set(DFTRACER_ROCPROFILER_VERSION_MAJOR "${_dftracer_rocprofiler_major}" PARENT_SCOPE)
                set(DFTRACER_ROCPROFILER_VERSION_MINOR "${_dftracer_rocprofiler_minor}" PARENT_SCOPE)
                set(DFTRACER_ROCPROFILER_VERSION_PATCH "${_dftracer_rocprofiler_patch}" PARENT_SCOPE)
                set(DFTRACER_ROCPROFILER_VERSION_FOUND TRUE PARENT_SCOPE)
        endif()
endfunction()

# install dependency
option(DFTRACER_INSTALL_DEPENDENCIES "Install DFTracer dependencies" OFF)

# testing
option(DFTRACER_ENABLE_TESTS "Enable tests for DFTRACER." OFF)
option(DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS "Enable dlio_benchmark tests" OFF)
option(DFTRACER_ENABLE_PAPER_TESTS "Enable paper tests" OFF)
set(DFTRACER_TEST_LD_LIBRARY_PATH "" CACHE STRING "Additional LD_LIBRARY_PATH to be included on testing")

# Extra MPI launcher flags used by CTest MPI tests (e.g. "-p pdebug" on Flux).
set(_dftracer_default_mpi_test_launcher_flags "")

if(DEFINED ENV{DFTRACER_TEST_MACHINE} AND "$ENV{DFTRACER_TEST_MACHINE}" STREQUAL "corona")
        set(_dftracer_default_mpi_test_launcher_flags "-p pdebug")
endif()

set(DFTRACER_MPI_TEST_LAUNCHER_FLAGS "${_dftracer_default_mpi_test_launcher_flags}" CACHE STRING
        "Additional launcher flags passed to MPIEXEC for DFTracer MPI tests (example: -p pdebug)")

option(DFTRACER_ENABLE_NATIVE_SCRIPT "Build with native scripting support." OFF)

# ------------------------------------------------------------------------------
# Compiler setup
# ------------------------------------------------------------------------------
include(ExternalProject)

# It requires include(GNUInstallDirs) before if that is needed
include(SetupCompiler)

# ------------------------------------------------------------------------------
# Build Type based configurations
# ------------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "")
        set(CMAKE_BUILD_TYPE "Release") # Setting default as Release
endif()

if(NOT DEFINED BUILD_SHARED_LIBS)
        set(BUILD_SHARED_LIBS ON)
endif()

# ------------------------------------------------------------------------------
# Disallow in-source build
# ------------------------------------------------------------------------------
if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
        message(FATAL_ERROR
                "${PROJECT_NAME} requires an out of source Build. "
                "Please create a separate binary directory and run CMake there.")
endif()

# ------------------------------------------------------------------------------
# Set a default build type if none was specified
# ------------------------------------------------------------------------------
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)

        # Set the possible values of build type for cmake-gui
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
                "MinSizeRel" "RelWithDebInfo")
endif()

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
if(DFTRACER_INSTALL_DEPENDENCIES)
        message(STATUS "[${PROJECT_NAME}] downloading dependencies. Please run make for downloading depedencies and then do reconfigure without dependency flag.")
        message(STATUS "[${PROJECT_NAME}] Installing depedencies at ${CMAKE_INSTALL_PREFIX}")
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependency)
else()
        # Required Dependencies
        # =============================================================================
        set(DEPENDENCY_LIB ${CMAKE_EXE_LINKER_FLAGS} $ENV{LDFLAGS} -lpthread)
        set(DEPENDENCY_LIBRARY_DIRS "")

        find_package(cpp-logger 2.0.0 REQUIRED)

        if(${cpp-logger_FOUND})
                include_directories(${CPP_LOGGER_INCLUDE_DIRS})
                set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${CPP_LOGGER_LIBRARIES})
                set(CPP_LOGGER_LIBRARY_DIR ${cpp-logger_DIR}/../../)

                # Get absolute path for CPP_LOGGER_LIBRARY_DIR
                get_filename_component(CPP_LOGGER_LIBRARY_DIR "${CPP_LOGGER_LIBRARY_DIR}" ABSOLUTE)

                if(DEFINED CPP_LOGGER_LIBRARY_DIR)
                        list(APPEND DEPENDENCY_LIBRARY_DIRS ${CPP_LOGGER_LIBRARY_DIR})
                endif()

                message(STATUS "[CPP_LOGGER] found cpp-logger at ${CPP_LOGGER_INCLUDE_DIRS}")
        else()
                message(FATAL_ERROR "-- [CPP_LOGGER] is needed for ${PROJECT_NAME} build")
        endif()

        # Brahma can export an interface dependency on HDF5::HDF5
        # regardless of DFTRACER_ENABLE_HDF5 based on how Brahma was built.
        # Preload HDF5 targets before loading brahma package config.
        if(DFTRACER_ENABLE_HDF5)
                find_package(HDF5 REQUIRED COMPONENTS C)
        else()
                find_package(HDF5 QUIET COMPONENTS C)
        endif()

        if(HDF5_FOUND)
                # Collect HDF5 library directories for RPATH and LD_LIBRARY_PATH.
                # FindHDF5 does not always set HDF5_LIBRARY_DIRS, so derive the
                # dirs from the actual library file paths as a fallback.
                set(_hdf5_lib_dirs "")
                foreach(_hdf5_lib IN LISTS HDF5_C_LIBRARIES HDF5_LIBRARIES)
                        if(EXISTS "${_hdf5_lib}" AND NOT IS_DIRECTORY "${_hdf5_lib}")
                                get_filename_component(_hdf5_lib_dir "${_hdf5_lib}" DIRECTORY)
                                list(APPEND _hdf5_lib_dirs "${_hdf5_lib_dir}")
                        endif()
                endforeach()
                if(DEFINED HDF5_LIBRARY_DIRS)
                        list(APPEND _hdf5_lib_dirs ${HDF5_LIBRARY_DIRS})
                endif()
                if(_hdf5_lib_dirs)
                        list(REMOVE_DUPLICATES _hdf5_lib_dirs)
                        list(APPEND DEPENDENCY_LIBRARY_DIRS ${_hdf5_lib_dirs})
                        message(STATUS "[DFTRACER] HDF5 library dirs added to RPATH: ${_hdf5_lib_dirs}")
                endif()
        endif()

        # Brahma can export an interface dependency on MPI::MPI_C.
        # Preload MPI targets before loading brahma package config.
        # Policy: DFTRACER_ENABLE_MPI=ON requires MPI for dftracer,
        # but brahma MPI can be disabled separately by dependency logic.
        find_package(MPI QUIET COMPONENTS C)
        if(DFTRACER_ENABLE_MPI AND NOT (MPI_FOUND OR TARGET MPI::MPI_C))
                message(FATAL_ERROR "[${PROJECT_NAME}] DFTRACER_ENABLE_MPI=ON but MPI was not found")
        endif()

        # MPI implementation detection via compiled C probe
        set(DFTRACER_MPI_IMPL_NAME "UNKNOWN")
        if(MPI_FOUND OR TARGET MPI::MPI_C)
                if(DFTRACER_MPI_IMPL)
                        # User override via -DDFTRACER_MPI_IMPL=<impl>
                        set(DFTRACER_MPI_IMPL_NAME "${DFTRACER_MPI_IMPL}")
                        message(STATUS "[${PROJECT_NAME}] MPI implementation overridden: ${DFTRACER_MPI_IMPL_NAME}")
                else()
                        # Auto-detect by compiling and running a small C probe
                        try_run(
                                _dftracer_mpi_run_result
                                _dftracer_mpi_compile_result
                                "${CMAKE_BINARY_DIR}/dftracer_mpi_detect"
                                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/mpi_detect.c"
                                CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${MPI_C_INCLUDE_DIRS}"
                                LINK_LIBRARIES ${MPI_C_LIBRARIES}
                                COMPILE_OUTPUT_VARIABLE _dftracer_mpi_compile_out
                                RUN_OUTPUT_VARIABLE _dftracer_mpi_detect_out
                        )
                        set(DFTRACER_MPI_PACKAGE_VERSION "")
                        if(_dftracer_mpi_compile_result AND
                           NOT "${_dftracer_mpi_run_result}" STREQUAL "FAILED_TO_RUN")
                                if(_dftracer_mpi_detect_out MATCHES "IMPL=([A-Z]+)")
                                        set(DFTRACER_MPI_IMPL_NAME "${CMAKE_MATCH_1}")
                                endif()
                                if(_dftracer_mpi_detect_out MATCHES "VERSION=([0-9]+\\.[0-9]+\\.[0-9]+)")
                                        set(DFTRACER_MPI_PACKAGE_VERSION "${CMAKE_MATCH_1}")
                                endif()
                                message(STATUS "[${PROJECT_NAME}] MPI C probe: impl=${DFTRACER_MPI_IMPL_NAME} version=${DFTRACER_MPI_PACKAGE_VERSION}")
                        else()
                                message(WARNING "[${PROJECT_NAME}] MPI C probe did not run (compile=${_dftracer_mpi_compile_result} run=${_dftracer_mpi_run_result}); impl detection skipped")
                        endif()
                endif()
        endif()

        find_package(brahma 4.0.1 REQUIRED)

        if(${brahma_FOUND})
                message(STATUS "[BRAHMA] found brahma at ${BRAHMA_INCLUDE_DIRS}")
                include_directories(${BRAHMA_INCLUDE_DIRS})
                set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${BRAHMA_LIBRARIES})
                set(BRAHMA_LIBRARY_DIR ${brahma_DIR}/../../)

                # Get absolute path for BRAHMA_LIBRARY_DIR
                get_filename_component(BRAHMA_LIBRARY_DIR "${BRAHMA_LIBRARY_DIR}" ABSOLUTE)

                if(DEFINED BRAHMA_LIBRARY_DIR)
                        list(APPEND DEPENDENCY_LIBRARY_DIRS ${BRAHMA_LIBRARY_DIR})
                endif()
        else()
                message(FATAL_ERROR "-- [BRAHMA] is needed for ${PROJECT_NAME} build")
        endif()

        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools)

        find_package(yaml-cpp REQUIRED)

        if(${yaml-cpp_FOUND})
                message(STATUS "[DFTRACER] found yaml-cpp at ${YAML_CPP_INCLUDE_DIR}")
                include_directories(${YAML_CPP_INCLUDE_DIR})
                set(YAML_CPP_LIBRARY_DIR ${yaml-cpp_DIR}/../../)

                # Get absolute path for YAML_CPP_LIBRARY_DIR
                get_filename_component(YAML_CPP_LIBRARY_DIR "${YAML_CPP_LIBRARY_DIR}" ABSOLUTE)

                if(DEFINED YAML_CPP_LIBRARY_DIR)
                        list(APPEND DEPENDENCY_LIBRARY_DIRS ${YAML_CPP_LIBRARY_DIR})
                endif()

                set(DEPENDENCY_LIB ${DEPENDENCY_LIB} -L${YAML_CPP_LIBRARY_DIR} ${YAML_CPP_LIBRARIES})
                list(APPEND DEPENDENCY_LIBRARY_DIRS ${YAML_CPP_LIBRARY_DIR})
        else()
                message(FATAL_ERROR "-- [DFTRACER] yaml-cpp is needed for ${PROJECT_NAME} build")
        endif()

        set(DFTRACER_LIBUV_LINK_LIBS "")
        set(DFTRACER_LIBUV_LIBRARY_DIRS "")
        find_package(libuv 1.0.0 QUIET)
        if(libuv_FOUND)
                if(TARGET libuv::libuv)
                        list(APPEND DFTRACER_LIBUV_LINK_LIBS libuv::libuv)
                endif()
                if(DEFINED LIBUV_INCLUDE_DIRS)
                        include_directories(${LIBUV_INCLUDE_DIRS})
                endif()
                if(DEFINED libuv_INCLUDE_DIRS)
                        include_directories(${libuv_INCLUDE_DIRS})
                endif()
                if(DEFINED LIBUV_LIBRARIES)
                        list(APPEND DFTRACER_LIBUV_LINK_LIBS ${LIBUV_LIBRARIES})
                endif()
                if(DEFINED libuv_LIBRARIES)
                        list(APPEND DFTRACER_LIBUV_LINK_LIBS ${libuv_LIBRARIES})
                endif()
                if(DEFINED LIBUV_LIBRARY_DIRS)
                        list(APPEND DFTRACER_LIBUV_LIBRARY_DIRS ${LIBUV_LIBRARY_DIRS})
                endif()
                if(DEFINED libuv_LIBRARY_DIRS)
                        list(APPEND DFTRACER_LIBUV_LIBRARY_DIRS ${libuv_LIBRARY_DIRS})
                endif()
                message(STATUS "[DFTRACER] found libuv")
        else()
                message(STATUS "[DFTRACER] libuv package config not found, will try manual discovery")
        endif()

        # Fallback: if find_package succeeded but exported no usable target/variable,
        # locate the library and header manually (handles libuv installs that only
        # ship a pkg-config or bare library without a CMake config target).
        if(NOT DFTRACER_LIBUV_LINK_LIBS)
                find_path(DFTRACER_LIBUV_INCLUDE_DIR uv.h
                        HINTS ${CMAKE_INSTALL_PREFIX}/include)
                find_library(DFTRACER_LIBUV_LIBRARY NAMES uv libuv
                        HINTS ${CMAKE_INSTALL_PREFIX}/lib ${CMAKE_INSTALL_PREFIX}/lib64)
                if(DFTRACER_LIBUV_INCLUDE_DIR)
                        include_directories(${DFTRACER_LIBUV_INCLUDE_DIR})
                endif()
                if(DFTRACER_LIBUV_LIBRARY)
                        list(APPEND DFTRACER_LIBUV_LINK_LIBS ${DFTRACER_LIBUV_LIBRARY})
                        get_filename_component(_uv_lib_dir "${DFTRACER_LIBUV_LIBRARY}" DIRECTORY)
                        if(_uv_lib_dir)
                                list(APPEND DFTRACER_LIBUV_LIBRARY_DIRS ${_uv_lib_dir})
                        endif()
                endif()
        endif()

        if(NOT DFTRACER_LIBUV_LINK_LIBS)
                message(FATAL_ERROR "-- [DFTRACER] libuv is needed for ${PROJECT_NAME}_service build")
        endif()

        foreach(_lib ${DFTRACER_LIBUV_LINK_LIBS})
                if(IS_ABSOLUTE "${_lib}")
                        get_filename_component(_lib_dir "${_lib}" DIRECTORY)
                        if(_lib_dir)
                                list(APPEND DFTRACER_LIBUV_LIBRARY_DIRS ${_lib_dir})
                        endif()
                endif()
        endforeach()

        set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${DFTRACER_LIBUV_LINK_LIBS})
        if(DFTRACER_LIBUV_LIBRARY_DIRS)
                list(APPEND DEPENDENCY_LIBRARY_DIRS ${DFTRACER_LIBUV_LIBRARY_DIRS})
        endif()

        # Optional Dependencies
        # =============================================================================
        if(DFTRACER_BUILD_PYTHON_BINDINGS)
                find_package(pybind11 REQUIRED)

                # add_subdirectory(dependency/py11bind)
        endif()

        find_package(MPI COMPONENTS CXX QUIET)

        if(MPI_CXX_FOUND AND(DFTRACER_ENABLE_MPI OR DFTRACER_ENABLE_DYNAMIC_DETECTION))
                message(STATUS "[${PROJECT_NAME}] MPI found at ${MPI_CXX_INCLUDE_DIRS}.")
                message(STATUS "[${PROJECT_NAME}] MPI implementation: ${DFTRACER_MPI_IMPL_NAME}")
                set(DFTRACER_MPI_ENABLE 1)
                include_directories(${MPI_CXX_INCLUDE_DIRS})
                set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${MPI_CXX_LIBRARIES})

                # Store impl compile definition; applied after targets are created below
                if(NOT DFTRACER_MPI_IMPL_NAME STREQUAL "UNKNOWN")
                        set(DFTRACER_MPI_IMPL_COMPILE_DEF "DFTRACER_MPI_IMPL_${DFTRACER_MPI_IMPL_NAME}")
                endif()

                # Get the directory of each MPI_CXX_LIBRARY and append to MPI_CXX_LIBRARY_DIRS
                set(MPI_CXX_LIBRARY_DIRS "")

                foreach(_lib ${MPI_CXX_LIBRARIES})
                        get_filename_component(_lib_dir "${_lib}" DIRECTORY)

                        if(_lib_dir)
                                list(APPEND MPI_CXX_LIBRARY_DIRS "${_lib_dir}")
                        endif()
                endforeach()

                list(REMOVE_DUPLICATES MPI_CXX_LIBRARY_DIRS)

                if(DEFINED MPI_CXX_LIBRARY_DIRS)
                        list(APPEND DEPENDENCY_LIBRARY_DIRS ${MPI_CXX_LIBRARY_DIRS})
                endif()
        else()
                if(DFTRACER_ENABLE_MPI)
                        message(FATAL_ERROR "[${PROJECT_NAME}] DFTRACER_ENABLE_MPI=ON but MPI CXX was not found")
                endif()
                message(STATUS "[${PROJECT_NAME}] MPI not found.")
                set(DFTRACER_MPI_ENABLE 0)
        endif()

        find_package(HWLOC QUIET)

        if(HWLOC_FOUND AND(NOT DFTRACER_DISABLE_HWLOC OR DFTRACER_ENABLE_DYNAMIC_DETECTION))
                set(DFTRACER_HWLOC_ENABLE 1)
                include_directories(${HWLOC_INCLUDE_DIRS})
                set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${HWLOC_LIBRARIES})

                if(DEFINED HWLOC_LIBDIR)
                        list(APPEND DEPENDENCY_LIBRARY_DIRS ${HWLOC_LIBDIR})
                endif()

                message(STATUS "[DFTRACER] found hwloc at ${HWLOC_INCLUDE_DIRS}")
        else()
                message(STATUS "[DFTRACER] hwloc not found")
                set(DFTRACER_HWLOC_ENABLE 0)
        endif()

        find_package(rocprofiler-sdk QUIET)

        # rocprofiler-sdk exists starting from ROCm 6.2.0.  For older ROCm
        # (5.x – 6.1.x) the package simply won't be found; that's expected.
        if(rocprofiler-sdk_FOUND AND(DFTRACER_ENABLE_HIP_TRACING OR DFTRACER_ENABLE_DYNAMIC_DETECTION))
                set(DFTRACER_HIP_TRACING_ENABLE 1)
                message(STATUS "[DFTRACER] found rocprofiler-sdk at ${rocprofiler-sdk_INCLUDE_DIR}")
                set(DFTRACER_ROCPROFILER_VERSION_FOUND FALSE)

                # 1) Try the version string exposed by find_package (populated from
                #    the config-version file; works for all ROCm >= 6.2).
                dftracer_set_rocprofiler_version_from_string("${rocprofiler-sdk_VERSION}")

                # 2) Fall back to parsing the installed version.h header directly.
                if(NOT DFTRACER_ROCPROFILER_VERSION_FOUND)
                        if(DEFINED rocprofiler-sdk_INCLUDE_DIR AND NOT "${rocprofiler-sdk_INCLUDE_DIR}" STREQUAL "")
                                dftracer_set_rocprofiler_version_from_header("${rocprofiler-sdk_INCLUDE_DIR}")
                        endif()
                endif()

                if(NOT DFTRACER_ROCPROFILER_VERSION_FOUND)
                        if(DEFINED rocprofiler-sdk_INCLUDE_DIRS)
                                foreach(_dftracer_rocprofiler_include_dir IN LISTS rocprofiler-sdk_INCLUDE_DIRS)
                                        dftracer_set_rocprofiler_version_from_header("${_dftracer_rocprofiler_include_dir}")
                                        if(DFTRACER_ROCPROFILER_VERSION_FOUND)
                                                break()
                                        endif()
                                endforeach()
                        endif()
                endif()

                # 3) Last resort: query the imported target's include directories.
                if(NOT DFTRACER_ROCPROFILER_VERSION_FOUND AND TARGET rocprofiler-sdk::rocprofiler-sdk)
                        get_target_property(_dftracer_rocprofiler_target_include_dirs rocprofiler-sdk::rocprofiler-sdk INTERFACE_INCLUDE_DIRECTORIES)
                        if(_dftracer_rocprofiler_target_include_dirs)
                                foreach(_dftracer_rocprofiler_include_dir IN LISTS _dftracer_rocprofiler_target_include_dirs)
                                        dftracer_set_rocprofiler_version_from_header("${_dftracer_rocprofiler_include_dir}")
                                        if(DFTRACER_ROCPROFILER_VERSION_FOUND)
                                                break()
                                        endif()
                                endforeach()
                        endif()
                endif()

                # 4) If ROCM_PATH is set, try its include directory as a final fallback
                #    (handles installations where the cmake config omits include-dir vars).
                if(NOT DFTRACER_ROCPROFILER_VERSION_FOUND AND DEFINED ENV{ROCM_PATH}
                   AND NOT "$ENV{ROCM_PATH}" STREQUAL "")
                        dftracer_set_rocprofiler_version_from_header("$ENV{ROCM_PATH}/include")
                endif()

                message(STATUS "[DFTRACER] rocprofiler-sdk version: ${DFTRACER_ROCPROFILER_VERSION_MAJOR}.${DFTRACER_ROCPROFILER_VERSION_MINOR}.${DFTRACER_ROCPROFILER_VERSION_PATCH}")
                set(HIP_TRACING_LIBRARIES rocprofiler-sdk::rocprofiler-sdk)
                set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${HIP_TRACING_LIBRARIES})
        else()
                if(DFTRACER_ENABLE_HIP_TRACING OR DFTRACER_ENABLE_DYNAMIC_DETECTION)
                        # Only warn when the user explicitly requested HIP tracing but
                        # rocprofiler-sdk wasn't found.  ROCm < 6.2 doesn't ship it.
                        if(DEFINED ENV{ROCM_PATH} AND NOT "$ENV{ROCM_PATH}" STREQUAL "")
                                message(WARNING "[DFTRACER] rocprofiler-sdk not found under ROCM_PATH=$ENV{ROCM_PATH}. "
                                        "HIP tracing requires ROCm >= 6.2.0.")
                        else()
                                message(WARNING "[DFTRACER] rocprofiler-sdk not found. "
                                        "Set ROCM_PATH or load a rocm >= 6.2.0 module to enable HIP tracing.")
                        endif()
                else()
                        message(STATUS "-- [DFTRACER] rocprofiler-sdk is not found.")
                endif()
        endif()

        find_package(ZLIB REQUIRED)

        if(ZLIB_FOUND)
                message(STATUS "[DFTRACER] found zlib at ${ZLIB_INCLUDE_DIRS}")
                # Keep the toolchain zlib headers ahead of dependency include roots.
                include_directories(BEFORE ${ZLIB_INCLUDE_DIRS})
                set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${ZLIB_LIBRARIES})
                get_filename_component(ZLIB_LIBRARY "${ZLIB_LIBRARIES}" DIRECTORY)
                set(ZLIB_LIBRARY_DIR "${ZLIB_LIBRARY}")

                # Get absolute path for ZLIB_LIBRARY_DIR
                get_filename_component(ZLIB_LIBRARY_DIR "${ZLIB_LIBRARY_DIR}" ABSOLUTE)

                if(DEFINED ZLIB_LIBRARY_DIR)
                        list(APPEND DEPENDENCY_LIBRARY_DIRS ${ZLIB_LIBRARY_DIR})
                endif()

                if(DEFINED ZLIB_LIBRARY_DIRS)
                        list(APPEND DEPENDENCY_LIBRARY_DIRS ${ZLIB_LIBRARY_DIRS})
                endif()
        else()
                message(FATAL_ERROR "-- [DFTRACER] zlib is needed for ${PROJECT_NAME} build")
        endif()

        # print_all_variables()

        # Put the active runtime library dirs before system dirs such as
        # /usr/lib64. Some platforms emit DT_RPATH, which the loader searches
        # before LD_LIBRARY_PATH.
        set(_dftracer_runtime_library_dirs "")
        if(NOT "${DFTRACER_TEST_LD_LIBRARY_PATH}" STREQUAL "")
                string(REPLACE ":" ";" _dftracer_test_runtime_dirs "${DFTRACER_TEST_LD_LIBRARY_PATH}")
                foreach(_dftracer_test_runtime_dir IN LISTS _dftracer_test_runtime_dirs)
                        if(IS_DIRECTORY "${_dftracer_test_runtime_dir}")
                                list(APPEND _dftracer_runtime_library_dirs "${_dftracer_test_runtime_dir}")
                        endif()
                endforeach()
        endif()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                execute_process(
                        COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++.so
                        OUTPUT_VARIABLE _dftracer_cxx_stdlib_for_rpath
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
                if(EXISTS "${_dftracer_cxx_stdlib_for_rpath}")
                        get_filename_component(_dftracer_cxx_stdlib_dir_for_rpath "${_dftracer_cxx_stdlib_for_rpath}" DIRECTORY)
                        list(APPEND _dftracer_runtime_library_dirs "${_dftracer_cxx_stdlib_dir_for_rpath}")
                endif()
        endif()
        if(_dftracer_runtime_library_dirs)
                list(PREPEND DEPENDENCY_LIBRARY_DIRS ${_dftracer_runtime_library_dirs})
        endif()

        # Set RPATH to all dependency library dirs
        list(REMOVE_DUPLICATES DEPENDENCY_LIBRARY_DIRS)
        set(CMAKE_INSTALL_RPATH "${DEPENDENCY_LIBRARY_DIRS}")
        set(CMAKE_BUILD_RPATH "${DEPENDENCY_LIBRARY_DIRS}")
        set(CMAKE_SKIP_BUILD_RPATH OFF)
        set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

        include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
        include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
        set(DFTRACER_PRELOAD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/dftracer_preload.cpp)
        set(DFTRACER_PRELOAD_PRIVATE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/dftracer_preload.h)

        set(DFTRACER_CORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/posix.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/stdio.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/finstrument/functions.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/function/hip/intercept.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/posix_bypass.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/stdio_bypass.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/dftracer.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/common/dftracer_main.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/configuration_manager.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/md5.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/df_logger.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/utils.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/serialization/json_line.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/compression/zlib_compression.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/buffer/buffer.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/writer/stdio_writer.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/aggregator/aggregator.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/common/datastructure.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/aggregator/rules.cpp)

        # Add MPI/MPIIO sources if MPI is enabled
        if(DFTRACER_MPI_ENABLE)
                list(APPEND DFTRACER_CORE_SRC
                        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/mpi.cpp
                        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/mpiio.cpp)
        endif()

        # Add HDF5 sources if HDF5 is enabled
        if(DFTRACER_HDF5_ENABLE)
                list(APPEND DFTRACER_CORE_SRC
                        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/hdf5.cpp)
        endif()

        set(DFTRACER_CORE_PUBLIC_INCLUDE
                ${CMAKE_CURRENT_SOURCE_DIR}/include/dftracer/dftracer.h
                ${CMAKE_CURRENT_SOURCE_DIR}/include/dftracer/core/common/constants.h
                ${CMAKE_CURRENT_SOURCE_DIR}/include/dftracer/core/common/typedef.h
                ${CMAKE_CURRENT_SOURCE_DIR}/include/dftracer/core/common/cpp_typedefs.h
                ${CMAKE_CURRENT_SOURCE_DIR}/include/dftracer/core/common/enumeration.h)
        set(DFTRACER_CORE_PRIVATE_INCLUDE
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/posix_bypass.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/stdio_bypass.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/utils.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/md5.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/finstrument/functions.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/posix.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/stdio.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/common/dftracer_main.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/common/logging.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/function/generic_function.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/function/hip/intercept.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/common/singleton.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/utils/configuration_manager.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/aggregator/aggregator.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/common/datastructure.h)

        # Add MPI/MPIIO/HDF5 private headers if enabled
        if(DFTRACER_MPI_ENABLE)
                list(APPEND DFTRACER_CORE_PRIVATE_INCLUDE
                        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/mpi.h
                        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/mpiio.h)
        endif()

        if(DFTRACER_HDF5_ENABLE)
                list(APPEND DFTRACER_CORE_PRIVATE_INCLUDE
                        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/brahma/hdf5.h)
        endif()

        add_library(${PROJECT_NAME}_core SHARED)
        add_library(dftracer::dftracer_core ALIAS ${PROJECT_NAME}_core)
        set_target_properties(${PROJECT_NAME}_core PROPERTIES LINKER_LANGUAGE CXX)
        target_compile_definitions(${PROJECT_NAME}_core PUBLIC DFTRACER_LOGGER_LEVEL_ERROR)
        if(DEFINED DFTRACER_MPI_IMPL_COMPILE_DEF)
                target_compile_definitions(${PROJECT_NAME}_core PUBLIC ${DFTRACER_MPI_IMPL_COMPILE_DEF})
        endif()
        if(DFTRACER_HDF5_ENABLE)
                # HDF5 1.12+ defines H5Oopen_async, H5Dopen_async, etc. as variadic macros
                # (guarded by #ifndef H5X_MODULE) that prepend __FILE__/__func__/__LINE__.
                # These conflict with brahma's virtual function declarations of the same names.
                # Defining the module guards suppresses the macro redefinitions so brahma's
                # installed header compiles correctly when included by dftracer's sources.
                foreach(_hdf5_mod A D F G L M O R T)
                        target_compile_definitions(${PROJECT_NAME}_core PUBLIC H5${_hdf5_mod}_MODULE)
                endforeach()
        endif()
        target_link_libraries(${PROJECT_NAME}_core ${DEPENDENCY_LIB})
        add_library(${PROJECT_NAME}_core_dbg SHARED)
        add_library(dftracer::dftracer_core_dbg ALIAS ${PROJECT_NAME}_core_dbg)
        set_target_properties(${PROJECT_NAME}_core_dbg PROPERTIES LINKER_LANGUAGE CXX)
        if(DEFINED DFTRACER_MPI_IMPL_COMPILE_DEF)
                target_compile_definitions(${PROJECT_NAME}_core_dbg PUBLIC ${DFTRACER_MPI_IMPL_COMPILE_DEF})
        endif()
        if(DFTRACER_HDF5_ENABLE)
                foreach(_hdf5_mod A D F G L M O R T)
                        target_compile_definitions(${PROJECT_NAME}_core_dbg PUBLIC H5${_hdf5_mod}_MODULE)
                endforeach()
        endif()
        target_link_libraries(${PROJECT_NAME}_core_dbg ${DEPENDENCY_LIB})
        add_library(${PROJECT_NAME}_preload SHARED)
        add_library(dftracer::dftracer_preload ALIAS ${PROJECT_NAME}_preload)
        target_link_libraries(${PROJECT_NAME}_preload ${DEPENDENCY_LIB} ${PROJECT_NAME}_core)
        add_library(${PROJECT_NAME}_preload_dbg SHARED)
        add_library(dftracer::dftracer_preload_dbg ALIAS ${PROJECT_NAME}_preload_dbg)
        target_link_libraries(${PROJECT_NAME}_preload_dbg ${DEPENDENCY_LIB} ${PROJECT_NAME}_core_dbg)

        if(DFTRACER_GENERATE_INTERFACES AND TARGET ${PROJECT_NAME}_generate_interfaces)
                # Ensure brahma's interface headers are regenerated by its
                # ExternalProject before our generator reads them; otherwise
                # the regen runs against the pre-shipped headers and the
                # produced overrides won't match the freshly generated ones.
                if(TARGET brahma)
                        add_dependencies(${PROJECT_NAME}_generate_interfaces brahma)
                endif()
                add_dependencies(${PROJECT_NAME}_core ${PROJECT_NAME}_generate_interfaces)
                add_dependencies(${PROJECT_NAME}_core_dbg ${PROJECT_NAME}_generate_interfaces)
        endif()

        set(DFTRACER_DEBUG_TARGETS ${PROJECT_NAME}_core_dbg ${PROJECT_NAME}_preload_dbg)

        set(DFTRACER_SERVICE_TELEMETRY_SRC
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/service/telemetry/cpu_collector.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/service/telemetry/memory_collector.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/service/telemetry/io_collector.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/service/telemetry/network_collector.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/service/telemetry/telemetry_factory.cpp)

        add_library(${PROJECT_NAME}_service_telemetry STATIC ${DFTRACER_SERVICE_TELEMETRY_SRC})
        target_include_directories(${PROJECT_NAME}_service_telemetry
                PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/src
                ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(${PROJECT_NAME}_service_telemetry ${DEPENDENCY_LIB} ${PROJECT_NAME}_core)

        set(DFTRACER_SERVICE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/service/service.cpp)

        add_executable(${PROJECT_NAME}_service ${DFTRACER_SERVICE_SRC})
        add_executable(dftracer::dftracer_service ALIAS ${PROJECT_NAME}_service)
        set_target_properties(${PROJECT_NAME}_service PROPERTIES LINKER_LANGUAGE CXX)
        target_compile_definitions(${PROJECT_NAME}_service PUBLIC DFTRACER_LOGGER_LEVEL_ERROR)
        target_link_libraries(${PROJECT_NAME}_service ${DEPENDENCY_LIB} ${PROJECT_NAME}_core ${PROJECT_NAME}_service_telemetry)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                # Ensure explicit C++ runtime linkage from the active compiler
                # toolchain in mixed MPI/library environments.
                execute_process(
                        COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++.so
                        OUTPUT_VARIABLE DFTRACER_CXX_STDLIB
                        OUTPUT_STRIP_TRAILING_WHITESPACE)

                if(EXISTS "${DFTRACER_CXX_STDLIB}")
                        target_link_libraries(${PROJECT_NAME}_core "${DFTRACER_CXX_STDLIB}")
                        target_link_libraries(${PROJECT_NAME}_core_dbg "${DFTRACER_CXX_STDLIB}")
                        target_link_libraries(${PROJECT_NAME}_service "${DFTRACER_CXX_STDLIB}")
                        message(STATUS "[DFTRACER] using C++ stdlib from ${DFTRACER_CXX_STDLIB}")
                else()
                        target_link_libraries(${PROJECT_NAME}_core stdc++)
                        target_link_libraries(${PROJECT_NAME}_core_dbg stdc++)
                        target_link_libraries(${PROJECT_NAME}_service stdc++)
                endif()
        endif()

        if(DFTRACER_BUILD_PYTHON_BINDINGS)
                pybind11_add_module(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/python/dftracer.cpp)
                target_compile_definitions(${PROJECT_NAME} PRIVATE VERSION_INFO=${EXAMPLE_VERSION_INFO})
                target_link_libraries(${PROJECT_NAME} PRIVATE ${DEPENDENCY_LIB} ${PROJECT_NAME}_core)

                pybind11_add_module(${PROJECT_NAME}_dbg NO_EXTRAS ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/python/dftracer_dbg.cpp)
                target_compile_definitions(${PROJECT_NAME}_dbg PRIVATE VERSION_INFO=${EXAMPLE_VERSION_INFO})
                target_link_libraries(${PROJECT_NAME}_dbg PRIVATE ${DEPENDENCY_LIB} ${PROJECT_NAME}_core_dbg)
                list(APPEND DFTRACER_DEBUG_TARGETS ${PROJECT_NAME}_dbg)
        endif()

        target_sources(${PROJECT_NAME}_core
                PRIVATE
                ${DFTRACER_CORE_SRC}
        )
        target_sources(${PROJECT_NAME}_core_dbg
                PRIVATE
                ${DFTRACER_CORE_SRC}
        )
        target_sources(${PROJECT_NAME}_preload
                PRIVATE
                ${DFTRACER_PRELOAD_SRC}
        )
        target_sources(${PROJECT_NAME}_preload_dbg
                PRIVATE
                ${DFTRACER_PRELOAD_SRC}
        )
        target_include_directories(${PROJECT_NAME}_core
                PRIVATE

                # where the library itself will look for its internal headers
                ${CMAKE_CURRENT_SOURCE_DIR}/src
                PUBLIC

                # where top-level project will look for the library's public headers
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>

                # where external projects will look for the library's public headers
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )
        target_include_directories(${PROJECT_NAME}_core_dbg
                PRIVATE

                # where the library itself will look for its internal headers
                ${CMAKE_CURRENT_SOURCE_DIR}/src
                PUBLIC

                # where top-level project will look for the library's public headers
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>

                # where external projects will look for the library's public headers
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )
        set_target_properties(${PROJECT_NAME}_core
                PROPERTIES
                DEBUG_OUTPUT_NAME ${PROJECT_NAME}_core
                RELEASE_OUTPUT_NAME ${PROJECT_NAME}_core
                MINSIZEREL_OUTPUT_NAME ${PROJECT_NAME}_core
                RELWITHDEBINFO_OUTPUT_NAME ${PROJECT_NAME}_core
                VERSION ${DFTRACER_LIBRARY_VERSION}
                SOVERSION ${DFTRACER_LIBRARY_VERSION}
        )

        dftracer_debug_config("${DFTRACER_DEBUG_TARGETS}")
        dftracer_install_headers("${DFTRACER_CORE_PUBLIC_INCLUDE}")
        install(
                TARGETS ${PROJECT_NAME}_core
                EXPORT ${DFTRACER_EXPORTED_TARGETS}
                LIBRARY DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                ARCHIVE DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                RUNTIME DESTINATION ${DFTRACER_INSTALL_BIN_DIR}
        )
        install(
                TARGETS ${PROJECT_NAME}_core_dbg
                EXPORT ${DFTRACER_EXPORTED_TARGETS}
                LIBRARY DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                ARCHIVE DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                RUNTIME DESTINATION ${DFTRACER_INSTALL_BIN_DIR}
        )
        install(
                TARGETS ${PROJECT_NAME}_service
                EXPORT ${DFTRACER_EXPORTED_TARGETS}
                LIBRARY DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                ARCHIVE DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                RUNTIME DESTINATION ${DFTRACER_INSTALL_BIN_DIR}
        )
        if(DFTRACER_BUILD_PYTHON_BINDINGS)
                install(CODE "
                set(_dftracer_python_exe \"${DFTRACER_PYTHON_EXE}\")
                set(_dftracer_service_src \"${CMAKE_INSTALL_PREFIX}/bin/dftracer_service\")

                if(\"\${_dftracer_python_exe}\" STREQUAL \"\")
                        message(FATAL_ERROR \"DFTRACER_PYTHON_EXE must be set to create dftracer_service venv/bin symlink\")
                endif()

                if(NOT EXISTS \"\${_dftracer_python_exe}\")
                        message(FATAL_ERROR \"DFTRACER_PYTHON_EXE does not exist: \${_dftracer_python_exe}\")
                endif()

                get_filename_component(_dftracer_venv_bin \"\${_dftracer_python_exe}\" DIRECTORY)
                if(NOT EXISTS \"\${_dftracer_venv_bin}\")
                        message(FATAL_ERROR \"Python parent directory does not exist: \${_dftracer_venv_bin}\")
                endif()

                if(NOT EXISTS \"\${_dftracer_service_src}\")
                        message(FATAL_ERROR \"dftracer_service source binary not found: \${_dftracer_service_src}\")
                endif()

                file(MAKE_DIRECTORY \"\${_dftracer_venv_bin}\")
                message(STATUS \"Installing: symlink \${_dftracer_venv_bin}/dftracer_service from \${_dftracer_service_src}\")
                execute_process(
                        COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"\${_dftracer_service_src}\" \"\${_dftracer_venv_bin}/dftracer_service\"
                        RESULT_VARIABLE _dftracer_symlink_rc)
                if(NOT _dftracer_symlink_rc EQUAL 0)
                        message(FATAL_ERROR \"Failed creating dftracer_service symlink at \${_dftracer_venv_bin}/dftracer_service\")
                endif()
                ")
        endif()
        install(TARGETS ${PROJECT_NAME}_preload
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
        install(TARGETS ${PROJECT_NAME}_preload_dbg
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

        install(
                TARGETS
                ${PROJECT_NAME}_preload
                EXPORT
                ${DFTRACER_EXPORTED_TARGETS}
                LIBRARY DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                ARCHIVE DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                RUNTIME DESTINATION ${DFTRACER_INSTALL_BIN_DIR}
        )
        install(
                TARGETS
                ${PROJECT_NAME}_preload_dbg
                EXPORT
                ${DFTRACER_EXPORTED_TARGETS}
                LIBRARY DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                ARCHIVE DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
                RUNTIME DESTINATION ${DFTRACER_INSTALL_BIN_DIR}
        )

        if(DFTRACER_BUILD_PYTHON_BINDINGS)
                install(TARGETS ${PROJECT_NAME}
                        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
                file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/symlink.sh CONTENT "mkdir -p ${MAIN_PYTHON_SITE_PACKAGES}/dftracer;echo -- Installing: symlink ${MAIN_PYTHON_SITE_PACKAGES}/dftracer/$<TARGET_FILE_NAME:${PROJECT_NAME}> from ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>;ln -sf ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}> ${MAIN_PYTHON_SITE_PACKAGES}/dftracer/$<TARGET_FILE_NAME:${PROJECT_NAME}>")
                install(CODE "execute_process(
                COMMAND bash -c \"set -e
                chmod +x ${CMAKE_BINARY_DIR}/symlink.sh
                . ${CMAKE_BINARY_DIR}/symlink.sh
                \")")

                install(TARGETS ${PROJECT_NAME}_dbg
                        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
                file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/symlink_dbg.sh CONTENT "mkdir -p ${MAIN_PYTHON_SITE_PACKAGES}/dftracer;echo -- Installing: symlink ${MAIN_PYTHON_SITE_PACKAGES}/dftracer/$<TARGET_FILE_NAME:${PROJECT_NAME}_dbg> from ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}_dbg>;ln -sf ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}_dbg> ${MAIN_PYTHON_SITE_PACKAGES}/dftracer/$<TARGET_FILE_NAME:${PROJECT_NAME}_dbg>")
                install(CODE "execute_process(
                COMMAND bash -c \"set -e
                chmod +x ${CMAKE_BINARY_DIR}/symlink_dbg.sh
                . ${CMAKE_BINARY_DIR}/symlink_dbg.sh
                \")")
        endif()

        if(DFTRACER_ENABLE_NATIVE_SCRIPT)
                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_compact ${EXECUTABLE_OUTPUT_PATH}/dftracer_compact COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_compact
                        DESTINATION
                        bin
                )

                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_compact_by_pid ${EXECUTABLE_OUTPUT_PATH}/dftracer_compact_by_pid COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_compact_by_pid
                        DESTINATION
                        bin
                )

                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_merge ${EXECUTABLE_OUTPUT_PATH}/dftracer_merge COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_merge
                        DESTINATION
                        bin
                )

                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_sanitize ${EXECUTABLE_OUTPUT_PATH}/dftracer_sanitize COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_sanitize
                        DESTINATION
                        bin
                )

                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_anonymize ${EXECUTABLE_OUTPUT_PATH}/dftracer_anonymize COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_anonymize
                        DESTINATION
                        bin
                )

                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_create_index ${EXECUTABLE_OUTPUT_PATH}/dftracer_create_index COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_create_index
                        DESTINATION
                        bin
                )

                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_pgzip ${EXECUTABLE_OUTPUT_PATH}/dftracer_pgzip COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_pgzip
                        DESTINATION
                        bin
                )

                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_split ${EXECUTABLE_OUTPUT_PATH}/dftracer_split COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_split
                        DESTINATION
                        bin
                )

                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_event_count ${EXECUTABLE_OUTPUT_PATH}/dftracer_event_count COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_event_count
                        DESTINATION
                        bin
                )

                configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/dftracer_validate ${EXECUTABLE_OUTPUT_PATH}/dftracer_validate COPYONLY)
                install(
                        FILES
                        ${EXECUTABLE_OUTPUT_PATH}/dftracer_validate
                        DESTINATION
                        bin
                )
        endif()

        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/git_sparse_clone_interactive ${EXECUTABLE_OUTPUT_PATH}/git_sparse_clone_interactive COPYONLY)
        install(
                FILES
                ${EXECUTABLE_OUTPUT_PATH}/git_sparse_clone_interactive
                DESTINATION
                bin
        )

        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/script/git_sparse_push_interactive ${EXECUTABLE_OUTPUT_PATH}/git_sparse_push_interactive COPYONLY)
        install(
                FILES
                ${EXECUTABLE_OUTPUT_PATH}/git_sparse_push_interactive
                DESTINATION
                bin
        )

        # cmake_policy(SET CMP0079 NEW) # In case that we need more control over the target building order
        if(DFTRACER_ENABLE_TESTS)
                message("-- " "[DFTracer] Enabling Testing")

                if(CMAKE_BUILD_TYPE STREQUAL "PROFILE")
                        message("-- " "[DFTracer] building with CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE}")
                        set(CMAKE_C_FLAGS_PROFILE --coverage)
                        set(CMAKE_CXX_FLAGS_PROFILE --coverage)

                        # Find gcovr for coverage reporting
                        find_program(GCOVR_EXECUTABLE gcovr)

                        if(GCOVR_EXECUTABLE)
                                message(STATUS "Found gcovr: ${GCOVR_EXECUTABLE}")
                        else()
                                message(WARNING "gcovr not found. Coverage reports will not be available.")
                        endif()
                endif()

                enable_testing()
                add_subdirectory(test)

                if(CMAKE_BUILD_TYPE STREQUAL "PROFILE")
                        get_property(COVERAGE_FILE_LIST GLOBAL PROPERTY COVERAGE_FILES)
                        add_custom_command(OUTPUT default.profdata COMMAND llvm-profdata merge -sparse ${COVERAGE_FILE_LIST} -o default.profdata VERBATIM)
                        add_custom_target(merged_coverage DEPENDS default.profdata)

                        # Add coverage target for easy local C/C++ coverage generation
                        if(GCOVR_EXECUTABLE)
                                add_custom_target(coverage
                                        COMMAND ${CMAKE_COMMAND} -E echo "Generating C/C++ coverage reports..."
                                        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
                                        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage/html

                                        # Generate HTML report with details
                                        COMMAND ${GCOVR_EXECUTABLE}
                                        -r ${CMAKE_SOURCE_DIR}
                                        ${CMAKE_BINARY_DIR}
                                        --html-details ${CMAKE_BINARY_DIR}/coverage/html/index.html
                                        -e ${CMAKE_SOURCE_DIR}/test/
                                        -e ${CMAKE_SOURCE_DIR}/examples/
                                        -e ${CMAKE_SOURCE_DIR}/dependency/
                                        -e ${CMAKE_SOURCE_DIR}/build/
                                        --exclude-unreachable-branches
                                        --exclude-throw-branches

                                        # Generate XML for CI
                                        COMMAND ${GCOVR_EXECUTABLE}
                                        -r ${CMAKE_SOURCE_DIR}
                                        ${CMAKE_BINARY_DIR}
                                        --xml ${CMAKE_BINARY_DIR}/coverage/coverage.xml
                                        -e ${CMAKE_SOURCE_DIR}/test/
                                        -e ${CMAKE_SOURCE_DIR}/examples/
                                        -e ${CMAKE_SOURCE_DIR}/dependency/
                                        -e ${CMAKE_SOURCE_DIR}/build/
                                        --exclude-unreachable-branches
                                        --exclude-throw-branches

                                        # Generate Coveralls JSON for CI
                                        COMMAND ${GCOVR_EXECUTABLE}
                                        -r ${CMAKE_SOURCE_DIR}
                                        ${CMAKE_BINARY_DIR}
                                        --coveralls ${CMAKE_BINARY_DIR}/coverage/coveralls.json
                                        -e ${CMAKE_SOURCE_DIR}/test/
                                        -e ${CMAKE_SOURCE_DIR}/examples/
                                        -e ${CMAKE_SOURCE_DIR}/dependency/
                                        -e ${CMAKE_SOURCE_DIR}/build/
                                        --exclude-unreachable-branches
                                        --exclude-throw-branches

                                        # Print summary
                                        COMMAND ${GCOVR_EXECUTABLE}
                                        -r ${CMAKE_SOURCE_DIR}
                                        ${CMAKE_BINARY_DIR}
                                        -e ${CMAKE_SOURCE_DIR}/test/
                                        -e ${CMAKE_SOURCE_DIR}/examples/
                                        -e ${CMAKE_SOURCE_DIR}/dependency/
                                        -e ${CMAKE_SOURCE_DIR}/build/
                                        --exclude-unreachable-branches
                                        --exclude-throw-branches
                                        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                                        COMMENT "Generating coverage reports in ${CMAKE_BINARY_DIR}/coverage/"
                                        VERBATIM
                                )

                                message(STATUS "Coverage target added. Run 'make coverage' after running tests.")
                        endif()
                endif()
        endif()

        # -----------------------------------------------------------------------------
        # Configure the unified config.cmake file for both build and install trees
        # -----------------------------------------------------------------------------
        include(CMakePackageConfigHelpers)

        # Single unified config file that auto-detects build vs install tree
        # Generate directly in build tree location: lib/cmake/dftracer/
        configure_package_config_file(
                ${CMAKE_CURRENT_SOURCE_DIR}/cmake/configure_files/${PROJECT_NAME}-config.cmake.in
                "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}/${PROJECT_NAME}-config.cmake"
                INSTALL_DESTINATION ${DFTRACER_LIBDIR}/cmake/${PROJECT_NAME}
                PATH_VARS
                DFTRACER_INSTALL_INCLUDE_DIR
                DFTRACER_INSTALL_LIB_DIR
                DFTRACER_LIBDIR
        )

        # Install the config file to install tree: lib/cmake/dftracer/
        install(
                FILES "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}/${PROJECT_NAME}-config.cmake"
                DESTINATION ${DFTRACER_LIBDIR}/cmake/${PROJECT_NAME}
        )

        # Copy dftracer-utils.cmake and dftracer-config-version.cmake to build and install locations
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/dftracer-utils.cmake"
                DESTINATION "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/dftracer")

        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/dftracer-utils.cmake"
                DESTINATION "${DFTRACER_LIBDIR}/cmake/dftracer")
        install(FILES "${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/cmake/${PROJECT_NAME}/dftracer-config-version.cmake"
                DESTINATION "${DFTRACER_LIBDIR}/cmake/dftracer")

        # -----------------------------------------------------------------------------
        # Configure the ${PROJECT_NAME}-config-version .cmake file for the install directory
        # -----------------------------------------------------------------------------

        # Always export targets as dftracer-targets.cmake for downstream find_package
        export(EXPORT ${DFTRACER_EXPORTED_TARGETS}
                FILE "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}/dftracer-targets.cmake")

        # Resolve the real path of libc's shared object via the compiler, for
        # dftracer::STDIOBypass/POSIXBypass (src/dftracer/core/utils) to
        # dlopen(..., RTLD_NOLOAD) + dlsym() the real libc I/O functions
        # directly, bypassing GOTCHA's PLT/GOT rewriting of these same
        # symbol names in this process (see those headers for why this is
        # necessary). Falls back to the bare soname if the compiler query
        # doesn't resolve to a real file (e.g. non-glibc libc); dlopen with
        # RTLD_NOLOAD still matches by recorded module name in that case.
        execute_process(
                COMMAND ${CMAKE_C_COMPILER} -print-file-name=libc.so.6
                OUTPUT_VARIABLE DFTRACER_LIBC_SO_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT EXISTS "${DFTRACER_LIBC_SO_PATH}")
                message(STATUS "Could not resolve libc.so.6 via ${CMAKE_C_COMPILER}; falling back to bare soname")
                set(DFTRACER_LIBC_SO_PATH "libc.so.6")
        endif()
        message(STATUS "DFTRACER_LIBC_SO_PATH: ${DFTRACER_LIBC_SO_PATH}")

        # Write the configure file
        configure_file("${CMAKE_SOURCE_DIR}/cmake/configure_files/dftracer_config.hpp.in"
                "${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/dftracer/core/dftracer_config.hpp" @ONLY)

        configure_file("${CMAKE_SOURCE_DIR}/cmake/configure_files/dftracer_config_dbg.hpp.in"
                "${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/dftracer/core/dftracer_config_dbg.hpp" @ONLY)

        # ###############################################################
        # Install DFTRACER
        # ###############################################################
        include(CMakePackageConfigHelpers)

        # Write the version file. This is independent of build/install tree.
        write_basic_package_version_file(
                ${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/cmake/${PROJECT_NAME}/dftracer-config-version.cmake
                VERSION "${DFTRACER_LIBRARY_VERSION}"
                COMPATIBILITY SameMajorVersion
        )

        install(FILES
                "${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/dftracer/core/dftracer_config.hpp"
                DESTINATION
                ${DFTRACER_INSTALL_INCLUDE_DIR}/dftracer/core)

        install(FILES
                "${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/dftracer/core/dftracer_config_dbg.hpp"
                DESTINATION
                ${DFTRACER_INSTALL_INCLUDE_DIR}/dftracer/core)

        install(EXPORT
                ${DFTRACER_EXPORTED_TARGETS}
                DESTINATION
                ${DFTRACER_LIBDIR}/cmake/${PROJECT_NAME}
                FILE
                dftracer-targets.cmake
        )

        # Install license and readme
        install(FILES
                "${PROJECT_SOURCE_DIR}/LICENSE"
                "${PROJECT_SOURCE_DIR}/README.md"
                DESTINATION
                ${DFTRACER_INSTALL_DOCDIR})

        # ##############################################################################
        # Print out configuration summary
        # ##############################################################################
        if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
                set(DFTRACER_GNU_LINUX TRUE)
        endif()

        # NOTE: message() outputs to stderr by default. We now use a string to
        # maintain this information and then have cmake echo it to stdout. The
        # only side effects are that if you use the CMake GUI, you won't see
        # this output anymore (they only report stderr) and that if you add
        # something to the list, you must remember your newline!
        set(_str "\n== DFTRACER Configuration Summary ==\n\n")
        string(APPEND _str "  PROJECT_SOURCE_DIR:   ${PROJECT_SOURCE_DIR}\n"
                "  PROJECT_BINARY_DIR:   ${PROJECT_BINARY_DIR}\n\n"
                "  CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}\n"
                "  CMAKE_BUILD_TYPE:     ${CMAKE_BUILD_TYPE}\n\n")

        if(CMAKE_BUILD_TYPE MATCHES None)
                string(APPEND _str
                        "  CXX FLAGS:            ${CMAKE_CXX_FLAGS}\n")
        elseif(CMAKE_BUILD_TYPE MATCHES Release)
                string(APPEND _str
                        "  CXX FLAGS:            ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}\n")
                string(APPEND _str
                        "  C FLAGS:              ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}\n")
        elseif(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
                string(APPEND _str
                        "  CXX FLAGS:            ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}\n")
                string(APPEND _str
                        "  C FLAGS:              ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELWITHDEBINFO}\n")
        elseif(CMAKE_BUILD_TYPE MATCHES Debug)
                string(APPEND _str
                        "  CXX FLAGS:            ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG}\n")
                string(APPEND _str
                        "  C FLAGS:              ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG}\n")
        endif()

        string(APPEND _str "\n")

        string(APPEND _str
                "  DFTRACER_PACKAGE_VERSION:        ${DFTRACER_PACKAGE_VERSION}\n")
        string(APPEND _str
                "  DFTRACER_GIT_VERSION:            ${DFTRACER_GIT_VERSION}\n")
        string(APPEND _str
                "  GLIBC_VERSION:               ${GLIBC_VERSION}\n")
        string(APPEND _str
                "  DFTRACER_PYTHON_EXE:               ${DFTRACER_PYTHON_EXE}\n")
        string(APPEND _str
                "  DFTRACER_PYTHON_SITE:               ${DFTRACER_PYTHON_SITE}\n")
        string(APPEND _str
                "  MAIN_PYTHON_SITE_PACKAGES:               ${MAIN_PYTHON_SITE_PACKAGES}\n")
        string(APPEND _str
                "  DEPENDENCY_LIB:               ${DEPENDENCY_LIB}\n")
        string(APPEND _str
                "  CMAKE_BUILD_RPATH:               ${CMAKE_BUILD_RPATH}\n")
        string(APPEND _str
                "  CMAKE_INSTALL_RPATH:             ${CMAKE_INSTALL_RPATH}\n")

        # Print the true/false variables
        append_str_tf(_str
                DFTRACER_BUILD_PYTHON_BINDINGS
                DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS
                DFTRACER_ENABLE_FTRACING
                DFTRACER_GENERATE_INTERFACES
                DFTRACER_ENABLE_HDF5
                DFTRACER_ENABLE_MPI
                DFTRACER_ENABLE_PAPER_TESTS
                DFTRACER_ENABLE_TESTS
                DFTRACER_GNU_LINUX
                DFTRACER_HIP_TRACING_ENABLE
                DFTRACER_HWLOC_ENABLE
                DFTRACER_LIBDIR_AS_LIB
                DFTRACER_USE_CLANG_LIBCXX
                DFTRACER_WARNINGS_AS_ERRORS
        )
        string(APPEND _str
                "\n== End DFTRACER Configuration Summary ==\n")

        # Output to stdout
        execute_process(COMMAND ${CMAKE_COMMAND} -E echo "${_str}")
        set(_str)

        # ##############################################################################
        # Write a basic modulefile
        # ##############################################################################
        set(DFTRACER_MODULEFILE_NAME "dftracer-${DFTRACER_LIBRARY_VERSION}.lua")

        # CACHE STRING
        # "The name of the DFTRACER modulefile to install. Must end in .lua.")
        if(NOT(DFTRACER_MODULEFILE_NAME MATCHES ".+\.lua"))
                message(WARNING
                        "DFTRACER_MODULEFILE_NAME must have extension \".lua\". Appending.")
                set(DFTRACER_MODULEFILE_NAME "${DFTRACER_MODULEFILE_NAME}.lua"
                        CACHE STRING "" FORCE)
        endif()

        configure_file(
                "${CMAKE_SOURCE_DIR}/cmake/configure_files/dftracer_module.lua.in"
                "${CMAKE_BINARY_DIR}/dftracer_module.lua.install"
                @ONLY)

        install(FILES "${CMAKE_BINARY_DIR}/dftracer_module.lua.install"
                RENAME "${DFTRACER_MODULEFILE_NAME}"
                DESTINATION
                "${DFTRACER_INSTALL_SYSCONFDIR}")
endif()
