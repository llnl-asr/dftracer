################################################################
# Set compiler standard
################################################################

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# This is off to use -std=c11 instead of -std=gnu11
set(CMAKE_C_EXTENSIONS OFF)
# Add _POSIX_C_SOURCE to avoid warning on setenv() as C_EXTENSIONS is off
add_definitions(-D_POSIX_C_SOURCE=200809L)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
#set(CMAKE_C_VISIBILITY_PRESET hidden)
#set(CMAKE_CXX_VISIBILITY_PRESET hidden)


################################################################
# Check and add compiler flags
################################################################

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckIncludeFileCXX)

MACRO (CHECK_GLIBC_VERSION)
    EXECUTE_PROCESS (
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=libc.so.6
        OUTPUT_VARIABLE GLIBC
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    GET_FILENAME_COMPONENT (GLIBC ${GLIBC} REALPATH)
    GET_FILENAME_COMPONENT (GLIBC_VERSION ${GLIBC} NAME)
    STRING (REPLACE "libc-" "" GLIBC_VERSION ${GLIBC_VERSION})
    STRING (REPLACE "libc" "" GLIBC_VERSION ${GLIBC_VERSION})
    STRING (REPLACE ".so" "" GLIBC_VERSION ${GLIBC_VERSION})
    IF (NOT GLIBC_VERSION MATCHES "^[0-9.]+$")
        MESSAGE (FATAL_ERROR "Unknown glibc version: ${GLIBC_VERSION}")
    ENDIF (NOT GLIBC_VERSION MATCHES "^[0-9.]+$")
ENDMACRO (CHECK_GLIBC_VERSION)

CHECK_GLIBC_VERSION()

# MACRO dftracer_add_c|cxx_flags
#
# Purpose: checks that all flags are valid and appends them to the
#   given list. Valid means that the cxx compiler does not throw an
#   error upon encountering the flag.
#
# Arguments:
#   MY_FLAGS The list of current flags
#   ARGN The flags to check
#
# Note: If flag is not valid, it is not appended.
macro(dftracer_add_cxx_flags MY_FLAGS)
  foreach(flag ${ARGN})
    string(FIND "${${MY_FLAGS}}" "${flag}" flag_already_set)
    if(flag_already_set EQUAL -1)
      string(REPLACE "-" "_" _CLEAN_FLAG "${flag}")

      set(CMAKE_REQUIRED_LIBRARIES "${flag}")
      check_cxx_compiler_flag("${flag}" FLAG_${_CLEAN_FLAG}_OK)
      unset(CMAKE_REQUIRED_LIBRARIES)

      if (FLAG_${_CLEAN_FLAG}_OK)
        set(${MY_FLAGS} "${${MY_FLAGS}} ${flag}")
      endif ()
      unset(FLAG_${_CLEAN_FLAG}_OK CACHE)
    endif()
  endforeach()
endmacro()

macro(dftracer_add_c_flags MY_FLAGS)
  foreach(flag ${ARGN})
    string(FIND "${${MY_FLAGS}}" "${flag}" flag_already_set)
    if(flag_already_set EQUAL -1)
      string(REPLACE "-" "_" _CLEAN_FLAG "${flag}")

      set(CMAKE_REQUIRED_LIBRARIES "${flag}")
      check_c_compiler_flag("${flag}" FLAG_${_CLEAN_FLAG}_OK)
      unset(CMAKE_REQUIRED_LIBRARIES)

      if (FLAG_${_CLEAN_FLAG}_OK)
        set(${MY_FLAGS} "${${MY_FLAGS}} ${flag}")
      endif ()
      unset(FLAG_${_CLEAN_FLAG}_OK CACHE)
    endif()
  endforeach()
endmacro()

dftracer_add_cxx_flags(CMAKE_CXX_FLAGS
  -fPIC -Wall -Wextra -pedantic -Wno-unused-parameter -Wnon-virtual-dtor
  -Wno-deprecated-declarations)

dftracer_add_c_flags(CMAKE_C_FLAGS
  -fPIC -Wall -Wextra -pedantic -Wno-unused-parameter
  -Wno-deprecated-declarations)

if (${GLIBC_VERSION} VERSION_GREATER_EQUAL "2.19")
  # to suppress usleep() warning
  add_definitions(-D_DEFAULT_SOURCE)
endif ()

################################################################
# Promote a compiler warning as an error for project targets
################################################################

if (DFTRACER_WARNINGS_AS_ERRORS)
  dftracer_add_cxx_flags(_WERROR_FLAGS -Werror)
  separate_arguments(_WERROR_FLAGS NATIVE_COMMAND "${_WERROR_FLAGS}")
  if (NOT TARGET DFTRACER_CXX_FLAGS_werror)
    add_library(DFTRACER_CXX_FLAGS_werror INTERFACE)
    set_property(TARGET DFTRACER_CXX_FLAGS_werror PROPERTY
      INTERFACE_COMPILE_OPTIONS $<$<COMPILE_LANGUAGE:CXX>:${_WERROR_FLAGS}>)

    add_library(DFTRACER_C_FLAGS_werror INTERFACE)
    set_property(TARGET DFTRACER_C_FLAGS_werror PROPERTY
      INTERFACE_COMPILE_OPTIONS $<$<COMPILE_LANGUAGE:C>:${_WERROR_FLAGS}>)

    # Add the "library" to the export
    install(TARGETS DFTRACER_C_FLAGS_werror EXPORT ${DFTRACER_EXPORTED_TARGETS})
    install(TARGETS DFTRACER_CXX_FLAGS_werror EXPORT ${DFTRACER_EXPORTED_TARGETS})
  endif ()
endif ()


################################################################
# Handle compiler dependent behaviors
################################################################

set(CMAKE_C_COMPILER_ID ${CMAKE_CXX_COMPILER_ID})

# Some behavior is dependent on the compiler version.
if (NOT CMAKE_CXX_COMPILER_VERSION)
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
    OUTPUT_VARIABLE CXX_VERSION)
