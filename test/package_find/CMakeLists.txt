# Minimal external project to test find_package(dftracer)
cmake_minimum_required(VERSION 3.24)
project(dftracer_external_test CXX)

# Find the installed dftracer package
find_package(dftracer REQUIRED)

# Create test executable using found package
add_executable(external_test_app test_external.cpp)
target_link_libraries(external_test_app dftracer::dftracer_core)

# Print found information for debugging
message(STATUS "DFTRACER_INCLUDE_DIRS: ${DFTRACER_INCLUDE_DIRS}")
message(STATUS "DFTRACER_LIBRARY_DIRS: ${DFTRACER_LIBRARY_DIRS}")
message(STATUS "DFTRACER_LIBRARIES: ${DFTRACER_LIBRARIES}")
message(STATUS "DFTRACER_VERSION: ${DFTRACER_VERSION}")
