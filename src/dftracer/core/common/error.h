//
// Created by haridev on 10/5/23.
//

#ifndef DFTRACER_ERROR_H
#define DFTRACER_ERROR_H
// clang-format off
#define DFTRACE_DEFINE_ERROR(name, code, message)   \
const char* DFTRACER_##name##_CODE = code;                    \
const char* DFTRACER_##name##_MSG = message;

// Main error codes
#define DFTRACER_SUCCESS_CODE "0"
#define DFTRACER_SUCCESS_MSG "Operation Successful"
#define DFTRACER_FAILURE_CODE "1001"
#define DFTRACER_FAILURE_MSG "Internal Failure"
// Invalid API calls
#define DFTRACER_UNKNOWN_PROFILER_TYPE_CODE "1002"
#define DFTRACER_UNKNOWN_PROFILER_TYPE_MSG "Code 1002: Unknown profiler type %d"

// Invalid configurations
#define DFTRACER_UNDEFINED_DATA_DIR_CODE "2001"
#define DFTRACER_UNDEFINED_DATA_DIR_MSG "Code 2001: Data dirs not defined. Please define env variable DFTRACER_DATA_DIR"
#define DFTRACER_UNDEFINED_LOG_FILE_CODE "2002"
#define DFTRACER_UNDEFINED_LOG_FILE_MSG "Code 2002: log file not defined. Please define env variable DFTRACER_LOG_FILE"

// clang-format on
#endif  // DFTRACER_ERROR_H
