# Test find_package with wrong exact version (should fail)
# This file is auto-configured from CMakeLists.txt.in
cmake_minimum_required(VERSION 3.24)
project(dftracer_version_wrong_exact_test CXX)

# Should fail - exact version with wrong patch doesn't match
find_package(dftracer @DFTRACER_VERSION_MAJOR@.@DFTRACER_VERSION_MINOR@.@DFTRACER_WRONG_PATCH@ EXACT REQUIRED)

add_executable(version_wrong_exact_test test_version.cpp)
target_link_libraries(version_wrong_exact_test dftracer::dftracer_core)
