cmake_minimum_required(VERSION 3.4...3.18)
#------------------------------------------------------------------------------
# Version information
#------------------------------------------------------------------------------

set(DFTRACER_VERSION_MAJOR "3")
set(DFTRACER_VERSION_MINOR "1")
set(DFTRACER_VERSION_PATCH "0")
set(DFTRACER_PACKAGE ${PROJECT_NAME})
set(DFTRACER_PACKAGE_NAME ${PROJECT_NAME})
set(DFTRACER_PACKAGE_VERSION "${DFTRACER_VERSION_MAJOR}.${DFTRACER_VERSION_MINOR}.${DFTRACER_VERSION_PATCH}")
set(DFTRACER_PACKAGE_VERSION_MAJOR "${DFTRACER_VERSION_MAJOR}.${DFTRACER_VERSION_MINOR}")
set(DFTRACER_PACKAGE_VERSION_MINOR "${DFTRACER_VERSION_PATCH}")
set(DFTRACER_PACKAGE_STRING "${DFTRACER_PACKAGE_NAME} ${DFTRACER_PACKAGE_VERSION}")
set(DFTRACER_PACKAGE_TARNAME "${DFTRACER_PACKAGE}")

set(DFTRACER_VERSION "(1, 0, 9)")

project(dftracer LANGUAGES C CXX)

# Convenience defines
string(TOUPPER "${PROJECT_NAME}" UPPER_PROJECT_NAME)
string(TOLOWER "${PROJECT_NAME}" LOWER_PROJECT_NAME)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-empty-body -Wno-format-extra-args")
#------------------------------------------------------------------------------
# Internal Includes for header and libraries
#------------------------------------------------------------------------------
# Get installation directories -- these get used in various places;
# best to just make them available
option (DFTRACER_LIBDIR_AS_LIB OFF)
if (NOT DFTRACER_LIBDIR_AS_LIB)
    include(GNUInstallDirs)
endif ()
#
if (CMAKE_INSTALL_LIBDIR)
   message(STATUS "CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR}")
   set(DFTRACER_LIBDIR ${CMAKE_INSTALL_LIBDIR})
   set(DFTRACER_INSTALL_INCLUDE_DIR
       ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR})
   set(DFTRACER_INSTALL_DOCDIR
       ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DOCDIR})
   set(DFTRACER_INSTALL_SYSCONFDIR
       ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}/modulefiles)
   set(DFTRACER_INSTALL_BINFDIR
           ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
else ()
   set(DFTRACER_LIBDIR "lib")
   set(DFTRACER_INSTALL_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
   set(DFTRACER_INSTALL_DOCDIR "${CMAKE_INSTALL_PREFIX}/doc")
   set(DFTRACER_INSTALL_SYSCONFDIR "${CMAKE_INSTALL_PREFIX}/etc/modulefiles")
   set(DFTRACER_INSTALL_BINARYDIR "${CMAKE_INSTALL_PREFIX}/bin")
   message(STATUS "DFTRACER_LIBDIR set to ${DFTRACER_LIBDIR}")
endif ()

#------------------------------------------------------------------------------
# Internal Paths for cmake libraries and Setup install and output Directories
#------------------------------------------------------------------------------
# This sets where to look for dependent libraries
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_BINARY_DIR} ${CMAKE_INSTALL_PREFIX})
# This sets where to look for dependent library's cmake files
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR}/share/cmake)

#------------------------------------------------------------------------------
if (NOT DFTRACER_EXTERNALLY_CONFIGURED)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin CACHE PATH "Single Directory for all Executables.")
    set(CMAKE_INCLUDE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/include CACHE PATH "Store the headers.")
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR} CACHE PATH "Single Directory for all Libraries")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR} CACHE PATH "Single Directory for all static libraries.")
endif ()

