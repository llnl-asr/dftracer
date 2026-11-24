# Provisioning for variorum (https://github.com/LLNL/variorum), the vendor
# neutral power API DFTracer uses for node-level power counters.
#
# Variorum is optional. When an installation is already on the system it is used
# as-is; when DFTracer is asked for power tracing and there is none, the source
# is fetched at a pinned release and built into DFTracer's install prefix.
#
# Two things make this more than a call to dftracer_install_external_project:
#
#  * variorum's CMakeLists.txt lives in src/, not at the top of the repository,
#    so the build needs SOURCE_SUBDIR.
#  * which power domains variorum can read is a compile-time decision. Its
#    VARIORUM_WITH_* options each pull in a vendor library and abort the
#    configure when it is missing, so the right set has to be worked out here
#    from what the build machine actually has.
#
# dftracer-compose runs this file as a script (see the end) to build Variorum,
# and E-SMI for AMD CPU power, into <libdir>/dftracer/variorum.

if(CMAKE_SCRIPT_MODE_FILE)
  cmake_policy(VERSION 3.24)
else()
  include(ExternalProject)
endif()

# Pinned to the newest variorum release. Update deliberately: the JSON key names
# the collector flattens are part of variorum's API and have changed between
# major versions.
set(DFTRACER_VARIORUM_REPOSITORY "https://github.com/LLNL/variorum.git"
  CACHE STRING "Repository to fetch variorum from when it is not installed")
set(DFTRACER_VARIORUM_TAG "v0.8.0"
  CACHE STRING "Variorum release tag to build when it is not installed")
# AMD's E-SMI, Variorum's AMD CPU back end (through /dev/hsmp). Built only by
# the script mode, when the machine has /dev/hsmp and no E-SMI.
set(DFTRACER_ESMI_REPOSITORY "https://github.com/amd/esmi_ib_library.git"
  CACHE STRING "Repository to fetch E-SMI from when it has to be built")
set(DFTRACER_ESMI_TAG "esmi_pkg_ver-5.2.1.1"
  CACHE STRING "E-SMI release tag to build when it has to be built")
# The static hwloc linked into Variorum.
include("${CMAKE_CURRENT_LIST_DIR}/DFTracerHwloc.cmake")

# Replaces `old` with `new` in `path`, once: a file that already has `new` is
# left alone, so patching a checkout again changes nothing. Warns when neither
# is there, which means the pinned release no longer matches the patch.
function(_dftracer_variorum_replace path old new)
  if(EXISTS "${path}")
    file(READ "${path}" _text)
    string(FIND "${_text}" "${new}" _done)
    string(FIND "${_text}" "${old}" _at)
  else()
    set(_done -1)
    set(_at -1)
  endif()
  if(NOT _done EQUAL -1)
    return()
  endif()
  if(_at EQUAL -1)
    message(WARNING "[DFTRACER] a Variorum patch does not apply to ${path}; "
      "update dftracer_variorum_patch_source for ${DFTRACER_VARIORUM_TAG}")
    return()
  endif()
  string(LENGTH "${old}" _old_length)
  string(SUBSTRING "${_text}" 0 ${_at} _before)
  math(EXPR _rest "${_at} + ${_old_length}")
  string(SUBSTRING "${_text}" ${_rest} -1 _after)
  file(WRITE "${path}" "${_before}${new}${_after}")
endfunction()

