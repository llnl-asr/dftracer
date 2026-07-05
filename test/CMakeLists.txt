set_property(GLOBAL PROPERTY COVERAGE_FILES "")
include_directories(${DFTRACER_PROJECT_DIR}/include)

set(DFTRACER_TEST_LOG_LEVEL "INFO" CACHE STRING "DFTracer log level for CTest runs")

function(df_add_test)
    add_test(${ARGV})
    set_tests_properties(${ARGV0} PROPERTIES
        ENVIRONMENT "LLVM_PROFILE_FILE=${ARGV0}.profraw;DFTRACER_BIND_SIGNALS=0")
    get_property(OLD_CF GLOBAL PROPERTY COVERAGE_FILES)
    set(NEW_CF ${OLD_CF} ${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}.profraw)
    set_property(GLOBAL PROPERTY COVERAGE_FILES ${NEW_CF})
endfunction()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_cpp cpp/test.cpp)
target_link_libraries(test_cpp ${PROJECT_NAME}_core_dbg)
add_dependencies(test_cpp ${PROJECT_NAME}_core_dbg)
add_dependencies(test_cpp ${PROJECT_NAME}_preload_dbg)

# API Coverage Tests
add_executable(test_cpp_api_coverage cpp/test_cpp_api_coverage.cpp)
target_link_libraries(test_cpp_api_coverage ${PROJECT_NAME}_core_dbg)
add_dependencies(test_cpp_api_coverage ${PROJECT_NAME}_core_dbg)

add_library(testlib SHARED c/testlib.c)
target_compile_options(testlib PRIVATE ${DFTRACER_FUNCTION_FLAGS})
add_executable(test_c c/test.c)
target_compile_options(test_c PRIVATE ${DFTRACER_FUNCTION_FLAGS})
target_link_libraries(test_c ${PROJECT_NAME}_core_dbg testlib)
add_dependencies(test_c ${PROJECT_NAME}_core_dbg)
add_dependencies(test_c ${PROJECT_NAME}_preload_dbg)

# C API Coverage Test
add_executable(test_c_api_coverage c/test_c_api_coverage.c)
target_compile_options(test_c_api_coverage PRIVATE ${DFTRACER_FUNCTION_FLAGS})
target_link_libraries(test_c_api_coverage ${PROJECT_NAME}_core_dbg)
add_dependencies(test_c_api_coverage ${PROJECT_NAME}_core_dbg)

function(set_common_linker_properties test_name)
    set(_ld_paths ${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR})

    if(ARGN)
        list(APPEND _ld_paths ${ARGN})
    endif()

    if(NOT "${DFTRACER_TEST_LD_LIBRARY_PATH}" STREQUAL "")
        string(REPLACE ":" ";" _dftracer_test_ld_paths "${DFTRACER_TEST_LD_LIBRARY_PATH}")
        list(APPEND _ld_paths ${_dftracer_test_ld_paths})
    endif()

    if(NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
        string(REPLACE ":" ";" _env_ld_paths "$ENV{LD_LIBRARY_PATH}")
        list(APPEND _ld_paths ${_env_ld_paths})
    endif()

    if(NOT "$ENV{CRAY_LD_LIBRARY_PATH}" STREQUAL "")
        string(REPLACE ":" ";" _env_cray_ld_paths "$ENV{CRAY_LD_LIBRARY_PATH}")
        list(APPEND _ld_paths ${_env_cray_ld_paths})
    endif()

    list(FILTER _ld_paths EXCLUDE REGEX "^$")
    list(REMOVE_DUPLICATES _ld_paths)
    string(REPLACE ";" ":" _ld_library_path "${_ld_paths}")
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_LIBRARY_PATH=${_ld_library_path})
endfunction()

function(set_common_dftracer_properties test_name)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_LEVEL=${DFTRACER_TEST_LOG_LEVEL})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_TRACE_COMPRESSION=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=OFF)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/data)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_BIND_SIGNALS=0)
endfunction()

function(set_common_properties test_name)
    set_common_linker_properties(${test_name} ${ARGN})
    set_common_dftracer_properties(${test_name})
endfunction()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/data)

set(DFTRACER_TEST_OPTIONAL_LIBDIR "")
set(_legacy_spack_libdir "${CMAKE_SOURCE_DIR}/dependency/.spack-env/view/lib64")
if(EXISTS "${_legacy_spack_libdir}")
    set(DFTRACER_TEST_OPTIONAL_LIBDIR "${_legacy_spack_libdir}")
