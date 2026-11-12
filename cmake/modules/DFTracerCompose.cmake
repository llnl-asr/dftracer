# Composable DFTracer: the core and each optional module are separate static
# archives, and libdftracer_core.so is only linked from them.
#
#   dftracer_core_base[_dbg]           everything that is always built
#   dftracer_module_<name>[_dbg]       one per optional module the build has
#   dftracer_core[_dbg]                composed: core + modules, whole-archive
#
# The core reaches the modules through weak hooks (see
# src/dftracer/core/common/module_hooks.h), so any subset of the modules links
# against the same core archive. The archives are installed under
# <libdir>/dftracer/compose with the project in cmake/compose, which links a
# libdftracer_core.so from them without compiling DFTracer again.
#
# This file is used both by the main build and by the installed compose
# project, so it must not depend on anything else of DFTracer's CMake.

# Turns link items into what the linker sees on this system: imported targets
# become their library files, INTERFACE targets their own link items and
# directories, recursively. The result can be written into a file and used by
# a project that has none of these targets.
function(dftracer_resolve_link_items out_var)
  set(_resolved "")
  set(_queue ${ARGN})
  set(_seen "")
  while(_queue)
    list(POP_FRONT _queue _item)
    if(_item STREQUAL "")
      continue()
    endif()
    # $<LINK_ONLY:x> and $<BUILD_INTERFACE:x> wrap a plain item.
    if(_item MATCHES "^\\$<(LINK_ONLY|BUILD_INTERFACE):(.*)>$")
      set(_item "${CMAKE_MATCH_2}")
    elseif(_item MATCHES "^\\$<")
      continue()
    endif()
    if(TARGET ${_item})
      if(_item IN_LIST _seen)
        continue()
      endif()
      list(APPEND _seen ${_item})
      get_target_property(_aliased ${_item} ALIASED_TARGET)
      if(_aliased)
        list(PREPEND _queue ${_aliased})
        continue()
      endif()
      get_target_property(_type ${_item} TYPE)
      get_target_property(_imported ${_item} IMPORTED)
      if(_imported AND NOT _type STREQUAL "INTERFACE_LIBRARY")
        set(_location "")
        foreach(_prop IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION
                      IMPORTED_LOCATION_RELWITHDEBINFO IMPORTED_LOCATION_NOCONFIG
                      IMPORTED_LOCATION_DEBUG)
          get_target_property(_location ${_item} ${_prop})
          if(_location)
            break()
          endif()
        endforeach()
        if(NOT _location)
          get_target_property(_configs ${_item} IMPORTED_CONFIGURATIONS)
          if(_configs)
            list(GET _configs 0 _config)
            get_target_property(_location ${_item} IMPORTED_LOCATION_${_config})
          endif()
        endif()
        if(_location)
          list(APPEND _resolved ${_location})
        endif()
      endif()
      get_target_property(_dirs ${_item} INTERFACE_LINK_DIRECTORIES)
      if(_dirs)
        foreach(_dir IN LISTS _dirs)
          list(APPEND _resolved "-L${_dir}")
        endforeach()
      endif()
      get_target_property(_deps ${_item} INTERFACE_LINK_LIBRARIES)
      if(_deps)
        list(PREPEND _queue ${_deps})
      endif()
    else()
      list(APPEND _resolved ${_item})
    endif()
  endwhile()
  set(${out_var} "${_resolved}" PARENT_SCOPE)
endfunction()

# dftracer_bundle_link_items(<out_items> <out_archives> <dir> <items>...)
#
# Static archives among link items (brahma's libbrahma.a, for one) have to
# travel with the compose tree: they are linked into libdftracer_core, and the
# machine that composes it does not have the build's prefix. Each one is
# listed in <out_archives> for installing into <dir> (relative to the compose
# root), and named in <out_items> as ${DFTRACER_COMPOSE_ROOT}/<dir>/<name>,
# which the compose project resolves. Everything else is kept as it is.
function(dftracer_bundle_link_items out_items out_archives dir)
  set(_items "")
  set(_archives "")
  foreach(_item IN LISTS ARGN)
    if(_item MATCHES "\\.a$" AND EXISTS "${_item}")
      list(APPEND _archives "${_item}")
      get_filename_component(_name "${_item}" NAME)
      list(APPEND _items "\${DFTRACER_COMPOSE_ROOT}/${dir}/${_name}")
    else()
      list(APPEND _items "${_item}")
    endif()
  endforeach()
  set(${out_items} "${_items}" PARENT_SCOPE)
  set(${out_archives} "${_archives}" PARENT_SCOPE)