else ()
  set(CXX_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
endif ()

# - Special handling if we're compiling with Clang's address sanitizer
# - gcc toolchain handling for interoperability, especially with the exteral
#   libraries pre-built using gcc
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if (USE_CLANG_LIBCXX)
    dftracer_add_cxx_flags(CMAKE_CXX_FLAGS "--stdlib=libc++")
  else (USE_CLANG_LIBCXX)
    if (GCC_TOOLCHAIN)
      dftracer_add_cxx_flags(CMAKE_CXX_FLAGS "--gcc-toolchain=${GCC_TOOLCHAIN}")
    endif (GCC_TOOLCHAIN)
  endif (USE_CLANG_LIBCXX)

  if (CMAKE_BUILD_TYPE MATCHES Debug)
    dftracer_add_cxx_flags(CMAKE_CXX_FLAGS
      -fsanitize=address -fno-omit-frame-pointer -fsanitize-recover=address)
    dftracer_add_c_flags(CMAKE_C_FLAGS
      -fsanitize=address -fno-omit-frame-pointer -fsanitize-recover=address)
    add_link_options(-fsanitize=address)
  else()
    dftracer_add_cxx_flags(CMAKE_CXX_FLAGS -fno-omit-frame-pointer)
    dftracer_add_c_flags(CMAKE_C_FLAGS -fno-omit-frame-pointer)
  endif ()
endif ()

# Turn off some annoying warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "Intel")
  # Bugs with Intel compiler version 19
  #https://community.intel.com/t5/Intel-C-Compiler/quot-if-constexpr-quot-and-quot-missing-return-statement-quot-in/td-p/1154551
  #https://bitbucket.org/berkeleylab/upcxx/issues/286/icc-bug-bogus-warning-use-of-offsetof-with
  if (GCC_PATH)
    set(GCC_INTEROP "-gcc-name=${GCC_PATH}")
  endif (GCC_PATH)
  # -openmp_profile
  dftracer_add_cxx_flags(CMAKE_CXX_FLAGS -diag-disable=2196 -wd1011 -wd1875 -diag-disable=11074 -diag-disable=11076 ${GCC_INTEROP})
  dftracer_add_cxx_flags(CMAKE_C_FLAGS -diag-disable=2196 -wd1011 -wd1875 -diag-disable=11074 -diag-disable=11076 ${GCC_INTEROP})
endif ()


################################################################
# Initialize RPATH
################################################################

# Use RPATH on OS X
if (APPLE)
  set(CMAKE_MACOSX_RPATH ON)
endif ()

# Use (i.e. don't skip) RPATH for build
set(CMAKE_SKIP_BUILD_RPATH FALSE)

# Use same RPATH for build and install
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

# Add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if (NOT DFTRACER_LIBDIR)
  if (CMAKE_INSTALL_LIBDIR)
    set(DFTRACER_LIBDIR ${CMAKE_INSTALL_LIBDIR})
  else ()
    set(DFTRACER_LIBDIR "lib")
  endif ()
endif ()

# $ORIGIN, not ../lib: a relative entry resolves against the working directory of whoever
# starts the binary, so bin/dftracer_service found its libraries only by luck once the
# build prefix was gone (a wheel built by pip, which installs elsewhere). bin/ is covered by
# $ORIGIN/../lib*, the libraries by $ORIGIN, and the Python extension by $ORIGIN/lib*.
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib:${CMAKE_INSTALL_PREFIX}/lib64:${PYTHON_SITE_PACKAGES}/lib:${PYTHON_SITE_PACKAGES}/lib64:$ORIGIN:$ORIGIN/../lib64:$ORIGIN/../lib:$ORIGIN/lib64:$ORIGIN/lib")

list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES
    "${CMAKE_INSTALL_PREFIX}/${DFTRACER_LIBDIR}" _IS_SYSTEM_DIR)

if (${_IS_SYSTEM_DIR} STREQUAL "-1")
    # Set the install RPATH correctly
    list(APPEND CMAKE_INSTALL_RPATH
      "${CMAKE_INSTALL_PREFIX}/${DFTRACER_LIBDIR}")
endif ()


################################################################
# Check if std::filesystem is available
################################################################

# Testing for compiler feature supports
#include(CheckCXXSourceCompiles)

try_compile(DFTRACER_HAS_STD_FILESYSTEM "${CMAKE_BINARY_DIR}/temp"
            "${CMAKE_SOURCE_DIR}/cmake/tests/has_filesystem.cpp"
            CMAKE_FLAGS ${CMAKE_CXX_FLAGS}
            LINK_LIBRARIES stdc++fs)
if (DFTRACER_HAS_STD_FILESYSTEM)
  message(STATUS "Compiler has std::filesystem support")
else ()
  message(STATUS "Compiler does not have std::filesystem support. Use boost::filesystem")
endif (DFTRACER_HAS_STD_FILESYSTEM)

try_compile(DFTRACER_HAS_STD_FSTREAM_FD "${CMAKE_BINARY_DIR}/temp"
            "${CMAKE_SOURCE_DIR}/cmake/tests/has_fd.cpp"
            CMAKE_FLAGS ${CMAKE_CXX_FLAGS}
            LINK_LIBRARIES stdc++fs)
if (DFTRACER_HAS_STD_FSTREAM_FD)
  message(STATUS "Compiler exposes the internal file descriptor of std::fstream")
else ()
  message(STATUS "Compiler does not expose the internal file descriptor of std::fstream")
endif (DFTRACER_HAS_STD_FSTREAM_FD)