endif()

set(DFTRACER_PYTHON_H5PY_LIBDIR "")
if(DEFINED DFTRACER_PYTHON_EXE AND NOT "${DFTRACER_PYTHON_EXE}" STREQUAL "")
    execute_process(
        COMMAND ${DFTRACER_PYTHON_EXE} -c "import importlib.util, os; spec=importlib.util.find_spec('h5py'); p=os.path.join(os.path.dirname(os.path.dirname(spec.origin)), 'h5py.libs') if spec and spec.origin else ''; print(p if p and os.path.isdir(p) else '')"
        OUTPUT_VARIABLE _h5py_libdir
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT "${_h5py_libdir}" STREQUAL "")
        set(DFTRACER_PYTHON_H5PY_LIBDIR "${_h5py_libdir}")
    endif()
endif()

set(test_name test_cpp_basic_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

find_program(BASH_PROGRAM bash)
df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 29)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_cpp_basic_app_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 29)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_cpp_basic_app_only_yaml)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_CONFIGURATION=${CMAKE_CURRENT_SOURCE_DIR}/yaml/conf.yaml)
set_common_properties(${test_name})

set(test_name test_cpp_basic_app_compress_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_TRACE_COMPRESSION=1)

set(test_name test_cpp_basic_app_aggregate_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 14)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_cpp_basic_app_aggregate_selective_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_TYPE=SELECTIVE)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/yaml/rules.yaml)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 14)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_cpp_basic_app_disable_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_not.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 0)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_c_basic_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 4)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_c_basic_app_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 4)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_c_basic_app_aggregate_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 11)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_c_basic_app_aggregate_selective_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_TYPE=SELECTIVE)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/yaml/rules.yaml)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 11)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_c_basic_app_disable_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_not.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 0)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_cpp_basic_meta)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 29)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_cpp_basic_affinity)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_SET_CORE_AFFINITY=1)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 29)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_py_disable_only)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/test.py --format=npz --data_dir=${CMAKE_CURRENT_BINARY_DIR})
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_not.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 0)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

set(test_name test_py_both)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/test.py --format=npz --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

set(test_name test_py_aggregation_both)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/test.py --format=npz --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_TYPE=SELECTIVE)

set(test_name test_py_aggregation_selective_both)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/test.py --format=npz --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_TYPE=SELECTIVE)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/yaml/rules.yaml)

# ########################################################################
# AI Logging Test
# ########################################################################

# # Normal Run
set(test_name test_py_ai_logging_normal)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}*-app.pfw* 74)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# # Normal Run + Epoch as Metadata
set(test_name test_py_ai_logging_epoch_as_metadata)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --epoch-as-metadata)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}*-app.pfw* 74)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# # Disable DFTracer
set(test_name test_py_ai_logging_disable_only)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_not.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}*-app.pfw*)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# # Disable Category

# ## ALL Category
set(test_name test_py_ai_logging_disable_cat_all)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat all)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_not.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}*-app.pfw*)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# ## DataLoader category
set(test_name test_py_ai_logging_disable_cat_dataloader)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat dataloader)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}*-app.pfw* 59)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# ## Device category
set(test_name test_py_ai_logging_disable_cat_device)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat device)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}*-app.pfw* 68)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# ## Compute category
set(test_name test_py_ai_logging_disable_cat_compute)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat compute)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}*-app.pfw* 50)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# ## Checkpoint category
set(test_name test_py_ai_logging_disable_cat_ckpt)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat ckpt)
set_common_properties(${test_name} ${DFTRACER_PYTHON_H5PY_LIBDIR} ${DFTRACER_TEST_OPTIONAL_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/venv/${DFTRACER_LIBDIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}*-app.pfw* 71)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# ########################################################################
# ## END OF AI LOGGING TEST
# ########################################################################
set(TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/util.h)

if(ENABLE_DLIO_BENCHMARK_TESTS)
    set(test_name python_test_py_disable_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz)
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)

    set(test_name python_test_py_disable_io_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DISABLE_IO=1)

    set(test_name python_test_py_io_all_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=all)

    set(test_name python_test_py_io_specific_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR})

    set(test_name python_test_py_io_specific_meta_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR})

    set(test_name python_test_py_io_all_meta_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=all)
    add_subdirectory(dlio_benchmark)