endfunction()

# The library major.minor from a dotted version ("4.1.6" -> "4.1").
function(dftracer_major_minor out_var version)
  if(version MATCHES "^([0-9]+)\\.([0-9]+)")
    set(${out_var} "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}" PARENT_SCOPE)
  elseif(version MATCHES "^([0-9]+)")
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(${out_var} "unknown" PARENT_SCOPE)
  endif()
endfunction()

# The first match of `regex` in `header` (searched in `dirs`), or "".
function(dftracer_header_match out_var header regex)
  set(_value "")
  # The first directory that has the header is the one the compiler uses;
  # stop there even if it does not match. Going on would read another
  # install's header further down the include path (hwloc 1.x has no
  # HWLOC_VERSION, and the search found the OS's hwloc 2 in /usr/include).
  foreach(_dir IN LISTS ARGN)
    if(EXISTS "${_dir}/${header}")
      file(STRINGS "${_dir}/${header}" _lines REGEX "${regex}")
      if(_lines)
        list(GET _lines 0 _line)
        string(REGEX MATCH "${regex}" _ignored "${_line}")
        set(_value "${CMAKE_MATCH_1}")
      endif()
      break()
    endif()
  endforeach()
  set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

# dftracer_mpi_abi(<out_var> <impl>)
#
# The MPI ABI family of an implementation as mpi_detect.c names it (lower
# case): mpich-abi for MPICH and its derivatives (int handles), openmpi-abi
# for OpenMPI (pointer handles), unknown-abi otherwise.
function(dftracer_mpi_abi out_var impl)
  if(impl MATCHES "^(mpich|mvapich|craympich|intelmpi)$")
    set(${out_var} "mpich-abi" PARENT_SCOPE)
  elseif(impl STREQUAL "openmpi")
    set(${out_var} "openmpi-abi" PARENT_SCOPE)
  else()
    set(${out_var} "unknown-abi" PARENT_SCOPE)
  endif()
endfunction()

