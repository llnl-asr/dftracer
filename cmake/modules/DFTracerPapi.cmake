# Provisioning for PAPI (https://github.com/icl-utk-edu/papi), the hardware
# counter library behind DFTracer's PAPI module.
#
# PAPI picks its components when it is configured, so a site PAPI may lack the
# one a run needs, such as rocp_sdk for AMD GPU counters (the Cray PAPI modules
# ship only the older rocm component). When that happens, or when there is no
# PAPI at all, the source is fetched at a pinned commit and built into
# DFTracer's install prefix.
#
# The build runs at configure time, not as an ExternalProject: the counter
# probe in the root CMakeLists.txt links and runs against libpapi during
# configure, so the library has to exist by then.

# A master commit, not a release: PAPI 7.2.0's rocp_sdk does not compile against
# the rocprofiler-sdk 1.x of ROCm 7. Update deliberately and rebuild on a ROCm
# machine before you move it.
set(DFTRACER_PAPI_REPOSITORY "https://github.com/icl-utk-edu/papi.git"
  CACHE STRING "Repository to fetch PAPI from when it has to be built")
set(DFTRACER_PAPI_COMMIT "a14a90f339d516aed1335ee8929b37a0c1da62e2"
  CACHE STRING "PAPI commit to build when it has to be built")

# Fixes applied on top of the commit, in name order. Each one is made against
# DFTRACER_PAPI_COMMIT: check that they still apply when you move it.
set(DFTRACER_PAPI_PATCH_DIR "${CMAKE_CURRENT_LIST_DIR}/papi-patches")

# TRUE when the PAPI library at `library` was built with the rocp_sdk
# component. PAPI keeps each component's name as a plain string in the library,
# so this needs no run of the library and works on a node without a GPU.
function(dftracer_papi_has_rocp_sdk library out)
  set(${out} FALSE PARENT_SCOPE)
  if(NOT library OR NOT EXISTS "${library}")
    return()
  endif()
  file(STRINGS "${library}" _match REGEX "^rocp_sdk$" LIMIT_COUNT 1)
  if(_match)
    set(${out} TRUE PARENT_SCOPE)
  endif()
endfunction()