endif()

# =============================================================================
# MPI Tests
# =============================================================================
if(DFTRACER_MPI_ENABLE)
    set(DFTRACER_MPI_TEST_LAUNCHER "auto" CACHE STRING
        "MPI launcher used by DFTracer CTest MPI tests. Options: auto, flux, srun, mpirun, or an absolute launcher path")
    set_property(CACHE DFTRACER_MPI_TEST_LAUNCHER PROPERTY STRINGS auto flux srun mpirun)

    set(_dftracer_mpi_test_launcher "")
    set(_dftracer_mpi_test_flags ${MPIEXEC_PREFLAGS})

    if(DFTRACER_MPI_TEST_LAUNCHER STREQUAL "auto")
        find_program(_dftracer_flux_launcher flux)
        find_program(_dftracer_srun_launcher srun)
        find_program(_dftracer_mpirun_launcher mpirun)

        if(_dftracer_flux_launcher)
            set(_dftracer_mpi_test_launcher ${_dftracer_flux_launcher})
            # Use "flux run" semantics for test launch.
            list(APPEND _dftracer_mpi_test_flags run)
        elseif(_dftracer_srun_launcher)
            set(_dftracer_mpi_test_launcher ${_dftracer_srun_launcher})
        elseif(_dftracer_mpirun_launcher)
            set(_dftracer_mpi_test_launcher ${_dftracer_mpirun_launcher})
        elseif(MPIEXEC)
            set(_dftracer_mpi_test_launcher ${MPIEXEC})
        else()
            message(FATAL_ERROR "[DFTracer] No MPI launcher found. Set DFTRACER_MPI_TEST_LAUNCHER to a valid launcher path.")
        endif()
    elseif(DFTRACER_MPI_TEST_LAUNCHER STREQUAL "flux")
        find_program(_dftracer_flux_launcher flux)
        if(NOT _dftracer_flux_launcher)
            message(FATAL_ERROR "[DFTracer] DFTRACER_MPI_TEST_LAUNCHER=flux but 'flux' was not found in PATH")
        endif()
        set(_dftracer_mpi_test_launcher ${_dftracer_flux_launcher})
        list(APPEND _dftracer_mpi_test_flags run)
    elseif(DFTRACER_MPI_TEST_LAUNCHER STREQUAL "srun")
        find_program(_dftracer_srun_launcher srun)
        if(NOT _dftracer_srun_launcher)
            message(FATAL_ERROR "[DFTracer] DFTRACER_MPI_TEST_LAUNCHER=srun but 'srun' was not found in PATH")
        endif()
        set(_dftracer_mpi_test_launcher ${_dftracer_srun_launcher})
    elseif(DFTRACER_MPI_TEST_LAUNCHER STREQUAL "mpirun")
        find_program(_dftracer_mpirun_launcher mpirun)
        if(NOT _dftracer_mpirun_launcher)
            message(FATAL_ERROR "[DFTracer] DFTRACER_MPI_TEST_LAUNCHER=mpirun but 'mpirun' was not found in PATH")
        endif()
        set(_dftracer_mpi_test_launcher ${_dftracer_mpirun_launcher})
    else()
        if(NOT IS_ABSOLUTE "${DFTRACER_MPI_TEST_LAUNCHER}")
            message(FATAL_ERROR "[DFTracer] DFTRACER_MPI_TEST_LAUNCHER must be auto/flux/srun/mpirun or an absolute path")
        endif()
        if(NOT EXISTS "${DFTRACER_MPI_TEST_LAUNCHER}")
            message(FATAL_ERROR "[DFTracer] DFTRACER_MPI_TEST_LAUNCHER path does not exist: ${DFTRACER_MPI_TEST_LAUNCHER}")
        endif()
        set(_dftracer_mpi_test_launcher ${DFTRACER_MPI_TEST_LAUNCHER})
    endif()

    if(NOT "${DFTRACER_MPI_TEST_LAUNCHER_FLAGS}" STREQUAL "")
        separate_arguments(_dftracer_mpi_test_user_flags NATIVE_COMMAND "${DFTRACER_MPI_TEST_LAUNCHER_FLAGS}")
        list(APPEND _dftracer_mpi_test_flags ${_dftracer_mpi_test_user_flags})
    endif()

    message(STATUS "[DFTracer] MPI test launcher: ${_dftracer_mpi_test_launcher}")
    message(STATUS "[DFTracer] MPI test launcher flags: ${_dftracer_mpi_test_flags}")

    function(df_set_mpi_test_properties test_name)
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 300)
        # Flux/MPIR launchers manage signal handlers; forcing DFTRACER signal binding can break launcher startup.
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_BIND_SIGNALS=0)
    endfunction()

    # Build MPI test executables
    add_executable(test_c_mpi c/test_mpi.c)
    target_compile_options(test_c_mpi PRIVATE ${DFTRACER_FUNCTION_FLAGS})
    target_link_libraries(test_c_mpi ${PROJECT_NAME}_core_dbg ${MPI_CXX_LIBRARIES})
    target_include_directories(test_c_mpi PRIVATE ${MPI_CXX_INCLUDE_DIRS})
    add_dependencies(test_c_mpi ${PROJECT_NAME}_core_dbg)

    add_executable(test_cpp_mpi cpp/test_mpi.cpp)
    target_compile_options(test_cpp_mpi PRIVATE ${DFTRACER_FUNCTION_FLAGS})
    target_link_libraries(test_cpp_mpi ${PROJECT_NAME}_core_dbg ${MPI_CXX_LIBRARIES})
    target_include_directories(test_cpp_mpi PRIVATE ${MPI_CXX_INCLUDE_DIRS})
    add_dependencies(test_cpp_mpi ${PROJECT_NAME}_core_dbg)

    # C MPI test with preload
    set(test_name test_c_mpi_preload)
    df_add_test(${test_name} ${_dftracer_mpi_test_launcher} ${_dftracer_mpi_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_c_mpi ${CMAKE_CURRENT_BINARY_DIR}/data)
    set_common_properties(${test_name})
    df_set_mpi_test_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

    df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 10)
    set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

    # C MPI test with app init
    set(test_name test_c_mpi_app)
    df_add_test(${test_name} ${_dftracer_mpi_test_launcher} ${_dftracer_mpi_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_c_mpi ${CMAKE_CURRENT_BINARY_DIR}/data 1)
    set_common_properties(${test_name})
    df_set_mpi_test_properties(${test_name})

    df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 10)
    set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

    # C++ MPI test with preload
    set(test_name test_cpp_mpi_preload)
    df_add_test(${test_name} ${_dftracer_mpi_test_launcher} ${_dftracer_mpi_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_cpp_mpi ${CMAKE_CURRENT_BINARY_DIR}/data)
    set_common_properties(${test_name})
    df_set_mpi_test_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

    df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 10)
    set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

    # C++ MPI test with app init
    set(test_name test_cpp_mpi_app)
    df_add_test(${test_name} ${_dftracer_mpi_test_launcher} ${_dftracer_mpi_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_cpp_mpi ${CMAKE_CURRENT_BINARY_DIR}/data 1)
    set_common_properties(${test_name})
    df_set_mpi_test_properties(${test_name})

    df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 10)
    set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})
