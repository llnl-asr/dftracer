#!/bin/bash
# Automated build script for DFTracer
# This script installs dependencies and builds DFTracer with configurable options

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build}"
INSTALL_PREFIX="${INSTALL_PREFIX:-${SCRIPT_DIR}/install}"
BUILD_TYPE="${DFTRACER_BUILD_TYPE:-Release}"
PYTHON_EXE="${PYTHON_EXE:-}"
USE_PYTHON="${USE_PYTHON:-auto}"
PYTHON_EXPLICITLY_SET="0"
BUILD_DEPENDENCIES="${DFTRACER_BUILD_DEPENDENCIES:-1}"
ENABLE_TESTS="${DFTRACER_ENABLE_TESTS:-OFF}"
ENABLE_FTRACING="${DFTRACER_ENABLE_FTRACING:-OFF}"
ENABLE_HIP_TRACING="${DFTRACER_ENABLE_HIP_TRACING:-OFF}"
ENABLE_MPI="${DFTRACER_ENABLE_MPI:-OFF}"
ENABLE_HDF5="${DFTRACER_ENABLE_HDF5:-OFF}"
ENABLE_DYNAMIC_DETECTION="${DFTRACER_ENABLE_DYNAMIC_DETECTION:-OFF}"
GENERATE_INTERFACES="${DFTRACER_GENERATE_INTERFACES:-OFF}"
DISABLE_HWLOC="${DFTRACER_DISABLE_HWLOC:-ON}"
ENABLE_DLIO_TESTS="${DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS:-OFF}"
ENABLE_PAPER_TESTS="${DFTRACER_ENABLE_PAPER_TESTS:-OFF}"
CMAKE_ARGS="${DFTRACER_CMAKE_ARGS:-}"
HDF5_ROOT_DIR="${DFTRACER_HDF5_ROOT:-}"
MPI_ROOT_DIR="${DFTRACER_MPI_ROOT:-}"
C_COMPILER="${DFTRACER_C_COMPILER:-}"
CXX_COMPILER="${DFTRACER_CXX_COMPILER:-}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
CLEAN_BUILD="${CLEAN_BUILD:-0}"
CLEAN_INSTALL="${CLEAN_INSTALL:-0}"
INSTALL_MODE="${INSTALL_MODE:-pip}"  # pip or cmake
INSTALL_DFANALYZER="${INSTALL_DFANALYZER:-0}"  # Install dfanalyzer extras
DRY_RUN="${DRY_RUN:-0}"
VERBOSE="${VERBOSE:-0}"
QUIET="${QUIET:-0}"
ENABLE_COVERAGE="${ENABLE_COVERAGE:-0}"  # Build with coverage support
RUN_SMOKE_TEST="${RUN_SMOKE_TEST:-1}"
RUN_VALGRIND_CTEST="${RUN_VALGRIND_CTEST:-0}"
VALGRIND_CTEST_TIMEOUT="${VALGRIND_CTEST_TIMEOUT:-900}"
VALGRIND_DLIO_TIMEOUT="${VALGRIND_DLIO_TIMEOUT:-3600}"
SKIP_BUILD_RUN_TESTS="${SKIP_BUILD_RUN_TESTS:-0}"
RUN_VALGRIND_DLIO="${RUN_VALGRIND_DLIO:-auto}"
RUN_PR_CI_LOCAL="${RUN_PR_CI_LOCAL:-0}"
PR_CI_VENV_DIR="${PR_CI_VENV_DIR:-${SCRIPT_DIR}/.pr-ci-venv}"
PR_CI_MASTER_LOG="${PR_CI_MASTER_LOG:-${BUILD_DIR}/ci-local.log}"
SKIP_HDF5_TRACE_CI="${SKIP_HDF5_TRACE_CI:-0}"
HDF5_CI_CACHE_DIR="${HDF5_CI_CACHE_DIR:-${SCRIPT_DIR}/.hdf5-ci-cache}"
START_FROM_STEP="${START_FROM_STEP:-1}"

# Print usage
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Automated build script for DFTracer. Installs dependencies and builds DFTracer.

OPTIONS:
    -h, --help              Show this help message
    --build-dir DIR         Build directory (default: ./build)
    --install-prefix DIR    Install prefix (default: ./install)
    --build-type TYPE       Build type: Release, Debug, RelWithDebInfo, PROFILE (default: Release)
    --python PATH           Python executable path (enables Python support, default: none)
    --skip-deps             Skip building dependencies
    --enable-tests          Enable tests
    --enable-coverage       Enable coverage analysis (sets build type to PROFILE and enables tests)
    --enable-ftracing       Enable function tracing
    --enable-hip            Enable HIP tracing
    --enable-mpi            Enable MPI support
    --enable-hdf5           Enable HDF5 support
    --with-hdf5 DIR         Path to custom HDF5 installation (sets HDF5_ROOT for CMake)
    --with-mpi DIR          Path to custom MPI installation (sets MPI_HOME for CMake)
    --with-c-compiler PATH  C compiler to use (sets CMAKE_C_COMPILER)
    --with-cxx-compiler PATH C++ compiler to use (sets CMAKE_CXX_COMPILER)
    --enable-dynamic-detection Enable dynamic detection of MPI, HWLOC, and HIP at runtime
    --generate-interfaces   Generate Brahma/DFTracer interfaces from discovered MPI/HDF5 headers
    --enable-hwloc          Enable HWLOC (default: disabled)
    --enable-dlio-tests     Enable DLIO benchmark tests
    --enable-paper-tests    Enable paper tests
    --jobs N                Number of parallel jobs (default: auto-detected)
    --clean                 Clean build directory before building
    --clean-install         Remove all DFTracer installations from system/venv (site-packages, bin, lib, lib64)
    --with-dfanalyzer       Install dfanalyzer dependencies (for analysis tools)
    --install-mode MODE     Installation mode: pip or cmake (default: pip)
    --dry-run               Show what would be done without executing
    --quite, --quiet        Noninteractive mode: answer yes to prompts
    --verbose, -v           Enable verbose output
    --skip-smoke-test       Skip post-build dftracer_service start/stop smoke test
    --run-valgrind-ctest    Run CTest tests under valgrind after build (enables tests)
    --valgrind-ctest-timeout SECONDS
                            Timeout per Valgrind CTest case (default: 900)
    --run-valgrind-dlio     Run DLIO benchmark workloads under valgrind if dlio_benchmark is installed
    --skip-valgrind-dlio    Skip the optional DLIO benchmark valgrind gate
    --skip-build-run-tests  Skip build/install and run tests from an existing build tree
    --run-pr-ci-local       Run local pre-push CI suite: clean build + format check + CTest + valgrind gates + install dlio_benchmark + benchmark + HDF5 trace CI
    --skip-hdf5-trace-ci    Skip the HDF5+MPI multi-version trace verification stage in --run-pr-ci-local
    --list-steps            List all --run-pr-ci-local steps with their numbers and exit
    --skip-to-step N        Start --run-pr-ci-local from step N (skips earlier steps)

ENVIRONMENT VARIABLES (same as setup.py):
    DFTRACER_BUILD_TYPE                     Build type (Release/Debug)
    DFTRACER_BUILD_DEPENDENCIES             Build dependencies (1/0, default: 1)
    DFTRACER_ENABLE_TESTS                   Enable tests (ON/OFF)
    DFTRACER_ENABLE_FTRACING                Enable function tracing (ON/OFF)
    DFTRACER_ENABLE_HIP_TRACING             Enable HIP tracing (ON/OFF)
    DFTRACER_ENABLE_MPI                     Enable MPI (ON/OFF)
    DFTRACER_ENABLE_HDF5                    Enable HDF5 (ON/OFF)
    DFTRACER_ENABLE_DYNAMIC_DETECTION       Enable dynamic detection (ON/OFF)
    DFTRACER_GENERATE_INTERFACES            Generate interfaces from system headers (ON/OFF)
    DFTRACER_DISABLE_HWLOC                  Disable HWLOC (ON/OFF)
    DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS    Enable DLIO tests (ON/OFF)
    DFTRACER_ENABLE_PAPER_TESTS             Enable paper tests (ON/OFF)
    DFTRACER_CMAKE_ARGS                     Additional CMake arguments (semicolon-separated)
    DFTRACER_HDF5_ROOT                      Custom HDF5 installation prefix (sets HDF5_ROOT)
    DFTRACER_MPI_ROOT                       Custom MPI installation prefix (sets MPI_HOME)
    DFTRACER_C_COMPILER                     C compiler path (sets CMAKE_C_COMPILER)
    DFTRACER_CXX_COMPILER                   C++ compiler path (sets CMAKE_CXX_COMPILER)
    DFTRACER_INSTALL_DIR                    Installation directory
    DFTRACER_PYTHON_SITE                    Python site-packages directory
    QUIET                                   Noninteractive mode (1/0)
    RUN_VALGRIND_CTEST                      Run CTest valgrind gates after build (1/0)
    VALGRIND_CTEST_TIMEOUT                  Timeout per Valgrind CTest case in seconds
    VALGRIND_DLIO_TIMEOUT                   Timeout per DLIO Valgrind workload in seconds
    SKIP_BUILD_RUN_TESTS                    Skip build/install and run existing tests (1/0)
    SKIP_HDF5_TRACE_CI                      Skip HDF5+MPI trace verification stage (1/0)
    HDF5_CI_CACHE_DIR                       Directory for cached HDF5 source builds (default: .hdf5-ci-cache)
    RUN_VALGRIND_DLIO                       Run DLIO valgrind gate (auto/1/0)
    RUN_PR_CI_LOCAL                         Run full local PR-CI-equivalent checks (1/0, quiet stage progress and failure-only logs)
    PR_CI_VENV_DIR                          Project-local dedicated virtualenv for --run-pr-ci-local
    PR_CI_MASTER_LOG                        Master log file for --run-pr-ci-local output

EXAMPLES:
    # Basic build with pip installation (recommended)
    $0

    # Build with tests enabled
    $0 --enable-tests

    # Build with coverage analysis support
    $0 --enable-coverage

    # Clean build with custom install prefix
    $0 --clean --install-prefix /usr/local

    # Build with CMake installation
    $0 --install-mode cmake

    # Build with MPI support
    $0 --enable-mpi

    # Build with MPI + HDF5 and generated interfaces
    $0 --enable-mpi --enable-hdf5 --generate-interfaces

    # Build against a custom HDF5 install (also enables HDF5 automatically)
    $0 --with-hdf5 /path/to/hdf5-install --enable-mpi --enable-tests

    # Build with a custom MPI and HDF5 using MPI wrapper compilers
    $0 --with-mpi /path/to/mpi --with-hdf5 /path/to/hdf5 \
       --with-c-compiler mpicc --with-cxx-compiler mpicxx \
       --enable-tests --install-mode cmake

    # Build with dfanalyzer for analysis tools
    $0 --with-dfanalyzer

    # Debug build with all tests
    $0 --build-type Debug --enable-tests --enable-dlio-tests --enable-paper-tests

    # Clean all DFTracer installations
    $0 --clean-install

    # Dry run to see what would be executed
    $0 --dry-run --enable-tests

    # Verbose output for debugging
    $0 --verbose --enable-tests

    # Noninteractive clean install
    $0 --clean-install --quite

    # Build tests and run CTest Valgrind gates
    $0 --python python3 --enable-mpi --enable-hdf5 --run-valgrind-ctest

    # Run existing CTests without rebuilding
    $0 --skip-build-run-tests

    # Run existing CTests under Valgrind without rebuilding
    $0 --python python3 --skip-build-run-tests --run-valgrind-ctest

    # Run existing CTests and DLIO workloads under Valgrind when dlio_benchmark is installed
    $0 --python python3 --skip-build-run-tests --run-valgrind-ctest --run-valgrind-dlio

    # Clean build and run local pre-push CI-equivalent checks
    $0 --python python3 --run-pr-ci-local

COVERAGE ANALYSIS WORKFLOW:
    # 1. Build with coverage support
    $0 --enable-coverage

    # 2. Run tests and generate coverage report
    ./script/coverage_after_autobuild.sh

    # 3. Generate detailed report for analysis
    ./script/generate_coverage_report.sh > coverage_report.txt

    # 4. View HTML report
    open build/coverage/html/index.html

EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --install-prefix)
            INSTALL_PREFIX="$2"
            export DFTRACER_INSTALL_DIR="$2"
            shift 2
            ;;
        --build-type)
            BUILD_TYPE="$2"
            export DFTRACER_BUILD_TYPE="$2"
            shift 2
            ;;
        --python)
            PYTHON_EXE="$2"
            USE_PYTHON="yes"
            PYTHON_EXPLICITLY_SET="1"
            shift 2
            ;;
        --skip-deps)
            BUILD_DEPENDENCIES="0"
            export DFTRACER_BUILD_DEPENDENCIES="0"
            shift
            ;;
        --enable-tests)
            ENABLE_TESTS="ON"
            export DFTRACER_ENABLE_TESTS="ON"
            shift
            ;;
        --enable-coverage)
            ENABLE_COVERAGE="1"
            BUILD_TYPE="PROFILE"
            ENABLE_TESTS="ON"
            export DFTRACER_BUILD_TYPE="PROFILE"
            export DFTRACER_ENABLE_TESTS="ON"
            shift
            ;;
        --enable-ftracing)
            ENABLE_FTRACING="ON"
            export DFTRACER_ENABLE_FTRACING="ON"
            shift
            ;;
        --enable-hip)
            ENABLE_HIP_TRACING="ON"
            export DFTRACER_ENABLE_HIP_TRACING="ON"
            shift
            ;;
        --enable-mpi)
            ENABLE_MPI="ON"
            export DFTRACER_ENABLE_MPI="ON"
            shift
            ;;
        --enable-hdf5)
            ENABLE_HDF5="ON"
            export DFTRACER_ENABLE_HDF5="ON"
            shift
            ;;
        --enable-dynamic-detection)
            ENABLE_DYNAMIC_DETECTION="ON"
            export DFTRACER_ENABLE_DYNAMIC_DETECTION="ON"
            shift
            ;;
        --generate-interfaces)
            GENERATE_INTERFACES="ON"
            export DFTRACER_GENERATE_INTERFACES="ON"
            shift
            ;;
        --enable-hwloc)
            DISABLE_HWLOC="OFF"
            export DFTRACER_DISABLE_HWLOC="OFF"
            shift
            ;;
        --enable-dlio-tests)
            ENABLE_DLIO_TESTS="ON"
            export DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS="ON"
            shift
            ;;
        --enable-paper-tests)
            ENABLE_PAPER_TESTS="ON"
            export DFTRACER_ENABLE_PAPER_TESTS="ON"
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --clean)
            CLEAN_BUILD="1"
            shift
            ;;
        --clean-install)
            CLEAN_INSTALL="1"
            shift
            ;;
        --with-dfanalyzer)
            INSTALL_DFANALYZER="1"
            shift
            ;;
        --install-mode)
            INSTALL_MODE="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN="1"
            shift
            ;;
        --quite|--quiet)
            QUIET="1"
            shift
            ;;
        --verbose|-v)
            VERBOSE="1"
            shift
            ;;
        --skip-smoke-test)
            RUN_SMOKE_TEST="0"
            shift
            ;;
        --run-valgrind-ctest)
            RUN_VALGRIND_CTEST="1"
            ENABLE_TESTS="ON"
            export DFTRACER_ENABLE_TESTS="ON"
            shift
            ;;
        --valgrind-ctest-timeout)
            VALGRIND_CTEST_TIMEOUT="$2"
            shift 2
            ;;
        --run-valgrind-dlio)
            RUN_VALGRIND_DLIO="1"
            ENABLE_TESTS="ON"
            export DFTRACER_ENABLE_TESTS="ON"
            shift
            ;;
        --skip-valgrind-dlio)
            RUN_VALGRIND_DLIO="0"
            shift
            ;;
        --skip-build-run-tests|--skip-build)
            SKIP_BUILD_RUN_TESTS="1"
            ENABLE_TESTS="ON"
            export DFTRACER_ENABLE_TESTS="ON"
            shift
            ;;
        --run-pr-ci-local)
            RUN_PR_CI_LOCAL="1"
            CLEAN_BUILD="1"
            ENABLE_TESTS="ON"
            ENABLE_MPI="ON"
            ENABLE_HDF5="ON"
            RUN_VALGRIND_CTEST="1"
            RUN_VALGRIND_DLIO="1"
            export DFTRACER_ENABLE_TESTS="ON"
            export DFTRACER_ENABLE_MPI="ON"
            export DFTRACER_ENABLE_HDF5="ON"
            shift
            ;;
        --skip-hdf5-trace-ci)
            SKIP_HDF5_TRACE_CI="1"
            shift
            ;;
        --list-steps)
            # Handled after functions are defined; set a flag and continue parsing.
            LIST_STEPS="1"
            shift
            ;;
        --skip-to-step)
            START_FROM_STEP="$2"
            RUN_PR_CI_LOCAL="1"
            shift 2
            ;;
        --with-hdf5)
            HDF5_ROOT_DIR="$2"
            export DFTRACER_HDF5_ROOT="$2"
            ENABLE_HDF5="ON"
            export DFTRACER_ENABLE_HDF5="ON"
            shift 2
            ;;
        --with-mpi)
            MPI_ROOT_DIR="$2"
            export DFTRACER_MPI_ROOT="$2"
            ENABLE_MPI="ON"
            export DFTRACER_ENABLE_MPI="ON"
            shift 2
            ;;
        --with-c-compiler)
            C_COMPILER="$2"
            export DFTRACER_C_COMPILER="$2"
            shift 2
            ;;
        --with-cxx-compiler)
            CXX_COMPILER="$2"
            export DFTRACER_CXX_COMPILER="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            usage
            exit 1
            ;;
    esac