# dftracer_module_variant(<module> <out_var>)
#
# The variant key of a module: the library it was compiled against, at the
# granularity where that library's interface changes.
#
#   mpi    mpi-<impl>-<major.minor>             mpi-openmpi-4.1
#   hdf5   hdf5-<major.minor>-serial            hdf5-1.14-serial
#          hdf5-<major.minor>-par+<mpi abi>     hdf5-1.14-par+openmpi-abi
#   papi   papi-<major.minor>                   papi-7.0
#   hwloc  hwloc-<major>                        hwloc-2
#   hip    rocprof-<major.minor>                rocprof-0.6
#   cuda   cupti-<cuda major.minor>             cupti-12.8
#
# Also sets DFTRACER_PAPI_VERSION / DFTRACER_HWLOC_VERSION in the caller,
# which their find modules do not report.
function(dftracer_module_variant module out_var)
  set(_key "${module}-unknown")
  if(module STREQUAL "mpi" OR module STREQUAL "hdf5")
    string(TOLOWER "${DFTRACER_MPI_IMPL_NAME}" _impl)
    if(_impl STREQUAL "")
      set(_impl "unknown")
    endif()
    dftracer_major_minor(_mpi_version "${DFTRACER_MPI_PACKAGE_VERSION}")
    set(_mpi "${_impl}-${_mpi_version}")
  endif()
  if(module STREQUAL "mpi")
    set(_key "mpi-${_mpi}")
  elseif(module STREQUAL "hdf5")
    dftracer_major_minor(_version "${HDF5_VERSION}")
    if(HDF5_IS_PARALLEL)
      # HDF5's parallel API is the same with every MPI; what the module
      # depends on is the MPI's handle types, which it forwards by value
      # (H5Pset_fapl_mpio). MPICH and its derivatives use int handles,
      # OpenMPI pointers, so one build serves each family.
      dftracer_mpi_abi(_abi "${_impl}")
      set(_key "hdf5-${_version}-par+${_abi}")
    else()
      set(_key "hdf5-${_version}-serial")
    endif()
  elseif(module STREQUAL "papi")
    dftracer_header_match(_numbers papi.h
      "#define[ \t]+PAPI_VERSION[ \t]+PAPI_VERSION_NUMBER\\(([0-9, ]+)\\)"
      ${PAPI_INCLUDE_DIRS})
    string(REPLACE " " "" _numbers "${_numbers}")
    string(REPLACE "," "." _version "${_numbers}")
    set(DFTRACER_PAPI_VERSION "${_version}" PARENT_SCOPE)
    dftracer_major_minor(_version "${_version}")
    set(_key "papi-${_version}")
  elseif(module STREQUAL "hwloc")
    # The include path in the compiler's order: pkg-config leaves system
    # directories out of HWLOC_INCLUDE_DIRS (CMake 3.24 left it empty for an
    # hwloc in /usr, and the variant came out hwloc-unknown), so add its
    # includedir and the compiler's own directories after it.
    set(_hwloc_dirs ${HWLOC_INCLUDE_DIRS} ${HWLOC_INCLUDEDIR}
        ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES})
    dftracer_header_match(_version hwloc/autogen/config.h
      "#define[ \t]+HWLOC_VERSION[ \t]+\"([0-9.]+)" ${_hwloc_dirs})
    if(_version STREQUAL "")
      # hwloc 1.x has no HWLOC_VERSION header macro, only the API version
      # (0x00MMmmrr) in hwloc.h. That is the API the headers declare, not the
      # release (1.11.13 declares 0x00010b06), so keep only its major.
      dftracer_header_match(_api hwloc.h
        "#define[ \t]+HWLOC_API_VERSION[ \t]+0x([0-9a-fA-F]+)" ${_hwloc_dirs})
      if(_api MATCHES "^0*([0-9a-fA-F]?[0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])$")
        math(EXPR _version "0x${CMAKE_MATCH_1}")
      endif()
    endif()
    set(DFTRACER_HWLOC_VERSION "${_version}" PARENT_SCOPE)
    string(REGEX MATCH "^[0-9]+" _major "${_version}")
    if(_major STREQUAL "")
      set(_major "unknown")
    endif()
    set(_key "hwloc-${_major}")
  elseif(module STREQUAL "hip")
    set(_key "rocprof-${DFTRACER_ROCPROFILER_VERSION_MAJOR}.${DFTRACER_ROCPROFILER_VERSION_MINOR}")
  elseif(module STREQUAL "cuda")
    dftracer_major_minor(_version "${CUPTI_CUDA_VERSION}")
    set(_key "cupti-${_version}")
  endif()
  set(${out_var} "${_key}" PARENT_SCOPE)
endfunction()

