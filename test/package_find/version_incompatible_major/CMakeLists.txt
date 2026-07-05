# Test find_package with incompatible major version (should fail)
# This file is auto-configured from CMakeLists.txt.in
cmake_minimum_required(VERSION 3.24)
project(dftracer_version_incompatible_major_test CXX)

# Should fail - major version (current+1) is incompatible
find_package(dftracer @DFTRACER_NEXT_MAJOR@.0.0 REQUIRED)

add_executable(version_incompatible_major_test test_version.cpp)
target_link_libraries(version_incompatible_major_test dftracer::dftracer_core)
