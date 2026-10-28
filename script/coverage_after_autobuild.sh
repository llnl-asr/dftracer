#!/bin/bash
# Run coverage analysis after autobuild.sh
# This script works with the build environment created by autobuild.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
CLEAN_COVERAGE=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_COVERAGE=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Run coverage analysis after autobuild.sh"
            echo ""
            echo "OPTIONS:"
            echo "  --clean    Remove old coverage data and regenerate fresh coverage"
            echo "  --help     Show this help message"
            echo ""
            echo "EXAMPLES:"
            echo "  $0              # Generate coverage (incremental if data exists)"
            echo "  $0 --clean      # Clean old data, rerun tests, generate fresh coverage"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo -e "${BLUE}==========================================="
echo "DFTracer Coverage After Autobuild"
echo "==========================================${NC}"
echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: Build directory not found: $BUILD_DIR${NC}"
    echo ""
    echo "Please run autobuild.sh first:"
    echo "  ./autobuild.sh --build-type PROFILE --enable-tests"
    exit 1
fi

# Check if this is a PROFILE build
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo -e "${RED}Error: CMakeCache.txt not found in build directory${NC}"
    exit 1
fi

BUILD_TYPE=$(grep "CMAKE_BUILD_TYPE:" "$BUILD_DIR/CMakeCache.txt" | cut -d '=' -f2)
if [ "$BUILD_TYPE" != "PROFILE" ]; then
    echo -e "${YELLOW}Warning: Build type is '$BUILD_TYPE', not 'PROFILE'${NC}"
    echo ""
    echo "For coverage analysis, rebuild with:"
    echo "  ./autobuild.sh --build-type PROFILE --enable-tests"
    echo ""
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Check if tests are enabled
TESTS_ENABLED=$(grep "DFTRACER_ENABLE_TESTS:" "$BUILD_DIR/CMakeCache.txt" | cut -d '=' -f2)
if [ "$TESTS_ENABLED" != "ON" ]; then
    echo -e "${RED}Error: Tests are not enabled${NC}"
    echo ""
    echo "Rebuild with tests enabled:"
    echo "  ./autobuild.sh --build-type PROFILE --enable-tests"
    exit 1
fi

echo -e "${GREEN}✓ Build directory found: $BUILD_DIR${NC}"
echo -e "${GREEN}✓ Build type: $BUILD_TYPE${NC}"
echo -e "${GREEN}✓ Tests enabled${NC}"
echo ""

# Check for gcovr
if ! command -v gcovr &> /dev/null; then
    echo -e "${RED}Error: gcovr not found${NC}"
    echo ""
    echo "Install with:"
    echo "  pip install gcovr"
    exit 1
fi

# Clean old coverage data if requested
if [ "$CLEAN_COVERAGE" -eq 1 ]; then
    echo -e "${YELLOW}Cleaning old coverage data...${NC}"
    
    # Remove .gcda files (coverage data)
    GCDA_FILES=$(find "$BUILD_DIR" -name "*.gcda" 2>/dev/null)
    if [ -n "$GCDA_FILES" ]; then
        echo "$GCDA_FILES" | xargs rm -f
        echo -e "${GREEN}✓ Removed .gcda files${NC}"
    fi
    
    # Remove old coverage reports
    if [ -d "$BUILD_DIR/coverage" ]; then
        rm -rf "$BUILD_DIR/coverage"
        echo -e "${GREEN}✓ Removed old coverage reports${NC}"
    fi
    
    echo ""
fi

# Check if tests have been run
GCDA_COUNT=$(find "$BUILD_DIR" -name "*.gcda" 2>/dev/null | wc -l)
if [ "$GCDA_COUNT" -eq 0 ]; then
    if [ "$CLEAN_COVERAGE" -eq 1 ]; then
        echo -e "${BLUE}Running tests to generate fresh coverage data...${NC}"
    else
        echo -e "${YELLOW}No coverage data found. Running tests...${NC}"
    fi
    echo ""
    cd "$BUILD_DIR"
    if ! ctest --output-on-failure; then
        echo -e "${RED}Tests failed${NC}"
        exit 1
    fi
    cd "$PROJECT_DIR"
    echo ""
    GCDA_COUNT=$(find "$BUILD_DIR" -name "*.gcda" 2>/dev/null | wc -l)
fi

echo -e "${GREEN}✓ Found $GCDA_COUNT coverage data files${NC}"
echo ""

# Remove coverage data for Python bindings to avoid gcov errors
echo -e "${BLUE}Cleaning Python binding coverage files...${NC}"
PYTHON_GCDA=$(find "$BUILD_DIR" -path "*/src/dftracer/python/*" -name "*.gcda" 2>/dev/null | wc -l)
PYTHON_GCNO=$(find "$BUILD_DIR" -path "*/src/dftracer/python/*" -name "*.gcno" 2>/dev/null | wc -l)
echo "Found $PYTHON_GCDA Python .gcda files and $PYTHON_GCNO Python .gcno files to remove"

find "$BUILD_DIR" -path "*/src/dftracer/python/*" -name "*.gcda" -delete 2>/dev/null || true
find "$BUILD_DIR" -path "*/src/dftracer/python/*" -name "*.gcno" -delete 2>/dev/null || true

REMAINING_GCDA=$(find "$BUILD_DIR" -name "*.gcda" 2>/dev/null | wc -l)
echo "Remaining coverage data files after cleanup: $REMAINING_GCDA"

if [ "$REMAINING_GCDA" -eq 0 ]; then
    echo -e "${RED}Warning: No coverage data files remaining after cleanup!${NC}"
    echo "Listing all .gcda files before cleanup:"
    find "$BUILD_DIR" -name "*.gcda" 2>/dev/null || echo "None found"
fi

echo -e "${GREEN}✓ Python binding coverage files removed${NC}"
echo ""

# Generate coverage reports
echo -e "${BLUE}Generating coverage reports...${NC}"
echo ""

# Ensure coverage/html directory exists
mkdir -p "$BUILD_DIR/coverage/html"

# Always set SOURCE_DIR to project root
SOURCE_DIR="$PROJECT_DIR"
bash "${SCRIPT_DIR}/generate_coverage.sh" "$BUILD_DIR" "$SOURCE_DIR"

# Show summary
echo ""
echo -e "${BLUE}==========================================="
echo "Coverage Summary"
echo "==========================================${NC}"

cd "$BUILD_DIR"
gcovr -r "$PROJECT_DIR" . \
    -e "$PROJECT_DIR/test/" \
    -e "$PROJECT_DIR/examples/" \
    -e "$PROJECT_DIR/dependency/" \
    -e "$PROJECT_DIR/build/" \
    --gcov-ignore-parse-errors=negative_hits.warn_once_per_file \
    --exclude-unreachable-branches \
    --exclude-throw-branches

cd "$PROJECT_DIR"

echo ""
echo -e "${GREEN}==========================================="
echo "Next Steps"
echo "==========================================${NC}"
echo ""
echo "1. View detailed HTML report:"
echo -e "   ${GREEN}open $BUILD_DIR/coverage/html/index.html${NC}"
echo ""
echo "2. Generate detailed analysis for AI assistant:"
echo -e "   ${GREEN}./script/generate_coverage_report.sh > coverage_analysis.txt${NC}"
echo ""
echo "3. Or start a local server:"
echo "   python3 -m http.server -d $BUILD_DIR/coverage/html 8000"
echo ""
echo "==========================================="