include_directories(${CMAKE_BINARY_DIR}/include)
link_directories(${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

#-----------------------------------------------------------------------------
# Targets built within this project are exported at Install time for use
# by other projects.
#-----------------------------------------------------------------------------
if (NOT DFTRACER_EXPORTED_TARGETS)
    set(DFTRACER_EXPORTED_TARGETS "dftracer-targets")
endif ()

include(dftracer-utils)
#------------------------------------------------------------------------------
# Build options
#------------------------------------------------------------------------------
# option(ENABLE_DFTRACER_DEBUG "Include debugging prints and logging" OFF) # This is not needed as we have CMAKE_BUILD_TYPE
# This is verbose, maybe an alternate might help simplify
#option(BUILD_URPC "Build DFTRACER's URPC code" OFF)
#option(ENABLE_PERFFLOW "Build with PerfFlow Aspect support" OFF)
#option(ENABLE_UCX_DTL "Build DFTRACER's UCX data transport layer" OFF)

# This option is off by default because non-developers should not use
# this option under normal circumstances.
option(DFTRACER_WARNINGS_AS_ERRORS "Build with warnings promoted to errors." OFF)
mark_as_advanced(DFTRACER_WARNINGS_AS_ERRORS)

option (DFTRACER_USE_CLANG_LIBCXX OFF)
if (DFTRACER_USE_CLANG_LIBCXX)
    set (GCC_TOOLCHAIN "")
endif (DFTRACER_USE_CLANG_LIBCXX)

# Options
option (DFTRACER_DISABLE_HWLOC "Disable HWLOC" On)
if (NOT DFTRACER_DISABLE_HWLOC)
    set(DFTRACER_HWLOC_ENABLE 1)
endif()

# Options
option (DFTRACER_ENABLE_FTRACING "Enable Function Tracing" OFF)
if (DFTRACER_ENABLE_FTRACING)
    set(DFTRACER_FTRACING_ENABLE 1)
    set(DFTRACER_FUNCTION_FLAGS "-g" "-finstrument-functions" "-Wl,-E" "-fvisibility=default")
else()
    set(DFTRACER_FUNCTION_FLAGS )
endif()

option (DFTRACER_ENABLE_MPI "Enable MPI" OFF)
if (DFTRACER_ENABLE_MPI)
    set(DFTRACER_MPI_ENABLE 1)
endif()
option (DFTRACER_BUILD_PYTHON_BINDINGS "Build python bindings." ON)
set(DFTRACER_PYTHON_EXE "python3" CACHE STRING "Python executable to use for building.")
set(DFTRACER_PYTHON_SITE $ENV{DFTRACER_PYTHON_SITE} CACHE STRING "Python site packages to use for building.")

if (DFTRACER_BUILD_PYTHON_BINDINGS)
    if (DFTRACER_PYTHON_SITE)
        set(PYTHON_SITE_PACKAGES "${DFTRACER_PYTHON_SITE}")
        set(MAIN_PYTHON_SITE_PACKAGES ${PYTHON_SITE_PACKAGES})
    else()
        set(PYTHON_SITE_PACKAGES ${CMAKE_INSTALL_PREFIX})
        execute_process (COMMAND ${DFTRACER_PYTHON_EXE} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())" OUTPUT_VARIABLE MAIN_PYTHON_SITE_PACKAGES OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    include_directories(${PYTHON_SITE_PACKAGES}/dftracer/include)
    link_directories(${PYTHON_SITE_PACKAGES}/dftracer/lib)
endif()

# install dependency
option (DFTRACER_INSTALL_DEPENDENCIES "Install DFTracer dependencies" OFF)

# testing
option (DFTRACER_ENABLE_TESTS "Enable tests for DFTRACER." OFF)
option (DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS "Enable dlio_benchmark tests" OFF)
option (DFTRACER_ENABLE_PAPER_TESTS "Enable paper tests" OFF)
set (DFTRACER_TEST_LD_LIBRARY_PATH "" CACHE STRING "Additional LD_LIBRARY_PATH to be included on testing")

#------------------------------------------------------------------------------
# Compiler setup
#------------------------------------------------------------------------------
include(ExternalProject)
# It requires include(GNUInstallDirs) before if that is needed
include(SetupCompiler)

#------------------------------------------------------------------------------
# Build Type based configurations
#------------------------------------------------------------------------------

if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release") # Setting default as Release
endif()

if (NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS ON)
endif ()
#------------------------------------------------------------------------------
# Disallow in-source build
#------------------------------------------------------------------------------
if ("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR
            "${PROJECT_NAME} requires an out of source Build. "
            "Please create a separate binary directory and run CMake there.")
endif ()
#------------------------------------------------------------------------------
# Set a default build type if none was specified
#------------------------------------------------------------------------------
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
            "MinSizeRel" "RelWithDebInfo")
endif ()
#------------------------------------------------------------------------------
# Dependencies
#------------------------------------------------------------------------------

if (DFTRACER_INSTALL_DEPENDENCIES)
    message(STATUS "[${PROJECT_NAME}] downloading dependencies. Please run make for downloading depedencies and then do reconfigure without dependency flag.")
    message(STATUS "[${PROJECT_NAME}] Installing depedencies at ${CMAKE_INSTALL_PREFIX}")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependency)