# dftracer_add_module(NAME <name> VARIANT <key> SOURCES ...
#                     [LIBRARIES ...] [DEFINITIONS ...] [INCLUDES ...]
#                     [VERSIONS k=v ...])
#
# Builds dftracer_module_<name> and its _dbg twin as position-independent
# static archives. VARIANT names the library build it was compiled against
# (see dftracer_module_variant); archives are installed under it, so modules
# built against different versions sit side by side. INCLUDES come first on the
# module's include path (its own brahma build, ahead of the core's).
function(dftracer_add_module)
  cmake_parse_arguments(M "" "NAME;VARIANT"
                        "SOURCES;LIBRARIES;DEFINITIONS;INCLUDES;VERSIONS" ${ARGN})
  foreach(_suffix "" "_dbg")
    set(_target ${PROJECT_NAME}_module_${M_NAME}${_suffix})
    add_library(${_target} STATIC ${M_SOURCES})
    set_target_properties(${_target} PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      LINKER_LANGUAGE CXX)
    target_compile_definitions(${_target} PRIVATE ${M_DEFINITIONS})
    if(M_INCLUDES)
      target_include_directories(${_target} BEFORE PRIVATE ${M_INCLUDES})
    endif()
    # For the libraries' usage requirements (include directories); a static
    # archive's private libraries are linked into whatever links it.
    target_link_libraries(${_target} PRIVATE ${M_LIBRARIES})
    if(_suffix STREQUAL "")
      target_compile_definitions(${_target} PRIVATE DFTRACER_LOGGER_LEVEL_ERROR)
    endif()
  endforeach()
  dftracer_resolve_link_items(_resolved ${M_LIBRARIES})
  set_property(GLOBAL APPEND PROPERTY DFTRACER_MODULES ${M_NAME})
  set_property(GLOBAL PROPERTY DFTRACER_MODULE_${M_NAME}_VARIANT "${M_VARIANT}")
  set_property(GLOBAL PROPERTY DFTRACER_MODULE_${M_NAME}_LIBRARIES "${M_LIBRARIES}")
  set_property(GLOBAL PROPERTY DFTRACER_MODULE_${M_NAME}_LINK "${_resolved}")
  set_property(GLOBAL PROPERTY DFTRACER_MODULE_${M_NAME}_VERSIONS "${M_VERSIONS}")
  message(STATUS "[DFTRACER] module ${M_NAME}: ${M_VARIANT}")
endfunction()

# Writes the generated source that records which modules a library was
# composed with (dftracer_composition_modules in module_hooks.h).
function(dftracer_write_composition file)
  string(REPLACE ";" "," _list "${ARGN}")
  # Valid C and C++: the compose project links with the C driver when it
  # brings its own C++ runtime (DFTRACER_COMPOSE_RUNTIME).
  file(CONFIGURE OUTPUT "${file}" CONTENT [[
/* Generated: the optional modules this DFTracer was composed with.
   See src/dftracer/core/common/module_hooks.h. */
#ifdef __cplusplus
extern "C"
#endif
__attribute__((weak, visibility("hidden"))) const char*
dftracer_composition_modules(void) {
  return "@_list@";
}
]] @ONLY)
endfunction()

# dftracer_compose_library(<target> BASE <archive> [MODULES <archive>...]
#                          [MODULE_NAMES <name>...] [LIBRARIES ...]
#                          [LANGUAGE C|CXX])
#
# Links the shared library <target> from the core archive and the module
# archives, whole-archive so every object and its constructors come along
# (weak hook references alone would not pull archive members in), plus the
# libraries they need. BASE and MODULES are targets or archive files.
# LANGUAGE C links with the C driver, which adds no C++ runtime of its own:
# the caller then supplies one (see the compose project).
function(dftracer_compose_library target)
  cmake_parse_arguments(C "" "BASE;LANGUAGE" "MODULES;MODULE_NAMES;LIBRARIES" ${ARGN})
  if(NOT C_LANGUAGE)
    set(C_LANGUAGE CXX)
  endif()
  if(C_LANGUAGE STREQUAL "C")
    set(_composition "${CMAKE_CURRENT_BINARY_DIR}/${target}_composition.c")
  else()
    set(_composition "${CMAKE_CURRENT_BINARY_DIR}/${target}_composition.cpp")
  endif()
  dftracer_write_composition("${_composition}" ${C_MODULE_NAMES})
  add_library(${target} SHARED "${_composition}")
  set_target_properties(${target} PROPERTIES LINKER_LANGUAGE ${C_LANGUAGE})
  target_link_libraries(${target} PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,${C_BASE}>")
  foreach(_module IN LISTS C_MODULES)
    target_link_libraries(${target} PRIVATE
      "$<LINK_LIBRARY:WHOLE_ARCHIVE,${_module}>")
  endforeach()
  target_link_libraries(${target} PRIVATE ${C_LIBRARIES})
endfunction()