# Builds PAPI into `install_prefix`, with rocp_sdk when `rocm_root` is given.
# Sets DFTRACER_PAPI_PROVISIONED in the caller. A stamp file next to the
# library skips the rebuild on a later configure of the same commit.
function(dftracer_provision_papi install_prefix rocm_root)
  set(DFTRACER_PAPI_PROVISIONED FALSE PARENT_SCOPE)
  if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "[DFTRACER] PAPI can only be built on Linux; not building it")
    return()
  endif()
  find_package(Git QUIET)
  find_program(DFTRACER_PAPI_MAKE NAMES gmake make)
  if(NOT GIT_FOUND OR NOT DFTRACER_PAPI_MAKE)
    message(WARNING "[DFTRACER] building PAPI needs git and make; not building it")
    return()
  endif()

  set(_components "")
  if(rocm_root)
    set(_components "rocp_sdk")
  endif()
  file(GLOB _patches "${DFTRACER_PAPI_PATCH_DIR}/*.patch")
  list(SORT _patches)
  set(_patch_text "")
  foreach(_patch IN LISTS _patches)
    file(READ "${_patch}" _one)
    string(APPEND _patch_text "${_one}")
  endforeach()
  string(SHA256 _patch_hash "${_patch_text}")
  string(SUBSTRING "${_patch_hash}" 0 12 _patch_hash)
  # The patch hash is in the name, so a PAPI built without the current patches
  # is rebuilt, not reused.
  set(_stamp "${install_prefix}/share/dftracer/papi-${DFTRACER_PAPI_COMMIT}-${_patch_hash}-${_components}.stamp")
  if(EXISTS "${_stamp}")
    message(STATUS "[DFTRACER] reusing PAPI ${DFTRACER_PAPI_COMMIT} in ${install_prefix}")
    set(DFTRACER_PAPI_PROVISIONED TRUE PARENT_SCOPE)
    return()
  endif()

  set(_src "${CMAKE_BINARY_DIR}/papi-src")
  set(_logs "${CMAKE_BINARY_DIR}/papi-logs")
  message(STATUS
    "[DFTRACER] building PAPI ${DFTRACER_PAPI_COMMIT} (components: ${_components}) "
    "into ${install_prefix}; logs in ${_logs}")

  file(REMOVE_RECURSE "${_src}" "${_logs}")
  file(MAKE_DIRECTORY "${_src}" "${_logs}")
  set(_env "")
  if(rocm_root)
    list(APPEND _env "PAPI_ROCP_SDK_ROOT=${rocm_root}")
  endif()
  # With no compiler given, PAPI's configure tries xlc and icc before gcc,
  # and its rocp_sdk component then passes icc's -diag-disable flags to g++.
  set(_cc "${CMAKE_C_COMPILER}")
  set(_cxx "${CMAKE_CXX_COMPILER}")
  if(NOT _cc AND "$ENV{CC}" STREQUAL "")
    find_program(_dftracer_papi_gcc gcc)
    set(_cc "${_dftracer_papi_gcc}")
  endif()
  if(NOT _cxx AND "$ENV{CXX}" STREQUAL "")
    find_program(_dftracer_papi_gxx g++)
    set(_cxx "${_dftracer_papi_gxx}")
  endif()
  if(_cc)
    list(APPEND _env "CC=${_cc}")
  endif()
  if(_cxx)
    list(APPEND _env "CXX=${_cxx}")
  endif()
  # Only the library: the rocp_sdk tests link with amdclang++, which misses -ldl
  # when configure ran with a compiler (the Cray cc) that links libdl itself.
  set(_configure "./configure" "--prefix=${install_prefix}" "--with-tests=no")
  if(_components)
    list(APPEND _configure "--with-components=${_components}")
  endif()
  cmake_host_system_information(RESULT _jobs QUERY NUMBER_OF_LOGICAL_CORES)

  # A commit is fetched by itself, so no history is cloned.
  foreach(_step "init" "fetch" "checkout")
    if(_step STREQUAL "init")
      set(_cmd ${GIT_EXECUTABLE} init -q "${_src}")
    elseif(_step STREQUAL "fetch")
      set(_cmd ${GIT_EXECUTABLE} -C "${_src}" fetch -q --depth 1
        "${DFTRACER_PAPI_REPOSITORY}" "${DFTRACER_PAPI_COMMIT}")
    else()
      set(_cmd ${GIT_EXECUTABLE} -C "${_src}" checkout -q FETCH_HEAD)
    endif()
    execute_process(COMMAND ${_cmd} RESULT_VARIABLE _rc
      OUTPUT_FILE "${_logs}/git-${_step}.log" ERROR_FILE "${_logs}/git-${_step}.log")
    if(NOT _rc EQUAL 0)
      message(WARNING "[DFTRACER] git ${_step} of PAPI failed (${_rc}); see ${_logs}/git-${_step}.log")
      return()
    endif()
  endforeach()

  foreach(_patch IN LISTS _patches)
    get_filename_component(_name "${_patch}" NAME_WE)
    execute_process(COMMAND ${GIT_EXECUTABLE} -C "${_src}" apply "${_patch}"
      RESULT_VARIABLE _rc
      OUTPUT_FILE "${_logs}/patch-${_name}.log" ERROR_FILE "${_logs}/patch-${_name}.log")
    if(NOT _rc EQUAL 0)
      message(WARNING "[DFTRACER] PAPI patch ${_name} does not apply (${_rc}); see ${_logs}/patch-${_name}.log")
      return()
    endif()
  endforeach()

  # PAPI's build system is autoconf and lives in src/.
  foreach(_step "configure" "build" "install")
    if(_step STREQUAL "configure")
      set(_cmd ${CMAKE_COMMAND} -E env ${_env} ${_configure})
    elseif(_step STREQUAL "build")
      set(_cmd ${CMAKE_COMMAND} -E env ${_env} ${DFTRACER_PAPI_MAKE} -j ${_jobs})
    else()
      set(_cmd ${DFTRACER_PAPI_MAKE} install)
    endif()
    execute_process(COMMAND ${_cmd} WORKING_DIRECTORY "${_src}/src"
      RESULT_VARIABLE _rc
      OUTPUT_FILE "${_logs}/${_step}.log" ERROR_FILE "${_logs}/${_step}.log")
    if(NOT _rc EQUAL 0)
      message(WARNING "[DFTRACER] PAPI ${_step} failed (${_rc}); see ${_logs}/${_step}.log")
      return()
    endif()
  endforeach()

  file(WRITE "${_stamp}" "${DFTRACER_PAPI_REPOSITORY} ${DFTRACER_PAPI_COMMIT} ${_components}\n")
  set(DFTRACER_PAPI_PROVISIONED TRUE PARENT_SCOPE)
endfunction()

# Script mode, used by dftracer-compose:
#   cmake -DDFTRACER_PAPI_PREFIX=<dir> [-DDFTRACER_PAPI_ROCM_ROOT=<rocm>] -P DFTracerPapi.cmake
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(NOT DFTRACER_PAPI_PREFIX)
    message(FATAL_ERROR "set DFTRACER_PAPI_PREFIX to the directory to install PAPI into")
  endif()
  dftracer_provision_papi("${DFTRACER_PAPI_PREFIX}" "${DFTRACER_PAPI_ROCM_ROOT}")
  if(NOT DFTRACER_PAPI_PROVISIONED)
    message(FATAL_ERROR "PAPI was not built")
  endif()
endif()
