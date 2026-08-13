# Unit tests for DFTracer core components

# Test aggregator functionality
add_executable(test_aggregator test_aggregator.cpp)
target_link_libraries(test_aggregator ${PROJECT_NAME}_core_dbg)
add_dependencies(test_aggregator ${PROJECT_NAME}_core_dbg)

# Test configuration manager
add_executable(test_configuration test_configuration.cpp)
target_link_libraries(test_configuration ${PROJECT_NAME}_core_dbg yaml-cpp)
add_dependencies(test_configuration ${PROJECT_NAME}_core_dbg)

# Test serialization
add_executable(test_serialization test_serialization.cpp)
target_link_libraries(test_serialization ${PROJECT_NAME}_core_dbg)
add_dependencies(test_serialization ${PROJECT_NAME}_core_dbg)

# Test service
add_executable(test_service test_service.cpp)
target_link_libraries(test_service ${PROJECT_NAME}_core_dbg ${PROJECT_NAME}_service_telemetry)
add_dependencies(test_service ${PROJECT_NAME}_core_dbg ${PROJECT_NAME}_service_telemetry)

# Test omnistat ingestion
add_executable(test_omnistat test_omnistat.cpp)
target_link_libraries(test_omnistat ${PROJECT_NAME}_core_dbg ${PROJECT_NAME}_service_telemetry yaml-cpp)
add_dependencies(test_omnistat ${PROJECT_NAME}_core_dbg ${PROJECT_NAME}_service_telemetry)

# Test variorum power ingestion
add_executable(test_variorum test_variorum.cpp)
target_link_libraries(test_variorum ${PROJECT_NAME}_core_dbg ${PROJECT_NAME}_service_telemetry yaml-cpp)
add_dependencies(test_variorum ${PROJECT_NAME}_core_dbg ${PROJECT_NAME}_service_telemetry)

# Test aggregated key hash
add_executable(test_aggregated_key_hash test_aggregated_key_hash.cpp)
target_link_libraries(test_aggregated_key_hash ${PROJECT_NAME}_core_dbg)
add_dependencies(test_aggregated_key_hash ${PROJECT_NAME}_core_dbg)

# Test trace event type enum
add_executable(test_trace_event_type test_trace_event_type.cpp)
target_link_libraries(test_trace_event_type ${PROJECT_NAME}_core_dbg)
add_dependencies(test_trace_event_type ${PROJECT_NAME}_core_dbg)

# Test aggregated value
add_executable(test_aggregated_value test_aggregated_value.cpp)
target_link_libraries(test_aggregated_value ${PROJECT_NAME}_core_dbg)
add_dependencies(test_aggregated_value ${PROJECT_NAME}_core_dbg)

# Add tests to CTest
set(test_name unit_test_aggregator)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_aggregator)
set_common_properties(${test_name})

set(test_name unit_test_configuration)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_configuration)
set_common_properties(${test_name})

set(test_name unit_test_serialization)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_serialization)
set_common_properties(${test_name})

set(test_name unit_test_service)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_service)
set_common_properties(${test_name})

set(test_name unit_test_omnistat)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_omnistat)
set_common_properties(${test_name})

set(test_name unit_test_variorum)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_variorum)
set_common_properties(${test_name})

set(test_name unit_test_aggregated_key_hash)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_aggregated_key_hash)
set_common_properties(${test_name})

set(test_name unit_test_trace_event_type)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_trace_event_type)
set_common_properties(${test_name})

set(test_name unit_test_aggregated_value)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_aggregated_value)
set_common_properties(${test_name})

# Existing mmap test
add_executable(test_mmap mmap.cpp)
target_link_libraries(test_mmap ${PROJECT_NAME}_core_dbg)
add_dependencies(test_mmap ${PROJECT_NAME}_core_dbg)
add_dependencies(test_mmap ${PROJECT_NAME}_preload_dbg)

set(test_name unit_test_mmap)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_mmap)
set_common_properties(${test_name})
