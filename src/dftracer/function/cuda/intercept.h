// Created by Druva on 7/15/25

#ifndef DFTRACER_CUPTI_INTERCEPT_H
#define DFTRACER_CUPTI_INTERCEPT_H

#ifdef DFTRACER_DEBUG
#include <dftracer/dftracer_config_dbg.hpp>
#else
#include <dftracer/dftracer_config.hpp>
#endif
#define DFTRACER_CUPTI_TRACING_ENABLE 1

#ifdef DFTRACER_CUPTI_TRACING_ENABLE

#include <cuda.h>
#include <cupti.h>
#include <dftracer/core/logging.h>
#include <dftracer/function/generic_function.h>

#include <cstdint>
#include <string>
#include <unordered_map>

#define CUPTI_CALL(call)                                                     \
  do {                                                                       \
    CUptiResult _status = call;                                              \
    if (_status != CUPTI_SUCCESS) {                                          \
      const char *errstr;                                                    \
      cuptiGetResultString(_status, &errstr);                                \
      DFTRACER_LOG_ERROR(                                                    \
          "CUPTI call failed: %s:%d: function %s failed with error %s",      \
          __FILE__, __LINE__, #call, errstr);                                \
      printf("CUPTI call failed: %s:%d: function %s failed with error %s\n", \
             __FILE__, __LINE__, #call, errstr);                             \
    }                                                                        \
  } while (0)

#define BUF_SIZE (1024 * 1024)  // 1MB buffer size

namespace dftracer {

// Used to trace NVIDIA GPU APIs - CUDA Runtime, Driver, and kernel activities
class CUPTIFunction : public dftracer::GenericFunction {
 private:
  // Timestamp at trace initialization time for normalization
  uint64_t startTimestamp;

  // Buffer management
  static const size_t buffer_size = BUF_SIZE;

  TimeResolution time_diff;
  TimeResolution transform_timestamp(uint64_t timestamp);
  TimeResolution transform_time(uint64_t end_time, uint64_t start_time);

  // Helper functions for activity processing
  const char *getMemcpyKindString(CUpti_ActivityMemcpyKind kind);
  const char *getActivityOverheadKindString(CUpti_ActivityOverheadKind kind);
  const char *getActivityObjectKindString(CUpti_ActivityObjectKind kind);
  const char *getComputeApiKindString(CUpti_ActivityComputeApiKind kind);
  uint32_t getActivityObjectKindId(CUpti_ActivityObjectKind kind,
                                   CUpti_ActivityObjectKindId *id);

  // Activity record processing
  void processActivity(CUpti_Activity *record);
  void processKernelActivity(CUpti_ActivityKernel5 *kernel,
                             const char *kindString);
  void processMemcpyActivity(CUpti_ActivityMemcpy4 *memcpy);
  void processMemsetActivity(CUpti_ActivityMemset3 *memset);
  void processRuntimeActivity(CUpti_ActivityAPI *api, const char *apiType);
  void processDriverActivity(CUpti_ActivityAPI *api);
  void processContextActivity(CUpti_ActivityContext *context);
  void processDeviceActivity(CUpti_ActivityDevice2 *device);

 public:
  CUPTIFunction() : dftracer::GenericFunction() {
    DFTRACER_LOG_DEBUG("Creating CUPTIFunction instance", "");
    time_diff = 0;
    startTimestamp = 0;
  }

  // Static callback functions for CUPTI
  static void CUPTIAPI bufferRequested(uint8_t **buffer, size_t *size,
                                       size_t *maxNumRecords);
  static void CUPTIAPI bufferCompleted(CUcontext ctx, uint32_t streamId,
                                       uint8_t *buffer, size_t size,
                                       size_t validSize);

  void initialize() override;
  void finalize() override;
};

}  // namespace dftracer

#endif  // DFTRACER_CUPTI_TRACING_ENABLE
#endif  // DFTRACER_CUPTI_INTERCEPT_H