else()
# Required Dependencies
# =============================================================================
set(DEPENDENCY_LIB ${CMAKE_EXE_LINKER_FLAGS} $ENV{LDFLAGS})
find_package(cpp-logger 1.0.0 REQUIRED)
if (${cpp-logger_FOUND})
    include_directories(${CPP_LOGGER_INCLUDE_DIRS})
    set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${CPP_LOGGER_LIBRARIES})
    message(STATUS "[CPP_LOGGER] found cpp-logger at ${CPP_LOGGER_INCLUDE_DIRS}")
else ()
    message(FATAL_ERROR "-- [CPP_LOGGER] is needed for ${PROJECT_NAME} build")
endif ()
find_package(brahma 2.1.0 REQUIRED)
if (${brahma_FOUND})
    message(STATUS "[BRAHMA] found brahma at ${BRAHMA_INCLUDE_DIRS}")
    include_directories(${BRAHMA_INCLUDE_DIRS})
    set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${BRAHMA_LIBRARIES})
else ()
    message(FATAL_ERROR "-- [BRAHMA] is needed for ${PROJECT_NAME} build")
endif ()
find_package(yaml-cpp REQUIRED)
if (${yaml-cpp_FOUND})
    message(STATUS "[DFTRACER] found yaml-cpp at ${YAML_CPP_INCLUDE_DIR}")
    include_directories(${YAML_CPP_INCLUDE_DIR})
    set(YAML_CPP_LIBRARY_DIR "${YAML_CPP_CMAKE_DIR}/../../")
    set(DEPENDENCY_LIB ${DEPENDENCY_LIB} -L${YAML_CPP_LIBRARY_DIR} ${YAML_CPP_LIBRARIES})
else ()
    message(FATAL_ERROR "-- [DFTRACER] yaml-cpp is needed for ${PROJECT_NAME} build")
endif ()

# Optional Dependencies
# =============================================================================
if (DFTRACER_BUILD_PYTHON_BINDINGS)
    find_package(pybind11 REQUIRED)
    #add_subdirectory(dependency/py11bind)
endif()

if (DFTRACER_ENABLE_MPI)
  find_package(MPI COMPONENTS CXX REQUIRED)
  if (MPI_FOUND)
      message(STATUS "[DFTRACER] found mpi.h at ${MPI_CXX_INCLUDE_DIRS}")
      include_directories(${MPI_CXX_INCLUDE_DIRS})
      set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${MPI_CXX_LIBRARIES})
  else ()
      message(FATAL_ERROR "-- [DFTRACER] mpi is needed for ${PROJECT_NAME} build")
  endif ()
endif()

if (NOT DFTRACER_DISABLE_HWLOC)
    find_package(HWLOC REQUIRED)
    if (${HWLOC_FOUND})
        include_directories(${HWLOC_INCLUDE_DIRS})
        set(DEPENDENCY_LIB ${DEPENDENCY_LIB} ${HWLOC_LIBRARIES})
        message(STATUS "[DFTRACER] found hwloc at ${HWLOC_INCLUDE_DIRS}")
    else ()
        message(FATAL_ERROR "-- [DFTRACER] hwloc is needed for ${PROJECT_NAME} build with -DDFTRACER_DISABLE_HWLOC-ON option.")
    endif ()
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
set(DFTRACER_PRELOAD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/dftracer_preload.cpp)
set(DFTRACER_PRELOAD_PRIVATE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/dftracer_preload.h)