# Variorum 0.8.0 does not build or run as is on AMD CPUs: epyc.h declares
# amd_cpu_epyc_print_energy(void) for a function that takes an int, and
# set_amd_func_ptrs() accepts no family 0x19 model past 0x3F, which leaves out
# the MI300A (model 0x90). E-SMI reads that CPU's socket power the same way.
# On the MI300A, an APU, that socket power already holds the GPU's, so the
# ROCm SMI part must not add the GPU power to power_node_watts again. The
# library links ${NVML_HEADER}, which is never set, instead of NVML itself.
function(dftracer_variorum_patch_source source_dir)
  _dftracer_variorum_replace("${source_dir}/src/variorum/AMD/epyc.h"
    "int amd_cpu_epyc_print_energy(\n    void\n)"
    "int amd_cpu_epyc_print_energy(\n    int long_ver\n)")
  _dftracer_variorum_replace("${source_dir}/src/variorum/AMD/config_amd.c"
    "            case 0x30 ... 0x3F:\n"
    "            case 0x30 ... 0x3F:\n            case 0x90 ... 0x9F:\n")
  _dftracer_variorum_replace("${source_dir}/src/variorum/CMakeLists.txt"
    "target_link_libraries(variorum PUBLIC \${NVML_HEADER})"
    "target_link_libraries(variorum PUBLIC \${NVML_LIBRARY})")
  set(_gpu "${source_dir}/src/variorum/AMD_GPU/amd_gpu_power_features.c")
  _dftracer_variorum_replace("${_gpu}"
    "#include <sys/time.h>\n"
    "#include <sys/time.h>
#if defined(__x86_64__)
#include <cpuid.h>
#endif

/* DFTracer: true on an MI300A, whose CPU socket power includes the GPU. */
static int dftracer_cpu_is_apu(void)
{
#if defined(__x86_64__)
    unsigned int a, b, c, d;
    unsigned int family, model;
    if (!__get_cpuid(1, &a, &b, &c, &d))
    {
        return 0;
    }
    family = ((a >> 8) & 0xf) + ((a >> 20) & 0xff);
    model = (((a >> 16) & 0xf) << 4) | ((a >> 4) & 0xf);
    return family == 0x19 && model >= 0x90 && model <= 0x9f;
#else
    return 0;
#endif
}
")
  _dftracer_variorum_replace("${_gpu}"
    "    if (json_object_get(get_power_obj, \"power_node_watts\") != NULL)\n"
    "    if (json_object_get(get_power_obj, \"power_node_watts\") != NULL\n        && !dftracer_cpu_is_apu())\n")
endfunction()

# Fetches `tag` of `repository` into `dir`, a tag or commit by itself so no
# history is cloned. Sets `out` to TRUE on success; logs go to `logs`.
function(_dftracer_variorum_fetch repository tag dir logs out)
  set(${out} FALSE PARENT_SCOPE)
  find_package(Git QUIET)
  if(NOT GIT_FOUND)
    message(WARNING "[DFTRACER] fetching ${repository} needs git")
    return()
  endif()
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}" "${logs}")
  get_filename_component(_name "${dir}" NAME)
  foreach(_step "init" "fetch" "checkout")
    if(_step STREQUAL "init")
      set(_cmd ${GIT_EXECUTABLE} init -q "${dir}")
    elseif(_step STREQUAL "fetch")
      set(_cmd ${GIT_EXECUTABLE} -C "${dir}" fetch -q --depth 1 "${repository}" "${tag}")
    else()
      set(_cmd ${GIT_EXECUTABLE} -C "${dir}" checkout -q FETCH_HEAD)
    endif()
    execute_process(COMMAND ${_cmd} RESULT_VARIABLE _rc
      OUTPUT_FILE "${logs}/${_name}-git-${_step}.log"
      ERROR_FILE "${logs}/${_name}-git-${_step}.log")
    if(NOT _rc EQUAL 0)
      message(WARNING "[DFTRACER] git ${_step} of ${repository} failed (${_rc}); "
        "see ${logs}/${_name}-git-${_step}.log")
      return()
    endif()
  endforeach()
  set(${out} TRUE PARENT_SCOPE)
endfunction()

# Configures, builds and installs the CMake project in `source_dir` into
# `prefix`. Sets `out` to TRUE on success; logs go to `logs`/<name>-<step>.log.
function(_dftracer_variorum_cmake_build name source_dir build_dir prefix logs out)
  set(${out} FALSE PARENT_SCOPE)
  cmake_host_system_information(RESULT _jobs QUERY NUMBER_OF_LOGICAL_CORES)
  file(REMOVE_RECURSE "${build_dir}")
  foreach(_step "configure" "build" "install")
    if(_step STREQUAL "configure")
      set(_cmd ${CMAKE_COMMAND} -S "${source_dir}" -B "${build_dir}"
        "-DCMAKE_INSTALL_PREFIX=${prefix}" ${ARGN})
    elseif(_step STREQUAL "build")
      set(_cmd ${CMAKE_COMMAND} --build "${build_dir}" --parallel ${_jobs})
    else()
      set(_cmd ${CMAKE_COMMAND} --install "${build_dir}" --prefix "${prefix}")
    endif()
    execute_process(COMMAND ${_cmd} RESULT_VARIABLE _rc
      OUTPUT_FILE "${logs}/${name}-${_step}.log" ERROR_FILE "${logs}/${name}-${_step}.log")
    if(NOT _rc EQUAL 0)
      message(WARNING "[DFTRACER] ${name} ${_step} failed (${_rc}); see ${logs}/${name}-${_step}.log")
      return()
    endif()
  endforeach()
  set(${out} TRUE PARENT_SCOPE)
endfunction()

