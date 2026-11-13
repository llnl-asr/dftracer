# Links libdftracer_core.so and libdftracer_core_dbg.so from the archives an
# installed DFTracer keeps next to this file, without compiling DFTracer:
# only a one-line source naming the modules is compiled.
#
#   cmake -S <prefix>/<libdir>/dftracer/compose -B build \
#         -DCMAKE_INSTALL_PREFIX=<prefix> [-DDFTRACER_COMPOSE_MODULES=...]
#   cmake --build build && cmake --install build
#
# DFTRACER_COMPOSE_MODULES  which modules to link:
#     detect (default)  each module whose library this machine has, in the
#                       variant that matches it (DFTracerDetect.cmake)
#     all               every module here (one variant each), linked against
#                       the libraries recorded when it was built
#     none              the core alone
#     mpi,papi,...      those modules, matched to this machine like detect;
#                       the others are recorded as off
# DFTRACER_COMPOSE_<MODULE> the variant of <MODULE> to link, e.g.
#                           -DDFTRACER_COMPOSE_MPI=mpi-openmpi-4.1, instead of
#                           the one detected (or, for all, the only one here).
# DFTRACER_COMPOSE_RUNTIME  which C++ runtime to link (runtime/ holds the one
#                           the archives were built with):
#     nonshared (default when runtime/ has libstdc++_nonshared.a) the system's
#         libstdc++.so.6 plus the toolset's libstdc++_nonshared.a, linked in
#         and exported as the prebuilt library did -- the manylinux model: one
#         C++ runtime per process, and anything linked against the prebuilt
#         library (the Python extension) still finds what it needs
#     static  all of runtime/'s libstdc++.a, hidden, so the library needs no
#         libstdc++.so at all
#     compiler  whatever the compiler links by default
#   nonshared and static link with the C driver, so a compiler with an older
#   runtime can compose the archives.
#
# composition.txt (build dir, installed next to the library) lists one line
# per module: "<module> <variant> linked|missing|stale|absent". A missing
# variant is one this machine needs but the tree lacks, a stale one was built
# against other core headers than the core archive here (their interface
# hashes differ): build it from source and compose again.
cmake_minimum_required(VERSION 3.24)
project(dftracer_compose LANGUAGES C CXX)

set(_root "${CMAKE_CURRENT_LIST_DIR}")
# Archives that travel with the tree are named relative to this in the
# generated core.cmake and component.cmake files.
set(DFTRACER_COMPOSE_ROOT "${_root}")
list(APPEND CMAKE_MODULE_PATH "${_root}")
include(DFTracerCompose)
include(DFTracerDetect)
include("${_root}/core/core.cmake")

# A library recorded at build time by an absolute path that is not on this
# machine was bundled with the install (a wheel keeps gotcha, yaml-cpp, libuv,
# libdeflate in the lib directory this tree sits in): link the copy there.
get_filename_component(_libdir "${_root}/../.." ABSOLUTE)
function(_dftracer_relocate out_var)
  set(_out "")
  foreach(_item IN LISTS ARGN)
    if(IS_ABSOLUTE "${_item}" AND NOT EXISTS "${_item}")
      get_filename_component(_name "${_item}" NAME)
      string(REGEX REPLACE "\\.so.*$" "" _stem "${_name}")
      file(GLOB _copies "${_libdir}/${_stem}.so*")
      if(_copies)
        list(SORT _copies)
        list(GET _copies 0 _item)
      elseif(_item MATCHES "libstdc\\+\\+\\.so")
        continue()
      else()
        message(WARNING "[DFTRACER] ${_item} is not on this machine nor in ${_libdir}")
      endif()
    elseif(_item MATCHES "^-L(/.*)$")
      # A search directory of the build machine.
      if(NOT EXISTS "${CMAKE_MATCH_1}")
        continue()
      endif()
    endif()
    list(APPEND _out "${_item}")
  endforeach()
  set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()
# The core's interface hash (tools/ci/source_hash.py), when it has one.
set(_core_interface "")
if(EXISTS "${_root}/core/interface_hash")
  file(STRINGS "${_root}/core/interface_hash" _core_interface LIMIT_COUNT 1)
endif()
_dftracer_relocate(DFTRACER_CORE_LINK ${DFTRACER_CORE_LINK})
_dftracer_relocate(DFTRACER_CORE_LINK_DBG ${DFTRACER_CORE_LINK_DBG})

set(DFTRACER_COMPOSE_MODULES "detect" CACHE STRING
    "Modules to link: detect, all, none, or a comma-separated list")
