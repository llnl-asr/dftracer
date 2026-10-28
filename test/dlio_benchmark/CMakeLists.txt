set(DLIO_BENCHMARK_EXE ${MAIN_PYTHON_SITE_PACKAGES}/dlio_benchmark/main.py)
message("-- Found dlio_benchmark at location " ${DLIO_BENCHMARK_EXE})
find_program(PYTHON_EXE python)
set(COMMON_ARGS workload=unet3d ++workload.train.computation_time=0.05
        ++workload.evaluation.eval_time=0.01 ++workload.train.epochs=2 ++workload.workflow.train=True
        ++workload.workflow.generate_data=True ++workload.dataset.num_files_train=16
        ++workload.dataset.num_files_eval=16 ++workload.reader.read_threads=2
        ++workload.dataset.record_length=4096 ++workload.dataset.record_length_stdev=0
        ++workload.framework=tensorflow ++workload.reader.data_loader=tensorflow)
set(formats npz hdf5 tfrecord png jpeg)
set(lines 2055 1159 715 935 903)
list(LENGTH formats formats_length)
math(EXPR formats_length "${formats_length} - 1")
message(STATUS "Number of formats: ${formats_length}")
foreach (index RANGE ${formats_length})
    list(GET formats ${index} format)
    list(GET lines ${index} line)
    set(test_name dlio_${format})
    df_add_test(${test_name} ${PYTHON_EXE} ${DLIO_BENCHMARK_EXE} ${COMMON_ARGS} ++workload.dataset.format=${format}
            ++workload.output.folder=${CMAKE_CURRENT_BINARY_DIR}/output_${format})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    #set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/output_${format}/trace)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_LEVEL=INFO)

    df_add_test(${test_name}_check_file_exists ${BASH_PROGRAM} ${CMAKE_SOURCE_DIR}/test/check_file.sh ${CMAKE_CURRENT_BINARY_DIR}/output_${format}/.trace*.pfw* ${line})
    set_tests_properties(${test_name}_check_file_exists PROPERTIES DEPENDS ${test_name})
endforeach ()
