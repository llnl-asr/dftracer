# Checks that every installed public header compiles on its own.
#
# A wheel or `make install` ships only the headers in
# DFTRACER_CORE_PUBLIC_INCLUDE. Private dependencies (cpp-logger, brahma,
# gotcha, yaml-cpp, ...) are not shipped, so a public header that includes
# one breaks every downstream C/C++ build. This script copies the public
# headers into an empty include tree and compiles each one with only that
# tree on the include path.
#
# Inputs (-D):
#   HEADERS      ;-list of absolute public header paths
#   SOURCE_ROOT  directory the include paths are relative to (<src>/include)
#   STAGE_DIR    scratch directory, wiped on every run
#   CXX_COMPILER C++ compiler
#   C_COMPILER   C compiler
#   C_HEADERS    ;-list of headers (relative to SOURCE_ROOT) that must also
#                compile as C

foreach(var HEADERS SOURCE_ROOT STAGE_DIR CXX_COMPILER C_COMPILER)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "check_public_headers: ${var} is not set")
  endif()
endforeach()

file(REMOVE_RECURSE "${STAGE_DIR}")
set(stage_include "${STAGE_DIR}/include")
file(MAKE_DIRECTORY "${stage_include}")

set(relative_headers)
foreach(header ${HEADERS})
  file(RELATIVE_PATH rel "${SOURCE_ROOT}" "${header}")
  if(rel MATCHES "^\\.\\./")
    message(FATAL_ERROR "check_public_headers: ${header} is not under ${SOURCE_ROOT}")
  endif()
  get_filename_component(rel_dir "${rel}" DIRECTORY)
  file(COPY "${header}" DESTINATION "${stage_include}/${rel_dir}")
  list(APPEND relative_headers "${rel}")
endforeach()

set(failures 0)

function(compile_header compiler lang std rel)
  string(MAKE_C_IDENTIFIER "${rel}_${lang}" stem)
  set(tu "${STAGE_DIR}/${stem}.${lang}")
  file(WRITE "${tu}" "#include <${rel}>\nint main(void) { return 0; }\n")
  execute_process(
    COMMAND "${compiler}" ${std} -fsyntax-only -I "${stage_include}" "${tu}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out)
  if(rc EQUAL 0)
    message(STATUS "PASS [${lang}] ${rel}")
  else()
    message(STATUS "FAIL [${lang}] ${rel}\n${out}")
    math(EXPR n "${failures} + 1")
    set(failures ${n} PARENT_SCOPE)
  endif()
endfunction()

foreach(rel ${relative_headers})
  compile_header("${CXX_COMPILER}" cpp -std=c++17 "${rel}")
endforeach()
foreach(rel ${C_HEADERS})
  compile_header("${C_COMPILER}" c -std=c99 "${rel}")
endforeach()

if(failures GREATER 0)
  message(FATAL_ERROR
    "${failures} public header check(s) failed: an installed header includes "
    "something that is not installed with it.")
endif()