endif()

# =============================================================================
# HDF5 Tests
# =============================================================================
if(DFTRACER_HDF5_ENABLE)
    function(df_link_hdf5 target_name)
        if(TARGET HDF5::HDF5)
            target_link_libraries(${target_name} HDF5::HDF5)
        else()
            if(DEFINED HDF5_C_LIBRARIES)
                target_link_libraries(${target_name} ${HDF5_C_LIBRARIES})
            endif()
            if(DEFINED HDF5_LIBRARIES)
                target_link_libraries(${target_name} ${HDF5_LIBRARIES})
            endif()
            if(DEFINED HDF5_INCLUDE_DIRS)
                target_include_directories(${target_name} PRIVATE ${HDF5_INCLUDE_DIRS})
            endif()
        endif()
    endfunction()

    # Build standalone HDF5 test executables
    add_executable(test_c_hdf5 c/test_hdf5.c)
    target_compile_options(test_c_hdf5 PRIVATE ${DFTRACER_FUNCTION_FLAGS})
    target_link_libraries(test_c_hdf5 ${PROJECT_NAME}_core_dbg)
    df_link_hdf5(test_c_hdf5)
    add_dependencies(test_c_hdf5 ${PROJECT_NAME}_core_dbg)

    add_executable(test_cpp_hdf5 cpp/test_hdf5.cpp)
    target_compile_options(test_cpp_hdf5 PRIVATE ${DFTRACER_FUNCTION_FLAGS})
    target_link_libraries(test_cpp_hdf5 ${PROJECT_NAME}_core_dbg)
    df_link_hdf5(test_cpp_hdf5)
    add_dependencies(test_cpp_hdf5 ${PROJECT_NAME}_core_dbg)

    # C HDF5 test with preload
    set(test_name test_c_hdf5_preload)
    df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c_hdf5 ${CMAKE_CURRENT_BINARY_DIR}/data)
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

    df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 8)
    set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

    # C HDF5 test with app init
    set(test_name test_c_hdf5_app)
    df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c_hdf5 ${CMAKE_CURRENT_BINARY_DIR}/data 1)
    set_common_properties(${test_name})

    df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 8)
    set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

    # C++ HDF5 test with preload
    set(test_name test_cpp_hdf5_preload)
    df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp_hdf5 ${CMAKE_CURRENT_BINARY_DIR}/data)
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

    df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 8)
    set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

    # C++ HDF5 test with app init
    set(test_name test_cpp_hdf5_app)
    df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp_hdf5 ${CMAKE_CURRENT_BINARY_DIR}/data 1)
    set_common_properties(${test_name})

    df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 8)
    set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

    # Build MPI + HDF5 test executables and tests when MPI is enabled
    if(DFTRACER_MPI_ENABLE)
        set(_dftracer_mpi_hdf5_test_launcher ${_dftracer_mpi_test_launcher})
        set(_dftracer_mpi_hdf5_test_flags ${_dftracer_mpi_test_flags})

        if(HDF5_IS_PARALLEL)
        # test_c_hdf5_mpi uses H5Pset_fapl_mpio which requires parallel HDF5
        add_executable(test_c_hdf5_mpi c/test_hdf5_mpi.c)
        target_compile_options(test_c_hdf5_mpi PRIVATE ${DFTRACER_FUNCTION_FLAGS})
        target_link_libraries(test_c_hdf5_mpi ${PROJECT_NAME}_core_dbg ${MPI_CXX_LIBRARIES})
        target_include_directories(test_c_hdf5_mpi PRIVATE ${MPI_CXX_INCLUDE_DIRS})
        df_link_hdf5(test_c_hdf5_mpi)
        add_dependencies(test_c_hdf5_mpi ${PROJECT_NAME}_core_dbg)

        add_executable(test_cpp_hdf5_mpi cpp/test_hdf5_mpi.cpp)
        target_compile_options(test_cpp_hdf5_mpi PRIVATE ${DFTRACER_FUNCTION_FLAGS})
        target_link_libraries(test_cpp_hdf5_mpi ${PROJECT_NAME}_core_dbg ${MPI_CXX_LIBRARIES})
        target_include_directories(test_cpp_hdf5_mpi PRIVATE ${MPI_CXX_INCLUDE_DIRS})
        df_link_hdf5(test_cpp_hdf5_mpi)
        add_dependencies(test_cpp_hdf5_mpi ${PROJECT_NAME}_core_dbg)

        # C MPI+HDF5 test with preload
        set(test_name test_c_hdf5_mpi_preload)
        df_add_test(${test_name} ${_dftracer_mpi_hdf5_test_launcher} ${_dftracer_mpi_hdf5_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_c_hdf5_mpi ${CMAKE_CURRENT_BINARY_DIR}/data)
        set_common_properties(${test_name})
        df_set_mpi_test_properties(${test_name})
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

        df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 12)
        set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

        # C MPI+HDF5 test with app init
        set(test_name test_c_hdf5_mpi_app)
        df_add_test(${test_name} ${_dftracer_mpi_hdf5_test_launcher} ${_dftracer_mpi_hdf5_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_c_hdf5_mpi ${CMAKE_CURRENT_BINARY_DIR}/data 1)
        set_common_properties(${test_name})
        df_set_mpi_test_properties(${test_name})

        df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 12)
        set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

        # C++ MPI+HDF5 test with preload
        set(test_name test_cpp_hdf5_mpi_preload)
        df_add_test(${test_name} ${_dftracer_mpi_hdf5_test_launcher} ${_dftracer_mpi_hdf5_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_cpp_hdf5_mpi ${CMAKE_CURRENT_BINARY_DIR}/data)
        set_common_properties(${test_name})
        df_set_mpi_test_properties(${test_name})
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

        df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 12)
        set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

        # C++ MPI+HDF5 test with app init
        set(test_name test_cpp_hdf5_mpi_app)
        df_add_test(${test_name} ${_dftracer_mpi_hdf5_test_launcher} ${_dftracer_mpi_hdf5_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_cpp_hdf5_mpi ${CMAKE_CURRENT_BINARY_DIR}/data 1)
        set_common_properties(${test_name})
        df_set_mpi_test_properties(${test_name})

        df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 12)
        set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

        # Parallel HDF5-specific tests (H5Pset_fapl_mpio etc.)
            # Build Parallel HDF5-specific test executables
            add_executable(test_c_hdf5_parallel c/test_hdf5_parallel.c)
            target_compile_options(test_c_hdf5_parallel PRIVATE ${DFTRACER_FUNCTION_FLAGS})
            target_link_libraries(test_c_hdf5_parallel ${PROJECT_NAME}_core_dbg ${MPI_CXX_LIBRARIES})
            target_include_directories(test_c_hdf5_parallel PRIVATE ${MPI_CXX_INCLUDE_DIRS})
            df_link_hdf5(test_c_hdf5_parallel)
            add_dependencies(test_c_hdf5_parallel ${PROJECT_NAME}_core_dbg)

            add_executable(test_cpp_hdf5_parallel cpp/test_hdf5_parallel.cpp)
            target_compile_options(test_cpp_hdf5_parallel PRIVATE ${DFTRACER_FUNCTION_FLAGS})
            target_link_libraries(test_cpp_hdf5_parallel ${PROJECT_NAME}_core_dbg ${MPI_CXX_LIBRARIES})
            target_include_directories(test_cpp_hdf5_parallel PRIVATE ${MPI_CXX_INCLUDE_DIRS})
            df_link_hdf5(test_cpp_hdf5_parallel)
            add_dependencies(test_cpp_hdf5_parallel ${PROJECT_NAME}_core_dbg)

            # Keep line threshold permissive for small output variability.
            set(_hdf5_parallel_expected_lines 6)

            # C Parallel-HDF5 test with preload
            set(test_name test_c_hdf5_parallel_preload)
            df_add_test(${test_name} ${_dftracer_mpi_hdf5_test_launcher} ${_dftracer_mpi_hdf5_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_c_hdf5_parallel ${CMAKE_CURRENT_BINARY_DIR}/data)
            set_common_properties(${test_name})
            df_set_mpi_test_properties(${test_name})
            set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
            set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

            df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* ${_hdf5_parallel_expected_lines})
            set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

            # C Parallel-HDF5 test with app init
            set(test_name test_c_hdf5_parallel_app)
            df_add_test(${test_name} ${_dftracer_mpi_hdf5_test_launcher} ${_dftracer_mpi_hdf5_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_c_hdf5_parallel ${CMAKE_CURRENT_BINARY_DIR}/data 1)
            set_common_properties(${test_name})
            df_set_mpi_test_properties(${test_name})

            df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* ${_hdf5_parallel_expected_lines})
            set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

            # C++ Parallel-HDF5 test with preload
            set(test_name test_cpp_hdf5_parallel_preload)
            df_add_test(${test_name} ${_dftracer_mpi_hdf5_test_launcher} ${_dftracer_mpi_hdf5_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_cpp_hdf5_parallel ${CMAKE_CURRENT_BINARY_DIR}/data)
            set_common_properties(${test_name})
            df_set_mpi_test_properties(${test_name})
            set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
            set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)

            df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* ${_hdf5_parallel_expected_lines})
            set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

            # C++ Parallel-HDF5 test with app init
            set(test_name test_cpp_hdf5_parallel_app)
            df_add_test(${test_name} ${_dftracer_mpi_hdf5_test_launcher} ${_dftracer_mpi_hdf5_test_flags} ${MPIEXEC_NUMPROC_FLAG} 2 ${CMAKE_BINARY_DIR}/bin/test_cpp_hdf5_parallel ${CMAKE_CURRENT_BINARY_DIR}/data 1)
            set_common_properties(${test_name})
            df_set_mpi_test_properties(${test_name})

            df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* ${_hdf5_parallel_expected_lines})
            set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})
        else()
            message(STATUS "[DFTracer] HDF5 parallel tests disabled: HDF5_IS_PARALLEL=FALSE")
        endif()
    endif()
