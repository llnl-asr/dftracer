# Test find_package with different (older) major version (should fail)
# This file is auto-configured from CMakeLists.txt.in
cmake_minimum_required(VERSION 3.24)
project(dftracer_version_old_major_test CXX)

# Should fail - different major version is incompatible (SameMajorVersion policy)
find_package(dftracer @DFTRACER_PREV_MAJOR@.9.9 REQUIRED)

add_executable(version_old_major_test test_version.cpp)
target_link_libraries(version_old_major_test dftracer::dftracer_core)