if(EXISTS "${_root}/runtime/libstdc++_nonshared.a")
  set(_default_runtime nonshared)
elseif(EXISTS "${_root}/runtime/libstdc++.a")
  set(_default_runtime static)
else()
  set(_default_runtime compiler)
endif()
set(DFTRACER_COMPOSE_RUNTIME "${_default_runtime}" CACHE STRING
    "C++ runtime to link: nonshared, static or compiler")

set(_available "")
if(EXISTS "${_root}/modules")
  file(GLOB _entries RELATIVE "${_root}/modules" "${_root}/modules/*")
  foreach(_entry IN LISTS _entries)
    if(IS_DIRECTORY "${_root}/modules/${_entry}")
      list(APPEND _available ${_entry})
    endif()
  endforeach()
  list(SORT _available)
endif()

# Every module DFTracer has; detect looks for each one's library, so a module
# this machine could use but the tree has no archives for is reported too.
set(_known mpi hdf5 papi hwloc hip cuda)

set(_detect ON)
if(DFTRACER_COMPOSE_MODULES STREQUAL "detect")
  set(_selected ${_known})
elseif(DFTRACER_COMPOSE_MODULES STREQUAL "all")
  set(_selected ${_available})
  set(_detect OFF)
elseif(DFTRACER_COMPOSE_MODULES STREQUAL "none" OR DFTRACER_COMPOSE_MODULES STREQUAL "")
  set(_selected "")
else()
  string(REPLACE "," ";" _selected "${DFTRACER_COMPOSE_MODULES}")
endif()

set(_names "")
set(_archives "")
set(_archives_dbg "")
set(_links "")
set(_record "")
foreach(_module IN LISTS _selected)
  if(NOT _module IN_LIST _available AND NOT (_detect AND _module IN_LIST _known))
    message(FATAL_ERROR "[DFTRACER] module '${_module}' is not available here "
                        "(available: ${_available})")
  endif()
  string(TOUPPER "${_module}" _upper)
  set(DFTRACER_COMPOSE_${_upper} "" CACHE STRING "Variant of the ${_module} module")
  set(_variants "")
  if(EXISTS "${_root}/modules/${_module}")
    file(GLOB _variants RELATIVE "${_root}/modules/${_module}"
         "${_root}/modules/${_module}/*")
  endif()

  set(_system_libs "")
  set(_use_recorded ON)
  if(DFTRACER_COMPOSE_${_upper})
    set(_variant "${DFTRACER_COMPOSE_${_upper}}")
    # A variant chosen by name still links this machine's library when it
    # has one: the paths recorded at build time are the build machine's.
    if(_detect)
      dftracer_detect_module(${_module} _found _detected _system_libs)
      if(_found)
        set(_use_recorded OFF)
        if(NOT _detected STREQUAL _variant)
          message(WARNING "[DFTRACER] ${_module}: linking ${_variant} as asked, "
                          "but this machine's library is ${_detected}")
        endif()
      endif()
    endif()
  elseif(_detect)
    dftracer_detect_module(${_module} _found _variant _system_libs)
    if(NOT _found)
      message(STATUS "[DFTRACER] ${_module}: not on this machine")
      string(APPEND _record "${_module} - absent\n")
      continue()
    endif()
    set(_use_recorded OFF)
  else()
    list(LENGTH _variants _count)
    if(NOT _count EQUAL 1)
      message(FATAL_ERROR "[DFTRACER] ${_count} variants of ${_module} "
                          "(${_variants}); choose one with -DDFTRACER_COMPOSE_${_upper}=")
    endif()
    list(GET _variants 0 _variant)
  endif()

  if(NOT _variant IN_LIST _variants)
    if(_detect AND NOT DFTRACER_COMPOSE_${_upper})
      message(STATUS "[DFTRACER] ${_module}: this machine needs ${_variant}, "
                     "which is not here (have: ${_variants}); build it from source")
      string(APPEND _record "${_module} ${_variant} missing\n")
      continue()
    endif()
    message(FATAL_ERROR "[DFTRACER] no ${_module} variant ${_variant} "
                        "(available: ${_variants})")
  endif()

  set(_dir "${_root}/modules/${_module}/${_variant}")
  # Only a module built against the same core headers links with this core.
  if(_core_interface AND EXISTS "${_dir}/component.json")
    file(READ "${_dir}/component.json" _json)
    string(JSON _interface ERROR_VARIABLE _no_hash GET "${_json}" interface_hash)
    if(NOT _no_hash AND NOT _interface STREQUAL _core_interface)
      message(STATUS "[DFTRACER] ${_module}: ${_variant} was built against other "
                     "core headers than this core; build it from source")
      string(APPEND _record "${_module} ${_variant} stale\n")
      continue()
    endif()
  endif()
  include("${_dir}/component.cmake")
  # Archives that travel with the module (its brahma) always come from the
  # tree; the library itself is the one found on this machine, or the one
  # recorded at build time when not detecting.
  set(_module_links "")
  foreach(_item IN LISTS DFTRACER_COMPONENT_LINK)
    if(_item MATCHES "\\.a$" OR _use_recorded)
      list(APPEND _module_links "${_item}")
    endif()
  endforeach()
  list(APPEND _module_links ${_system_libs})
  list(APPEND _names ${_module})
  list(APPEND _archives "${_dir}/libdftracer_module_${_module}.a")
  list(APPEND _archives_dbg "${_dir}/libdftracer_module_${_module}_dbg.a")
  list(APPEND _links ${_module_links})
  string(APPEND _record "${_module} ${_variant} linked\n")
  message(STATUS "[DFTRACER] composing ${_module}: ${_variant}")
