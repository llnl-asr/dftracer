#!/bin/bash

set -e

if [ -z "$1" ]; then
    echo "Error: Build directory not provided"
    echo "Usage: $0 <build_directory>"
    exit 1
fi

BUILD_DIR="$1"

if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory '$BUILD_DIR' does not exist"
    exit 1
fi

BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
SOURCE_DIR=$(cd "$(dirname "$0")/.." && pwd)
COVERAGE_DIR="$BUILD_DIR/coverage"

echo "Generating coverage reports..."

GCDA_COUNT=$(find "$BUILD_DIR" -name "*.gcda" -type f | wc -l)
if [ "$GCDA_COUNT" -eq 0 ]; then
    echo "Error: No coverage data files found"
    exit 1
fi

mkdir -p "$COVERAGE_DIR"
cd "$BUILD_DIR"

echo "Generating Coveralls JSON..."
COVERALLS_JSON="$COVERAGE_DIR/coveralls.json"
gcovr -r "$SOURCE_DIR" . \
    --coveralls "$COVERALLS_JSON" \
    --exclude "build/" \
    --exclude "install/" \
    --exclude "test/" \
    --exclude ".*/pybind11/.*" \
    --exclude "/usr/.*" \
    --gcov-ignore-errors=no_working_dir_found \
    --gcov-ignore-parse-errors=negative_hits.warn_once_per_file \
    --exclude-unreachable-branches \
    --exclude-throw-branches

echo "✓ Coveralls JSON: $COVERALLS_JSON"
