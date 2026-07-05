# Test find_package with higher minor version (should fail based on SameMajorVersion)
# This file is auto-configured from CMakeLists.txt.in
cmake_minimum_required(VERSION 3.24)
project(dftracer_version_higher_minor_test CXX)

# Should fail - requesting higher minor version than available
find_package(dftracer @DFTRACER_VERSION_MAJOR@.@DFTRACER_NEXT_MINOR@.0 REQUIRED)

add_executable(version_higher_minor_test test_version.cpp)
target_link_libraries(version_higher_minor_test dftracer::dftracer_core)
