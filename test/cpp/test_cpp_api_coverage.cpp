//
// Comprehensive C++ API Coverage Test
// Tests all C++ API functions provided by dftracer
//

#include <dftracer/dftracer.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "ignore_result.h"

// Test all function-level tracing APIs
void test_function_tracing() {
  DFTRACER_CPP_FUNCTION();

  // Test function metadata updates
  DFTRACER_CPP_FUNCTION_UPDATE("int_key", 42);
  DFTRACER_CPP_FUNCTION_UPDATE("string_key", "test_value");
  DFTRACER_CPP_FUNCTION_UPDATE("double_key", 3.14);

  usleep(1000);
}

// Test region-based tracing APIs
void test_region_tracing() {
  DFTRACER_CPP_FUNCTION();

  // Static region
  {
    DFTRACER_CPP_REGION(STATIC_REGION);
    DFTRACER_CPP_REGION_UPDATE(STATIC_REGION, "region_key", "region_value");
    DFTRACER_CPP_REGION_UPDATE(STATIC_REGION, "region_int", 100);
    usleep(500);
  }

  // Dynamic region with start/end
  DFTRACER_CPP_REGION_START(DYNAMIC_REGION);
  DFTRACER_CPP_REGION_DYN_UPDATE(DYNAMIC_REGION, "dyn_key", 200);
  DFTRACER_CPP_REGION_DYN_UPDATE(DYNAMIC_REGION, "dyn_str", "dynamic");
  usleep(500);
  DFTRACER_CPP_REGION_END(DYNAMIC_REGION);

  // Nested regions
  {
    DFTRACER_CPP_REGION(OUTER_REGION);
    usleep(200);
    {
      DFTRACER_CPP_REGION(INNER_REGION);
      usleep(200);
    }
  }
}

// Test metadata APIs
void test_metadata() {
  DFTRACER_CPP_FUNCTION();

  DFTRACER_CPP_METADATA(global_meta, "app_name", "api_coverage_test");
  DFTRACER_CPP_METADATA(version_meta, "version", "1.0.0");
  DFTRACER_CPP_METADATA(config_meta, "config", "test_mode");
}

// Test I/O tracing with POSIX calls
void test_io_operations(const char* data_dir) {
  DFTRACER_CPP_FUNCTION();

  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/cpp_api_test.dat", data_dir);

  // File operations
  int fd = open(filename, O_CREAT | O_RDWR, 0644);
  if (fd != -1) {
    char buf[128] = "test data";
    DFT_IGNORE(write(fd, buf, strlen(buf)));
    lseek(fd, 0, SEEK_SET);
    DFT_IGNORE(read(fd, buf, sizeof(buf)));
    fsync(fd);
    close(fd);
  }

  // FILE* operations
  FILE* fp = fopen(filename, "r+");
  if (fp != nullptr) {
    char buf[128];
    fwrite("more data", 1, 9, fp);
    fseek(fp, 0, SEEK_SET);
    DFT_IGNORE(fread(buf, 1, sizeof(buf), fp));
    fflush(fp);
    fclose(fp);
  }

  // File metadata operations
  struct stat st;
  stat(filename, &st);
  chmod(filename, 0600);

  // Cleanup
  unlink(filename);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <data_dir>\n", argv[0]);
    return 1;
  }

  // Initialize DFTracer
  DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);

  // Run all test functions
  test_metadata();
  test_function_tracing();
  test_region_tracing();
  test_io_operations(argv[1]);

  // Finalize
  DFTRACER_CPP_FINI();

  return 0;
}