endforeach()

# The C++ runtime: the one the archives were built with, linked in and hidden,
# or the compiler's own.
set(_runtime "")
set(_language CXX)
if(DFTRACER_COMPOSE_RUNTIME STREQUAL "nonshared")
  set(_language C)
  set(_runtime "${_root}/runtime/libstdc++_nonshared.a" -lstdc++ -lm -lpthread)
elseif(DFTRACER_COMPOSE_RUNTIME STREQUAL "static")
  set(_language C)
  set(_runtime
    "${_root}/runtime/libstdc++.a" "${_root}/runtime/libgcc_eh.a"
    "${_root}/runtime/libgcc.a"
    "-Wl,--exclude-libs,libstdc++.a:libgcc.a:libgcc_eh.a"
    -lm -lpthread)
endif()

foreach(_suffix "" "_dbg")
  set(_target dftracer_core${_suffix})
  if(_suffix STREQUAL "")
    set(_core_link ${DFTRACER_CORE_LINK})
    set(_modules ${_archives})
  else()
    set(_core_link ${DFTRACER_CORE_LINK_DBG})
    set(_modules ${_archives_dbg})
  endif()
  if(NOT DFTRACER_COMPOSE_RUNTIME STREQUAL "compiler")
    # The build's own C++ runtime is replaced by the one chosen above.
    list(FILTER _core_link EXCLUDE REGEX "libstdc\\+\\+\\.so")
  endif()
  dftracer_compose_library(${_target}
    BASE "${_root}/core/libdftracer_core_base${_suffix}.a"
    MODULES ${_modules}
    MODULE_NAMES ${_names} ${DFTRACER_CORE_BUILT_WITH}
    LIBRARIES ${_links} ${_core_link} ${_runtime}
    LANGUAGE ${_language})
  # Instrumentation the core was built with (coverage, sanitizers): empty for
  # a release build. A tree from an older build does not set it.
  if(DFTRACER_CORE_LINK_OPTIONS)
    target_link_options(${_target} PRIVATE ${DFTRACER_CORE_LINK_OPTIONS})
  endif()
  set_target_properties(${_target} PROPERTIES
    OUTPUT_NAME ${_target}
    VERSION "${DFTRACER_COMPOSE_VERSION}"
    SOVERSION "${DFTRACER_COMPOSE_VERSION}"
    BUILD_WITH_INSTALL_RPATH ON
    INSTALL_RPATH "${DFTRACER_COMPOSE_INSTALL_RPATH}"
    INSTALL_RPATH_USE_LINK_PATH ON)
  install(TARGETS ${_target} LIBRARY DESTINATION "${DFTRACER_COMPOSE_LIBDIR}")
endforeach()

# Modules left out of an explicit list (dftracer-compose --disable) or by
# none: recorded, so the composition says they are off rather than absent.
if(NOT DFTRACER_COMPOSE_MODULES STREQUAL "detect" AND NOT DFTRACER_COMPOSE_MODULES STREQUAL "all")
  foreach(_module IN LISTS _known)
    if(NOT _module IN_LIST _selected)
      string(APPEND _record "${_module} - off\n")
    endif()
  endforeach()
endif()
file(WRITE "${CMAKE_BINARY_DIR}/composition.txt" "${_record}")
install(FILES "${CMAKE_BINARY_DIR}/composition.txt"
        DESTINATION "${DFTRACER_COMPOSE_LIBDIR}/dftracer")
