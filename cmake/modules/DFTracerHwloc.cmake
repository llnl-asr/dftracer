# A static hwloc that Variorum and the composed core link in and hide.
# Included by DFTracerVariorum.cmake and cmake/compose/CMakeLists.txt.

set(DFTRACER_STATIC_HWLOC_VERSION "2.11.2")
set(DFTRACER_STATIC_HWLOC_URL
  "https://download.open-mpi.org/release/hwloc/v2.11/hwloc-2.11.2.tar.bz2")
set(DFTRACER_STATIC_HWLOC_SHA256
  "f7f88fecae067100f1a1a915b658add0f4f71561259482910a69baea22fe8409")

# Builds a static, PIC hwloc into `prefix`, reused when it is there, for a
# library to link in and hide (-Wl,--exclude-libs,libhwloc.a). MVAPICH2's
# libmpi.so exports the hwloc 1.x it embeds and comes first in the global
# search of an MPI program, so calls to the system libhwloc.so.15, and its own
# internal calls, bind to that 1.x. With `build` off only a library already
# in `prefix` is used. Sets `out` to the library on success; on failure it
# warns and leaves `out` empty, and the caller links the system hwloc.
function(dftracer_provision_static_hwloc prefix work build out)
  set(${out} "" PARENT_SCOPE)
  set(_lib "${prefix}/lib/libhwloc.a")
  if(EXISTS "${_lib}" AND EXISTS "${prefix}/include/hwloc.h")
    set(${out} "${_lib}" PARENT_SCOPE)
    return()
  endif()
  if(NOT build)
    message(WARNING "[DFTRACER] hwloc: no static hwloc in ${prefix} and "
      "building is off; linking the system hwloc")
    return()
  endif()
  set(_logs "${work}/logs")
  file(MAKE_DIRECTORY "${_logs}")
  set(_tarball "${work}/hwloc-${DFTRACER_STATIC_HWLOC_VERSION}.tar.bz2")
  # The hash is checked after: with EXPECTED_HASH a failed download is a
  # fatal error, not a status.
  file(DOWNLOAD "${DFTRACER_STATIC_HWLOC_URL}" "${_tarball}"
    INACTIVITY_TIMEOUT 60 TIMEOUT 600 STATUS _status)
  list(GET _status 0 _rc)
  if(_rc EQUAL 0)
    file(SHA256 "${_tarball}" _sha)
    if(NOT _sha STREQUAL DFTRACER_STATIC_HWLOC_SHA256)
      set(_rc 1)
      set(_status "1;SHA256 ${_sha}, expected ${DFTRACER_STATIC_HWLOC_SHA256}")
    endif()
  endif()
  if(NOT _rc EQUAL 0)
    file(REMOVE "${_tarball}")
    message(WARNING "[DFTRACER] hwloc download failed (${_status}); "
      "linking the system hwloc")
    return()
  endif()
  file(REMOVE_RECURSE "${work}/hwloc-src")
  file(ARCHIVE_EXTRACT INPUT "${_tarball}" DESTINATION "${work}/hwloc-src")
  set(_src "${work}/hwloc-src/hwloc-${DFTRACER_STATIC_HWLOC_VERSION}")
  cmake_host_system_information(RESULT _jobs QUERY NUMBER_OF_LOGICAL_CORES)
  # Only the CPU topology: no I/O discovery, so no plugin pulls in CUDA,
  # NVML, ROCm SMI, libpciaccess or libxml2.
  foreach(_step "configure" "build" "install")
    if(_step STREQUAL "configure")
      set(_cmd sh "${_src}/configure" "--prefix=${prefix}" --enable-static
        --disable-shared --with-pic --disable-io --disable-libxml2 --disable-cairo
        --disable-libudev --disable-plugin-dlopen --disable-doxygen)
    elseif(_step STREQUAL "build")
      set(_cmd make -j ${_jobs})
    else()
      set(_cmd make install)
    endif()
    execute_process(COMMAND ${_cmd} WORKING_DIRECTORY "${_src}" RESULT_VARIABLE _rc
      OUTPUT_FILE "${_logs}/hwloc-${_step}.log" ERROR_FILE "${_logs}/hwloc-${_step}.log")
    if(NOT _rc EQUAL 0)
      message(WARNING "[DFTRACER] hwloc ${_step} failed (${_rc}); see "
        "${_logs}/hwloc-${_step}.log; linking the system hwloc")
      return()
    endif()
  endforeach()
  set(${out} "${_lib}" PARENT_SCOPE)
endfunction()
