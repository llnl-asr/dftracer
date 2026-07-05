# Test find_package with exact version match
# This file is auto-configured from CMakeLists.txt.in
cmake_minimum_required(VERSION 3.24)
project(dftracer_version_exact_test CXX)

# Should succeed - exact version match
find_package(dftracer @DFTRACER_VERSION_MAJOR@.@DFTRACER_VERSION_MINOR@.@DFTRACER_VERSION_PATCH@ EXACT REQUIRED)

add_executable(version_exact_test test_version.cpp)
target_link_libraries(version_exact_test dftracer::dftracer_core)

message(STATUS "Found dftracer version: ${DFTRACER_VERSION}")
