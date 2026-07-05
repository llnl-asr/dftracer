# Test find_package with minimum version requirement
# This file is auto-configured from CMakeLists.txt.in
cmake_minimum_required(VERSION 3.24)
project(dftracer_version_minimum_test CXX)

# Should succeed - current version >= (major.0.0)
find_package(dftracer @DFTRACER_VERSION_MAJOR@.0.0 REQUIRED)

add_executable(version_minimum_test test_version.cpp)
target_link_libraries(version_minimum_test dftracer::dftracer_core)

message(STATUS "Found dftracer version: ${DFTRACER_VERSION}")