done

# Helper function for executing commands
execute_cmd() {
    local description="$1"
    shift
    
    if [ "$VERBOSE" = "1" ]; then
        echo -e "${BLUE}[VERBOSE] ${description}${NC}"
        echo -e "${BLUE}[VERBOSE] Command: $*${NC}"
    fi
    
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: $*${NC}"
        return 0
    else
        "$@"
    fi
}

DFTRACER_TEST_LD_LIBRARY_PATH_HINT="${DFTRACER_TEST_LD_LIBRARY_PATH:-}"

append_test_ld_path() {
    local _path="$1"
    if [ -z "${_path}" ] || [ ! -d "${_path}" ]; then
        return 0
    fi

    case ":${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}:" in
        *":${_path}:"*) ;;
        *)
            DFTRACER_TEST_LD_LIBRARY_PATH_HINT="${DFTRACER_TEST_LD_LIBRARY_PATH_HINT:+${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}:}${_path}"
            ;;
    esac
}

path_has_required_cxx_runtime() {
    local _path="$1"
    local _lib=""

    for _lib in "${_path}/libstdc++.so.6" "${_path}/libstdc++.so"; do
        if [ -f "${_lib}" ] && strings "${_lib}" 2>/dev/null | grep -q "GLIBCXX_3.4.29" && \
           strings "${_lib}" 2>/dev/null | grep -q "CXXABI_1.3.13"; then
            return 0
        fi
    done

    return 1
}

collect_cxx_runtime_library_paths() {
    local _path=""
    local _compiler=""
    local _lib=""
    local _lib_name=""
    local _candidate_dirs=()

    if [ -n "${DFTRACER_CXX_RUNTIME_DIR:-}" ]; then
        _candidate_dirs+=("${DFTRACER_CXX_RUNTIME_DIR}")
    fi

    local _old_ifs="${IFS}"
    IFS=":"
    for _path in ${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}; do
        if [ -n "${_path}" ]; then
            _candidate_dirs+=("${_path}")
        fi
    done
    IFS="${_old_ifs}"

    for _compiler in "${CXX_COMPILER:-}" "${CXX:-}" c++ g++ CC; do
        if [ -z "${_compiler}" ] || ! command -v "${_compiler}" >/dev/null 2>&1; then
            continue
        fi
        for _lib_name in libstdc++.so.6 libstdc++.so; do
            _lib="$("${_compiler}" -print-file-name="${_lib_name}" 2>/dev/null || true)"
            if [ -n "${_lib}" ] && [ -f "${_lib}" ]; then
                _candidate_dirs+=("$(dirname "${_lib}")")
            fi
        done
    done

    for _path in "${_candidate_dirs[@]}"; do
        if [ -d "${_path}" ] && path_has_required_cxx_runtime "${_path}"; then
            append_test_ld_path "${_path}"
            return 0
        fi
    done
}

collect_python_runtime_library_paths() {
    if [ -z "${PYTHON_EXE}" ] || ! command -v "${PYTHON_EXE}" >/dev/null 2>&1; then
        return 0
    fi

    local _python_ld_paths
    _python_ld_paths="$("${PYTHON_EXE}" -c 'import os, sysconfig
paths = [
    sysconfig.get_config_var("LIBDIR"),
    sysconfig.get_config_var("LIBPL"),
]
seen = []
for path in paths:
    if path and os.path.isdir(path) and path not in seen:
        seen.append(path)
print(":".join(seen))' 2>/dev/null || true)"

    local _old_ifs="${IFS}"
    IFS=":"
    for _path in ${_python_ld_paths}; do
        append_test_ld_path "${_path}"
    done
    IFS="${_old_ifs}"
}

collect_hdf5_runtime_library_paths() {
    local _hdf5_root="$1"
    for _path in "${_hdf5_root}/lib" "${_hdf5_root}/lib64"; do
        append_test_ld_path "${_path}"
    done
}

append_cmake_test_ld_library_path_arg() {
    if [ -n "${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}" ]; then
        CMAKE_FULL_ARGS+=("-DDFTRACER_TEST_LD_LIBRARY_PATH=${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}")
    fi
}

prepend_runtime_hint_to_rpath() {
    local file_path="$1"

    if [ -z "${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}" ] || ! command -v patchelf >/dev/null 2>&1; then
        return 0
    fi
    if [ ! -f "${file_path}" ]; then
        return 0
    fi

    local old_rpath=""
    old_rpath="$(patchelf --print-rpath "${file_path}" 2>/dev/null || true)"
    if [ -z "${old_rpath}" ]; then
        return 0
    fi

    local new_rpath=""
    local _old_ifs="${IFS}"
    local path_entry=""
    IFS=":"
    for path_entry in ${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}:${old_rpath}; do
        if [ -z "${path_entry}" ]; then
            continue
        fi
        case ":${new_rpath}:" in
            *":${path_entry}:"*) ;;
            *) new_rpath="${new_rpath:+${new_rpath}:}${path_entry}" ;;
        esac
    done
    IFS="${_old_ifs}"

    if [ -n "${new_rpath}" ] && [ "${new_rpath}" != "${old_rpath}" ]; then
        patchelf --set-rpath "${new_rpath}" "${file_path}" 2>/dev/null || true
    fi
}

refresh_existing_runtime_rpaths() {
    local ctest_build_dir="$1"

    if [ -z "${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}" ]; then
        return 0
    fi
    if ! command -v patchelf >/dev/null 2>&1; then
        echo -e "${YELLOW}Warning: patchelf not found; existing binaries may still prefer stale RPATH entries${NC}"
        return 0
    fi

    echo -e "${BLUE}Refreshing runtime RPATHs without rebuilding...${NC}"
    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would prepend ${DFTRACER_TEST_LD_LIBRARY_PATH_HINT} to existing ELF RPATHs${NC}"
        return 0
    fi

    local search_roots=(
        "${ctest_build_dir}/bin"
        "${ctest_build_dir}/lib"
        "${ctest_build_dir}/lib64"
        "${INSTALL_PREFIX}/bin"
        "${INSTALL_PREFIX}/lib"
        "${INSTALL_PREFIX}/lib64"
    )
    if [ -n "${VIRTUAL_ENV}" ]; then
        search_roots+=("${VIRTUAL_ENV}/bin")
    fi
    if [ -n "${CONDA_PREFIX}" ]; then
        search_roots+=("${CONDA_PREFIX}/bin")
    fi

    local search_root=""
    local file_path=""
    for search_root in "${search_roots[@]}"; do
        if [ ! -d "${search_root}" ]; then
            continue
        fi
        while IFS= read -r -d '' file_path; do
            prepend_runtime_hint_to_rpath "${file_path}"
        done < <(find "${search_root}" -type f -print0)
    done
}

run_ci_logged_cmd() {
    local step_name="$1"
    shift

    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: $*${NC}"
        return 0
    fi

    if [ "${RUN_PR_CI_LOCAL}" != "1" ]; then
        "$@"
        return $?
    fi

    local ci_log_dir="${BUILD_DIR}/ci-local-logs"
    mkdir -p "${ci_log_dir}"
    local safe_name
    safe_name="$(echo "${step_name}" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9._-]/_/g')"
    local step_log="${ci_log_dir}/setup-${safe_name}.log"

    echo -e "${BLUE}[CI setup] ${step_name}...${NC}"
    echo -e "${BLUE}[CI setup] log: ${step_log}${NC}"
    if "$@" >"${step_log}" 2>&1; then
        echo -e "${GREEN}[CI setup] OK: ${step_name}${NC}"
        return 0
    fi

    echo -e "${RED}[CI setup] FAIL: ${step_name}${NC}"
    echo -e "${YELLOW}--- stage log: ${step_log} ---${NC}"
    cat "${step_log}"
    echo -e "${YELLOW}--- end stage log ---${NC}"
    return 1
}

format_ci_duration() {
    local total_seconds="$1"
    if [ -z "${total_seconds}" ] || [ "${total_seconds}" -lt 0 ] 2>/dev/null; then
        total_seconds=0
    fi

    local hours=$((total_seconds / 3600))
    local minutes=$(((total_seconds % 3600) / 60))
    local seconds=$((total_seconds % 60))
    printf "%02d:%02d:%02d" "${hours}" "${minutes}" "${seconds}"
}

report_pr_ci_total_elapsed() {
    local status_label="$1"

    if [ "${RUN_PR_CI_LOCAL}" != "1" ]; then
        return 0
    fi

    if [ -z "${PR_CI_TOTAL_START_TS:-}" ]; then
        return 0
    fi

    local now_ts
    now_ts="$(date +%s)"
    local total_elapsed="$(( now_ts - PR_CI_TOTAL_START_TS ))"
    echo -e "${GREEN}Local PR CI ${status_label} total elapsed: $(format_ci_duration "${total_elapsed}")${NC}"
}

