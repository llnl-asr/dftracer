/*
 * Comprehensive C API Coverage Test
 * Tests all C API functions provided by dftracer
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#include <dftracer/dftracer.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ignore_result.h"

/* Helper function for sleeping */
static void sleep_ms(int milliseconds) {
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

/* Test C function tracing macros */
void test_c_function_tracing() {
  DFTRACER_C_FUNCTION_START();

  /* Test metadata updates */
  DFTRACER_C_FUNCTION_UPDATE_INT("c_int", 42);
  DFTRACER_C_FUNCTION_UPDATE_STR("c_string", "c_value");
  DFTRACER_C_FUNCTION_UPDATE_INT("iterations", 100);
  DFTRACER_C_FUNCTION_UPDATE_STR("function_name", __func__);

  /* Test more metadata variations */
  DFTRACER_C_FUNCTION_UPDATE_INT("buffer_size", 4096);
  DFTRACER_C_FUNCTION_UPDATE_STR("status", "running");

  sleep_ms(1);

  DFTRACER_C_FUNCTION_END();
}

/* Test C function tracing with typed updates */
void test_c_function_tracing_typed() {
  DFTRACER_C_FUNCTION_START();

  /* Test typed metadata updates */
  DFTRACER_C_FUNCTION_UPDATE_INT_TYPE("typed_int", 123, 0);
  DFTRACER_C_FUNCTION_UPDATE_STR_TYPE("typed_string", "typed_value", 0);
  DFTRACER_C_FUNCTION_UPDATE_INT_TYPE("priority", 5, 1);
  DFTRACER_C_FUNCTION_UPDATE_STR_TYPE("category", "test", 1);

  sleep_ms(1);

  DFTRACER_C_FUNCTION_END();
}

/* Test C region tracing */
void test_c_region_tracing() {
  DFTRACER_C_FUNCTION_START();

  /* Start a custom region */
  DFTRACER_C_REGION_START(C_REGION_1);
  DFTRACER_C_REGION_UPDATE_STR(C_REGION_1, "region_name", "first_region");
  DFTRACER_C_REGION_UPDATE_INT(C_REGION_1, "size", 1024);
  DFTRACER_C_REGION_UPDATE_STR(C_REGION_1, "operation", "read");
  sleep_ms(1);
  DFTRACER_C_REGION_END(C_REGION_1);

  /* Another region */
  DFTRACER_C_REGION_START(C_REGION_2);
  DFTRACER_C_REGION_UPDATE_INT(C_REGION_2, "iteration", 1);
  DFTRACER_C_REGION_UPDATE_STR(C_REGION_2, "status", "running");
  DFTRACER_C_REGION_UPDATE_INT(C_REGION_2, "count", 42);
  sleep_ms(1);
  DFTRACER_C_REGION_END(C_REGION_2);

  /* Test nested regions */
  DFTRACER_C_REGION_START(OUTER_REGION);
  DFTRACER_C_REGION_UPDATE_STR(OUTER_REGION, "level", "outer");

  DFTRACER_C_REGION_START(INNER_REGION);
  DFTRACER_C_REGION_UPDATE_STR(INNER_REGION, "level", "inner");
  sleep_ms(1);
  DFTRACER_C_REGION_END(INNER_REGION);

  DFTRACER_C_REGION_END(OUTER_REGION);

  DFTRACER_C_FUNCTION_END();
}

/* Test C region tracing with typed updates */
void test_c_region_tracing_typed() {
  DFTRACER_C_FUNCTION_START();

  /* Region with typed updates */
  DFTRACER_C_REGION_START(C_REGION_TYPED);
  DFTRACER_C_REGION_UPDATE_INT_TYPE(C_REGION_TYPED, "count", 999, 0);
  DFTRACER_C_REGION_UPDATE_STR_TYPE(C_REGION_TYPED, "type", "typed_region", 0);
  DFTRACER_C_REGION_UPDATE_INT_TYPE(C_REGION_TYPED, "version", 2, 1);
  DFTRACER_C_REGION_UPDATE_STR_TYPE(C_REGION_TYPED, "mode", "advanced", 1);
  sleep_ms(1);
  DFTRACER_C_REGION_END(C_REGION_TYPED);

  DFTRACER_C_FUNCTION_END();
}

/* Test C metadata */
void test_c_metadata() {
  DFTRACER_C_FUNCTION_START();

  DFTRACER_C_METADATA(app_meta, "c_app", "api_test");
  DFTRACER_C_METADATA(lang_meta, "language", "C");
  DFTRACER_C_METADATA(version_meta, "version", "1.0");

  DFTRACER_C_FUNCTION_END();
}

/* Test I/O operations with C API */
void test_c_io_operations(const char* data_dir) {
  DFTRACER_C_FUNCTION_START();

  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/c_api_test.dat", data_dir);

  DFTRACER_C_FUNCTION_UPDATE_STR("filename", filename);
  DFTRACER_C_FUNCTION_UPDATE_INT("buffer_size", 128);

  /* POSIX file operations */
  int fd = open(filename, O_CREAT | O_RDWR, 0644);
  if (fd != -1) {
    char buf[128] = "C test data";
    DFT_IGNORE(write(fd, buf, strlen(buf)));
    lseek(fd, 0, SEEK_SET);
    DFT_IGNORE(read(fd, buf, sizeof(buf)));
    close(fd);
  }

  /* Standard C file operations */
  FILE* fp = fopen(filename, "r");
  if (fp != NULL) {
    char buf[128];
    DFT_IGNORE(fread(buf, 1, sizeof(buf), fp));
    fclose(fp);
  }

  /* Cleanup */
  unlink(filename);

  DFTRACER_C_FUNCTION_END();
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <data_dir>\n", argv[0]);
    return 1;
  }

  /* Initialize DFTracer with main binding */
  DFTRACER_C_INIT(NULL, NULL, NULL);

  /* Run all test functions - complete C API coverage */
  test_c_metadata();
  test_c_function_tracing();
  test_c_function_tracing_typed();
  test_c_region_tracing();
  test_c_region_tracing_typed();
  test_c_io_operations(argv[1]);

  /* Finalize */
  DFTRACER_C_FINI();

  printf("All C API tests completed successfully (16/16 APIs tested)\n");

  return 0;
}
