include(FindPackageHandleStandardArgs)

set(PAPI_DIR "" CACHE PATH "Installation directory of PAPI")
set(PAPI_INCDIR "" CACHE PATH "Directory containing papi.h")
set(PAPI_LIBDIR "" CACHE PATH "Directory containing libpapi")

set(_PAPI_HINTS)
foreach(_var PAPI_DIR PAPI_INCDIR PAPI_LIBDIR)
  if(${_var})
    list(APPEND _PAPI_HINTS "${${_var}}")
  endif()
endforeach()

foreach(_env_var PAPI_DIR PAPI_INCDIR PAPI_LIBDIR)
  if(DEFINED ENV{${_env_var}} AND NOT "$ENV{${_env_var}}" STREQUAL "")
    list(APPEND _PAPI_HINTS "$ENV{${_env_var}}")
  endif()
endforeach()

list(REMOVE_DUPLICATES _PAPI_HINTS)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_search_module(PC_PAPI QUIET papi)
endif()

find_path(PAPI_INCLUDE_DIRS
  NAMES papi.h
  HINTS ${_PAPI_HINTS} ${PC_PAPI_INCLUDEDIR} ${PC_PAPI_INCLUDE_DIRS}
  PATH_SUFFIXES include)

find_library(PAPI_LIBRARIES
  NAMES papi
  HINTS ${_PAPI_HINTS} ${PC_PAPI_LIBDIR} ${PC_PAPI_LIBRARY_DIRS}
  PATH_SUFFIXES lib lib64)

if(PAPI_LIBRARIES)
  get_filename_component(PAPI_LIBRARY_DIRS "${PAPI_LIBRARIES}" DIRECTORY)
endif()

find_package_handle_standard_args(PAPI DEFAULT_MSG PAPI_INCLUDE_DIRS PAPI_LIBRARIES)

mark_as_advanced(PAPI_DIR PAPI_INCDIR PAPI_LIBDIR PAPI_INCLUDE_DIRS PAPI_LIBRARIES PAPI_LIBRARY_DIRS)