# Builds E-SMI into `prefix` (library in <prefix>/lib, where Variorum's ESMI_DIR
# looks). Sets `out` to TRUE when it is there.
function(_dftracer_variorum_provision_esmi prefix work out)
  set(${out} FALSE PARENT_SCOPE)
  if(EXISTS "${prefix}/lib/libe_smi64.so" AND EXISTS "${prefix}/include/e_smi/e_smi.h")
    set(${out} TRUE PARENT_SCOPE)
    return()
  endif()
  message(STATUS "[DFTRACER] building E-SMI ${DFTRACER_ESMI_TAG} into ${prefix}")
  _dftracer_variorum_fetch("${DFTRACER_ESMI_REPOSITORY}" "${DFTRACER_ESMI_TAG}"
    "${work}/esmi-src" "${work}/logs" _fetched)
  if(NOT _fetched)
    return()
  endif()
  _dftracer_variorum_cmake_build(esmi "${work}/esmi-src" "${work}/esmi-build" "${prefix}"
    "${work}/logs" _built
    "-DCMAKE_INSTALL_LIBDIR=lib" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5")
  if(_built)
    set(${out} TRUE PARENT_SCOPE)
  endif()
endfunction()

# Everything variorum needs unconditionally. Both are ordinary distribution
# packages (libhwloc-dev, libjansson-dev) and variorum's own CMake aborts with a
# fatal error rather than degrading when either is absent, so they are checked
# up front to keep that failure out of a sub-build's log.
function(_dftracer_variorum_find_prerequisites out_ok out_args)
  set(${out_ok} FALSE PARENT_SCOPE)
  set(${out_args} "" PARENT_SCOPE)

  find_path(DFTRACER_VARIORUM_HWLOC_INCLUDE_DIR NAMES hwloc.h)
  find_library(DFTRACER_VARIORUM_HWLOC_LIBRARY NAMES hwloc)
  find_path(DFTRACER_VARIORUM_JANSSON_INCLUDE_DIR NAMES jansson.h)
  find_library(DFTRACER_VARIORUM_JANSSON_LIBRARY NAMES jansson)

  if(NOT DFTRACER_VARIORUM_HWLOC_INCLUDE_DIR OR NOT DFTRACER_VARIORUM_HWLOC_LIBRARY)
    message(STATUS "[DFTRACER] variorum needs hwloc (hwloc.h + libhwloc); not found")
    return()
  endif()
  if(NOT DFTRACER_VARIORUM_JANSSON_INCLUDE_DIR OR NOT DFTRACER_VARIORUM_JANSSON_LIBRARY)
    message(STATUS "[DFTRACER] variorum needs jansson (jansson.h + libjansson); not found")
    return()
  endif()

  # variorum expects the prefix, not the include or library directory, and
  # looks under <dir>/include and <dir>/lib itself.
  get_filename_component(_hwloc_prefix "${DFTRACER_VARIORUM_HWLOC_INCLUDE_DIR}" DIRECTORY)
  get_filename_component(_jansson_prefix "${DFTRACER_VARIORUM_JANSSON_INCLUDE_DIR}" DIRECTORY)

  set(_args
    "-DHWLOC_DIR=${_hwloc_prefix}"
    "-DJANSSON_DIR=${_jansson_prefix}")
  set(${out_ok} TRUE PARENT_SCOPE)
  set(${out_args} "${_args}" PARENT_SCOPE)
endfunction()

# Work out which VARIORUM_WITH_* options this machine can satisfy.
#
# Each one is only turned on when the vendor library it needs is present,
# because variorum aborts its configure otherwise. The result is a build that
# reports the domains this node really has rather than the ones it might have.
function(_dftracer_variorum_detect_architectures out_args)
  set(_args "")
  set(_enabled "")

  # Intel CPU support is variorum's default. Turn it off unless the host is
  # Intel with msr-safe: without msr-safe the Intel part fails every
  # variorum_get_power_json call, which loses the GPU readings too.
  set(_intel_cpu OFF)
  set(_amd_cpu OFF)
  set(_arm_cpu OFF)
  set(_ibm_cpu OFF)

  set(_processor "${CMAKE_SYSTEM_PROCESSOR}")
  if(NOT _processor)
    set(_processor "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()
  if(NOT _processor)
    # Unset in script mode, which loads no platform files.
    cmake_host_system_information(RESULT _processor QUERY OS_PLATFORM)
  endif()
  if(_processor MATCHES "^(aarch64|arm64)")
    set(_arm_cpu ON)
  elseif(_processor MATCHES "^(ppc64|powerpc64)")
    set(_ibm_cpu ON)
  elseif(EXISTS "/proc/cpuinfo")
    file(READ "/proc/cpuinfo" _cpuinfo)
    if(_cpuinfo MATCHES "GenuineIntel")
      if(EXISTS "/dev/cpu/msr_batch")
        set(_intel_cpu ON)
      else()
        message(STATUS
          "[DFTRACER] variorum: Intel CPU without msr-safe (/dev/cpu/msr_batch); "
          "CPU power domains will not be available")
      endif()
    elseif(_cpuinfo MATCHES "AuthenticAMD")
      # AMD CPU power comes from AMD's E-SMI library, which is a separate
      # install and usually absent. Without it variorum's configure fails, so
      # only ask for AMD CPU support when E-SMI is actually there.
      find_path(DFTRACER_VARIORUM_ESMI_INCLUDE_DIR NAMES e_smi/e_smi.h)
      find_library(DFTRACER_VARIORUM_ESMI_LIBRARY NAMES e_smi64)
      if((NOT DFTRACER_VARIORUM_ESMI_INCLUDE_DIR OR NOT DFTRACER_VARIORUM_ESMI_LIBRARY)
         AND DFTRACER_VARIORUM_ESMI_PREFIX AND EXISTS "/dev/hsmp")
        _dftracer_variorum_provision_esmi("${DFTRACER_VARIORUM_ESMI_PREFIX}"
          "${DFTRACER_VARIORUM_WORK}" _esmi_built)
        if(_esmi_built)
          set(DFTRACER_VARIORUM_ESMI_INCLUDE_DIR "${DFTRACER_VARIORUM_ESMI_PREFIX}/include")
          set(DFTRACER_VARIORUM_ESMI_LIBRARY "${DFTRACER_VARIORUM_ESMI_PREFIX}/lib/libe_smi64.so")
        endif()
      endif()
      if(DFTRACER_VARIORUM_ESMI_INCLUDE_DIR AND DFTRACER_VARIORUM_ESMI_LIBRARY)
        get_filename_component(_esmi_prefix
          "${DFTRACER_VARIORUM_ESMI_INCLUDE_DIR}" DIRECTORY)
        list(APPEND _args "-DESMI_DIR=${_esmi_prefix}")
        set(_amd_cpu ON)
      else()
        message(STATUS
          "[DFTRACER] variorum: AMD CPU detected but E-SMI is not installed; "
          "CPU power domains will not be available")
      endif()
    endif()
  endif()

  # AMD GPUs, through ROCm SMI. On an APU such as MI300A this is also where the
  # package power for the whole socket comes from, so it matters even on a part
  # with no discrete GPU.
  set(_amd_gpu OFF)
  set(_rocm_hints "$ENV{ROCM_PATH}" "$ENV{ROCM_DIR}" "${ROCM_PATH}" "/opt/rocm")
  list(REMOVE_ITEM _rocm_hints "")
  find_path(DFTRACER_VARIORUM_ROCM_INCLUDE_DIR
    NAMES rocm_smi/rocm_smi.h
    HINTS ${_rocm_hints}
    PATH_SUFFIXES include)
  find_library(DFTRACER_VARIORUM_ROCM_LIBRARY
    NAMES rocm_smi64
    HINTS ${_rocm_hints}
    PATH_SUFFIXES lib lib64)
  # A build for this machine (the script mode) also needs the GPU: a ROCm
  # install on an NVIDIA node would build an AMD part that cannot read it.
  if(DFTRACER_VARIORUM_THIS_MACHINE AND NOT EXISTS "/dev/kfd")
    set(DFTRACER_VARIORUM_ROCM_LIBRARY "")
  endif()
  set(DFTRACER_VARIORUM_ROCM_LIBDIR "" PARENT_SCOPE)
  if(DFTRACER_VARIORUM_ROCM_INCLUDE_DIR AND DFTRACER_VARIORUM_ROCM_LIBRARY)
    get_filename_component(_rocm_prefix
      "${DFTRACER_VARIORUM_ROCM_INCLUDE_DIR}" DIRECTORY)
    get_filename_component(_rocm_libdir "${DFTRACER_VARIORUM_ROCM_LIBRARY}" DIRECTORY)
    if(NOT _rocm_libdir STREQUAL "${_rocm_prefix}/lib")
      # variorum takes <ROCM_DIR>/lib/librocm_smi64.so and no other path, so
      # a library in lib64 gets a directory of links in that layout, and the
      # RPATH names the real directory.
      set(_rocm_links "${CMAKE_BINARY_DIR}/variorum-rocm")
      if(DFTRACER_VARIORUM_WORK)
        set(_rocm_links "${DFTRACER_VARIORUM_WORK}/rocm")
      endif()
      file(REMOVE_RECURSE "${_rocm_links}")
      file(MAKE_DIRECTORY "${_rocm_links}/lib")
      file(CREATE_LINK "${DFTRACER_VARIORUM_ROCM_INCLUDE_DIR}"
        "${_rocm_links}/include" SYMBOLIC)
      file(CREATE_LINK "${DFTRACER_VARIORUM_ROCM_LIBRARY}"
        "${_rocm_links}/lib/librocm_smi64.so" SYMBOLIC)
      set(_rocm_prefix "${_rocm_links}")
      set(DFTRACER_VARIORUM_ROCM_LIBDIR "${_rocm_libdir}" PARENT_SCOPE)
    endif()
    list(APPEND _args "-DROCM_DIR=${_rocm_prefix}")
    set(_amd_gpu ON)
  endif()

  # NVIDIA GPUs, through NVML. variorum wants <NVML_DIR>/include/nvml.h and
  # <NVML_DIR>/lib/libnvml.so, a name neither the driver (libnvidia-ml.so.1)
  # nor CUDA ships, so a directory of links in that layout stands in for it.
  set(_nvidia_gpu OFF)
  if(NVML_DIR)
    list(APPEND _args "-DNVML_DIR=${NVML_DIR}")
    set(_nvidia_gpu ON)
  else()
    set(_cuda_hints "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}" "$ENV{DFTRACER_CUDA_PATH}"
      "/usr/local/cuda")
    list(REMOVE_ITEM _cuda_hints "")
    find_path(DFTRACER_VARIORUM_NVML_INCLUDE_DIR NAMES nvml.h
      HINTS ${DFTRACER_VARIORUM_NVML_INCLUDE} ${_cuda_hints}
      PATH_SUFFIXES include targets/x86_64-linux/include targets/sbsa-linux/include)
    # The driver's library first: CUDA's stub only links, it reads nothing.
    find_library(DFTRACER_VARIORUM_NVML_LIBRARY NAMES nvidia-ml libnvidia-ml.so.1)
    if(NOT DFTRACER_VARIORUM_NVML_LIBRARY)
      find_library(DFTRACER_VARIORUM_NVML_LIBRARY NAMES nvidia-ml
        HINTS ${_cuda_hints} PATH_SUFFIXES lib64/stubs lib/stubs
        targets/x86_64-linux/lib/stubs targets/sbsa-linux/lib/stubs)
    endif()
    if(DFTRACER_VARIORUM_THIS_MACHINE AND NOT EXISTS "/dev/nvidiactl")
      set(DFTRACER_VARIORUM_NVML_LIBRARY "")
    endif()
    if(DFTRACER_VARIORUM_NVML_INCLUDE_DIR AND DFTRACER_VARIORUM_NVML_LIBRARY)
      set(_nvml_prefix "${CMAKE_BINARY_DIR}/variorum-nvml")
      if(DFTRACER_VARIORUM_WORK)
        set(_nvml_prefix "${DFTRACER_VARIORUM_WORK}/nvml")
      endif()
      file(REMOVE_RECURSE "${_nvml_prefix}")
      file(MAKE_DIRECTORY "${_nvml_prefix}/include" "${_nvml_prefix}/lib")
      file(CREATE_LINK "${DFTRACER_VARIORUM_NVML_INCLUDE_DIR}/nvml.h"
        "${_nvml_prefix}/include/nvml.h" SYMBOLIC)
      file(CREATE_LINK "${DFTRACER_VARIORUM_NVML_LIBRARY}"
        "${_nvml_prefix}/lib/libnvml.so" SYMBOLIC)
      list(APPEND _args "-DNVML_DIR=${_nvml_prefix}")
      set(_nvidia_gpu ON)
    endif()
  endif()

  list(APPEND _args
    "-DVARIORUM_WITH_INTEL_CPU=${_intel_cpu}"
    "-DVARIORUM_WITH_AMD_CPU=${_amd_cpu}"
    "-DVARIORUM_WITH_ARM_CPU=${_arm_cpu}"
    "-DVARIORUM_WITH_IBM_CPU=${_ibm_cpu}"
    "-DVARIORUM_WITH_AMD_GPU=${_amd_gpu}"
    "-DVARIORUM_WITH_NVIDIA_GPU=${_nvidia_gpu}")

  foreach(_pair "INTEL_CPU;${_intel_cpu}" "AMD_CPU;${_amd_cpu}"
                "ARM_CPU;${_arm_cpu}" "IBM_CPU;${_ibm_cpu}"
                "AMD_GPU;${_amd_gpu}" "NVIDIA_GPU;${_nvidia_gpu}")
    list(GET _pair 0 _name)
    list(GET _pair 1 _value)
    if(_value)
      list(APPEND _enabled "${_name}")
    endif()
  endforeach()

  set(DFTRACER_VARIORUM_DOMAINS "${_enabled}" PARENT_SCOPE)
  if(_enabled)
    string(REPLACE ";" ", " _enabled_str "${_enabled}")
    message(STATUS "[DFTRACER] variorum power domains to build: ${_enabled_str}")
  else()
    # Still worth building: the collector reports that variorum found nothing
    # readable and disables itself, rather than the build failing outright.
    # The usual cause on a GPU node is a ROCm or CUDA install CMake cannot see,
    # so say where it looked.
    message(STATUS
      "[DFTRACER] variorum: no supported power domain detected on this machine. "
      "For AMD GPUs set ROCM_PATH (or load a rocm module) so rocm_smi is found.")
  endif()

  set(${out_args} "${_args}" PARENT_SCOPE)
endfunction()

# Build variorum into `install_prefix` as an external project.
#
# Sets DFTRACER_VARIORUM_PROVISIONED in the caller's scope when a build was
# added, along with DFTRACER_VARIORUM_INCLUDE_DIRS / DFTRACER_VARIORUM_LIBRARIES
# pointing into the prefix, and creates a `variorum` target to depend on. Leaves
# DFTRACER_VARIORUM_PROVISIONED false when the machine cannot support a build.
function(dftracer_provision_variorum install_prefix)
  set(DFTRACER_VARIORUM_PROVISIONED FALSE PARENT_SCOPE)

  _dftracer_variorum_find_prerequisites(_prereqs_ok _prereq_args)
  if(NOT _prereqs_ok)
    message(WARNING
      "[DFTRACER] variorum requested but its prerequisites are missing; "
      "install hwloc and jansson development packages, or point at an existing "
      "variorum with -DVARIORUM_DIR=. Building without power tracing.")
    return()
  endif()

  _dftracer_variorum_detect_architectures(_arch_args)

  # Only pass a toolchain setting that actually has a value: variorum's
  # project() enables both C and CXX, and an empty -DCMAKE_CXX_COMPILER= makes
  # its configure fail outright rather than fall back to the default compiler.
  set(_toolchain_args "")
  foreach(_var CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_BUILD_TYPE)
    if(NOT "${${_var}}" STREQUAL "")
      list(APPEND _toolchain_args "-D${_var}=${${_var}}")
    endif()
  endforeach()

  # Deliberately not called "variorum": DEPENDENCY_LIB carries the library by
  # bare name, and a target of that name would shadow it, turning every link
  # into "may not be linked into another target".
  ExternalProject_Add(
    variorum_external
    PREFIX ${CMAKE_BINARY_DIR}
    GIT_REPOSITORY ${DFTRACER_VARIORUM_REPOSITORY}
    GIT_TAG ${DFTRACER_VARIORUM_TAG}
    GIT_SHALLOW TRUE
    # variorum's project lives one level down.
    SOURCE_SUBDIR src
    PATCH_COMMAND ${CMAKE_COMMAND} -DDFTRACER_VARIORUM_PATCH_SOURCE=<SOURCE_DIR>
      -P ${CMAKE_CURRENT_FUNCTION_LIST_FILE}
    CMAKE_ARGS
    "-DCMAKE_INSTALL_PREFIX=${install_prefix}"
    ${_toolchain_args}
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    # variorum declares cmake_minimum_required(VERSION 3.0), which CMake 4
    # refuses outright.
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    "-DBUILD_SHARED_LIBS=ON"
    # Only the C library is wanted here. Its Fortran and Python bindings, its
    # gtest suite and its OpenMP examples are all on by default and each adds a
    # hard requirement DFTracer has no use for.
    "-DBUILD_TESTS=OFF"
    "-DENABLE_FORTRAN=OFF"
    "-DENABLE_PYTHON=OFF"
    "-DENABLE_OPENMP=OFF"
    "-DENABLE_MPI=OFF"
    ${_prereq_args}
    ${_arch_args}
    ${DFTRACER_VARIORUM_EXTRA_CMAKE_ARGS}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel
    # --prefix for the same reason as dftracer_install_external_project: a stale
    # sub-build keeps its cached CMAKE_INSTALL_PREFIX when configure is skipped.
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --prefix ${install_prefix}
    LOG_DOWNLOAD ON
  )

  set(DFTRACER_VARIORUM_PROVISIONED TRUE PARENT_SCOPE)
  set(DFTRACER_VARIORUM_TARGET variorum_external PARENT_SCOPE)
  set(DFTRACER_VARIORUM_INCLUDE_DIRS "${install_prefix}/include" PARENT_SCOPE)
  # By name, not by path: the library does not exist yet at configure time.
  set(DFTRACER_VARIORUM_LIBRARIES "variorum" PARENT_SCOPE)
  set(DFTRACER_VARIORUM_LIBRARY_DIRS
    "${install_prefix}/lib" "${install_prefix}/lib64" PARENT_SCOPE)

  message(STATUS
    "[DFTRACER] variorum ${DFTRACER_VARIORUM_TAG} will be built into ${install_prefix}")
endfunction()

# Builds Variorum (and E-SMI when the machine has /dev/hsmp and no E-SMI) into
# `install_prefix` now, not as an ExternalProject. Sets
# DFTRACER_VARIORUM_PROVISIONED in the caller. A stamp file names the tag and
# the domains, and a later call with the same ones reuses the build.
function(dftracer_build_variorum_now install_prefix)
  set(DFTRACER_VARIORUM_PROVISIONED FALSE PARENT_SCOPE)
  if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "[DFTRACER] Variorum can only be built on Linux; not building it")
    return()
  endif()
  _dftracer_variorum_find_prerequisites(_prereqs_ok _prereq_args)
  if(NOT _prereqs_ok)
    message(WARNING "[DFTRACER] Variorum needs the hwloc and jansson development "
      "packages; not building it")
    return()
  endif()
  set(DFTRACER_VARIORUM_ESMI_PREFIX "${install_prefix}")
  _dftracer_variorum_detect_architectures(_arch_args)
  if(NOT DFTRACER_VARIORUM_DOMAINS)
    # Built and stamped, it would be reused once the machine shows a domain
    # (a CUDA module loaded later for nvml.h).
    message(WARNING "[DFTRACER] Variorum: no power domain to build for here; "
      "not building it")
    return()
  endif()
  string(REPLACE ";" "," _domains "${DFTRACER_VARIORUM_DOMAINS}")
  if(NOT DFTRACER_VARIORUM_HWLOC_ROOT)
    set(DFTRACER_VARIORUM_HWLOC_ROOT "${DFTRACER_VARIORUM_WORK}/hwloc-root")
  endif()
  set(_hwloc_prefix "${DFTRACER_VARIORUM_HWLOC_ROOT}/${DFTRACER_STATIC_HWLOC_VERSION}")
  dftracer_provision_static_hwloc("${_hwloc_prefix}" "${DFTRACER_VARIORUM_WORK}" ON
    _hwloc_lib)
  # The linker flags keep Variorum's own globals (it has one named
  # "topology", as MVAPICH2's libmpi.so does) bound inside the library, and
  # with the static hwloc, hwloc's symbols too.
  set(_link_flags "-Wl,-Bsymbolic")
  set(_hwloc_args "")
  set(_hwloc_tag "system-hwloc")
  if(_hwloc_lib)
    # Later -D values win over the system HWLOC_DIR in _prereq_args; the
    # include directory is named too, as variorum searches CMAKE_PREFIX_PATH
    # before HWLOC_DIR for it.
    set(_hwloc_args "-DHWLOC_DIR=${_hwloc_prefix}"
      "-DHWLOC_INCLUDE_DIRS=${_hwloc_prefix}/include"
      "-DHWLOC_LIBRARY=${_hwloc_lib}")
    set(_link_flags "-Wl,--exclude-libs,libhwloc.a -Wl,-Bsymbolic")
    set(_hwloc_tag "hwloc${DFTRACER_STATIC_HWLOC_VERSION}")
  endif()
  set(_stamp "${install_prefix}/share/dftracer/variorum-${DFTRACER_VARIORUM_TAG}-${_domains}-${_hwloc_tag}.stamp")
  if(EXISTS "${_stamp}" AND EXISTS "${install_prefix}/lib/libvariorum.so")
    message(STATUS "[DFTRACER] reusing Variorum ${DFTRACER_VARIORUM_TAG} in ${install_prefix}")
    set(DFTRACER_VARIORUM_PROVISIONED TRUE PARENT_SCOPE)
    return()
  endif()

  set(_logs "${DFTRACER_VARIORUM_WORK}/logs")
  message(STATUS "[DFTRACER] building Variorum ${DFTRACER_VARIORUM_TAG} (domains: ${_domains}) "
    "into ${install_prefix}; logs in ${_logs}")
  _dftracer_variorum_fetch("${DFTRACER_VARIORUM_REPOSITORY}" "${DFTRACER_VARIORUM_TAG}"
    "${DFTRACER_VARIORUM_WORK}/variorum-src" "${_logs}" _fetched)
  if(NOT _fetched)
    return()
  endif()
  dftracer_variorum_patch_source("${DFTRACER_VARIORUM_WORK}/variorum-src")
  set(_rpath "${install_prefix}/lib")
  if(DFTRACER_VARIORUM_ROCM_LIBDIR)
    string(APPEND _rpath ":${DFTRACER_VARIORUM_ROCM_LIBDIR}")
  endif()
  # RPATH, not the loader's search, finds E-SMI and ROCm SMI: the service
  # dlopens this library with no LD_LIBRARY_PATH set for them.
  _dftracer_variorum_cmake_build(variorum "${DFTRACER_VARIORUM_WORK}/variorum-src/src"
    "${DFTRACER_VARIORUM_WORK}/variorum-build" "${install_prefix}" "${_logs}" _built
    "-DCMAKE_INSTALL_LIBDIR=lib"
    "-DCMAKE_INSTALL_RPATH=${_rpath}"
    "-DCMAKE_SHARED_LINKER_FLAGS=${_link_flags}"
    "-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON"
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    "-DBUILD_SHARED_LIBS=ON"
    "-DBUILD_TESTS=OFF"
    "-DENABLE_FORTRAN=OFF"
    "-DENABLE_PYTHON=OFF"
    "-DENABLE_OPENMP=OFF"
    "-DENABLE_MPI=OFF"
    ${_prereq_args}
    ${_hwloc_args}
    ${_arch_args})
  if(NOT _built)
    return()
  endif()
  file(GLOB _old_stamps "${install_prefix}/share/dftracer/variorum-*.stamp")
  if(_old_stamps)
    file(REMOVE ${_old_stamps})
  endif()
  file(WRITE "${_stamp}" "${DFTRACER_VARIORUM_REPOSITORY} ${DFTRACER_VARIORUM_TAG} ${_domains}\n")
  set(DFTRACER_VARIORUM_PROVISIONED TRUE PARENT_SCOPE)
endfunction()

# Script mode:
#   cmake -DDFTRACER_VARIORUM_PATCH_SOURCE=<variorum checkout> -P DFTracerVariorum.cmake
#     patches the checkout (the ExternalProject's PATCH_COMMAND)
#   cmake -DDFTRACER_VARIORUM_PREFIX=<dir> [-DDFTRACER_VARIORUM_ROCM_ROOT=<rocm>]
#         [-DDFTRACER_VARIORUM_NVML_INCLUDE=<dir with nvml.h>]
#         [-DDFTRACER_VARIORUM_WORK=<dir>] [-DDFTRACER_VARIORUM_HWLOC_ROOT=<dir>]
#         -P DFTracerVariorum.cmake
#     builds Variorum into <dir>, as dftracer-compose does
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(DFTRACER_VARIORUM_PATCH_SOURCE)
    dftracer_variorum_patch_source("${DFTRACER_VARIORUM_PATCH_SOURCE}")
    return()
  endif()
  if(NOT DFTRACER_VARIORUM_PREFIX)
    message(FATAL_ERROR "set DFTRACER_VARIORUM_PREFIX to the directory to install Variorum into")
  endif()
  if(NOT DFTRACER_VARIORUM_WORK)
    set(DFTRACER_VARIORUM_WORK "${CMAKE_CURRENT_BINARY_DIR}/variorum-work")
  endif()
  if(DFTRACER_VARIORUM_ROCM_ROOT)
    set(ROCM_PATH "${DFTRACER_VARIORUM_ROCM_ROOT}")
  endif()
  # A script loads no platform files, so find_path and find_library know no
  # system directory or library name form until they are given here.
  set(DFTRACER_VARIORUM_THIS_MACHINE ON)
  set(CMAKE_SYSTEM_PREFIX_PATH /usr/local /usr /)
  set(CMAKE_FIND_LIBRARY_PREFIXES lib)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .so .a)
  set(CMAKE_SIZEOF_VOID_P 8)
  cmake_host_system_information(RESULT _dftracer_processor QUERY OS_PLATFORM)
  set(CMAKE_LIBRARY_ARCHITECTURE "${_dftracer_processor}-linux-gnu")
  set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS TRUE)
  dftracer_build_variorum_now("${DFTRACER_VARIORUM_PREFIX}")
  if(NOT DFTRACER_VARIORUM_PROVISIONED)
    message(FATAL_ERROR "Variorum was not built")
  endif()
endif()