ci_stage_needs_progress_tracker() {
    local stage_name="$1"
    case "${stage_name}" in
        *CTest*|*Valgrind*|*DLIO*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

ci_get_expected_ctest_total() {
    local ctest_build_dir=""
    if ! ctest_build_dir="$(find_ctest_build_dir 2>/dev/null)"; then
        return 1
    fi

    local ctest_listing=""
    ctest_listing="$(ctest --test-dir "${ctest_build_dir}" -N 2>/dev/null || true)"
    local total_tests=""
    total_tests="$(echo "${ctest_listing}" | sed -n 's/^[[:space:]]*Total Tests:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | tail -n 1)"

    if [[ "${total_tests}" =~ ^[0-9]+$ ]] && [ "${total_tests}" -gt 0 ]; then
        echo "${total_tests}"
        return 0
    fi

    return 1
}

ci_extract_progress_from_log() {
    local stage_log="$1"
    local stage_name="$2"
    local expected_total="$3"

    if [ ! -f "${stage_log}" ]; then
        return 0
    fi

    local progress_line=""

    if [[ "${stage_name}" == *"CTest (non-valgrind)"* ]]; then
        progress_line="$(grep -E '^[[:space:]]*[0-9]+/[0-9]+[[:space:]]+Test[[:space:]]*#' "${stage_log}" | tail -n 1)"
        if [ -n "${progress_line}" ]; then
            local done_count
            done_count="$(echo "${progress_line}" | sed -n 's/^[[:space:]]*\([0-9][0-9]*\)\/[0-9][0-9]*[[:space:]]\+Test[[:space:]]*#.*/\1/p')"
            if [[ "${done_count}" =~ ^[0-9]+$ ]]; then
                if [[ "${expected_total}" =~ ^[0-9]+$ ]] && [ "${expected_total}" -gt 0 ]; then
                    echo "progress ${done_count}/${expected_total}"
                else
                    local parsed_total
                    parsed_total="$(echo "${progress_line}" | sed -n 's/^[[:space:]]*[0-9][0-9]*\/\([0-9][0-9]*\)[[:space:]]\+Test[[:space:]]*#.*/\1/p')"
                    echo "progress ${done_count}/${parsed_total}"
                fi
                return 0
            fi
        fi
    fi

    if [[ "${stage_name}" == *"Valgrind CTests"* ]]; then
        local run_count=""
        run_count="$(grep -Ec '\[valgrind-ctest\][[:space:]]+RUN[[:space:]]|\[valgrind-python-ctest\][[:space:]]+RUN[[:space:]]' "${stage_log}")"
        if [[ "${run_count}" =~ ^[0-9]+$ ]] && [ "${run_count}" -gt 0 ]; then
            if [[ "${expected_total}" =~ ^[0-9]+$ ]] && [ "${expected_total}" -gt 0 ]; then
                echo "progress ${run_count}/${expected_total}"
            else
                echo "progress ${run_count}"
            fi
            return 0
        fi
    fi

    # CTest-style progress (% tests passed)
    progress_line="$(grep -E '^[[:space:]]*[0-9]{1,3}% tests passed' "${stage_log}" | tail -n 1)"
    if [ -n "${progress_line}" ]; then
        echo "${progress_line}"
        return 0
    fi

    # Fractional progress from test runners (e.g. [12/48], 12/48 Test #, or 12/48 tests).
    # Keep this strict to avoid matching transfer logs like 532.2/532.2 MB.
    progress_line="$(grep -E '(^|[[:space:]])\[?[0-9]+/[0-9]+\]?([[:space:]]+(Test|tests|test)|$)' "${stage_log}" | tail -n 1 | sed -E 's/^.*(\[?[0-9]+\/[0-9]+\]?).*$/\1/')"
    if [ -n "${progress_line}" ]; then
        echo "progress ${progress_line}"
        return 0
    fi

    # Last completed/started CTest test marker
    progress_line="$(grep -E 'Test #[0-9]+:|Start [0-9]+:' "${stage_log}" | tail -n 1)"
    if [ -n "${progress_line}" ]; then
        echo "${progress_line}"
        return 0
    fi

    return 0
}

ci_progress_indicates_complete() {
    local progress_text="$1"

    if [[ "${progress_text}" =~ (^|[^0-9])100%([^0-9]|$) ]]; then
        return 0
    fi

    if [[ "${progress_text}" =~ ([0-9]+)[[:space:]]*/[[:space:]]*([0-9]+) ]]; then
        if [ "${BASH_REMATCH[1]}" -eq "${BASH_REMATCH[2]}" ]; then
            return 0
        fi
    fi

    return 1
}

ci_parse_fraction_progress() {
    local progress_text="$1"
    if [[ "${progress_text}" =~ ([0-9]+)[[:space:]]*/[[:space:]]*([0-9]+) ]]; then
        echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
        return 0
    fi
    return 1
}

ci_progress_is_regression() {
    local current_progress="$1"
    local previous_progress="$2"

    if [ -z "${previous_progress}" ]; then
        return 1
    fi

    local current_parts=""
    local previous_parts=""
    if ! current_parts="$(ci_parse_fraction_progress "${current_progress}")"; then
        return 1
    fi
    if ! previous_parts="$(ci_parse_fraction_progress "${previous_progress}")"; then
        return 1
    fi

    local current_done current_total previous_done previous_total
    read -r current_done current_total <<<"${current_parts}"
    read -r previous_done previous_total <<<"${previous_parts}"

    if [ "${current_total}" -ne "${previous_total}" ]; then
        return 1
    fi

    if [ "${current_done}" -lt "${previous_done}" ]; then
        return 0
    fi

    return 1
}

find_ctest_build_dir() {
    local candidate=""
    local python_abi_tag=""
    local candidates=()
    local viable_candidates=()

    if [ -d "${BUILD_DIR}" ]; then
        while IFS= read -r candidate; do
            candidates+=("${candidate}")
        done < <(find "${BUILD_DIR}" -type d -name "dftracer.dftracer" | sort)
    fi

    if [ ${#candidates[@]} -eq 0 ]; then
        return 1
    fi

    if [ -n "${PYTHON_EXE}" ] && command -v "${PYTHON_EXE}" >/dev/null 2>&1; then
        python_abi_tag="$("${PYTHON_EXE}" -c 'import sysconfig; print(sysconfig.get_config_var("SOABI") or "")' 2>/dev/null || true)"
    fi

    for candidate in "${candidates[@]}"; do
        local total_tests=""
        total_tests="$(ctest --test-dir "${candidate}" -N 2>/dev/null | sed -n 's/^[[:space:]]*Total Tests:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | tail -n 1)"
        if [[ "${total_tests}" =~ ^[0-9]+$ ]] && [ "${total_tests}" -gt 0 ]; then
            viable_candidates+=("${candidate}")
        fi
    done

    if [ ${#viable_candidates[@]} -eq 0 ]; then
        return 1
    fi

    if [ -n "${python_abi_tag}" ]; then
        for candidate in "${viable_candidates[@]}"; do
            if [[ "${candidate}" == *"${python_abi_tag}"* ]]; then
                realpath "${candidate}"
                return 0
            fi
        done
    fi

    realpath "${viable_candidates[0]}"
}

run_valgrind_ctest_tests() {
    if [ "${RUN_VALGRIND_CTEST}" != "1" ]; then
        return 0
    fi

    echo -e "${GREEN}=== Running CTest Valgrind Gates ===${NC}"

    if [ "${ENABLE_TESTS}" != "ON" ]; then
        echo -e "${RED}Error: Valgrind CTest run requires tests to be enabled${NC}"
        echo "Use --enable-tests or --run-valgrind-ctest."
        return 1
    fi

    if ! command -v valgrind &> /dev/null; then
        echo -e "${RED}Error: valgrind not found in PATH${NC}"
        return 1
    fi

    local ctest_build_dir
    if ! ctest_build_dir="$(find_ctest_build_dir)"; then
        echo -e "${RED}Error: Could not find CTest build directory under ${BUILD_DIR}${NC}"
        echo "Expected a directory named dftracer.dftracer."
        return 1
    fi

    echo "CTest build directory: ${ctest_build_dir}"

    local suppression_file="${SCRIPT_DIR}/test/valgrind/test_cpp_known_syscall.supp"
    local common_log_dir="${ctest_build_dir}/valgrind-ctest-autobuild"
    local python_log_dir="${ctest_build_dir}/valgrind-python-ctest-autobuild"
    local rc=0

    local ctest_valgrind_cmd=(
        python3 "${SCRIPT_DIR}/scripts/valgrind_ctest_runner.py"
        --build-dir "${ctest_build_dir}"
        --log-dir "${common_log_dir}"
        --summary-json "${common_log_dir}/summary.json"
        --log-level INFO
        --debug-rerun-on-failure
        --debug-log-level DEBUG
    )

    if [ -f "${suppression_file}" ]; then
        ctest_valgrind_cmd+=(--suppression "${suppression_file}")
    fi

    echo -e "${BLUE}Running non-Python CTests under valgrind...${NC}"
    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: ${ctest_valgrind_cmd[*]}${NC}"
    elif ! "${ctest_valgrind_cmd[@]}"; then
        rc=1
    fi

    if [ -f "${SCRIPT_DIR}/scripts/valgrind_ctest_python_runner.py" ]; then
        local python_runner="${PYTHON_EXE:-python3}"
        local python_valgrind_cmd=(
            "${python_runner}" "${SCRIPT_DIR}/scripts/valgrind_ctest_python_runner.py"
            --build-dir "${ctest_build_dir}"
            --log-dir "${python_log_dir}"
            --summary-json "${python_log_dir}/summary.json"
            --log-level INFO
            --timeout "${VALGRIND_CTEST_TIMEOUT}"
            --fail-on-project-leaks-only
        )

        if [ -f "${suppression_file}" ]; then
            python_valgrind_cmd+=(--suppression "${suppression_file}")
        fi

        echo -e "${BLUE}Running Python-backed CTests under valgrind...${NC}"
        if [ "${DRY_RUN}" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would execute: ${python_valgrind_cmd[*]}${NC}"
        elif ! "${python_valgrind_cmd[@]}"; then
            rc=1
        fi
    else
        echo -e "${YELLOW}Warning: Python CTest Valgrind runner not found; skipping Python-backed CTests${NC}"
    fi

    echo ""
    echo "Valgrind summaries:"
    echo "  ${common_log_dir}/summary.json"
    echo "  ${python_log_dir}/summary.json"
    echo ""

    if [ "${DRY_RUN}" != "1" ]; then
        if ! python3 - "${ctest_build_dir}" "${common_log_dir}/summary.json" "${python_log_dir}/summary.json" <<'PY'
import json
import subprocess
import sys
from pathlib import Path

build_dir = Path(sys.argv[1])
common_summary = Path(sys.argv[2])
python_summary = Path(sys.argv[3])

if not common_summary.is_file():
    print(f"[valgrind-coverage] missing summary file: {common_summary}")
    sys.exit(1)
if not python_summary.is_file():
    print(f"[valgrind-coverage] missing summary file: {python_summary}")
    sys.exit(1)

raw = subprocess.check_output(["ctest", "--show-only=json-v1"], cwd=build_dir, text=True)
ctest_total = len(json.loads(raw).get("tests", []))

with common_summary.open("r", encoding="utf-8") as f:
    common = json.load(f)
with python_summary.open("r", encoding="utf-8") as f:
    py = json.load(f)

common_selected = int(common.get("selected_tests", 0))
common_executed = int(common.get("valgrind_executed", 0))
common_skipped = int(common.get("wrapper_skipped", 0))
py_selected = int(py.get("selected_tests", 0))
py_executed = int(py.get("valgrind_executed", 0))

errors = []
if common_selected != ctest_total:
    errors.append(
        f"non-python runner selected {common_selected}, expected full ctest total {ctest_total}"
    )
if common_skipped != py_selected:
    errors.append(
        f"non-python valgrind_skipped={common_skipped} does not match python selected_tests={py_selected}"
    )
if py_selected != py_executed:
    errors.append(
        f"python runner selected_tests={py_selected} but valgrind_executed={py_executed}"
    )
if common_executed + py_executed != ctest_total:
    errors.append(
        "combined valgrind executed tests "
        f"({common_executed} non-python + {py_executed} python) "
        f"!= ctest total ({ctest_total})"
    )

if errors:
    print("[valgrind-coverage] FAIL: not all CTests were executed under valgrind")
    print(f"[valgrind-coverage] ctest_total={ctest_total}")
    print(
        "[valgrind-coverage] non_python: "
        f"selected={common_selected} executed={common_executed} skipped={common_skipped}"
    )
    print(
        "[valgrind-coverage] python: "
        f"selected={py_selected} executed={py_executed}"
    )
    for err in errors:
        print(f"[valgrind-coverage] {err}")
    sys.exit(1)

print(
    "[valgrind-coverage] PASS: all CTests executed under valgrind "
    f"(total={ctest_total}, non-python={common_executed}, python={py_executed})"
)
PY
        then
            rc=1
        fi
    fi

    return "${rc}"
}

run_valgrind_dlio_tests() {
    if [ "${RUN_VALGRIND_DLIO}" = "0" ]; then
        return 0
    fi

    if [ "${RUN_VALGRIND_CTEST}" != "1" ] && [ "${RUN_VALGRIND_DLIO}" != "1" ]; then
        return 0
    fi

    if [ ! -f "${SCRIPT_DIR}/scripts/valgrind_dlio_runner.py" ]; then
        echo -e "${YELLOW}Warning: DLIO Valgrind runner not found; skipping DLIO benchmark gate${NC}"
        return 0
    fi

    if ! command -v valgrind &> /dev/null; then
        echo -e "${RED}Error: valgrind not found in PATH${NC}"
        return 1
    fi

    if ! command -v gdb &> /dev/null; then
        echo -e "${RED}Error: gdb not found in PATH${NC}"
        return 1
    fi

    local python_runner="${PYTHON_EXE:-python3}"
    if ! "${python_runner}" -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('dlio_benchmark') else 1)" 2>/dev/null; then
        if [ "${RUN_VALGRIND_DLIO}" = "1" ]; then
            echo -e "${RED}Error: dlio_benchmark is not installed in ${python_runner}${NC}"
            echo "Install dlio_benchmark first, or use --skip-valgrind-dlio."
            return 1
        fi
        echo -e "${YELLOW}Skipping DLIO benchmark Valgrind gate: dlio_benchmark not installed in ${python_runner}${NC}"
        return 0
    fi

    local ctest_build_dir
    if ! ctest_build_dir="$(find_ctest_build_dir)"; then
        echo -e "${RED}Error: Could not find CTest build directory under ${BUILD_DIR}${NC}"
        echo "Expected a directory named dftracer.dftracer."
        return 1
    fi

    local suppression_file="${SCRIPT_DIR}/test/valgrind/test_cpp_known_syscall.supp"
    local dlio_log_dir="${ctest_build_dir}/valgrind-dlio-autobuild"
    local dlio_run_root="${ctest_build_dir}/dlio-valgrind-run"
    local dlio_cmd=(
        "${python_runner}" "${SCRIPT_DIR}/scripts/valgrind_dlio_runner.py"
        --log-dir "${dlio_log_dir}"
        --run-root "${dlio_run_root}"
        --summary-json "${dlio_log_dir}/summary.json"
        --timeout "${VALGRIND_DLIO_TIMEOUT}"
        --exclude-workload unet3d_a100_s3
        --exclude-workload unet3d_h100_s3
        --exclude-workload unet3d_v100_s3
        --fail-on-project-leaks-only
    )

    if [ -f "${suppression_file}" ]; then
        dlio_cmd+=(--suppression "${suppression_file}")
    fi

    echo -e "${BLUE}Running DLIO benchmark workloads under valgrind...${NC}"
    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: RDMAV_FORK_SAFE=1 ${dlio_cmd[*]}${NC}"
        return 0
    fi

    if ! RDMAV_FORK_SAFE=1 "${dlio_cmd[@]}"; then
        return 1
    fi

    echo "DLIO Valgrind summary:"
    echo "  ${dlio_log_dir}/summary.json"
    return 0
}

run_existing_ctest_tests() {
    echo -e "${GREEN}=== Running Existing CTest Tests ===${NC}"

    local ctest_build_dir
    if ! ctest_build_dir="$(find_ctest_build_dir)"; then
        echo -e "${RED}Error: Could not find CTest build directory under ${BUILD_DIR}${NC}"
        echo "Build once with tests enabled before using --skip-build-run-tests."
        return 1
    fi

    echo "CTest build directory: ${ctest_build_dir}"

    if [ -n "${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}" ]; then
        local cmake_refresh_cmd=(
            cmake
            "${SCRIPT_DIR}"
            "-DDFTRACER_TEST_LD_LIBRARY_PATH=${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}"
            "-DDFTRACER_ENABLE_TESTS=ON"
            "-DDFTRACER_INSTALL_DEPENDENCIES=OFF"
        )

        echo -e "${BLUE}Refreshing CTest metadata without rebuilding...${NC}"
        if [ "${DRY_RUN}" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would execute in ${ctest_build_dir}: ${cmake_refresh_cmd[*]}${NC}"
        elif ! (cd "${ctest_build_dir}" && "${cmake_refresh_cmd[@]}"); then
            echo -e "${RED}Error: failed to refresh CTest metadata in ${ctest_build_dir}${NC}"
            return 1
        fi
    fi
    refresh_existing_runtime_rpaths "${ctest_build_dir}"

    if [ "${USE_PYTHON}" = "yes" ] && [ -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
        local python_runner="${PYTHON_EXE:-python3}"
        echo -e "${BLUE}Ensuring Python test requirements are installed for CTest...${NC}"
        if [ "${DRY_RUN}" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would execute: ${python_runner} -m pip install -r ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
        elif ! "${python_runner}" -m pip install -r "${SCRIPT_DIR}/test/py/requirements.txt"; then
            echo -e "${RED}Error: failed to install Python test requirements${NC}"
            return 1
        fi
    fi

    if [ "${RUN_VALGRIND_CTEST}" = "1" ] || [ "${RUN_VALGRIND_DLIO}" = "1" ]; then
        local rc=0
        if [ "${RUN_VALGRIND_CTEST}" = "1" ]; then
            if ! run_valgrind_ctest_tests; then
                rc=1
            fi
        fi
        if ! run_valgrind_dlio_tests; then
            rc=1
        fi
        return "${rc}"
    fi

    local ctest_cmd=(
        ctest
        --test-dir "${ctest_build_dir}"
        --output-on-failure
    )

    echo -e "${BLUE}Running CTests without rebuilding...${NC}"
    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: ${ctest_cmd[*]}${NC}"
        return 0
    fi

    "${ctest_cmd[@]}"
}

run_format_check() {
    echo -e "${GREEN}=== Running Format Check (CI parity) ===${NC}"

    local format_script="${SCRIPT_DIR}/script/formatting/check-formatting.sh"
    if [ ! -x "${format_script}" ]; then
        echo -e "${RED}Error: format check script not found or not executable: ${format_script}${NC}"
        return 1
    fi

    local clang_format_bin=""
    if [ -x "/usr/bin/clang-format-19" ]; then
        clang_format_bin="/usr/bin/clang-format-19"
    elif command -v clang-format-19 &> /dev/null; then
        clang_format_bin="$(command -v clang-format-19)"
    elif command -v clang-format &> /dev/null; then
        clang_format_bin="$(command -v clang-format)"
        echo -e "${YELLOW}Warning: clang-format-19 not found; using ${clang_format_bin}${NC}"
    else
        echo -e "${RED}Error: clang-format is not installed (required by format-check workflow)${NC}"
        return 1
    fi

    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: ${format_script} ${clang_format_bin}${NC}"
        return 0
    fi

    "${format_script}" "${clang_format_bin}"
}

run_non_valgrind_ctest_tests() {
    echo -e "${GREEN}=== Running CTest (non-valgrind, CI parity) ===${NC}"

    if [ "${ENABLE_TESTS}" != "ON" ]; then
        echo -e "${RED}Error: non-valgrind CTest run requires tests to be enabled${NC}"
        return 1
    fi

    local ctest_build_dir
    if ! ctest_build_dir="$(find_ctest_build_dir)"; then
        echo -e "${RED}Error: Could not find CTest build directory under ${BUILD_DIR}${NC}"
        return 1
    fi

    if [ "${USE_PYTHON}" = "yes" ] && [ -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
        local python_runner="${PYTHON_EXE:-python3}"
        echo -e "${BLUE}Ensuring Python test requirements are installed for CTest...${NC}"
        if ! "${python_runner}" -m pip install -r "${SCRIPT_DIR}/test/py/requirements.txt"; then
            echo -e "${RED}Error: failed to install Python test requirements${NC}"
            return 1
        fi
        if [ "${RUN_PR_CI_LOCAL}" = "1" ]; then
            echo -e "${BLUE}Pinning local PR-CI numpy/h5py to a stable ABI-compatible pair...${NC}"
            if ! "${python_runner}" -m pip install --force-reinstall --no-cache-dir "numpy==1.26.4" "h5py==3.9.0"; then
                echo -e "${RED}Error: failed to install compatible numpy/h5py for local PR-CI${NC}"
                return 1
            fi
        fi
        if ! "${python_runner}" -c "import numpy" >/dev/null 2>&1; then
            echo -e "${RED}Error: numpy import failed after installing test requirements${NC}"
            return 1
        fi
        if ! "${python_runner}" -c "import h5py" >/dev/null 2>&1; then
            echo -e "${RED}Error: h5py import failed after test requirements setup${NC}"
            return 1
        fi
        if [ "${RUN_PR_CI_LOCAL}" = "1" ]; then
            echo -e "${BLUE}Refreshing CMake test metadata after Python dependency updates...${NC}"
            if ! cmake -S "${SCRIPT_DIR}" -B "${ctest_build_dir}"; then
                echo -e "${RED}Error: failed to refresh CMake metadata for local PR-CI CTests${NC}"
                return 1
            fi
        fi
    fi

    # Mirror CI shell-level default; per-test env still applies and can override as needed.
    export DFTRACER_BIND_SIGNALS=1

    local ctest_cmd=(
        ctest
        --test-dir "${ctest_build_dir}"
        --output-on-failure
    )

    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: ${ctest_cmd[*]}${NC}"
        return 0
    fi

    "${ctest_cmd[@]}"
}

install_ior_if_missing() {
    if command -v ior &> /dev/null; then
        return 0
    fi

    echo -e "${YELLOW}Warning: ior executable not found in PATH. Attempting install...${NC}"

    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would attempt to install ior via package manager${NC}"
        return 0
    fi

    if command -v apt-get &> /dev/null; then
        echo "Detected apt-get, installing ior..."
        sudo apt-get update && sudo apt-get install -y ior || true
    elif command -v dnf &> /dev/null; then
        echo "Detected dnf, installing ior..."
        sudo dnf install -y ior || true
    elif command -v yum &> /dev/null; then
        echo "Detected yum, installing ior..."
        sudo yum install -y ior || true
    elif command -v zypper &> /dev/null; then
        echo "Detected zypper, installing ior..."
        sudo zypper --non-interactive install ior || true
    elif command -v brew &> /dev/null; then
        echo "Detected brew, installing ior..."
        brew install ior || true
    else
        echo -e "${RED}Error: Could not detect a supported package manager for ior installation${NC}"
    fi

    if ! command -v ior &> /dev/null; then
        echo -e "${RED}Error: ior is still not available in PATH after install attempt${NC}"
        echo "Install manually, then re-run PR-CI local suite."
        return 1
    fi

    echo -e "${GREEN}✓ ior is available: $(command -v ior)${NC}"
    return 0
}

run_ior_benchmark_tests() {
    echo -e "${GREEN}=== Running IOR Benchmark (local benchmark workflow parity) ===${NC}"

    if [ "${USE_PYTHON}" != "yes" ]; then
        echo -e "${RED}Error: benchmark parity run requires Python support${NC}"
        return 1
    fi

    if ! install_ior_if_missing; then
        return 1
    fi

    local preload_lib=""
    preload_lib="$(find "${BUILD_DIR}" -type f -name "libdftracer_preload.so" 2>/dev/null | head -n 1)"
    if [ -z "${preload_lib}" ] && [ -n "${VIRTUAL_ENV:-}" ]; then
        preload_lib="$(find "${VIRTUAL_ENV}" -type f -name "libdftracer_preload.so" 2>/dev/null | head -n 1)"
    fi
    if [ -z "${preload_lib}" ] && [ -d "${INSTALL_PREFIX}" ]; then
        preload_lib="$(find "${INSTALL_PREFIX}" -type f -name "libdftracer_preload.so" 2>/dev/null | head -n 1)"
    fi

    if [ -z "${preload_lib}" ]; then
        echo -e "${RED}Error: libdftracer_preload.so not found (benchmark workflow requires LD_PRELOAD mode)${NC}"
        return 1
    fi

    local benchmark_rules="${SCRIPT_DIR}/test/yaml/benchmark-rules.yaml"
    if [ ! -f "${benchmark_rules}" ]; then
        echo -e "${RED}Error: benchmark rules file missing: ${benchmark_rules}${NC}"
        return 1
    fi

    local analysis_script="${SCRIPT_DIR}/test/analysis_ior.py"
    if [ ! -f "${analysis_script}" ]; then
        echo -e "${RED}Error: benchmark analysis script missing: ${analysis_script}${NC}"
        return 1
    fi

    local benchmark_dir="${BUILD_DIR}/ci-benchmark"
    local modes=(trace none profile selective)
    local transfer_sizes=(4 1024)

    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would create benchmark dir: ${benchmark_dir}${NC}"
        echo -e "${YELLOW}[DRY-RUN] Would run ior matrix and generate cases.csv/overhead.csv${NC}"
        return 0
    fi

    mkdir -p "${benchmark_dir}"
    pushd "${benchmark_dir}" >/dev/null || return 1

    rm -f case-*.csv cases.csv overhead.csv testfile.dftracer*

    for mode in "${modes[@]}"; do
        for ts in "${transfer_sizes[@]}"; do
            if [ "${mode}" = "trace" ] || [ "${mode}" = "profile" ]; then
                export LD_PRELOAD="${preload_lib}"
            else
                unset LD_PRELOAD
            fi

            if [ "${mode}" = "profile" ]; then
                export DFTRACER_ENABLE_AGGREGATION="ON"
                unset DFTRACER_AGGREGATION_TYPE
                unset DFTRACER_AGGREGATION_FILE
            elif [ "${mode}" = "selective" ]; then
                export DFTRACER_ENABLE_AGGREGATION="1"
                export DFTRACER_AGGREGATION_TYPE="SELECTIVE"
                export DFTRACER_AGGREGATION_FILE="${benchmark_rules}"
            else
                unset DFTRACER_ENABLE_AGGREGATION
                unset DFTRACER_AGGREGATION_TYPE
                unset DFTRACER_AGGREGATION_FILE
            fi

            local summary_file="case-${mode}-${ts}.csv"
            local ior_cmd=(
                ior
                -w
                -r
                -i 5
                -t "${ts}k"
                -b "$((ts * 16))k"
                -o testfile.dftracer
                -O summaryFormat=CSV
                -O "summaryFile=${summary_file}"
            )
            echo "Running: ${ior_cmd[*]}"
            if ! "${ior_cmd[@]}"; then
                popd >/dev/null || true
                return 1
            fi

            awk 'BEGIN{FS=OFS=","} NR==1{$(NF+1)="mode"} NR>1{$(NF+1)="'"${mode}"'"} 1' "${summary_file}" > tmp.csv && mv tmp.csv "${summary_file}"

            unset LD_PRELOAD
            unset DFTRACER_ENABLE_AGGREGATION
            unset DFTRACER_AGGREGATION_TYPE
            unset DFTRACER_AGGREGATION_FILE
        done
    done

    head -n 1 case-trace-4.csv > cases.csv
    for f in case-*.csv; do
        tail -n +2 "$f" >> cases.csv
    done

    if ! "${PYTHON_EXE}" -m pip show pandas >/dev/null 2>&1; then
        "${PYTHON_EXE}" -m pip install pandas numpy >/dev/null
    fi
    if ! "${PYTHON_EXE}" "${analysis_script}" > overhead.csv; then
        popd >/dev/null || true
        return 1
    fi

    echo "Benchmark outputs:"
    echo "  ${benchmark_dir}/cases.csv"
    echo "  ${benchmark_dir}/overhead.csv"

    popd >/dev/null || true
    return 0
}

run_ci_stage() {
    local index="$1"
    local total="$2"
    local stage_name="$3"
    shift 3

    local ci_log_dir="${BUILD_DIR}/ci-local-logs"
    mkdir -p "${ci_log_dir}"
    local safe_name
    safe_name="$(echo "${stage_name}" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9._-]/_/g')"
    local stage_log="${ci_log_dir}/${index}-${safe_name}.log"

    local stage_start_ts
    stage_start_ts="$(date +%s)"
    echo -e "${BLUE}[CI ${index}/${total}] ${stage_name}...${NC}"
    echo -e "${BLUE}[CI ${index}/${total}] log: ${stage_log}${NC}"

    if [ "${DRY_RUN}" = "1" ]; then
        if "$@"; then
            local stage_elapsed="$(( $(date +%s) - stage_start_ts ))"
            echo -e "${GREEN}[CI ${index}/${total}] OK (dry-run): ${stage_name} (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
            return 0
        fi
        local stage_elapsed="$(( $(date +%s) - stage_start_ts ))"
        echo -e "${RED}[CI ${index}/${total}] FAIL (dry-run): ${stage_name} (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
        return 1
    fi

    if [ "${RUN_PR_CI_LOCAL}" = "1" ] && ci_stage_needs_progress_tracker "${stage_name}"; then
        "$@" >"${stage_log}" 2>&1 &
        local stage_pid="$!"
        local last_progress=""
        local last_heartbeat_ts="${stage_start_ts}"
        local expected_ctest_total=""

        if [[ "${stage_name}" == *"CTest"* ]] || [[ "${stage_name}" == *"Valgrind"* ]]; then
            expected_ctest_total="$(ci_get_expected_ctest_total || true)"
            if [[ "${expected_ctest_total}" =~ ^[0-9]+$ ]] && [ "${expected_ctest_total}" -gt 0 ]; then
                echo -e "${BLUE}[CI ${index}/${total}] ${stage_name} expected total tests from ctest -N: ${expected_ctest_total}${NC}"
            fi
        fi

        while kill -0 "${stage_pid}" 2>/dev/null; do
            sleep 15
            if ! kill -0 "${stage_pid}" 2>/dev/null; then
                break
            fi

            local now_ts
            now_ts="$(date +%s)"
            local stage_elapsed="$(( now_ts - stage_start_ts ))"
            local progress
            progress="$(ci_extract_progress_from_log "${stage_log}" "${stage_name}" "${expected_ctest_total}")"

            if [ -n "${progress}" ] && ci_progress_is_regression "${progress}" "${last_progress}"; then
                progress="${last_progress}"
            fi

            if [ -n "${progress}" ] && [ "${progress}" != "${last_progress}" ]; then
                if ci_progress_indicates_complete "${progress}"; then
                    echo -e "${BLUE}[CI ${index}/${total}] ${stage_name} progress: ${progress} (test execution complete, finalizing stage) (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
                else
                    echo -e "${BLUE}[CI ${index}/${total}] ${stage_name} progress: ${progress} (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
                fi
                last_progress="${progress}"
                last_heartbeat_ts="${now_ts}"
            elif [ $(( now_ts - last_heartbeat_ts )) -ge 60 ]; then
                if [ -n "${last_progress}" ]; then
                    if ci_progress_indicates_complete "${last_progress}"; then
                        echo -e "${BLUE}[CI ${index}/${total}] ${stage_name} finalizing stage artifacts after ${last_progress} (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
                    else
                        echo -e "${BLUE}[CI ${index}/${total}] ${stage_name} still running (latest progress: ${last_progress}) (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
                    fi
                else
                    echo -e "${BLUE}[CI ${index}/${total}] ${stage_name} still running (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
                fi
                last_heartbeat_ts="${now_ts}"
            fi
        done

        local stage_rc=0
        if ! wait "${stage_pid}"; then
            stage_rc=$?
        fi

        local stage_elapsed="$(( $(date +%s) - stage_start_ts ))"
        if [ "${stage_rc}" -eq 0 ]; then
            echo -e "${GREEN}[CI ${index}/${total}] OK: ${stage_name} (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
            return 0
        fi

        echo -e "${RED}[CI ${index}/${total}] FAIL: ${stage_name} (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
    elif "$@" >"${stage_log}" 2>&1; then
        local stage_elapsed="$(( $(date +%s) - stage_start_ts ))"
        echo -e "${GREEN}[CI ${index}/${total}] OK: ${stage_name} (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
        return 0
    else
        local stage_elapsed="$(( $(date +%s) - stage_start_ts ))"
        echo -e "${RED}[CI ${index}/${total}] FAIL: ${stage_name} (elapsed $(format_ci_duration "${stage_elapsed}"))${NC}"
    fi

    echo -e "${YELLOW}--- stage log: ${stage_log} ---${NC}"
    cat "${stage_log}"
    echo -e "${YELLOW}--- end stage log ---${NC}"
    return 1
}

select_local_pr_ci_compilers() {
    if [ -n "${CC:-}" ] || [ -n "${CXX:-}" ]; then
        echo -e "${GREEN}PR-CI compiler toolchain from environment: CC=${CC:-<unset>} CXX=${CXX:-<unset>}${NC}"
        return 0
    fi

    if command -v gcc-11 >/dev/null 2>&1 && command -v g++-11 >/dev/null 2>&1; then
        export CC="gcc-11"
        export CXX="g++-11"
    elif command -v gcc >/dev/null 2>&1 && command -v g++ >/dev/null 2>&1; then
        export CC="gcc"
        export CXX="g++"
    elif command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
        export CC="clang"
        export CXX="clang++"
    fi

    if [ -n "${CC:-}" ] && [ -n "${CXX:-}" ]; then
        echo -e "${GREEN}PR-CI compiler toolchain auto-selected: CC=${CC} CXX=${CXX}${NC}"
    else
        echo -e "${YELLOW}Warning: no explicit compiler pair selected for PR-CI; using environment defaults${NC}"
    fi
    return 0
}

clean_local_pr_ci_workspace() {
    echo -e "${GREEN}=== Cleaning local PR-CI workspace ===${NC}"
    echo "Removing project-local build/install/venv artifacts before running CI parity checks."

    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would remove: ${BUILD_DIR}${NC}"
        echo -e "${YELLOW}[DRY-RUN] Would remove: ${INSTALL_PREFIX}${NC}"
        echo -e "${YELLOW}[DRY-RUN] Would remove: ${PR_CI_VENV_DIR}${NC}"
        return 0
    fi

    rm -rf "${BUILD_DIR}" "${INSTALL_PREFIX}" "${PR_CI_VENV_DIR}"
    return 0
}

prepare_local_pr_ci_venv() {
    local bootstrap_python="${PYTHON_EXE:-}"
    if [ -z "${bootstrap_python}" ] || [ ! -x "${bootstrap_python}" ]; then
        local py_candidate
        for py_candidate in python3.11 python3.10 python3.9 python3 python; do
            if command -v "${py_candidate}" >/dev/null 2>&1; then
                bootstrap_python="$(command -v "${py_candidate}")"
                break
            fi
        done
    fi

    if [ -z "${bootstrap_python}" ]; then
        echo -e "${RED}Error: no bootstrap Python found to create ${PR_CI_VENV_DIR}${NC}"
        return 1
    fi

    if [ "${DRY_RUN}" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would recreate dedicated PR-CI venv at: ${PR_CI_VENV_DIR}${NC}"
        return 0
    fi

    rm -rf "${PR_CI_VENV_DIR}"
    if ! "${bootstrap_python}" -m venv "${PR_CI_VENV_DIR}"; then
        echo -e "${RED}Error: failed to create dedicated PR-CI venv at ${PR_CI_VENV_DIR}${NC}"
        return 1
    fi

    export VENV_PATH="${PR_CI_VENV_DIR}"
    export PYTHON_EXE="${PR_CI_VENV_DIR}/bin/python"
    USE_PYTHON="yes"

    echo -e "${GREEN}Prepared dedicated PR-CI venv: ${PR_CI_VENV_DIR}${NC}"
    echo -e "${GREEN}PR-CI bootstrap Python: ${bootstrap_python} ($(${PYTHON_EXE} --version 2>/dev/null || echo unknown))${NC}"
    if [ -n "${CC:-}" ] || [ -n "${CXX:-}" ]; then
        echo -e "${GREEN}PR-CI compiler toolchain from environment: CC=${CC:-<unset>} CXX=${CXX:-<unset>}${NC}"
    fi
    return 0
}

install_dlio_benchmark_for_ci() {
    if [ "${USE_PYTHON}" != "yes" ]; then
        echo -e "${RED}Error: dlio_benchmark install requires Python support${NC}"
        return 1
    fi

    local python_runner="${PYTHON_EXE:-python3}"
    local dlio_ref="git+https://github.com/argonne-lcf/dlio_benchmark.git@main"

    "${python_runner}" -m pip install "${dlio_ref}" || return 1

    "${python_runner}" - <<'PY'
import importlib.util
import pathlib
import sys

spec = importlib.util.find_spec("dlio_benchmark")
if spec is None or spec.origin is None:
    print("dlio_benchmark import failed")
    sys.exit(1)

package_dir = pathlib.Path(spec.origin).resolve().parent
print(f"Installed dlio_benchmark from: {package_dir}")
PY
}

# Build (or reuse cached) parallel HDF5 from source.
# Sets HDF5_ROOT_DIR to the install prefix if successful.
# Uses the same cache dir as run_hdf5_mpi_trace_ci so builds are shared.
build_default_parallel_hdf5() {
    local version="1.14.6"
    local tag="hdf5_1.14.6"
    local hdf5_install="${HDF5_CI_CACHE_DIR}/hdf5-${version}/install"
    local hdf5_src="${HDF5_CI_CACHE_DIR}/hdf5-${version}/src"
    local hdf5_build="${HDF5_CI_CACHE_DIR}/hdf5-${version}/build"

    local mpi_cc mpi_cxx
    mpi_cc="$(command -v mpicc 2>/dev/null || echo "${CC:-gcc}")"
    mpi_cxx="$(command -v mpicxx 2>/dev/null || echo "${CXX:-g++}")"

    if [ -f "${hdf5_install}/include/H5public.h" ]; then
        echo -e "${GREEN}Using cached parallel HDF5 ${version} at ${hdf5_install}${NC}"
        HDF5_ROOT_DIR="${hdf5_install}"
        return 0
    fi

    echo -e "${BLUE}Building parallel HDF5 ${version} from source (CC=${mpi_cc})...${NC}"
    mkdir -p "${hdf5_src}" "${hdf5_build}" "${hdf5_install}"

    if ! curl -fsSL \
        "https://github.com/HDFGroup/hdf5/archive/refs/tags/${tag}.tar.gz" \
        | tar xz -C "${hdf5_src}" --strip-components=1 2>/dev/null; then
        echo -e "${RED}ERROR: failed to download HDF5 ${version}${NC}"
        return 1
    fi

    if ! cmake -S "${hdf5_src}" -B "${hdf5_build}" \
        -DCMAKE_C_COMPILER="${mpi_cc}" \
        -DCMAKE_CXX_COMPILER="${mpi_cxx}" \
        -DCMAKE_INSTALL_PREFIX="${hdf5_install}" \
        -DHDF5_ENABLE_PARALLEL=ON \
        -DHDF5_BUILD_TOOLS=OFF \
        -DHDF5_BUILD_EXAMPLES=OFF \
        -DBUILD_TESTING=OFF \
        -DHDF5_BUILD_HL_LIB=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DHDF5_ENABLE_Z_LIB_SUPPORT=OFF \
        -DHDF5_ENABLE_SZIP_SUPPORT=OFF \
        >/dev/null 2>&1; then
        echo -e "${RED}ERROR: failed to configure HDF5 ${version}${NC}"
        return 1
    fi

    if ! cmake --build "${hdf5_build}" --parallel "${JOBS}" >/dev/null 2>&1; then
        echo -e "${RED}ERROR: failed to build HDF5 ${version}${NC}"
        return 1
    fi

    cmake --install "${hdf5_build}" >/dev/null 2>&1
    echo -e "${GREEN}Parallel HDF5 ${version} installed at ${hdf5_install}${NC}"
    HDF5_ROOT_DIR="${hdf5_install}"
}

run_hdf5_mpi_trace_ci() {
    echo -e "${GREEN}=== HDF5+MPI Multi-Version Trace CI ===${NC}"

    if [ "${ENABLE_HDF5}" != "ON" ] || [ "${ENABLE_MPI}" != "ON" ]; then
        echo -e "${YELLOW}Skipping: requires --enable-hdf5 and --enable-mpi${NC}"
        return 0
    fi

    if [ "${SKIP_HDF5_TRACE_CI}" = "1" ]; then
        echo -e "${YELLOW}Skipping: SKIP_HDF5_TRACE_CI=1${NC}"
        return 0
    fi

    # HDF5 installs are cached outside BUILD_DIR so --clean doesn't force re-download
    local hdf5_cache="${HDF5_CI_CACHE_DIR}"
    # per-version dftracer builds live inside BUILD_DIR (wiped by --clean)
    local hdf5_work_dir="${BUILD_DIR}/hdf5-ci"

    if [ "${DRY_RUN}" != "1" ]; then
        mkdir -p "${hdf5_cache}" "${hdf5_work_dir}"
    fi

    # Detect MPI wrapper compilers
    local mpi_cc mpi_cxx
    if command -v mpicc >/dev/null 2>&1; then
        mpi_cc="mpicc"
    else
        mpi_cc="${CC:-gcc}"
    fi
    if command -v mpicxx >/dev/null 2>&1; then
        mpi_cxx="mpicxx"
    else
        mpi_cxx="${CXX:-g++}"
    fi
    echo -e "${BLUE}MPI compilers: CC=${mpi_cc}  CXX=${mpi_cxx}${NC}"

    # brahma v1.0.8 HDF5 supported ranges (major*100000+minor*100+patch):
    #   1.10.x [101005,101100): 1.10.11=101011 ✓
    #   1.12.x [101203,101300): 1.12.3=101203 ✓  (1.12.2=101202 is below threshold)
    #   1.14.x [101405,101500): 1.14.6=101406 ✓
    declare -a hdf5_versions=("1.10.11" "1.12.3" "1.14.6")
    declare -A hdf5_tags=(
        ["1.10.11"]="hdf5-1_10_11"
        ["1.12.3"]="hdf5-1_12_3"
        ["1.14.6"]="hdf5_1.14.6"
    )

    local overall_rc=0

    for version in "${hdf5_versions[@]}"; do
        local tag="${hdf5_tags[$version]}"
        local hdf5_install="${hdf5_cache}/hdf5-${version}/install"
        local hdf5_src="${hdf5_cache}/hdf5-${version}/src"
        local hdf5_build="${hdf5_cache}/hdf5-${version}/build"
        local dftracer_build="${hdf5_work_dir}/hdf5-${version}/dftracer-build"
        local dftracer_install="${hdf5_work_dir}/hdf5-${version}/dftracer-install"

        echo ""
        echo -e "${BLUE}--- HDF5 ${version} ---${NC}"

        # ---- 1. Build HDF5 from source (cached) ----
        if [ ! -f "${hdf5_install}/include/H5public.h" ]; then
            echo -e "${BLUE}  Building HDF5 ${version} from source (tag: ${tag})...${NC}"

            if [ "${DRY_RUN}" = "1" ]; then
                echo -e "${YELLOW}  [DRY-RUN] Would download and build HDF5 ${version}${NC}"
            else
                mkdir -p "${hdf5_src}" "${hdf5_build}" "${hdf5_install}"

                if ! curl -fsSL \
                    "https://github.com/HDFGroup/hdf5/archive/refs/tags/${tag}.tar.gz" \
                    | tar xz -C "${hdf5_src}" --strip-components=1 2>/dev/null; then
                    echo -e "${RED}  ERROR: failed to download HDF5 ${version}${NC}"
                    overall_rc=1
                    continue
                fi

                if ! cmake -S "${hdf5_src}" -B "${hdf5_build}" \
                    -DCMAKE_C_COMPILER="${mpi_cc}" \
                    -DCMAKE_CXX_COMPILER="${mpi_cxx}" \
                    -DCMAKE_INSTALL_PREFIX="${hdf5_install}" \
                    -DHDF5_ENABLE_PARALLEL=ON \
                    -DHDF5_BUILD_TOOLS=OFF \
                    -DHDF5_BUILD_EXAMPLES=OFF \
                    -DBUILD_TESTING=OFF \
                    -DHDF5_BUILD_HL_LIB=ON \
                    -DCMAKE_BUILD_TYPE=Release \
                    -DHDF5_ENABLE_Z_LIB_SUPPORT=OFF \
                    -DHDF5_ENABLE_SZIP_SUPPORT=OFF \
                    >/dev/null 2>&1; then
                    echo -e "${RED}  ERROR: failed to configure HDF5 ${version}${NC}"
                    overall_rc=1
                    continue
                fi

                if ! cmake --build "${hdf5_build}" --parallel "${JOBS}" >/dev/null 2>&1; then
                    echo -e "${RED}  ERROR: failed to build HDF5 ${version}${NC}"
                    overall_rc=1
                    continue
                fi

                cmake --install "${hdf5_build}" >/dev/null 2>&1
                echo -e "${GREEN}  HDF5 ${version} installed at ${hdf5_install}${NC}"
            fi
        else
            echo -e "${GREEN}  Using cached HDF5 ${version} at ${hdf5_install}${NC}"
        fi

        # ---- 2. Build dftracer against this HDF5 ----
        if [ "${DRY_RUN}" = "1" ]; then
            echo -e "${YELLOW}  [DRY-RUN] Would build dftracer against HDF5 ${version}${NC}"
        else
            echo -e "${BLUE}  Building dftracer against HDF5 ${version}...${NC}"
            mkdir -p "${dftracer_build}" "${dftracer_install}"

            # Build LD_LIBRARY_PATH hint for CTest (belt-and-suspenders beside RPATH)
            local _hdf5_ld_hint=""
            for _d in "${hdf5_install}/lib" "${hdf5_install}/lib64"; do
                [ -d "${_d}" ] && _hdf5_ld_hint="${_hdf5_ld_hint:+${_hdf5_ld_hint}:}${_d}"
            done

            local cmake_full_args=(
                "-DCMAKE_BUILD_TYPE=Release"
                "-DCMAKE_INSTALL_PREFIX=${dftracer_install}"
                "-DCMAKE_C_COMPILER=${mpi_cc}"
                "-DCMAKE_CXX_COMPILER=${mpi_cxx}"
                "-DCMAKE_PREFIX_PATH=${INSTALL_PREFIX}"
                "-DDFTRACER_ENABLE_MPI=ON"
                "-DDFTRACER_ENABLE_HDF5=ON"
                "-DHDF5_ROOT=${hdf5_install}"
                "-DDFTRACER_ENABLE_TESTS=ON"
                "-DDFTRACER_BUILD_PYTHON_BINDINGS=OFF"
                "-DDFTRACER_INSTALL_DEPENDENCIES=OFF"
                "-Dyaml-cpp_DIR=${INSTALL_PREFIX}"
            )
            if [ -n "${_hdf5_ld_hint}" ]; then
                cmake_full_args+=("-DDFTRACER_TEST_LD_LIBRARY_PATH=${_hdf5_ld_hint}")
            fi

            if ! (cd "${dftracer_build}" && cmake "${SCRIPT_DIR}" "${cmake_full_args[@]}" >/dev/null 2>&1); then
                echo -e "${RED}  ERROR: cmake configure failed for HDF5 ${version}${NC}"
                overall_rc=1
                continue
            fi

            if ! cmake --build "${dftracer_build}" --parallel "${JOBS}" >/dev/null 2>&1; then
                echo -e "${RED}  ERROR: cmake build failed for HDF5 ${version}${NC}"
                overall_rc=1
                continue
            fi
            echo -e "${GREEN}  dftracer built for HDF5 ${version}${NC}"
        fi

        # ---- 3. Run test binary directly with LD_PRELOAD and verify traces ----
        if [ "${DRY_RUN}" = "1" ]; then
            echo -e "${YELLOW}  [DRY-RUN] Would run test_c_hdf5_mpi with LD_PRELOAD for HDF5 ${version}${NC}"
        else
            echo -e "${BLUE}  Running verify test for HDF5 ${version}...${NC}"

            local preload_lib
            preload_lib="$(find "${dftracer_build}" -name "libdftracer_preload_dbg.so" | head -1)"
            if [ -z "${preload_lib}" ]; then
                echo -e "${RED}  ERROR: libdftracer_preload_dbg.so not found under ${dftracer_build}${NC}"
                overall_rc=1
                continue
            fi

            local test_bin="${dftracer_build}/bin/test_c_hdf5_mpi"
            if [ ! -x "${test_bin}" ]; then
                echo -e "${RED}  ERROR: test binary not found: ${test_bin}${NC}"
                overall_rc=1
                continue
            fi

            local verify_trace_dir="${dftracer_build}/trace-verify"
            local verify_data_dir="${verify_trace_dir}/data"
            rm -rf "${verify_trace_dir}"
            mkdir -p "${verify_data_dir}"

            local hdf5_ld_path=""
            for _d in "${hdf5_install}/lib" "${hdf5_install}/lib64"; do
                [ -d "${_d}" ] && hdf5_ld_path="${hdf5_ld_path:+${hdf5_ld_path}:}${_d}"
            done

            local run_rc=0
            (
                export DFTRACER_ENABLE=1
                export DFTRACER_INC_METADATA=1
                export DFTRACER_LOG_FILE="${verify_trace_dir}/hdf5_mpi_verify"
                export DFTRACER_DATA_DIR=/
                export DFTRACER_INIT=PRELOAD
                export DFTRACER_TRACE_COMPRESSION=1
                export DFTRACER_BIND_SIGNALS=0
                export LD_PRELOAD="${preload_lib}"
                export LD_LIBRARY_PATH="${hdf5_ld_path}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
                "${test_bin}" "${verify_data_dir}"
            ) || run_rc=$?
            if [ "${run_rc}" -ne 0 ]; then
                echo -e "${RED}  ERROR: test binary exited with code ${run_rc} for HDF5 ${version}${NC}"
                overall_rc=1
                continue
            fi

            local py_result
            py_result="$(python3 - "${verify_trace_dir}" 2>&1 << 'PYEOF'
import sys, gzip, json, glob, os

trace_dir = sys.argv[1]
files = list(set(
    glob.glob(os.path.join(trace_dir, "*.pfw.gz")) +
    glob.glob(os.path.join(trace_dir, "**", "*.pfw.gz"), recursive=True)
))

if not files:
    print(f"ERROR: no trace files found in {trace_dir}")
    sys.exit(1)

print(f"Checking {len(files)} trace file(s)...")

hdf5_funcs, mpi_funcs = set(), set()
for path in files:
    with gzip.open(path, "rt") as fh:
        for line in fh:
            line = line.strip().rstrip(",")
            if not line or line in ("[", "]"):
                continue
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                continue
            cat  = ev.get("cat", "")
            name = ev.get("name", "")
            if cat == "HDF5":
                hdf5_funcs.add(name)
            elif cat in ("MPI", "MPIIO"):
                mpi_funcs.add(name)

print(f"HDF5 functions intercepted: {sorted(hdf5_funcs)}")
print(f"MPI  functions intercepted: {sorted(mpi_funcs)}")

required_hdf5 = {"H5Fcreate", "H5Dcreate2", "H5Dwrite", "H5Dread", "H5Fclose"}
required_mpi  = {"MPI_File_open"}
missing_hdf5  = required_hdf5 - hdf5_funcs
missing_mpi   = required_mpi  - mpi_funcs

ok = True
if missing_hdf5:
    print(f"FAIL: missing HDF5 events: {sorted(missing_hdf5)}")
    ok = False
else:
    print(f"PASS: all required HDF5 events present")
if missing_mpi:
    print(f"FAIL: missing MPI file I/O events: {sorted(missing_mpi)}")
    ok = False
else:
    print(f"PASS: MPI_File_open intercepted (MPIO driver active)")

sys.exit(0 if ok else 1)
PYEOF
)"
            local py_rc=$?
            echo -e "${BLUE}${py_result}${NC}"
            if [ "${py_rc}" -ne 0 ]; then
                echo -e "${RED}  ERROR: trace verification failed for HDF5 ${version}${NC}"
                overall_rc=1
            else
                echo -e "${GREEN}  Trace verification passed for HDF5 ${version}${NC}"
            fi
        fi
    done

    if [ "${overall_rc}" -eq 0 ]; then
        echo -e "${GREEN}All HDF5 versions passed.${NC}"
    fi
    return ${overall_rc}
}

list_pr_ci_steps() {
    cat << 'EOF'
Local PR CI steps (use --skip-to-step N to start from step N):

  1  Format Check
  2  CTest (non-valgrind)
  3  Valgrind CTests
  4  Install dlio_benchmark
  5  DLIO Valgrind Workloads
  6  IOR Benchmark
  7  HDF5+MPI Trace CI
EOF
}

run_local_pr_ci_suite() {
    echo -e "${GREEN}=== Running Local PR CI Suite ===${NC}"
    local total=7
    local suite_start_ts
    suite_start_ts="$(date +%s)"

    if [ "${START_FROM_STEP}" -gt 1 ]; then
        echo -e "${YELLOW}Skipping steps 1-$((START_FROM_STEP - 1)) (--skip-to-step ${START_FROM_STEP})${NC}"
    fi

    _ci_step() {
        local n="$1"; shift
        if [ "${n}" -lt "${START_FROM_STEP}" ]; then
            echo -e "${YELLOW}[CI ${n}/${total}] Skipped (--skip-to-step ${START_FROM_STEP})${NC}"
            return 0
        fi
        if ! run_ci_stage "${n}" "${total}" "$@"; then
            echo -e "${RED}Stopping local CI at first failure (stage ${n}, total elapsed $(format_ci_duration "$(( $(date +%s) - suite_start_ts ))")).${NC}"
            return 1
        fi
    }

    _ci_step 1 "Format Check"            run_format_check            || return 1
    _ci_step 2 "CTest (non-valgrind)"    run_non_valgrind_ctest_tests || return 1
    _ci_step 3 "Valgrind CTests"         run_valgrind_ctest_tests     || return 1
    _ci_step 4 "Install dlio_benchmark"  install_dlio_benchmark_for_ci || return 1
    _ci_step 5 "DLIO Valgrind Workloads" run_valgrind_dlio_tests      || return 1
    _ci_step 6 "IOR Benchmark"           run_ior_benchmark_tests      || return 1
    _ci_step 7 "HDF5+MPI Trace CI"       run_hdf5_mpi_trace_ci        || return 1

    echo -e "${GREEN}Local PR CI total elapsed: $(format_ci_duration "$(( $(date +%s) - suite_start_ts ))")${NC}"
    return 0
}

run_service_smoke_test() {
    if [ "${RUN_SMOKE_TEST}" != "1" ]; then
        echo -e "${YELLOW}Skipping smoke test (--skip-smoke-test)${NC}"
        return 0
    fi

    local service_bin=""
    if command -v dftracer_service &> /dev/null; then
        service_bin="$(command -v dftracer_service)"
    elif [ -x "${BUILD_DIR}/bin/dftracer_service" ]; then
        service_bin="${BUILD_DIR}/bin/dftracer_service"
    elif [ -x "${INSTALL_PREFIX}/bin/dftracer_service" ]; then
        service_bin="${INSTALL_PREFIX}/bin/dftracer_service"
    elif [ -n "${VIRTUAL_ENV}" ] && [ -x "${VIRTUAL_ENV}/bin/dftracer_service" ]; then
        service_bin="${VIRTUAL_ENV}/bin/dftracer_service"
    elif [ -n "${CONDA_PREFIX}" ] && [ -x "${CONDA_PREFIX}/bin/dftracer_service" ]; then
        service_bin="${CONDA_PREFIX}/bin/dftracer_service"
    fi

    if [ -z "${service_bin}" ]; then
        echo -e "${RED}Smoke test failed: could not find dftracer_service binary${NC}"
        return 1
    fi

    if [ -n "${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}" ]; then
        refresh_existing_runtime_rpaths "${BUILD_DIR}"
    fi

    local smoke_dir="${BUILD_DIR}/smoke_service"
    local pid_file="${smoke_dir}/dftracer_server.pid"
    mkdir -p "${smoke_dir}"

    (
        export DFTRACER_ENABLE=1
        export DFTRACER_LOG_FILE="${smoke_dir}/trace"
        export DFTRACER_TRACE_INTERVAL_MS=100
        : "${DFTRACER_LIBUV_THREADS:=1}"
        export DFTRACER_LIBUV_THREADS
        if [ -n "${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}" ]; then
            export LD_LIBRARY_PATH="${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
        fi

        echo -e "${BLUE}Running dftracer_service smoke test with ${service_bin}${NC}"
        if ! "${service_bin}" start "${smoke_dir}"; then
            echo -e "${RED}Smoke test failed: service start command failed${NC}"
            exit 1
        fi

        for _ in $(seq 1 20); do
            if [ -f "${pid_file}" ]; then
                break
            fi
            sleep 0.1
        done

        if [ ! -f "${pid_file}" ]; then
            echo -e "${RED}Smoke test failed: pid file was not created${NC}"
            exit 1
        fi

        if ! "${service_bin}" stop "${smoke_dir}"; then
            echo -e "${RED}Smoke test failed: service stop command failed${NC}"
            exit 1
        fi

        if [ -f "${pid_file}" ]; then
            echo -e "${RED}Smoke test failed: pid file still exists after stop${NC}"
            exit 1
        fi

        echo -e "${GREEN}Smoke test passed: dftracer_service started and stopped successfully${NC}"
        exit 0
    )
    return $?
}

# Handle --list-steps early (no build needed)
if [ "${LIST_STEPS:-0}" = "1" ]; then
    list_pr_ci_steps
    exit 0
fi

# Auto-detect Python if not explicitly disabled
if [ "$USE_PYTHON" = "auto" ]; then
    # Try to find Python
    if command -v python3 &> /dev/null; then
        PYTHON_EXE=$(which python3)
        USE_PYTHON="yes"
    elif command -v python &> /dev/null; then
        PYTHON_EXE=$(which python)
        USE_PYTHON="yes"
    else
        USE_PYTHON="no"
    fi
fi

# Verify we're in a virtual environment if Python is enabled
if [ "$USE_PYTHON" = "yes" ]; then
    # Check if we're in a virtual environment (venv or conda)
    IN_VENV=0
    if [ -n "$VIRTUAL_ENV" ]; then
        IN_VENV=1
        echo -e "${GREEN}Detected Python venv: ${VIRTUAL_ENV}${NC}"
    elif [ -n "$CONDA_PREFIX" ]; then
        IN_VENV=1
        echo -e "${GREEN}Detected Conda environment: ${CONDA_PREFIX}${NC}"
    fi
    
    if [ $IN_VENV -eq 0 ] && [ "$RUN_PR_CI_LOCAL" = "1" ]; then
        echo -e "${GREEN}Using project-local dedicated venv: ${PR_CI_VENV_DIR}${NC}"
    elif [ $IN_VENV -eq 0 ] && [ "$SKIP_BUILD_RUN_TESTS" = "1" ] && [ -n "${PYTHON_EXE}" ]; then
        echo -e "${YELLOW}Warning: no active virtual environment detected; using explicit Python for existing tests: ${PYTHON_EXE}${NC}"
    elif [ $IN_VENV -eq 0 ] && [ "$INSTALL_MODE" = "cmake" ] && [ "$PYTHON_EXPLICITLY_SET" = "0" ]; then
        # cmake mode without --python: Python bindings are not built, no venv needed
        USE_PYTHON="no"
        echo -e "${YELLOW}cmake mode without --python: Python support disabled (no venv required)${NC}"
    elif [ $IN_VENV -eq 0 ]; then
        echo -e "${RED}Error: Python support requires an active virtual environment${NC}"
        echo "Please activate a virtual environment before running this script:"
        echo ""
        echo "  For venv:"
        echo "    python3 -m venv dftracer_env"
        echo "    source dftracer_env/bin/activate"
        echo ""
        echo "  For conda:"
        echo "    conda create -n dftracer python=3.10"
        echo "    conda activate dftracer"
        echo ""
        echo "Then run this script again with --python python3"
        exit 1
    fi
fi

collect_python_runtime_library_paths
collect_cxx_runtime_library_paths

# Handle --clean-install flag
if [ "$CLEAN_INSTALL" = "1" ]; then
    echo -e "${GREEN}=== Cleaning DFTracer Installation ===${NC}"
    echo ""
    
    # First, uninstall via pip if Python is available
    if [ "$USE_PYTHON" = "yes" ] || command -v python3 &> /dev/null; then
        PYTHON_FOR_CLEAN="${PYTHON_EXE:-python3}"
        
        if command -v "${PYTHON_FOR_CLEAN}" &> /dev/null; then
            echo "Checking for pip-installed DFTracer..."
            
            # Check if dftracer or pydftracer is installed
            if "${PYTHON_FOR_CLEAN}" -m pip show dftracer &> /dev/null || "${PYTHON_FOR_CLEAN}" -m pip show pydftracer &> /dev/null; then
                if [ "$DRY_RUN" = "1" ]; then
                    echo -e "${YELLOW}[DRY-RUN] Would uninstall dftracer/pydftracer via pip${NC}"
                else
                    echo "Uninstalling dftracer and pydftracer via pip..."
                    "${PYTHON_FOR_CLEAN}" -m pip uninstall -y dftracer pydftracer 2>/dev/null || true
                    echo -e "${GREEN}Pip packages uninstalled${NC}"
                fi
            else
                echo "No pip-installed dftracer found"
            fi
            echo ""
        fi
    fi
    
    # Determine what to clean
    CLEAN_LOCATIONS=()
    
    # Check for Python site-packages
    if [ "$USE_PYTHON" = "yes" ] || command -v python3 &> /dev/null; then
        PYTHON_FOR_CLEAN="${PYTHON_EXE:-python3}"
        
        if command -v "${PYTHON_FOR_CLEAN}" &> /dev/null; then
            SITE_PACKAGES=$("${PYTHON_FOR_CLEAN}" -c "import site; print(site.getsitepackages()[0])" 2>/dev/null || echo "")
            
            if [ -n "${SITE_PACKAGES}" ]; then
                # Check for dftracer in site-packages
                if [ -d "${SITE_PACKAGES}/dftracer" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/dftracer")
                fi
                if [ -d "${SITE_PACKAGES}/pydftracer.egg-info" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/pydftracer.egg-info")
                fi
                if [ -d "${SITE_PACKAGES}/dftracer.egg-info" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/dftracer.egg-info")
                fi
                # Check for egg-link files (editable installs)
                if [ -f "${SITE_PACKAGES}/dftracer.egg-link" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/dftracer.egg-link")
                fi
                if [ -f "${SITE_PACKAGES}/pydftracer.egg-link" ]; then
                    CLEAN_LOCATIONS+=("${SITE_PACKAGES}/pydftracer.egg-link")
                fi
                # Check for .pth files
                for pth_file in "${SITE_PACKAGES}"/__editable__.dftracer*.pth "${SITE_PACKAGES}"/__editable__.pydftracer*.pth; do
                    if [ -f "$pth_file" ]; then
                        CLEAN_LOCATIONS+=("$pth_file")
                    fi
                done
                # Check for .so files
                for so_file in "${SITE_PACKAGES}"/dftracer*.so; do
                    if [ -f "$so_file" ]; then
                        CLEAN_LOCATIONS+=("$so_file")
                    fi
                done
            fi
        fi
    fi
    
    # Check install prefix
    if [ -d "${INSTALL_PREFIX}" ]; then
        # Check bin directory
        if [ -d "${INSTALL_PREFIX}/bin" ]; then
            for file in "${INSTALL_PREFIX}/bin/"dftracer*; do
                if [ -e "$file" ]; then
                    CLEAN_LOCATIONS+=("$file")
                fi
            done
        fi
        
        # Check lib directory
        for libdir in lib lib64; do
            if [ -d "${INSTALL_PREFIX}/${libdir}" ]; then
                for file in "${INSTALL_PREFIX}/${libdir}/"*dftracer*; do
                    if [ -e "$file" ]; then
                        CLEAN_LOCATIONS+=("$file")
                    fi
                done
            fi
        done
        
        # Check include directory
        if [ -d "${INSTALL_PREFIX}/include/dftracer" ]; then
            CLEAN_LOCATIONS+=("${INSTALL_PREFIX}/include/dftracer")
        fi
        
        # Check share directory
        if [ -d "${INSTALL_PREFIX}/share/dftracer" ]; then
            CLEAN_LOCATIONS+=("${INSTALL_PREFIX}/share/dftracer")
        fi
        
        # Check for DFTracer dependencies (cpp-logger, gotcha, brahma, yaml-cpp)
        for dep in cpp-logger cpp_logger gotcha brahma yaml-cpp; do
            # Check lib directories
            for libdir in lib lib64; do
                if [ -d "${INSTALL_PREFIX}/${libdir}" ]; then
                    # Check for libraries
                    for file in "${INSTALL_PREFIX}/${libdir}/"*${dep}* "${INSTALL_PREFIX}/${libdir}/cmake/${dep}"*; do
                        if [ -e "$file" ]; then
                            CLEAN_LOCATIONS+=("$file")
                        fi
                    done
                fi
            done
            
            # Check include directories
            if [ -d "${INSTALL_PREFIX}/include/${dep}" ]; then
                CLEAN_LOCATIONS+=("${INSTALL_PREFIX}/include/${dep}")
            fi
            
            # Check share/cmake directories
            if [ -d "${INSTALL_PREFIX}/share/${dep}" ]; then
                CLEAN_LOCATIONS+=("${INSTALL_PREFIX}/share/${dep}")
            fi
        done
    fi
    
    # Display what will be cleaned
    if [ ${#CLEAN_LOCATIONS[@]} -eq 0 ]; then
        echo -e "${YELLOW}No DFTracer installations found to clean.${NC}"
    else
        echo -e "${YELLOW}The following locations will be removed:${NC}"
        for location in "${CLEAN_LOCATIONS[@]}"; do
            echo "  - $location"
        done
        echo ""
        
        if [ "$DRY_RUN" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would remove the above locations${NC}"
        else
            if [ "$QUIET" = "1" ]; then
                echo "Quiet mode: assuming yes for cleanup prompt."
                REPLY="y"
            else
                read -p "Are you sure you want to remove these? (y/N): " -n 1 -r
                echo
            fi
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                for location in "${CLEAN_LOCATIONS[@]}"; do
                    if [ -e "$location" ]; then
                        echo "Removing: $location"
                        rm -rf "$location"
                    fi
                done
                echo -e "${GREEN}Cleanup complete!${NC}"
            else
                echo -e "${YELLOW}Cleanup cancelled.${NC}"
                exit 0
            fi
        fi
    fi
    
    # If only clean-install was requested, exit now
    if [ "$CLEAN_BUILD" = "0" ] && [ "$BUILD_DEPENDENCIES" = "0" ]; then
        exit 0
    fi
    echo ""
fi

# Print configuration
echo -e "${GREEN}=== DFTracer Auto-Build Configuration ===${NC}"
if [ "$DRY_RUN" = "1" ]; then
    echo -e "${YELLOW}*** DRY RUN MODE - No actual changes will be made ***${NC}"
fi
if [ "$VERBOSE" = "1" ]; then
    echo -e "${BLUE}*** VERBOSE MODE - Detailed output enabled ***${NC}"
fi
echo "Build Directory: ${BUILD_DIR}"
echo "Install Prefix: ${INSTALL_PREFIX}"
echo "Build Type: ${BUILD_TYPE}"
if [ "$USE_PYTHON" = "yes" ]; then
    echo "Python Support: Enabled"
    echo "Python Executable: ${PYTHON_EXE}"
else
    echo "Python Support: Disabled"
fi
echo "Build Dependencies: ${BUILD_DEPENDENCIES}"
echo "Enable Tests: ${ENABLE_TESTS}"
echo "Enable Function Tracing: ${ENABLE_FTRACING}"
echo "Enable HIP Tracing: ${ENABLE_HIP_TRACING}"
echo "Enable MPI: ${ENABLE_MPI}"
echo "Enable HDF5: ${ENABLE_HDF5}"
echo "Generate Interfaces: ${GENERATE_INTERFACES}"
echo "Disable HWLOC: ${DISABLE_HWLOC}"
echo "Enable DLIO Tests: ${ENABLE_DLIO_TESTS}"
echo "Enable Paper Tests: ${ENABLE_PAPER_TESTS}"
echo "Parallel Jobs: ${JOBS}"
echo "Install Mode: ${INSTALL_MODE}"
echo "Install DFAnalyzer: ${INSTALL_DFANALYZER}"
echo "Run Smoke Test: ${RUN_SMOKE_TEST}"
echo "Run Valgrind CTest: ${RUN_VALGRIND_CTEST}"
echo "Run Valgrind DLIO: ${RUN_VALGRIND_DLIO}"
echo "Run Local PR CI Suite: ${RUN_PR_CI_LOCAL}"
echo "Skip Build Run Tests: ${SKIP_BUILD_RUN_TESTS}"
if [ -n "${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}" ]; then
    echo "CTest LD_LIBRARY_PATH hint: ${DFTRACER_TEST_LD_LIBRARY_PATH_HINT}"
fi
echo "Dry Run: ${DRY_RUN}"
echo "Quiet: ${QUIET}"
echo "Verbose: ${VERBOSE}"
echo ""

if [ "$RUN_PR_CI_LOCAL" = "1" ]; then
    PR_CI_TOTAL_START_TS="$(date +%s)"
    if ! clean_local_pr_ci_workspace; then
        report_pr_ci_total_elapsed "failed"
        exit 1
    fi
    if [ "${DRY_RUN}" != "1" ]; then
        mkdir -p "$(dirname "${PR_CI_MASTER_LOG}")"
        : > "${PR_CI_MASTER_LOG}"
        exec > >(tee -a "${PR_CI_MASTER_LOG}") 2>&1
        echo -e "${GREEN}Master PR-CI log: ${PR_CI_MASTER_LOG}${NC}"
    fi
    if ! run_ci_stage 0 7 "Prepare dedicated venv" prepare_local_pr_ci_venv; then
        report_pr_ci_total_elapsed "failed"
        exit 1
    fi
    if [ "${DRY_RUN}" != "1" ] && [ -f "${PR_CI_VENV_DIR}/bin/activate" ]; then
        # Ensure CTest-launched python entrypoints resolve to the dedicated local venv.
        # shellcheck disable=SC1090
        source "${PR_CI_VENV_DIR}/bin/activate"
    fi
    if ! select_local_pr_ci_compilers; then
        report_pr_ci_total_elapsed "failed"
        exit 1
    fi
fi

# Verify Python if enabled
if [ "$USE_PYTHON" = "yes" ]; then
    if ! command -v "${PYTHON_EXE}" &> /dev/null; then
        echo -e "${RED}Error: Python executable not found: ${PYTHON_EXE}${NC}"
        echo "Please install Python 3 or specify a valid path with --python /path/to/python3"
        exit 1
    fi

    PYTHON_VERSION=$("${PYTHON_EXE}" --version 2>&1 | awk '{print $2}' || echo "unknown")
    if [ -n "${PYTHON_VERSION}" ] && [ "${PYTHON_VERSION}" != "unknown" ]; then
        echo -e "${GREEN}Using Python: ${PYTHON_VERSION}${NC}"
    else
        echo -e "${YELLOW}Warning: Could not determine Python version${NC}"
    fi
else
    echo -e "${YELLOW}Building without Python support${NC}"
fi
echo ""

# Clean build directory if requested
if [ "$CLEAN_BUILD" = "1" ] && [ "$SKIP_BUILD_RUN_TESTS" != "1" ] && [ -d "${BUILD_DIR}" ]; then
    echo -e "${YELLOW}Cleaning build directory: ${BUILD_DIR}${NC}"
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would remove: ${BUILD_DIR}${NC}"
    else
        rm -rf "${BUILD_DIR}"
    fi
elif [ "$CLEAN_BUILD" = "1" ] && [ "$SKIP_BUILD_RUN_TESTS" = "1" ]; then
    echo -e "${YELLOW}Ignoring --clean because --skip-build-run-tests needs the existing build tree${NC}"
fi

# Create build directory
if [ "$SKIP_BUILD_RUN_TESTS" = "1" ]; then
    :
elif [ "$DRY_RUN" = "1" ]; then
    echo -e "${YELLOW}[DRY-RUN] Would create directory: ${BUILD_DIR}${NC}"
else
    mkdir -p "${BUILD_DIR}"
fi

# When MPI and HDF5 are both enabled but no --with-hdf5 path was given,
# build parallel HDF5 from source so test_c_hdf5_mpi links correctly.
if [ "${ENABLE_HDF5}" = "ON" ] && [ "${ENABLE_MPI}" = "ON" ] && [ -z "${HDF5_ROOT_DIR}" ] && [ "${SKIP_BUILD_RUN_TESTS}" != "1" ] && [ "${DRY_RUN}" != "1" ]; then
    if ! build_default_parallel_hdf5; then
        echo -e "${RED}ERROR: could not build parallel HDF5 from source. Pass --with-hdf5 to provide one manually.${NC}"
        exit 1
    fi
fi

# Export environment variables
export DFTRACER_BUILD_TYPE="${BUILD_TYPE}"
export DFTRACER_BUILD_DEPENDENCIES="${BUILD_DEPENDENCIES}"
export DFTRACER_ENABLE_TESTS="${ENABLE_TESTS}"
export DFTRACER_ENABLE_FTRACING="${ENABLE_FTRACING}"
export DFTRACER_ENABLE_HIP_TRACING="${ENABLE_HIP_TRACING}"
export DFTRACER_ENABLE_MPI="${ENABLE_MPI}"
export DFTRACER_ENABLE_HDF5="${ENABLE_HDF5}"
export DFTRACER_DISABLE_HWLOC="${DISABLE_HWLOC}"
export DFTRACER_ENABLE_DLIO_BENCHMARK_TESTS="${ENABLE_DLIO_TESTS}"
export DFTRACER_ENABLE_PAPER_TESTS="${ENABLE_PAPER_TESTS}"
export DFTRACER_GENERATE_INTERFACES="${GENERATE_INTERFACES}"

# Pip build isolation hides venv site-packages. When interface generation is
# enabled, the generator imports clang.cindex, so mirror CI's default behavior.
if [ "${INSTALL_MODE}" = "pip" ] && [ "${GENERATE_INTERFACES}" = "ON" ] && [ -z "${DFTRACER_PIP_NO_BUILD_ISOLATION+x}" ]; then
    export DFTRACER_PIP_NO_BUILD_ISOLATION="1"
    echo -e "${YELLOW}Info: Enabled DFTRACER_PIP_NO_BUILD_ISOLATION=1 for interface generation (clang.cindex required).${NC}"
fi

if [ -n "${INSTALL_PREFIX}" ]; then
    export DFTRACER_INSTALL_DIR="${INSTALL_PREFIX}"
fi

if [ "${SKIP_BUILD_RUN_TESTS}" = "1" ]; then
    if [ "${RUN_PR_CI_LOCAL}" = "1" ]; then
        if run_local_pr_ci_suite; then
            echo ""
            echo -e "${GREEN}=== Local PR CI Suite Completed Successfully ===${NC}"
            report_pr_ci_total_elapsed "successful"
            exit 0
        fi
        echo ""
        echo -e "${RED}=== Local PR CI Suite Failed ===${NC}"
        report_pr_ci_total_elapsed "failed"
        exit 1
    fi

    if run_existing_ctest_tests; then
        echo ""
        echo -e "${GREEN}=== Existing Tests Completed Successfully ===${NC}"
        exit 0
    fi
    echo ""
    echo -e "${RED}=== Existing Tests Failed ===${NC}"
    exit 1
fi

if [ "$INSTALL_MODE" = "pip" ]; then
    if [ "$USE_PYTHON" != "yes" ]; then
        echo -e "${RED}Error: pip install mode requires Python${NC}"
        echo "Either specify --python /path/to/python3 or use --install-mode cmake"
        exit 1
    fi
    
    echo -e "${GREEN}=== Building and Installing DFTracer with pip ===${NC}"
    echo "This will install dependencies, build, and install DFTracer in the active virtual environment"
    echo ""
    
    # First, ensure build dependencies are installed
    echo -e "${GREEN}Step 0: Installing Python build dependencies${NC}"
    echo ""
    
    # Upgrade pip first to ensure we have the latest version
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: ${PYTHON_EXE} -m pip install --upgrade pip${NC}"
    else
        if ! run_ci_logged_cmd "Upgrade pip" "${PYTHON_EXE}" -m pip install --upgrade pip; then
            echo -e "${RED}Failed to upgrade pip${NC}"
            exit 1
        fi
    fi
    
    # Install build dependencies with normal isolation (not using --no-build-isolation here)
    BUILD_DEPS_CMD=("${PYTHON_EXE}" -m pip install --upgrade setuptools wheel setuptools-scm pybind11 scikit-build-core cmake ninja clang)
    
    if [ "$VERBOSE" = "1" ]; then
        echo -e "${BLUE}[VERBOSE] Build dependencies command: ${BUILD_DEPS_CMD[*]}${NC}"
    fi
    
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: ${BUILD_DEPS_CMD[*]}${NC}"
    else
            if ! run_ci_logged_cmd "Install Python build dependencies" "${BUILD_DEPS_CMD[@]}"; then
            echo -e "${RED}Failed to install Python build dependencies${NC}"
            exit 1
        fi
        echo -e "${GREEN}Python build dependencies installed successfully${NC}"
        # Install gcovr if coverage is enabled
        if [ "$ENABLE_COVERAGE" = "1" ]; then
            echo -e "${GREEN}Installing gcovr for coverage analysis...${NC}"
            if ! "${PYTHON_EXE}" -m pip install gcovr; then
                echo -e "${YELLOW}Warning: Failed to install gcovr. Coverage analysis may not work.${NC}"
            else
                echo -e "${GREEN}gcovr installed successfully${NC}"
            fi
        fi
    fi
    echo ""
    
    # If dependencies should be built, do it first
    if [ "$BUILD_DEPENDENCIES" = "1" ]; then
        echo -e "${GREEN}Step 1: Building C++ dependencies and DFTracer${NC}"
        echo ""
        
        # Set environment to build dependencies
        export DFTRACER_BUILD_DEPENDENCIES="1"
        
        # Build pip extras based on flags
        PIP_EXTRAS="test"
        if [ "$INSTALL_DFANALYZER" = "1" ]; then
            PIP_EXTRAS="test,dfanalyzer"
        fi
        
        # Do a full build which includes dependencies
        FULL_BUILD_CMD=("${PYTHON_EXE}" -m pip install --no-cache-dir ".[${PIP_EXTRAS}]")

        if [ "${DFTRACER_PIP_NO_BUILD_ISOLATION:-0}" = "1" ]; then
            FULL_BUILD_CMD+=(--no-build-isolation)
        fi

        if [ "$VERBOSE" = "1" ]; then
            FULL_BUILD_CMD+=(-v)
            echo -e "${BLUE}[VERBOSE] Full build command: ${FULL_BUILD_CMD[*]}${NC}"
        fi
        
        if [ "$DRY_RUN" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would execute: ${FULL_BUILD_CMD[*]}${NC}"
        else
            if ! run_ci_logged_cmd "Install DFTracer with pip (build dependencies enabled)" "${FULL_BUILD_CMD[@]}"; then
                echo -e "${RED}Failed to build DFTracer with dependencies${NC}"
                exit 1
            fi
            echo -e "${GREEN}DFTracer and dependencies built successfully${NC}"
        fi
        echo ""
    else
        # Skip dependency build
        echo -e "${GREEN}Building DFTracer (skipping dependencies)${NC}"
        echo ""
        
        # Set environment to skip dependency build
        export DFTRACER_BUILD_DEPENDENCIES="0"
        
        # Set environment variables to avoid file locking issues
        
        # Build pip extras based on flags
        PIP_EXTRAS="test"
        if [ "$INSTALL_DFANALYZER" = "1" ]; then
            PIP_EXTRAS="test,dfanalyzer"
        fi
        
        # Build and install with pip (will use the virtual environment)
        PIP_CMD=("${PYTHON_EXE}" -m pip install --no-cache-dir ".[${PIP_EXTRAS}]")

        if [ "${DFTRACER_PIP_NO_BUILD_ISOLATION:-0}" = "1" ]; then
            PIP_CMD+=(--no-build-isolation)
        fi

        if [ "$VERBOSE" = "1" ]; then
            PIP_CMD+=(-v)
        fi
        
        if [ "$DRY_RUN" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would execute: ${PIP_CMD[*]}${NC}"
        else
            if ! run_ci_logged_cmd "Install DFTracer with pip" "${PIP_CMD[@]}"; then
                echo -e "${RED}Failed to build DFTracer${NC}"
                exit 1
            fi
            echo -e "${GREEN}DFTracer built successfully${NC}"
            # Install Python test requirements if tests are enabled
            if [ "$ENABLE_TESTS" = "ON" ] && [ -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
                echo "Installing Python test requirements..."
                if [ "$DRY_RUN" = "1" ]; then
                    echo -e "${YELLOW}[DRY-RUN] Would execute: ${PYTHON_EXE} -m pip install -r ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
                else
                    if [ "$VERBOSE" = "1" ]; then
                        echo -e "${BLUE}[VERBOSE] Installing from: ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
                    fi
                    if "${PYTHON_EXE}" -m pip install -r "${SCRIPT_DIR}/test/py/requirements.txt"; then
                        echo -e "${GREEN}✓ Python test requirements installed${NC}"
                    else
                        echo -e "${YELLOW}Warning: Failed to install Python test requirements${NC}"
                        echo "Some tests may fail. Install manually with:"
                        echo "  ${PYTHON_EXE} -m pip install -r test/py/requirements.txt"
                    fi
                fi
            fi
        fi
        echo ""
    fi
    
    # Print success message
    if [ "$DRY_RUN" = "0" ]; then
        if [ "${RUN_PR_CI_LOCAL}" = "1" ]; then
            if ! run_local_pr_ci_suite; then
                report_pr_ci_total_elapsed "failed"
                exit 1
            fi
            report_pr_ci_total_elapsed "successful"
        else
            if ! run_service_smoke_test; then
                exit 1
            fi
            if ! run_valgrind_ctest_tests; then
                exit 1
            fi
            if ! run_valgrind_dlio_tests; then
                exit 1
            fi
        fi

        echo ""
        echo -e "${GREEN}=== Build and Installation Successful ===${NC}"
        echo ""
        echo "DFTracer has been installed in your virtual environment."
        echo ""
        
        # Determine where to put the environment script
        if [ -n "$VIRTUAL_ENV" ]; then
            ENV_SCRIPT="${VIRTUAL_ENV}/bin/dftracer_env.sh"
        elif [ -n "$CONDA_PREFIX" ]; then
            ENV_SCRIPT="${CONDA_PREFIX}/bin/dftracer_env.sh"
        else
            ENV_SCRIPT="${SCRIPT_DIR}/dftracer_env.sh"
        fi
        
        # Create environment setup script
        cat > "${ENV_SCRIPT}" << EOF
#!/bin/bash
# DFTracer Environment Setup
# Source this file to set up your environment for DFTracer

# Add Python modules to PYTHONPATH for editable install
export PYTHONPATH="${SCRIPT_DIR}/python:\${PYTHONPATH}"

# Add dfanalyzer_old to PYTHONPATH
export PYTHONPATH="${SCRIPT_DIR}/dfanalyzer_old:\${PYTHONPATH}"

echo "DFTracer environment configured."
echo "Python modules: ${SCRIPT_DIR}/python"
EOF
        chmod +x "${ENV_SCRIPT}"
        
        echo "Environment setup script created: ${ENV_SCRIPT}"
        echo ""
        echo "To configure your environment, run:"
        echo "  source ${ENV_SCRIPT}"
        echo ""
        echo "Or add to your shell profile (~/.bashrc or ~/.zshrc):"
        echo "  source ${ENV_SCRIPT}"
        echo ""
        echo "To verify the installation:"
        echo "  source ${ENV_SCRIPT}"
        echo "  ${PYTHON_EXE} -c 'import dftracer; print(dftracer.__version__)'"
        echo ""
        echo "To run tests:"
        echo "  pytest test/"
        echo ""
    else
        echo ""
        echo -e "${GREEN}=== Dry Run Complete ===${NC}"
        if [ "${RUN_VALGRIND_CTEST}" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would run CTest Valgrind gates after build${NC}"
        fi
        if [ "${RUN_VALGRIND_CTEST}" = "1" ] || [ "${RUN_VALGRIND_DLIO}" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would run DLIO Valgrind gate if dlio_benchmark is installed${NC}"
        fi
        echo "No changes were made. Remove --dry-run to execute."
        echo ""
    fi
else
    echo -e "${GREEN}=== Building DFTracer with CMake ===${NC}"
    echo ""
    
    # Prepare CMake arguments
    CMAKE_FULL_ARGS=(
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}"
        "-DDFTRACER_ENABLE_FTRACING=${ENABLE_FTRACING}"
        "-DDFTRACER_ENABLE_HIP_TRACING=${ENABLE_HIP_TRACING}"
        "-DDFTRACER_ENABLE_MPI=${ENABLE_MPI}"
        "-DDFTRACER_ENABLE_HDF5=${ENABLE_HDF5}"
        "-DDFTRACER_GENERATE_INTERFACES=${GENERATE_INTERFACES}"
        "-DDFTRACER_DISABLE_HWLOC=${DISABLE_HWLOC}"
        "-DDFTRACER_ENABLE_TESTS=${ENABLE_TESTS}"
        "-DDFTRACER_ENABLE_DLIO_BENCHMARK_TESTS=${ENABLE_DLIO_TESTS}"
        "-DDFTRACER_ENABLE_PAPER_TESTS=${ENABLE_PAPER_TESTS}"
    )
    
    # Add Python support if enabled
    if [ "$USE_PYTHON" = "yes" ]; then
        # Get pybind11 directory
        if [ "$DRY_RUN" = "1" ]; then
            PYBIND11_DIR="/path/to/pybind11"
            PYTHON_SITE_PACKAGES="/path/to/site-packages"
            echo -e "${YELLOW}[DRY-RUN] Would check for pybind11${NC}"
        else
            PYBIND11_DIR=$("${PYTHON_EXE}" -c "import pybind11; print(pybind11.get_cmake_dir())" 2>/dev/null || echo "")
            
            if [ -z "${PYBIND11_DIR}" ]; then
                echo -e "${YELLOW}pybind11 not found, installing in virtual environment...${NC}"
                "${PYTHON_EXE}" -m pip install --no-cache-dir pybind11
                PYBIND11_DIR=$("${PYTHON_EXE}" -c "import pybind11; print(pybind11.get_cmake_dir())")
            fi
            
            # Get Python site-packages directory
            PYTHON_SITE_PACKAGES=$("${PYTHON_EXE}" -c "import site; print(site.getsitepackages()[0])" 2>/dev/null || echo "")
            if [ -z "${PYTHON_SITE_PACKAGES}" ]; then
                echo -e "${YELLOW}Warning: Could not determine Python site-packages directory${NC}"
                PYTHON_SITE_PACKAGES=$("${PYTHON_EXE}" -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())" 2>/dev/null || echo "")
            fi
        fi
        
        if [ "$VERBOSE" = "1" ]; then
            echo -e "${BLUE}[VERBOSE] pybind11 directory: ${PYBIND11_DIR}${NC}"
            echo -e "${BLUE}[VERBOSE] Python site-packages: ${PYTHON_SITE_PACKAGES}${NC}"
        fi
        
        # Add Python-specific CMake arguments
        CMAKE_FULL_ARGS+=(
            "-DDFTRACER_PYTHON_EXE=${PYTHON_EXE}"
            "-DDFTRACER_PYTHON_SITE=${PYTHON_SITE_PACKAGES}"
            "-Dpybind11_DIR=${PYBIND11_DIR}"
            "-DDFTRACER_BUILD_PYTHON_BINDINGS=ON"
            "-DPYBIND11_FINDPYTHON=ON"
        )
    else
        # Disable Python bindings
        CMAKE_FULL_ARGS+=(
            "-DDFTRACER_BUILD_PYTHON_BINDINGS=OFF"
        )
    fi
    
    # Add compiler overrides
    if [ -n "${C_COMPILER}" ]; then
        CMAKE_FULL_ARGS+=("-DCMAKE_C_COMPILER=${C_COMPILER}")
    fi
    if [ -n "${CXX_COMPILER}" ]; then
        CMAKE_FULL_ARGS+=("-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
    fi

    # Add HDF5 root if specified
    if [ -n "${HDF5_ROOT_DIR}" ]; then
        CMAKE_FULL_ARGS+=("-DHDF5_ROOT=${HDF5_ROOT_DIR}")
        # Spack's HDF5 config can expose a directory-local target named
        # hdf5-shared.  Prefer FindHDF5's concrete library paths so HDF5 can
        # also be forwarded safely to Brahma's separate ExternalProject.
        CMAKE_FULL_ARGS+=("-DHDF5_PREFER_PARALLEL=TRUE")
        CMAKE_FULL_ARGS+=("-DHDF5_NO_FIND_PACKAGE_CONFIG_FILE=TRUE")
        # Also tell CTest to include the HDF5 lib dirs in LD_LIBRARY_PATH so
        # test binaries can find libhdf5 even before RPATH is fully resolved.
        collect_hdf5_runtime_library_paths "${HDF5_ROOT_DIR}"
    fi

    # Add MPI home if specified
    if [ -n "${MPI_ROOT_DIR}" ]; then
        CMAKE_FULL_ARGS+=("-DMPI_HOME=${MPI_ROOT_DIR}")
    fi

    append_cmake_test_ld_library_path_arg

    # Add custom CMake arguments
    if [ -n "${CMAKE_ARGS}" ]; then
        IFS=';' read -ra EXTRA_ARGS <<< "${CMAKE_ARGS}"
        for arg in "${EXTRA_ARGS[@]}"; do
            if [ -n "$arg" ]; then
                CMAKE_FULL_ARGS+=("$arg")
            fi
        done
    fi
    
    if [ "$VERBOSE" = "1" ]; then
        echo -e "${BLUE}[VERBOSE] CMake Arguments:${NC}"
        for arg in "${CMAKE_FULL_ARGS[@]}"; do
            echo -e "${BLUE}[VERBOSE]   $arg${NC}"
        done
    fi
    
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would change to directory: ${BUILD_DIR}${NC}"
    else
        cd "${BUILD_DIR}" || exit 1
    fi
    
    # Step 1: Install dependencies if requested
    if [ "$BUILD_DEPENDENCIES" = "1" ]; then
        echo -e "${BLUE}Step 1: Installing dependencies...${NC}"
        DEP_CMAKE_ARGS=("${CMAKE_FULL_ARGS[@]}")
        DEP_CMAKE_ARGS+=("-DDFTRACER_INSTALL_DEPENDENCIES=ON")
        
        if [ "$DRY_RUN" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would execute: cmake ${SCRIPT_DIR} ${DEP_CMAKE_ARGS[*]}${NC}"
            echo -e "${YELLOW}[DRY-RUN] Would execute: cmake --build . -j${JOBS}${NC}"
            echo -e "${GREEN}Dependencies would be installed${NC}"
        else
            if [ "$VERBOSE" = "1" ]; then
                echo -e "${BLUE}[VERBOSE] Configuring dependencies with CMake${NC}"
            fi
            
            if run_ci_logged_cmd "Configure dependencies with CMake" cmake "${SCRIPT_DIR}" "${DEP_CMAKE_ARGS[@]}"; then
                if [ "$VERBOSE" = "1" ]; then
                    echo -e "${BLUE}[VERBOSE] Building dependencies${NC}"
                fi
                
                if run_ci_logged_cmd "Build dependencies with CMake" cmake --build . -j"${JOBS}"; then
                    echo -e "${GREEN}Dependencies installed successfully${NC}"
                else
                    echo -e "${RED}Failed to build dependencies${NC}"
                    exit 1
                fi
            else
                echo -e "${RED}Failed to configure dependencies${NC}"
                exit 1
            fi
        fi
        echo ""
    fi
    
    # Step 2: Configure DFTracer
    echo -e "${BLUE}Step 2: Configuring DFTracer...${NC}"
    CMAKE_FULL_ARGS+=("-DDFTRACER_INSTALL_DEPENDENCIES=OFF")
    CMAKE_FULL_ARGS+=("-Dyaml-cpp_DIR=${INSTALL_PREFIX}")
    
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: cmake ${SCRIPT_DIR} ${CMAKE_FULL_ARGS[*]}${NC}"
    else
        if [ "$VERBOSE" = "1" ]; then
            echo -e "${BLUE}[VERBOSE] Final CMake configuration:${NC}"
            for arg in "${CMAKE_FULL_ARGS[@]}"; do
                echo -e "${BLUE}[VERBOSE]   $arg${NC}"
            done
        fi
        
        if ! run_ci_logged_cmd "Configure DFTracer with CMake" cmake "${SCRIPT_DIR}" "${CMAKE_FULL_ARGS[@]}"; then
            echo -e "${RED}Failed to configure DFTracer${NC}"
            exit 1
        fi
    fi
    echo ""
    
    # Step 3: Build DFTracer
    echo -e "${BLUE}Step 3: Building DFTracer...${NC}"
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: cmake --build . -j${JOBS}${NC}"
    else
        if [ "$VERBOSE" = "1" ]; then
            echo -e "${BLUE}[VERBOSE] Building with ${JOBS} parallel jobs${NC}"
        fi
        
        if ! run_ci_logged_cmd "Build DFTracer with CMake" cmake --build . -j"${JOBS}"; then
            echo -e "${RED}Failed to build DFTracer${NC}"
            exit 1
        fi
        # Install Python test requirements if tests are enabled
        if [ "$ENABLE_TESTS" = "ON" ] && [ "$USE_PYTHON" = "yes" ] && [ -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
            echo "Installing Python test requirements..."
            if [ "$DRY_RUN" = "1" ]; then
                echo -e "${YELLOW}[DRY-RUN] Would execute: ${PYTHON_EXE} -m pip install -r ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
            else
                if [ "$VERBOSE" = "1" ]; then
                    echo -e "${BLUE}[VERBOSE] Installing from: ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
                fi
                if "${PYTHON_EXE}" -m pip install -r "${SCRIPT_DIR}/test/py/requirements.txt"; then
                    echo -e "${GREEN}✓ Python test requirements installed${NC}"
                else
                    echo -e "${YELLOW}Warning: Failed to install Python test requirements${NC}"
                    echo "Some tests may fail. Install manually with:"
                    echo "  ${PYTHON_EXE} -m pip install -r test/py/requirements.txt"
                fi
            fi
        fi
    fi
    echo ""
    
    # Step 3.5: Install test dependencies if tests are enabled
    if [ "$ENABLE_TESTS" = "ON" ]; then
        echo -e "${BLUE}Step 3.5: Installing test dependencies...${NC}"
        
        # Check for jq (needed for coverage and test analysis)
        if ! command -v jq &> /dev/null; then
            echo -e "${YELLOW}Warning: jq not found. Attempting to install...${NC}"
            
            if [ "$DRY_RUN" = "1" ]; then
                echo -e "${YELLOW}[DRY-RUN] Would install jq${NC}"
            else
                # Try to detect package manager and install jq
                if command -v apt-get &> /dev/null; then
                    echo "Detected apt-get, installing jq..."
                    sudo apt-get update && sudo apt-get install -y jq || echo -e "${YELLOW}Could not install jq with apt-get${NC}"
                elif command -v yum &> /dev/null; then
                    echo "Detected yum, installing jq..."
                    sudo yum install -y jq || echo -e "${YELLOW}Could not install jq with yum${NC}"
                elif command -v brew &> /dev/null; then
                    echo "Detected brew, installing jq..."
                    brew install jq || echo -e "${YELLOW}Could not install jq with brew${NC}"
                else
                    echo -e "${YELLOW}Could not detect package manager. Please install jq manually:${NC}"
                    echo "  - Ubuntu/Debian: sudo apt-get install jq"
                    echo "  - RHEL/CentOS: sudo yum install jq"
                    echo "  - macOS: brew install jq"
                fi
            fi
        else
            echo -e "${GREEN}✓ jq is already installed${NC}"
        fi
        
        # Install Python test requirements if Python is enabled
        if [ "$USE_PYTHON" = "yes" ] && [ -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
            echo "Installing Python test requirements..."
            
            if [ "$DRY_RUN" = "1" ]; then
                echo -e "${YELLOW}[DRY-RUN] Would execute: ${PYTHON_EXE} -m pip install -r test/py/requirements.txt${NC}"
            else
                if [ "$VERBOSE" = "1" ]; then
                    echo -e "${BLUE}[VERBOSE] Installing from: ${SCRIPT_DIR}/test/py/requirements.txt${NC}"
                fi
                
                if "${PYTHON_EXE}" -m pip install -r "${SCRIPT_DIR}/test/py/requirements.txt"; then
                    echo -e "${GREEN}✓ Python test requirements installed${NC}"
                else
                    echo -e "${YELLOW}Warning: Failed to install Python test requirements${NC}"
                    echo "Some tests may fail. Install manually with:"
                    echo "  ${PYTHON_EXE} -m pip install -r test/py/requirements.txt"
                fi
            fi
        else
            if [ "$USE_PYTHON" != "yes" ]; then
                echo "Skipping Python test requirements (Python not enabled)"
            elif [ ! -f "${SCRIPT_DIR}/test/py/requirements.txt" ]; then
                echo -e "${YELLOW}Note: test/py/requirements.txt not found${NC}"
            fi
        fi
        
        echo ""
    fi
    
    # Step 4: Install DFTracer
    echo -e "${BLUE}Step 4: Installing DFTracer...${NC}"
    if [ "$DRY_RUN" = "1" ]; then
        echo -e "${YELLOW}[DRY-RUN] Would execute: cmake --install .${NC}"
        if [ "${RUN_VALGRIND_CTEST}" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would run CTest Valgrind gates after install${NC}"
        fi
        if [ "${RUN_VALGRIND_CTEST}" = "1" ] || [ "${RUN_VALGRIND_DLIO}" = "1" ]; then
            echo -e "${YELLOW}[DRY-RUN] Would run DLIO Valgrind gate if dlio_benchmark is installed${NC}"
        fi
        echo ""
        echo -e "${GREEN}=== Dry Run Complete ===${NC}"
        echo ""
        echo "Would install to: ${INSTALL_PREFIX}"
        echo ""
        echo "No changes were made. Remove --dry-run to execute."
    else
        if run_ci_logged_cmd "Install DFTracer with CMake" cmake --install .; then
            if [ "${RUN_PR_CI_LOCAL}" = "1" ]; then
                if ! run_local_pr_ci_suite; then
                    report_pr_ci_total_elapsed "failed"
                    exit 1
                fi
                report_pr_ci_total_elapsed "successful"
            else
                if ! run_service_smoke_test; then
                    exit 1
                fi
                if ! run_valgrind_ctest_tests; then
                    exit 1
                fi
                if ! run_valgrind_dlio_tests; then
                    exit 1
                fi
            fi

            echo ""
            echo -e "${GREEN}=== Build and Installation Successful ===${NC}"
            echo ""
            echo "Installation directory: ${INSTALL_PREFIX}"
            echo ""
            
            # Create environment setup script for CMake install
            ENV_SCRIPT="${INSTALL_PREFIX}/dftracer_env.sh"
            cat > "${ENV_SCRIPT}" << EOF
#!/bin/bash
# DFTracer Environment Setup
# Source this file to set up your environment for DFTracer

# Add DFTracer binaries to PATH
export PATH="${INSTALL_PREFIX}/bin:\${PATH}"

# Add DFTracer libraries to LD_LIBRARY_PATH
export LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${INSTALL_PREFIX}/lib64:\${LD_LIBRARY_PATH}"

# Add DFTracer to PYTHONPATH (if Python bindings were built)
export PYTHONPATH="${INSTALL_PREFIX}:\${PYTHONPATH}"

echo "DFTracer environment configured."
echo "Install prefix: ${INSTALL_PREFIX}"
EOF
            chmod +x "${ENV_SCRIPT}"
            
            echo "Environment setup script created: ${ENV_SCRIPT}"
            echo ""
            echo "To configure your environment, run:"
            echo "  source ${ENV_SCRIPT}"
            echo ""
            echo "Or add to your shell profile (~/.bashrc or ~/.zshrc):"
            echo "  source ${ENV_SCRIPT}"
            echo ""
            if [ "$ENABLE_TESTS" = "ON" ]; then
                echo "To run tests:"
                echo "  cd ${BUILD_DIR} && ctest"
                echo ""
            fi
            if [ "$ENABLE_COVERAGE" = "1" ] || [ "$BUILD_TYPE" = "PROFILE" ]; then
                echo -e "${BLUE}=== Coverage Analysis ===${NC}"
                echo ""
                echo "Build is configured for coverage analysis."
                echo ""
                echo "To generate coverage report:"
                echo "  ./script/coverage_after_autobuild.sh"
                echo ""
                echo "To generate detailed analysis for test improvement:"
                echo "  ./script/generate_coverage_report.sh > coverage_report.txt"
                echo ""
                echo "View HTML report:"
                echo "  open ${BUILD_DIR}/coverage/html/index.html"
                echo ""
            fi
            echo ""
        else
            echo -e "${RED}Failed to install DFTracer${NC}"
            exit 1
        fi
    fi
fi
