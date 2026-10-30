# An external project that defines its own event types at compile time and a
# provenance-style API (lineage) on them, built against an installed or
# build-tree dftracer package (-Ddftracer_DIR=.../lib64/cmake/dftracer).
# Built by dftracer's test/CMakeLists.txt; its apps are run as CTests and
# checked by test/check_prov_style_trace.py.
cmake_minimum_required(VERSION 3.16)
project(lineage LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

find_package(dftracer REQUIRED)
find_package(Threads REQUIRED)

add_library(lineage SHARED src/lineage.cpp)
target_include_directories(lineage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lineage PUBLIC dftracer_core)
target_compile_options(lineage PRIVATE -Wall -Wextra -pedantic -Werror)

add_executable(lineage_app_c app.c)
target_link_libraries(lineage_app_c lineage)
target_compile_options(lineage_app_c PRIVATE -Wall -Wextra -pedantic -Werror)
add_executable(lineage_app_cpp app.cpp)
target_link_libraries(lineage_app_cpp lineage Threads::Threads)
target_compile_options(lineage_app_cpp PRIVATE -Wall -Wextra -pedantic -Werror)