set(DFTRACER_CORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/brahma/posix.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/brahma/stdio.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/finstrument/functions.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/writer/chrome_writer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/utils/posix_internal.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/dftracer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/dftracer_main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/utils/configuration_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/utils/md5.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/df_logger.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/utils/utils.cpp)
set(DFTRACER_CORE_PUBLIC_INCLUDE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/dftracer/dftracer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/dftracer/core/constants.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/dftracer/core/typedef.h)
set(DFTRACER_CORE_PRIVATE_INCLUDE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/utils/posix_internal.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/utils/utils.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/utils/md5.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/finstrument/functions.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/brahma/posix.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/brahma/stdio.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/dftracer_main.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/logging.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/core/singleton.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/utils/configuration_manager.h)
add_library(${PROJECT_NAME} SHARED)
target_compile_definitions(${PROJECT_NAME} PUBLIC DFTRACER_LOGGER_LEVEL_ERROR)
target_link_libraries(${PROJECT_NAME} ${DEPENDENCY_LIB})
add_library(${PROJECT_NAME}_dbg SHARED)
target_link_libraries(${PROJECT_NAME}_dbg ${DEPENDENCY_LIB})
add_library(${PROJECT_NAME}_preload SHARED)
target_link_libraries(${PROJECT_NAME}_preload ${DEPENDENCY_LIB} ${PROJECT_NAME})
add_library(${PROJECT_NAME}_preload_dbg SHARED)
target_link_libraries(${PROJECT_NAME}_preload_dbg ${DEPENDENCY_LIB} ${PROJECT_NAME}_dbg)

set(DFTRACER_DEBUG_TARGETS ${PROJECT_NAME}_dbg ${PROJECT_NAME}_preload_dbg)

