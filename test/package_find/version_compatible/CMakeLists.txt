# Test find_package with compatible version (same major.minor.patch)
# This file is auto-configured from CMakeLists.txt.in
cmake_minimum_required(VERSION 3.24)
project(dftracer_version_compatible_test CXX)

# Should succeed - exact version match without EXACT keyword
find_package(dftracer @DFTRACER_VERSION_MAJOR@.@DFTRACER_VERSION_MINOR@.@DFTRACER_VERSION_PATCH@ REQUIRED)

add_executable(version_compatible_test test_version.cpp)
target_link_libraries(version_compatible_test dftracer::dftracer_core)

message(STATUS "Found dftracer version: ${DFTRACER_VERSION}")