endif()

if(DFTRACER_ENABLE_PAPER_TESTS)
    add_subdirectory(paper)
endif()

add_subdirectory(dfanalyzer)

# ########################################################################
# API Coverage Tests
# ########################################################################

# C++ API Coverage Test
set(test_name test_cpp_api_coverage_full)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp_api_coverage ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})

# Expected trace lines for C++ API coverage:
# - Metadata: 3 lines (app_name, version, config)
# - test_function_tracing: 1 function entry/exit
# - test_region_tracing: 5 regions (STATIC, DYNAMIC, OUTER, INNER, nested)
# - test_io_operations: ~15 I/O calls (open, write, read, lseek, fsync, close, fopen, fwrite, fread, fseek, fflush, fclose, stat, chmod, unlink)
# Total: approximately 24 lines
df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 22)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# C API Coverage Test
set(test_name test_c_api_coverage_full)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c_api_coverage ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})

# Expected trace lines for C API coverage:
# - Metadata: 2 lines (c_app, language)
# - test_c_function_tracing: 1 function
# - test_c_region_tracing: 2 regions
# - test_c_io_operations: ~8 I/O calls (open, write, read, lseek, close, fopen, fread, fclose, unlink)
# Total: approximately 13 lines
df_add_test(check_file_exists_${test_name} ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/check_file.sh ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* 31)
set_tests_properties(check_file_exists_${test_name} PROPERTIES DEPENDS ${test_name})

