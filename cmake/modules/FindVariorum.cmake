include(FindPackageHandleStandardArgs)

set(VARIORUM_DIR "" CACHE PATH "Installation directory of Variorum")
set(VARIORUM_INCDIR "" CACHE PATH "Directory containing variorum.h")
set(VARIORUM_LIBDIR "" CACHE PATH "Directory containing libvariorum")

set(_VARIORUM_HINTS)
foreach(_var VARIORUM_DIR VARIORUM_INCDIR VARIORUM_LIBDIR VARIORUM_ROOT)
  if(${_var})
    list(APPEND _VARIORUM_HINTS "${${_var}}")
  endif()
endforeach()

foreach(_env_var VARIORUM_DIR VARIORUM_INCDIR VARIORUM_LIBDIR VARIORUM_ROOT)
  if(DEFINED ENV{${_env_var}} AND NOT "$ENV{${_env_var}}" STREQUAL "")
    list(APPEND _VARIORUM_HINTS "$ENV{${_env_var}}")
  endif()
endforeach()

list(REMOVE_DUPLICATES _VARIORUM_HINTS)

find_path(VARIORUM_INCLUDE_DIRS
  NAMES variorum.h
  HINTS ${_VARIORUM_HINTS}
  PATH_SUFFIXES include)

find_library(VARIORUM_LIBRARIES
  NAMES variorum
  HINTS ${_VARIORUM_HINTS}
  PATH_SUFFIXES lib lib64)

if(VARIORUM_LIBRARIES)
  get_filename_component(VARIORUM_LIBRARY_DIRS "${VARIORUM_LIBRARIES}" DIRECTORY)
endif()

# variorum reports its version through variorum_get_current_version() at
# runtime, but VERSION lives in the source tree and is not installed, so a
# version constraint cannot be checked here. Only presence is.
find_package_handle_standard_args(Variorum DEFAULT_MSG
  VARIORUM_INCLUDE_DIRS VARIORUM_LIBRARIES)

mark_as_advanced(VARIORUM_DIR VARIORUM_INCDIR VARIORUM_LIBDIR
  VARIORUM_INCLUDE_DIRS VARIORUM_LIBRARIES VARIORUM_LIBRARY_DIRS)