if (DFTRACER_BUILD_PYTHON_BINDINGS)
    pybind11_add_module(py${PROJECT_NAME} ${DFTRACER_CORE_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/pydftracer.cpp)
    target_compile_definitions(py${PROJECT_NAME} PRIVATE VERSION_INFO=${EXAMPLE_VERSION_INFO})
    target_link_libraries(py${PROJECT_NAME} PRIVATE ${DEPENDENCY_LIB})

    pybind11_add_module(py${PROJECT_NAME}_dbg NO_EXTRAS ${DFTRACER_CORE_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/src/dftracer/pydftracer_dbg.cpp)
    target_compile_definitions(py${PROJECT_NAME}_dbg PRIVATE VERSION_INFO=${EXAMPLE_VERSION_INFO})
    target_link_libraries(py${PROJECT_NAME}_dbg PRIVATE ${DEPENDENCY_LIB})
    list(APPEND DFTRACER_DEBUG_TARGETS py${PROJECT_NAME}_dbg)
endif()
target_sources(${PROJECT_NAME}
        PRIVATE
        ${DFTRACER_CORE_SRC}
        )
target_sources(${PROJECT_NAME}_dbg
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
target_include_directories(${PROJECT_NAME}
        PRIVATE
        # where the library itself will look for its internal headers
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        PUBLIC
        # where top-level project will look for the library's public headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # where external projects will look for the library's public headers
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )
target_include_directories(${PROJECT_NAME}_dbg
        PRIVATE
        # where the library itself will look for its internal headers
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        PUBLIC
        # where top-level project will look for the library's public headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # where external projects will look for the library's public headers
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )
set_target_properties(${PROJECT_NAME}
        PROPERTIES
        DEBUG_OUTPUT_NAME ${PROJECT_NAME}
        RELEASE_OUTPUT_NAME ${PROJECT_NAME}
        MINSIZEREL_OUTPUT_NAME ${PROJECT_NAME}
        RELWITHDEBINFO_OUTPUT_NAME ${PROJECT_NAME}
        VERSION ${DFTRACER_PACKAGE_VERSION}
        SOVERSION ${DFTRACER_PACKAGE_VERSION}
        )

dftracer_debug_config("${DFTRACER_DEBUG_TARGETS}")
dftracer_install_headers("${DFTRACER_CORE_PUBLIC_INCLUDE}")
install(
        TARGETS ${PROJECT_NAME}
        EXPORT ${DFTRACER_EXPORTED_TARGETS}
        LIBRARY DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
        ARCHIVE DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
        RUNTIME DESTINATION ${DFTRACER_INSTALL_BIN_DIR}
)
install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
          TARGETS ${PROJECT_NAME}_dbg
          EXPORT ${DFTRACER_EXPORTED_TARGETS}
          LIBRARY DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
          ARCHIVE DESTINATION ${DFTRACER_INSTALL_LIB_DIR}
          RUNTIME DESTINATION ${DFTRACER_INSTALL_BIN_DIR}
  )
install(TARGETS ${PROJECT_NAME}_dbg
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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
if (DFTRACER_BUILD_PYTHON_BINDINGS)
    install(TARGETS py${PROJECT_NAME}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/symlink.sh CONTENT "echo -- Installing: symlink ${MAIN_PYTHON_SITE_PACKAGES}/$<TARGET_FILE_NAME:py${PROJECT_NAME}> from ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/$<TARGET_FILE_NAME:py${PROJECT_NAME}>;ln -sf ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/$<TARGET_FILE_NAME:py${PROJECT_NAME}> ${MAIN_PYTHON_SITE_PACKAGES}/$<TARGET_FILE_NAME:py${PROJECT_NAME}>")
    install(CODE "execute_process(
                COMMAND bash -c \"set -e
                chmod +x ${CMAKE_BINARY_DIR}/symlink.sh
                . ${CMAKE_BINARY_DIR}/symlink.sh
                \")")

    install(TARGETS py${PROJECT_NAME}_dbg
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/symlink_dbg.sh CONTENT "echo -- Installing: symlink ${MAIN_PYTHON_SITE_PACKAGES}/$<TARGET_FILE_NAME:py${PROJECT_NAME}_dbg> from ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/$<TARGET_FILE_NAME:py${PROJECT_NAME}_dbg>;ln -sf ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/$<TARGET_FILE_NAME:py${PROJECT_NAME}_dbg> ${MAIN_PYTHON_SITE_PACKAGES}/$<TARGET_FILE_NAME:py${PROJECT_NAME}_dbg>")
    install(CODE "execute_process(
                COMMAND bash -c \"set -e
                chmod +x ${CMAKE_BINARY_DIR}/symlink_dbg.sh
                . ${CMAKE_BINARY_DIR}/symlink_dbg.sh
                \")")
endif()
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

#cmake_policy(SET CMP0079 NEW) # In case that we need more control over the target building order

if(DFTRACER_ENABLE_TESTS)
    message("-- " "[DFTracer] Enabling Testing")
    if(CMAKE_BUILD_TYPE STREQUAL "PROFILE")
        message("-- " "[DFTracer] building with CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE}")
        set(CMAKE_C_FLAGS_PROFILE --coverage)
        set(CMAKE_CXX_FLAGS_PROFILE --coverage)
    endif()
    enable_testing()
    add_subdirectory(test)
    if(CMAKE_BUILD_TYPE STREQUAL "PROFILE")
        get_property(COVERAGE_FILE_LIST GLOBAL PROPERTY COVERAGE_FILES)
        add_custom_command(OUTPUT default.profdata COMMAND llvm-profdata merge -sparse ${COVERAGE_FILE_LIST} -o default.profdata VERBATIM)
        add_custom_target(merged_coverage DEPENDS default.profdata)
    endif()
endif()

#-----------------------------------------------------------------------------
# Configure the config.cmake file for the build directory
#-----------------------------------------------------------------------------
include(CMakePackageConfigHelpers)
configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/configure_files/${PROJECT_NAME}-config.cmake.build.in
        "${CMAKE_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
        INSTALL_DESTINATION  ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}/${PROJECT_NAME}-config.cmake
        PATH_VARS CMAKE_BINARY_DIR
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/configure_files/${PROJECT_NAME}-config.cmake.install.in
        "${CMAKE_BINARY_DIR}/install/${PROJECT_NAME}-config.cmake"
        INSTALL_DESTINATION  ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}/install/${PROJECT_NAME}-config.cmake
        PATH_VARS CMAKE_BINARY_DIR
)
install(
        FILES
        ${CMAKE_BINARY_DIR}/install/${PROJECT_NAME}-config.cmake
        DESTINATION
        ${DFTRACER_LIBDIR}/cmake/${PROJECT_NAME}
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/dftracer-utils.cmake"
        DESTINATION "${DFTRACER_LIBDIR}/cmake/dftracer")
#-----------------------------------------------------------------------------
# Configure the ${PROJECT_NAME}-config-version .cmake file for the install directory
#-----------------------------------------------------------------------------

install(
        FILES
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
        DESTINATION
        ${DFTRACER_LIBDIR}/cmake/${PROJECT_NAME}
)

export(EXPORT ${DFTRACER_EXPORTED_TARGETS}
        FILE "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}/${PROJECT_NAME}Targets.cmake")