# ########################################################################
# Package Find Test - Tests that external apps can find installed dftracer
# ########################################################################

# This test verifies the CMake package config works correctly
set(test_name test_package_find_build_tree)
find_program(SYSTEM_CMAKE cmake PATHS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
set(_dftracer_package_find_cmake_args
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})

add_test(NAME ${test_name}
    COMMAND ${SYSTEM_CMAKE}
    --fresh
    -S ${CMAKE_CURRENT_SOURCE_DIR}/package_find
    -B ${CMAKE_CURRENT_BINARY_DIR}/package_find_build
    -Ddftracer_DIR=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/dftracer
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}
    ${_dftracer_package_find_cmake_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_common_linker_properties(${test_name})

# Build the external test app
set(test_name test_package_build_external_app)
add_test(NAME ${test_name}
    COMMAND ${SYSTEM_CMAKE} --build ${CMAKE_CURRENT_BINARY_DIR}/package_find_build
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(${test_name} PROPERTIES DEPENDS test_package_find_build_tree)

# Run the external test app
set(test_name test_package_run_external_app)
add_test(NAME ${test_name}
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/package_find_build/external_test_app
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(${test_name} PROPERTIES DEPENDS test_package_build_external_app)
set(_package_run_ld_paths)
if(DEFINED YAML_CPP_LIBRARY_DIR AND NOT "${YAML_CPP_LIBRARY_DIR}" STREQUAL "")
    list(APPEND _package_run_ld_paths ${YAML_CPP_LIBRARY_DIR})
elseif(DEFINED yaml-cpp_DIR AND NOT "${yaml-cpp_DIR}" STREQUAL "")
    set(_yaml_cpp_library_dir ${yaml-cpp_DIR}/../../)
    get_filename_component(_yaml_cpp_library_dir "${_yaml_cpp_library_dir}" ABSOLUTE)
    list(APPEND _package_run_ld_paths ${_yaml_cpp_library_dir})
endif()
set_common_linker_properties(${test_name} ${_package_run_ld_paths})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/data)

# ########################################################################
# Version Compatibility Tests
# Tests that find_package correctly handles version requirements
# ########################################################################

# Calculate version values for tests
math(EXPR DFTRACER_NEXT_MAJOR "${DFTRACER_VERSION_MAJOR} + 1")
math(EXPR DFTRACER_PREV_MAJOR "${DFTRACER_VERSION_MAJOR} - 1")
math(EXPR DFTRACER_NEXT_MINOR "${DFTRACER_VERSION_MINOR} + 1")
math(EXPR DFTRACER_WRONG_PATCH "${DFTRACER_VERSION_PATCH} + 99")

# Configure all version test CMakeLists.txt files with current version
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_exact/CMakeLists.txt
    ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_exact/CMakeLists.txt
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_compatible/CMakeLists.txt
    ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_compatible/CMakeLists.txt
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_minimum/CMakeLists.txt
    ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_minimum/CMakeLists.txt
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_wrong_exact/CMakeLists.txt
    ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_wrong_exact/CMakeLists.txt
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_incompatible_major/CMakeLists.txt
    ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_incompatible_major/CMakeLists.txt
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_higher_minor/CMakeLists.txt
    ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_higher_minor/CMakeLists.txt
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_old_major/CMakeLists.txt
    ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_old_major/CMakeLists.txt
    @ONLY
)

# Copy test_version.cpp files to configured directories
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_exact/test_version.cpp
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_exact/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_compatible/test_version.cpp
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_compatible/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_minimum/test_version.cpp
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_minimum/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_wrong_exact/test_version.cpp
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_wrong_exact/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_incompatible_major/test_version.cpp
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_incompatible_major/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_higher_minor/test_version.cpp
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_higher_minor/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/package_find/version_old_major/test_version.cpp
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_old_major/)

