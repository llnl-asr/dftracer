# C/C++ Code Coverage Guide

## Overview

DFTracer uses **gcovr** for C/C++ code coverage analysis. This guide helps you:
- Generate coverage reports locally
- Improve test coverage
- Understand coverage metrics
- Integrate with CI/CD

## Quick Start

### Option 1: Using autobuild.sh (Recommended)

```bash
# 1. Build with coverage enabled
./autobuild.sh --enable-coverage

# 2. Generate coverage report
./script/coverage_after_autobuild.sh

# 3. View HTML report
open build/coverage/html/index.html
```

### Option 2: Manual Build

```bash
# 1. Build with Coverage
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=PROFILE \
      -DDFTRACER_ENABLE_TESTS=ON \
      ..
make -j

# 2. Run Tests
ctest --output-on-failure

# 3. Generate Coverage Report
cd ..
./script/check_coverage.sh
```

This will:
- ✓ Generate detailed HTML report
- ✓ Show coverage summary with color-coded targets
- ✓ Provide actionable next steps
- ✓ Automatically open report in browser

## Local Development Workflow

### Quick Coverage Check

```bash
./script/check_coverage.sh
```

### Comprehensive Analysis with autobuild.sh

```bash
# 1. Build with coverage
./autobuild.sh --enable-coverage

# 2. Run coverage analysis
./script/coverage_after_autobuild.sh

# 3. Generate detailed report for AI analysis
./script/generate_coverage_report.sh > coverage_report.txt

# 4. View interactive HTML report
open build/coverage/html/index.html
```

### Interactive Coverage Check

The `check_coverage.sh` script is your main tool for local development:

```bash
./script/check_coverage.sh
```

**Output includes:**
- Coverage percentages (line, branch, function)
- Color-coded target comparison
- Direct link to HTML report
- Instructions for improving coverage

### Generating Detailed Analysis Report

To get a comprehensive report you can share with AI assistants:

```bash
./script/generate_coverage_report.sh > coverage_report.txt
```

This report includes:
- Overall coverage statistics
- File-by-file breakdown (sorted by coverage %)
- Specific uncovered line numbers
- Partially covered branches
- Uncalled functions
- Recommendations for improvement

**Share with AI**: Send this report to an AI assistant and ask:
> "Based on this coverage report, help me write tests to improve coverage for files with lowest coverage. Start with files in src/dftracer/core/"

### Manual Coverage Generation

Use `make coverage` from the build directory:

```bash
cd build
make coverage
```

Or use the generation script directly:

```bash
./script/generate_coverage.sh build .
```

### Viewing Reports

**Option 1: Open directly**
```bash
open build/coverage/html/index.html
# Or on Linux: xdg-open build/coverage/html/index.html
```

**Option 2: Local web server**
```bash
python3 -m http.server -d build/coverage/html 8000
# Visit http://localhost:8000
```

## Improving Test Coverage

### Complete Workflow with AI Assistant

**Step 1: Generate Coverage Report**
```bash
./autobuild.sh --enable-coverage
./script/coverage_after_autobuild.sh
./script/generate_coverage_report.sh > coverage_report.txt
```

**Step 2: Share with AI Assistant**

Provide the coverage report to an AI assistant (like GitHub Copilot, ChatGPT, or Claude):

> "I have a C++ project called DFTracer. Here's my coverage report:
> 
> [paste contents of coverage_report.txt]
> 
> Please help me write ctest tests to improve coverage for the files with lowest coverage. Focus on src/dftracer/core/ first. For each uncovered function or line range, suggest a test case."

**Step 3: Implement Suggested Tests**

The AI will suggest tests like:
```cpp
// Test for uncovered error handling
TEST(MyClass, HandleNullPointer) {
    MyClass obj;
    EXPECT_THROW(obj.process(nullptr), std::invalid_argument);
}
```

Add these to your test files in `test/` directory.

