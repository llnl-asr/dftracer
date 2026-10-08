# ###############################################################
# Utilities
# ###############################################################

# A handy macro to add the current source directory to a local
# filename. To be used for creating a list of sources.
macro(set_full_path VAR)
  unset(__tmp_names)

  foreach(filename ${ARGN})
    list(APPEND __tmp_names "${CMAKE_CURRENT_SOURCE_DIR}/${filename}")
  endforeach()

  set(${VAR} "${__tmp_names}")
endmacro()

# A function to get a string of spaces. Useful for formatting output.
function(dftracer_get_space_string OUTPUT_VAR LENGTH)
  set(_curr_length 0)
  set(_out_str "")

  while(${_curr_length} LESS ${LENGTH})
    string(APPEND _out_str " ")
    math(EXPR _curr_length "${_curr_length} + 1")
  endwhile()

  set(${OUTPUT_VAR} "${_out_str}" PARENT_SCOPE)
endfunction()

# This computes the maximum length of the things given in "ARGN"
# interpreted as simple strings.
macro(dftracer_get_max_str_length OUTPUT_VAR)
  set(${OUTPUT_VAR} 0)

  foreach(var ${ARGN})
    string(LENGTH "${var}" _var_length)

    if(_var_length GREATER _max_length)
      set(${OUTPUT_VAR} ${_var_length})
    endif()
  endforeach()
endmacro()

# Check to see if we are in a git repo
find_program(__GIT_EXECUTABLE git)
mark_as_advanced(__GIT_EXECUTABLE)

if(__GIT_EXECUTABLE)
  execute_process(
    COMMAND ${__GIT_EXECUTABLE} rev-parse --is-inside-work-tree
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE __BUILDING_FROM_GIT_SOURCES
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(__BUILDING_FROM_GIT_SOURCES)
    # Get the git version so that we can embed it into the executable
    execute_process(
      COMMAND ${__GIT_EXECUTABLE} rev-parse --show-toplevel
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE __GIT_TOPLEVEL_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
      COMMAND ${__GIT_EXECUTABLE} rev-parse --git-dir
      WORKING_DIRECTORY "${__GIT_TOPLEVEL_DIR}"
      OUTPUT_VARIABLE __GIT_GIT_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
      COMMAND ${__GIT_EXECUTABLE} --git-dir "${__GIT_GIT_DIR}" describe
      --abbrev=7 --always --dirty --tags
      WORKING_DIRECTORY "${__GIT_TOPLEVEL_DIR}"
      OUTPUT_VARIABLE __GIT_DESCRIBE_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE)

    set(DFTRACER_GIT_VERSION "${__GIT_DESCRIBE_VERSION}"
      CACHE STRING "DFTRACER's version string as told by git.")
  endif(__BUILDING_FROM_GIT_SOURCES)
endif(__GIT_EXECUTABLE)

# ###############################################################
# Configuration summary
# ###############################################################

# This creates a formatted string that contains a list of variables,
# one per line, with their values interpreted as TRUE or FALSE. The
# purpose is to provide uniform output, rather than an odd mixture of
# "1", "0", "ON", "OFF", "TRUE" and "FALSE".
macro(append_str_tf STRING_VAR)
  dftracer_get_max_str_length(_max_length ${ARGN})
  math(EXPR _max_length "${_max_length} + 2")

  foreach(var ${ARGN})
    string(LENGTH "${var}" _var_length)
    math(EXPR _num_spaces "${_max_length} - ${_var_length}")
    dftracer_get_space_string(_spaces ${_num_spaces})

    if(${var})
      set(${var} "TRUE")
      string(APPEND ${STRING_VAR} "  ${var}:" "${_spaces}" "TRUE\n")
    else()
      set(${var} "FALSE")
      string(APPEND ${STRING_VAR} "  ${var}:" "${_spaces}" "FALSE\n")
    endif()
  endforeach()
endmacro()

# ###############################################################
# Install Public includes
# ###############################################################

# Set policy for normalized install paths (CMake 3.31+)
if(POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()

function(dftracer_install_headers public_headers)
  message("-- [${PROJECT_NAME}] " "installing headers ${public_headers}")

  foreach(header ${public_headers})
    file(RELATIVE_PATH header_file_path "${PROJECT_SOURCE_DIR}/src" "${header}")
    message("-- [${PROJECT_NAME}] " "installing header ${header_file_path}")
    get_filename_component(header_directory_path "${header_file_path}" DIRECTORY)
    install(
      FILES ${header}
      DESTINATION "include/${header_directory_path}"
    )
    file(COPY ${header}
      DESTINATION "${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/${header_directory_path}")
  endforeach()
endfunction()

# ###############################################################
# External Project configurations
# ###############################################################
include(ExternalProject)

function(dftracer_install_external_project name version var_name url tag install_prefix configure_args)
  find_package(${name} ${version} QUIET)
  set(found_var ${name}_FOUND)

  if(${${found_var}})
    set(include_var ${${var_name}_INCLUDE_DIRS} ${${var_name}_INCLUDE_DIR})
    set(library_var ${${var_name}_LIBRARY_DIRS})
    include_directories(${include_var})
    link_directories(${library_var})
    message(STATUS "[${PROJECT_NAME}] found dependency already installed ${name} with include ${include_var} and library ${library_var}")
  else()
    ExternalProject_Add(
      ${name}
      PREFIX ${CMAKE_BINARY_DIR}
      GIT_REPOSITORY ${url}
      GIT_TAG ${tag}
      TIMEOUT 10
      CMAKE_ARGS
      "-DCMAKE_INSTALL_PREFIX=${install_prefix}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      ${configure_args}
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel
      # --prefix rather than the install target: a sub-build directory left over
      # from an earlier run keeps its cached CMAKE_INSTALL_PREFIX even when the
      # configure step is skipped, and would install outside the prefix.
      INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --prefix ${install_prefix}
      LOG_DOWNLOAD ON
    )
    include_directories(${install_prefix}/include)
    link_directories(${install_prefix}/lib)
    link_directories(${install_prefix}/lib64)
    include_directories(${CMAKE_BINARY_DIR}/src/${name}/include)
  endif()
endfunction()

# Backward-compatible name; prefer dftracer_install_external_project in new code.
# Forward each positional arg explicitly (not `${ARGV}`) so empty trailing
# args like an empty `configure_args` are preserved rather than dropped.
function(install_external_project name version var_name url tag install_prefix configure_args)
  dftracer_install_external_project(
    "${name}" "${version}" "${var_name}" "${url}" "${tag}"
    "${install_prefix}" "${configure_args}"
  )
endfunction()

# ###############################################################
# Debug configurations
# ###############################################################
function(dftracer_debug_config target_list)
  foreach(target ${target_list})
    target_compile_definitions(${target} PUBLIC DFTRACER_DEBUG)
    target_compile_options(${target} PRIVATE -g -O0)
  endforeach()
endfunction()

function(print_all_variables)
  message(STATUS "CMake Variables:")
  get_cmake_property(_variableNames VARIABLES)
  list(SORT _variableNames)

  foreach(_variableName ${_variableNames})
    message(STATUS "${_variableName}=${${_variableName}}")
  endforeach()
endfunction()