# Test 1: Exact version match (should succeed)
set(test_name test_package_version_exact_match)
add_test(NAME ${test_name}
    COMMAND ${SYSTEM_CMAKE}
    --fresh
    -S ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_exact
    -B ${CMAKE_CURRENT_BINARY_DIR}/version_exact_build
    -Ddftracer_DIR=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/dftracer
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}
    ${_dftracer_package_find_cmake_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_common_linker_properties(${test_name})

# Test 2: Compatible version (same major, exact minor) (should succeed)
set(test_name test_package_version_compatible)
add_test(NAME ${test_name}
    COMMAND ${SYSTEM_CMAKE}
    --fresh
    -S ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_compatible
    -B ${CMAKE_CURRENT_BINARY_DIR}/version_compatible_build
    -Ddftracer_DIR=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/dftracer
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}
    ${_dftracer_package_find_cmake_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_common_linker_properties(${test_name})

# Test 3: Minimum version requirement (should succeed)
set(test_name test_package_version_minimum)
add_test(NAME ${test_name}
    COMMAND ${SYSTEM_CMAKE}
    --fresh
    -S ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_minimum
    -B ${CMAKE_CURRENT_BINARY_DIR}/version_minimum_build
    -Ddftracer_DIR=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/dftracer
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}
    ${_dftracer_package_find_cmake_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_common_linker_properties(${test_name})

# Test 4: Wrong exact version (should fail)
set(test_name test_package_version_wrong_exact_fail)
add_test(NAME ${test_name}
    COMMAND ${SYSTEM_CMAKE}
    --fresh
    -S ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_wrong_exact
    -B ${CMAKE_CURRENT_BINARY_DIR}/version_wrong_exact_build
    -Ddftracer_DIR=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/dftracer
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}
    ${_dftracer_package_find_cmake_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)

# Test 5: Incompatible major version (should fail)
set(test_name test_package_version_incompatible_major_fail)
add_test(NAME ${test_name}
    COMMAND ${SYSTEM_CMAKE}
    --fresh
    -S ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_incompatible_major
    -B ${CMAKE_CURRENT_BINARY_DIR}/version_incompatible_major_build
    -Ddftracer_DIR=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/dftracer
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}
    ${_dftracer_package_find_cmake_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)

# Test 6: Higher minor version than available (should fail)
set(test_name test_package_version_higher_minor_fail)
add_test(NAME ${test_name}
    COMMAND ${SYSTEM_CMAKE}
    --fresh
    -S ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_higher_minor
    -B ${CMAKE_CURRENT_BINARY_DIR}/version_higher_minor_build
    -Ddftracer_DIR=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/dftracer
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}
    ${_dftracer_package_find_cmake_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)

# Test 7: Different major version (older) (should fail due to SameMajorVersion)
set(test_name test_package_version_old_major_fail)
add_test(NAME ${test_name}
    COMMAND ${SYSTEM_CMAKE}
    --fresh
    -S ${CMAKE_CURRENT_BINARY_DIR}/package_find_configured/version_old_major
    -B ${CMAKE_CURRENT_BINARY_DIR}/version_old_major_build
    -Ddftracer_DIR=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/dftracer
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}
    ${_dftracer_package_find_cmake_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)

# ########################################################################
# END OF NEW TESTS
# ########################################################################

# Add unit tests subdirectory
add_subdirectory(unit)