**Step 4: Verify Improvement**
```bash
cd build
make -j && ctest --output-on-failure
cd ..
./script/coverage_after_autobuild.sh
```

**Step 5: Iterate**
```bash
# Generate new report
./script/generate_coverage_report.sh > coverage_report_v2.txt

# Compare improvement and continue
```

### Manual Analysis

### Step 1: Identify Uncovered Code

Open the HTML report and look for:
- 🔴 **Red lines**: Never executed
- 🟡 **Yellow lines**: Partially executed (branches)
- 🟢 **Green lines**: Fully covered

### Step 2: Analyze Coverage Gaps

**Common patterns to test:**
- Error handling paths
- Edge cases and boundary conditions
- Different configuration options
- Platform-specific code paths
- Exception handling

### Step 3: Add Tests

Create tests in `test/` directory targeting uncovered code:

```cpp
// Example: Testing error path
TEST(MyTest, HandleError) {
    // Test code that triggers error handling
    EXPECT_THROW(myFunction(nullptr), std::runtime_error);
}
```

### Step 4: Verify Improvement

```bash
cd build
ctest  # Run new tests
cd ..
./script/check_coverage.sh  # Check coverage improvement
```

## Coverage Metrics

### Targets

- **Line Coverage**: Target > 80%
- **Branch Coverage**: Target > 70%
- **Function Coverage**: Target > 75%

### What's Covered

DFTracer C/C++ coverage includes:
- Core library API (`src/dftracer/`)
- I/O interception and wrapping
- Tracing infrastructure
- Logging mechanisms
- Utility functions

### What's Excluded

The following are excluded from coverage metrics:
- Test code (`test/` directory)
- Examples (`examples/` directory)
- Third-party dependencies (`dependency/`)
- Build artifacts (`build/`)

### Understanding Coverage Types

**Line Coverage**: Percentage of code lines executed during tests
- Most important metric for overall coverage
- Target: >80%

**Branch Coverage**: Percentage of conditional branches tested
- Measures if/else, switch cases, loops
- Important for testing error paths
- Target: >70%

**Function Coverage**: Percentage of functions called
- Ensures all API functions are tested
- Target: >75%

## CI/CD Integration

### Automated Coverage Reports

Coverage is automatically collected in CI:

1. **Build with PROFILE** on Ubuntu 22.04
2. **Run all tests** with ctest
3. **Generate reports** using gcovr
4. **Upload to Coveralls.io** for tracking
5. **Store artifacts** in GitHub Actions (7 days)
6. **Display summary** in PR checks

### Coveralls.io

View coverage trends and PR impacts:
- **Badge**: Shows current coverage percentage
- **Trends**: Track coverage over time
- **PR Comments**: Automatic coverage change notifications
- **File-level**: Drill down to see specific coverage

## Troubleshooting

### No Coverage Data Found

**Problem**: `.gcda` files not generated

**Solution**:
```bash
# Ensure PROFILE build type
cmake -DCMAKE_BUILD_TYPE=PROFILE -DDFTRACER_ENABLE_TESTS=ON ..
make -j
# Run tests to generate coverage data
ctest
```

### gcovr Not Found

**Problem**: `gcovr: command not found`

**Solution**:
```bash
pip install gcovr
# Or system package
sudo apt-get install gcovr  # Ubuntu/Debian
brew install gcovr          # macOS
```

### Low Coverage Numbers

**Problem**: Coverage below targets

**Solution**:
1. Use `check_coverage.sh` to identify gaps
2. Open HTML report for detailed view
3. Focus on critical paths first
4. Add tests for error handling
5. Test edge cases and boundary conditions

### Coverage Report Not Opening

**Problem**: `open` or `xdg-open` doesn't work

**Solution**:
```bash
# Use local web server
python3 -m http.server -d build/coverage/html 8000
# Visit http://localhost:8000 in browser
```

## Advanced Usage

### Filtering Specific Files

Generate coverage for specific source files:

