# ISA-L (igzip) writes the gzip members of every trace. It is built from a
# pinned release inside this build and linked in statically; its symbols stay
# out of dftracer's exports, so an application's own ISA-L does not clash.
#
# Its x86-64 kernels are assembled with nasm. A nasm on PATH (or
# DFTRACER_NASM_EXECUTABLE) is used; without one, nasm is built from its
# pinned release first. Without nasm ISA-L falls back to plain C, which is much
# slower, so that fallback is never taken on x86-64.
#
# Sets DFTRACER_ISAL_LIBRARY, DFTRACER_ISAL_INCLUDE_DIR and the target
# dftracer_isal, which every target linking the archive must depend on.

include(ExternalProject)
include(ProcessorCount)

set(DFTRACER_ISAL_URL
    "https://github.com/intel/isa-l/archive/refs/tags/v2.31.1.tar.gz"
    CACHE STRING "ISA-L source tarball")
set(DFTRACER_ISAL_SHA256
    "e1d5573a4019738243b568ab1e1422e6ab7557c5cae33cc8686944d327ad6bb4"
    CACHE STRING "SHA256 of DFTRACER_ISAL_URL")
set(DFTRACER_NASM_URL
    "https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/nasm-2.16.03.tar.xz"
    CACHE STRING "nasm source tarball, used when no nasm is installed")
set(DFTRACER_NASM_SHA256
    "1412a1c760bbd05db026b6c0d1657affd6631cd0a63cddb6f73cc6d4aa616148"
    CACHE STRING "SHA256 of DFTRACER_NASM_URL")

ProcessorCount(_dft_jobs)
if(_dft_jobs EQUAL 0)
  set(_dft_jobs 4)
endif()
# ISA-L builds with its own Makefile.unx, whatever the CMake generator.
find_program(DFTRACER_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

set(_isal_depends "")
set(_isal_make_args "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  find_program(DFTRACER_NASM_EXECUTABLE nasm)
  # ISA-L wants nasm 2.15 or later; an older one is replaced like a missing one.
  if(DFTRACER_NASM_EXECUTABLE)
    execute_process(COMMAND ${DFTRACER_NASM_EXECUTABLE} -v
                    OUTPUT_VARIABLE _nasm_v ERROR_QUIET)
    string(REGEX MATCH "version ([0-9]+\\.[0-9]+)" _nasm_m "${_nasm_v}")
    if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 VERSION_LESS 2.15)
      message(STATUS "[DFTRACER] ${DFTRACER_NASM_EXECUTABLE} is older than "
                     "2.15; building nasm")
      unset(DFTRACER_NASM_EXECUTABLE CACHE)
    endif()
  endif()
  if(DFTRACER_NASM_EXECUTABLE)
    set(_nasm ${DFTRACER_NASM_EXECUTABLE})
  else()
    set(_nasm_prefix ${CMAKE_BINARY_DIR}/_deps/nasm)
    set(_nasm ${_nasm_prefix}/bin/nasm)
    ExternalProject_Add(
      dftracer_nasm
      PREFIX ${_nasm_prefix}
      URL ${DFTRACER_NASM_URL}
      URL_HASH SHA256=${DFTRACER_NASM_SHA256}
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      # nasm's Makefile does not create its object directories out of tree.
      BUILD_IN_SOURCE TRUE
      CONFIGURE_COMMAND ./configure --prefix=${_nasm_prefix}
      BUILD_COMMAND ${DFTRACER_MAKE_EXECUTABLE} -j${_dft_jobs} nasm
      INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/nasm ${_nasm}
      BUILD_BYPRODUCTS ${_nasm}
      LOG_CONFIGURE ON
      LOG_BUILD ON)
    list(APPEND _isal_depends dftracer_nasm)
  endif()
  message(STATUS "[DFTRACER] nasm for ISA-L: ${_nasm}")
  list(APPEND _isal_make_args "AS=${_nasm}")
endif()

set(_isal_prefix ${CMAKE_BINARY_DIR}/_deps/isal)
set(DFTRACER_ISAL_INCLUDE_DIR ${_isal_prefix}/include)
set(DFTRACER_ISAL_LIBRARY ${_isal_prefix}/src/dftracer_isal/bin/isa-l.a)
# The static archive goes into shared libraries, so its C objects need -fPIC;
# its assembly is position-independent already.
ExternalProject_Add(
  dftracer_isal
  PREFIX ${_isal_prefix}
  URL ${DFTRACER_ISAL_URL}
  URL_HASH SHA256=${DFTRACER_ISAL_SHA256}
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  DEPENDS ${_isal_depends}
  BUILD_IN_SOURCE TRUE
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ${DFTRACER_MAKE_EXECUTABLE} -f Makefile.unx -j${_dft_jobs} lib
                "CC=${CMAKE_C_COMPILER} -fPIC" ${_isal_make_args}
  INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/include
                  ${DFTRACER_ISAL_INCLUDE_DIR}/isa-l
  BUILD_BYPRODUCTS ${DFTRACER_ISAL_LIBRARY}
  LOG_BUILD ON)
message(STATUS "[DFTRACER] ISA-L: built from ${DFTRACER_ISAL_URL}")
