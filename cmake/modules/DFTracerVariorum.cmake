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

include(ExternalProject)

# Pinned to the newest variorum release. Update deliberately: the JSON key names
# the collector flattens are part of variorum's API and have changed between
# major versions.
set(DFTRACER_VARIORUM_REPOSITORY "https://github.com/LLNL/variorum.git"
  CACHE STRING "Repository to fetch variorum from when it is not installed")
set(DFTRACER_VARIORUM_TAG "v0.8.0"
  CACHE STRING "Variorum release tag to build when it is not installed")

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

  # Intel CPU support is variorum's default. Turn it off unless the host really
  # is Intel, so an AMD or ARM node does not build RAPL MSR code it cannot use.
  set(_intel_cpu OFF)
  set(_amd_cpu OFF)
  set(_arm_cpu OFF)
  set(_ibm_cpu OFF)

  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
    set(_arm_cpu ON)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ppc64|powerpc64)")
    set(_ibm_cpu ON)
  elseif(EXISTS "/proc/cpuinfo")
    file(READ "/proc/cpuinfo" _cpuinfo)
    if(_cpuinfo MATCHES "GenuineIntel")
      set(_intel_cpu ON)
    elseif(_cpuinfo MATCHES "AuthenticAMD")
      # AMD CPU power comes from AMD's E-SMI library, which is a separate
      # install and usually absent. Without it variorum's configure fails, so
      # only ask for AMD CPU support when E-SMI is actually there.
      find_path(DFTRACER_VARIORUM_ESMI_INCLUDE_DIR NAMES e_smi/e_smi.h)
      find_library(DFTRACER_VARIORUM_ESMI_LIBRARY NAMES e_smi64)
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
  if(DFTRACER_VARIORUM_ROCM_INCLUDE_DIR AND DFTRACER_VARIORUM_ROCM_LIBRARY)
    get_filename_component(_rocm_prefix
      "${DFTRACER_VARIORUM_ROCM_INCLUDE_DIR}" DIRECTORY)
    list(APPEND _args "-DROCM_DIR=${_rocm_prefix}")
    set(_amd_gpu ON)
  endif()

  # NVIDIA GPUs, through NVML. variorum looks for libnvml.so specifically, which
  # is not the name the driver ships (libnvidia-ml.so), so this only turns on
  # when a layout variorum can consume is present -- typically a CUDA install
  # where the stub has been named for it, or an explicit -DNVML_DIR.
  set(_nvidia_gpu OFF)
  if(NVML_DIR)
    list(APPEND _args "-DNVML_DIR=${NVML_DIR}")
    set(_nvidia_gpu ON)
  else()
    find_path(DFTRACER_VARIORUM_NVML_INCLUDE_DIR NAMES nvml.h)
    find_library(DFTRACER_VARIORUM_NVML_LIBRARY NAMES nvml)
    if(DFTRACER_VARIORUM_NVML_INCLUDE_DIR AND DFTRACER_VARIORUM_NVML_LIBRARY)
      get_filename_component(_nvml_prefix
        "${DFTRACER_VARIORUM_NVML_INCLUDE_DIR}" DIRECTORY)
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