```bash
cd build
gcovr -r .. . \
    --filter '../src/dftracer/core/' \
    --html-details coverage/core.html
```

### Branch Coverage Analysis

Focus on branch coverage:

```bash
gcovr -r .. . \
    --branches \
    --sort-percentage \
    --print-summary
```

### Coverage Diff

Compare coverage between branches:

```bash
# Generate baseline
git checkout main
./script/check_coverage.sh
cp build/coverage/coverage.xml baseline.xml

# Check feature branch
git checkout feature-branch
./script/check_coverage.sh
# Compare reports
```

## Best Practices

### Writing Testable Code

- Keep functions focused and small
- Avoid deeply nested conditions
- Use dependency injection
- Separate I/O from logic

### Comprehensive Testing

- **Happy path**: Normal execution flow
- **Error paths**: Invalid inputs, failures
- **Edge cases**: Boundary conditions, null/empty
- **Concurrency**: Multi-threaded scenarios (if applicable)

### Coverage Goals

- Prioritize critical code paths (>90% coverage)
- Maintain overall project coverage (>80%)
- Don't obsess over 100% - focus on quality tests
- Document intentionally untested code

## Quick Reference

### Commands

```bash
# Full workflow
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=PROFILE -DDFTRACER_ENABLE_TESTS=ON ..
make -j
ctest --output-on-failure
cd .. && ./script/check_coverage.sh

# Quick check
./script/check_coverage.sh

# Generate reports only
./script/generate_coverage.sh build .

# CMake target
cd build && make coverage

# View report
open build/coverage/html/index.html
```

### Files & Directories

- `script/check_coverage.sh` - Interactive coverage check
- `script/generate_coverage.sh` - Generate reports
- `build/coverage/html/` - HTML reports
- `build/coverage/coverage.xml` - XML for CI
- `build/coverage/coveralls.json` - Coveralls.io format
- `.github/workflows/ci.yml` - CI configuration

### Coverage Targets

| Metric | Target | Current |
|--------|--------|---------|
| Line Coverage | >80% | Check Coveralls |
| Branch Coverage | >70% | Check Coveralls |
| Function Coverage | >75% | Check Coveralls |

### Resources

- [gcovr Documentation](https://gcovr.com/)
- [Coveralls.io](https://coveralls.io/github/LLNL/dftracer)
- [GitHub Actions Artifacts](https://github.com/llnl-asr/dftracer/actions)
   - `test/py/` for Python tests

2. Add test to `test/CMakeLists.txt`

3. Run tests and verify coverage:
   ```bash
   cd build
   ctest -R your_new_test -VV
   ./script/generate_coverage.sh . ..
   ```

## Troubleshooting

### No coverage data generated

- Ensure building with `CMAKE_BUILD_TYPE=PROFILE`
- Verify `--coverage` flags are added (check compiler output)
- Run tests before generating reports

### Coverage percentage is 0%

- Check that `.gcda` files are generated in build directory
- Ensure tests actually run (not skipped)
- Verify source paths match between build and gcovr execution

### Coveralls upload fails

- Check `COVERALLS_REPO_TOKEN` secret is set in GitHub
- Verify JSON file format is valid
- Check Coveralls.io service status

## Configuration Files

- **`.coveragerc`**: Python coverage configuration
- **`script/generate_coverage.sh`**: Coverage generation script
- **`.github/workflows/ci.yml`**: CI coverage automation

## Coverage Reports on Pull Requests

Coverage reports are automatically added to:
- **Pull Request comments**: Coverage change summary
- **GitHub Actions Summary**: Detailed coverage table
- **Coveralls.io**: Historical tracking and comparison

## Badge

Add coverage badge to README.md:

```markdown
[![Coverage Status](https://coveralls.io/repos/github/LLNL/dftracer/badge.svg?branch=develop)](https://coveralls.io/github/LLNL/dftracer?branch=develop)
```