# Write the configure file
configure_file("${CMAKE_SOURCE_DIR}/cmake/configure_files/dftracer_config.hpp.in"
               "${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/dftracer/dftracer_config.hpp" @ONLY)

configure_file("${CMAKE_SOURCE_DIR}/cmake/configure_files/dftracer_config_dbg.hpp.in"
               "${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/dftracer/dftracer_config_dbg.hpp" @ONLY)

################################################################
# Install DFTRACER
################################################################

include(CMakePackageConfigHelpers)

# Write the version file. This is independent of build/install tree.
write_basic_package_version_file(
  dftracer-config-version.cmake
  VERSION "${DFTRACER_PACKAGE_VERSION}"
  COMPATIBILITY SameMajorVersion)

install(FILES
        "${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/dftracer/dftracer_config.hpp"
        DESTINATION
        ${DFTRACER_INSTALL_INCLUDE_DIR}/dftracer)

install(FILES
        "${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/dftracer/dftracer_config_dbg.hpp"
        DESTINATION
        ${DFTRACER_INSTALL_INCLUDE_DIR}/dftracer)

install(EXPORT
        ${DFTRACER_EXPORTED_TARGETS}
        DESTINATION
        ${DFTRACER_LIBDIR}/cmake/${PROJECT_NAME}
        FILE
        ${DFTRACER_EXPORTED_TARGETS}.cmake
        )

# Install license and readme
install(FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/README.md"
        DESTINATION
        ${DFTRACER_INSTALL_DOCDIR})

###############################################################################
#                      Print out configuration summary
###############################################################################

if (CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
  set(DFTRACER_GNU_LINUX TRUE)
endif ()

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
if (CMAKE_BUILD_TYPE MATCHES None)
  string(APPEND _str
    "  CXX FLAGS:            ${CMAKE_CXX_FLAGS}\n")
elseif (CMAKE_BUILD_TYPE MATCHES Release)
  string(APPEND _str
    "  CXX FLAGS:            ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}\n")
  string(APPEND _str
    "  C FLAGS:              ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}\n")
elseif (CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  string(APPEND _str
    "  CXX FLAGS:            ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}\n")
  string(APPEND _str
    "  C FLAGS:              ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELWITHDEBINFO}\n")
elseif (CMAKE_BUILD_TYPE MATCHES Debug)
  string(APPEND _str
    "  CXX FLAGS:            ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG}\n")
  string(APPEND _str
    "  C FLAGS:              ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG}\n")
endif ()
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
#Print the true/false variables
append_str_tf(_str
  DFTRACER_GNU_LINUX
  DFTRACER_LIBDIR_AS_LIB
  DFTRACER_ENABLE_FTRACING
  DFTRACER_ENABLE_MPI
  DFTRACER_DISABLE_HWLOC
  DFTRACER_USE_CLANG_LIBCXX
  DFTRACER_WARNINGS_AS_ERRORS
  DFTRACER_BUILD_PYTHON_BINDINGS
  DFTRACER_ENABLE_TESTS
  DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS
  DFTRACER_ENABLE_PAPER_TESTS
)
string(APPEND _str
  "\n== End DFTRACER Configuration Summary ==\n")

# Output to stdout
execute_process(COMMAND ${CMAKE_COMMAND} -E echo "${_str}")
set(_str)

###############################################################################
# Write a basic modulefile
###############################################################################

set(DFTRACER_MODULEFILE_NAME "dftracer-${DFTRACER_PACKAGE_VERSION}.lua")
#  CACHE STRING
#  "The name of the DFTRACER modulefile to install. Must end in .lua.")

if (NOT (DFTRACER_MODULEFILE_NAME MATCHES ".+\.lua"))
  message(WARNING
    "DFTRACER_MODULEFILE_NAME must have extension \".lua\". Appending.")
  set(DFTRACER_MODULEFILE_NAME "${DFTRACER_MODULEFILE_NAME}.lua"
    CACHE STRING "" FORCE)
endif ()

configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/configure_files/dftracer_module.lua.in"
  "${CMAKE_BINARY_DIR}/dftracer_module.lua.install"
  @ONLY)

install(FILES "${CMAKE_BINARY_DIR}/dftracer_module.lua.install"
  RENAME "${DFTRACER_MODULEFILE_NAME}"
  DESTINATION
  "${DFTRACER_INSTALL_SYSCONFDIR}")

endif()
