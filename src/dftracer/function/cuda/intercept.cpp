// Created by Druva on 7/15/25
#include <dftracer/function/cuda/intercept.h>

#include <dftracer/dftracer_config.hpp>

#define DFTRACER_CUPTI_TRACING_ENABLE 1
#ifdef DFTRACER_CUPTI_TRACING_ENABLE

#include <dftracer/core/logging.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

// If you get undefined symbol with Singleton instance, you need to add this to
// instantiate it as null
template <>
std::shared_ptr<dftracer::CUPTIFunction>
    dftracer::Singleton<dftracer::CUPTIFunction>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::CUPTIFunction>::stop_creating_instances =
    false;

long long unsigned int total_memory_allocated = 0;
int times = 0;

namespace dftracer {

// Helper function implementations
const char* CUPTIFunction::getMemcpyKindString(CUpti_ActivityMemcpyKind kind) {
  switch (kind) {
    case CUPTI_ACTIVITY_MEMCPY_KIND_HTOD:
      return "HtoD";
    case CUPTI_ACTIVITY_MEMCPY_KIND_DTOH:
      return "DtoH";
    case CUPTI_ACTIVITY_MEMCPY_KIND_HTOA:
      return "HtoA";
    case CUPTI_ACTIVITY_MEMCPY_KIND_ATOH:
      return "AtoH";
    case CUPTI_ACTIVITY_MEMCPY_KIND_ATOA:
      return "AtoA";
    case CUPTI_ACTIVITY_MEMCPY_KIND_ATOD:
      return "AtoD";
    case CUPTI_ACTIVITY_MEMCPY_KIND_DTOA:
      return "DtoA";
    case CUPTI_ACTIVITY_MEMCPY_KIND_DTOD:
      return "DtoD";
    case CUPTI_ACTIVITY_MEMCPY_KIND_HTOH:
      return "HtoH";
    default:
      return "<unknown>";
  }
}

const char* CUPTIFunction::getActivityOverheadKindString(
    CUpti_ActivityOverheadKind kind) {
  switch (kind) {
    case CUPTI_ACTIVITY_OVERHEAD_DRIVER_COMPILER:
      return "COMPILER";
    case CUPTI_ACTIVITY_OVERHEAD_CUPTI_BUFFER_FLUSH:
      return "BUFFER_FLUSH";
    case CUPTI_ACTIVITY_OVERHEAD_CUPTI_INSTRUMENTATION:
      return "INSTRUMENTATION";
    case CUPTI_ACTIVITY_OVERHEAD_CUPTI_RESOURCE:
      return "RESOURCE";
    default:
      return "<unknown>";
  }
}

const char* CUPTIFunction::getActivityObjectKindString(
    CUpti_ActivityObjectKind kind) {
  switch (kind) {
    case CUPTI_ACTIVITY_OBJECT_PROCESS:
      return "PROCESS";
    case CUPTI_ACTIVITY_OBJECT_THREAD:
      return "THREAD";
    case CUPTI_ACTIVITY_OBJECT_DEVICE:
      return "DEVICE";
    case CUPTI_ACTIVITY_OBJECT_CONTEXT:
      return "CONTEXT";
    case CUPTI_ACTIVITY_OBJECT_STREAM:
      return "STREAM";
    default:
      return "<unknown>";
  }
}

const char* CUPTIFunction::getComputeApiKindString(
    CUpti_ActivityComputeApiKind kind) {
  switch (kind) {
    case CUPTI_ACTIVITY_COMPUTE_API_CUDA:
      return "CUDA";
    case CUPTI_ACTIVITY_COMPUTE_API_CUDA_MPS:
      return "CUDA_MPS";
    default:
      return "<unknown>";
  }
}

uint32_t CUPTIFunction::getActivityObjectKindId(
    CUpti_ActivityObjectKind kind, CUpti_ActivityObjectKindId* id) {
  switch (kind) {
    case CUPTI_ACTIVITY_OBJECT_PROCESS:
      return id->pt.processId;
    case CUPTI_ACTIVITY_OBJECT_THREAD:
      return id->pt.threadId;
    case CUPTI_ACTIVITY_OBJECT_DEVICE:
      return id->dcs.deviceId;
    case CUPTI_ACTIVITY_OBJECT_CONTEXT:
      return id->dcs.contextId;
    case CUPTI_ACTIVITY_OBJECT_STREAM:
      return id->dcs.streamId;
    default:
      return 0xffffffff;
  }
}

TimeResolution CUPTIFunction::transform_timestamp(uint64_t timestamp) {
  return static_cast<TimeResolution>((timestamp - startTimestamp) /
                                     1e3);  // Convert to microseconds
}

TimeResolution CUPTIFunction::transform_time(uint64_t end_time,
                                             uint64_t start_time) {
  return std::floor(end_time / 1000.0) - std::floor(start_time / 1000.0);
}

// Static callback implementations
void CUPTIAPI CUPTIFunction::bufferRequested(uint8_t** buffer, size_t* size,
                                             size_t* maxNumRecords) {
  printf("CUPTI buffer requested\n");
  *size = BUF_SIZE;
  *buffer = static_cast<uint8_t*>(malloc(BUF_SIZE));
  *maxNumRecords = 1;

  if (*buffer == nullptr) {
    DFTRACER_LOG_ERROR("Failed to allocate CUPTI buffer of size %zu", BUF_SIZE);
    printf("Failed to allocate CUPTI buffer of size %zu\n", BUF_SIZE);
    *size = 0;
    *maxNumRecords = 0;
  } else {
    total_memory_allocated += BUF_SIZE;
    printf("CUPTI buffer allocated: %zu bytes\n", BUF_SIZE);
    printf("Total memory allocated: %llu bytes\n", total_memory_allocated);
    printf("Total times flushed: %d\n", times);
    if (++times == 20) {
      // Flush
      times = 0;
      printf("Flushing\n");
      CUPTI_CALL(cuptiActivityFlushAll(1));
    }
  }
}

void CUPTIAPI CUPTIFunction::bufferCompleted(CUcontext ctx, uint32_t streamId,
                                             uint8_t* buffer, size_t size,
                                             size_t validSize) {
  printf("CUPTI buffer completed\n");
  CUptiResult status;
  CUpti_Activity* record = nullptr;
  auto instance = dftracer::Singleton<dftracer::CUPTIFunction>::get_instance();

  if (validSize > 0) {
    do {
      status = cuptiActivityGetNextRecord(buffer, validSize, &record);
      if (status == CUPTI_SUCCESS) {
        instance->processActivity(record);
      } else if (status == CUPTI_ERROR_MAX_LIMIT_REACHED) {
        break;
      } else {
        CUPTI_CALL(status);
      }
    } while (1);

    // Report any records dropped from the queue
    size_t dropped;
    CUPTI_CALL(cuptiActivityGetNumDroppedRecords(ctx, streamId, &dropped));
    if (dropped != 0) {
      DFTRACER_LOG_WARN("Dropped %zu CUPTI activity records", dropped);
    }
  }

  free(buffer);
}

// Main class method implementations
void CUPTIFunction::initialize() {
  printf("CUPTIFunction::initialize() called\n");
  DFTRACER_LOG_DEBUG("Initializing CUPTIFunction instance", "");

  // Get initial timestamp for normalization
  CUPTI_CALL(cuptiGetTimestamp(&startTimestamp));

  size_t attrValue = 0, attrValueSize = sizeof(size_t);

  // Device activity record is created when CUDA initializes, so we
  // want to enable it before cuInit() or any CUDA runtime call.
  CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_DEVICE));

  // Enable all other activity record kinds
  // CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_CONTEXT));
  // CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_DRIVER));
  CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_RUNTIME));
  CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMCPY));
  // CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMSET));
  // CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_NAME));
  // CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MARKER));
  // CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL));
  // CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_OVERHEAD));
  printf("Enabled all activity kinds\n");

  // Register callbacks for buffer requests and completion
  CUPTI_CALL(cuptiActivityRegisterCallbacks(bufferRequested, bufferCompleted));

  // Configure buffer attributes
  CUPTI_CALL(cuptiActivityGetAttribute(CUPTI_ACTIVITY_ATTR_DEVICE_BUFFER_SIZE,
                                       &attrValueSize, &attrValue));
  attrValue *= 2;  // Double the buffer size
  CUPTI_CALL(cuptiActivitySetAttribute(CUPTI_ACTIVITY_ATTR_DEVICE_BUFFER_SIZE,
                                       &attrValueSize, &attrValue));

  CUPTI_CALL(
      cuptiActivityGetAttribute(CUPTI_ACTIVITY_ATTR_DEVICE_BUFFER_POOL_LIMIT,
                                &attrValueSize, &attrValue));
  attrValue = 20;  // Double the buffer pool limit
  CUPTI_CALL(
      cuptiActivitySetAttribute(CUPTI_ACTIVITY_ATTR_DEVICE_BUFFER_POOL_LIMIT,
                                &attrValueSize, &attrValue));
  printf("CUPTI buffer pool limit set to %zu\n", attrValue);
  DFTRACER_LOG_DEBUG("CUPTI initialization completed", "");
}

void CUPTIFunction::finalize() {
  DFTRACER_LOG_DEBUG("Finalizing CUPTIFunction instance", "");

  // Force flush any remaining activity buffers before termination
  CUPTI_CALL(cuptiActivityFlushAll(1));

  DFTRACER_LOG_DEBUG("CUPTI finalization completed", "");
}

// Activity processing implementation
void CUPTIFunction::processActivity(CUpti_Activity* record) {
  printf("Processing CUPTI activity: %d\n", record->kind);

  switch (record->kind) {
    case CUPTI_ACTIVITY_KIND_KERNEL:
    case CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL: {
      const char* kindString = (record->kind == CUPTI_ACTIVITY_KIND_KERNEL)
                                   ? "KERNEL"
                                   : "CONC_KERNEL";
      CUpti_ActivityKernel5* kernel =
          reinterpret_cast<CUpti_ActivityKernel5*>(record);
      processKernelActivity(kernel, kindString);
      break;
    }
    case CUPTI_ACTIVITY_KIND_MEMCPY: {
      CUpti_ActivityMemcpy4* memcpy =
          reinterpret_cast<CUpti_ActivityMemcpy4*>(record);
      processMemcpyActivity(memcpy);
      break;
    }
    case CUPTI_ACTIVITY_KIND_MEMSET: {
      CUpti_ActivityMemset3* memset =
          reinterpret_cast<CUpti_ActivityMemset3*>(record);
      processMemsetActivity(memset);
      break;
    }
    case CUPTI_ACTIVITY_KIND_RUNTIME: {
      CUpti_ActivityAPI* api = reinterpret_cast<CUpti_ActivityAPI*>(record);
      processRuntimeActivity(api, "RUNTIME");
      break;
    }
    case CUPTI_ACTIVITY_KIND_DRIVER: {
      CUpti_ActivityAPI* api = reinterpret_cast<CUpti_ActivityAPI*>(record);
      processDriverActivity(api);
      break;
    }
    case CUPTI_ACTIVITY_KIND_CONTEXT: {
      CUpti_ActivityContext* context =
          reinterpret_cast<CUpti_ActivityContext*>(record);
      processContextActivity(context);
      break;
    }
    case CUPTI_ACTIVITY_KIND_DEVICE: {
      printf("Device\n");
      CUpti_ActivityDevice2* device =
          reinterpret_cast<CUpti_ActivityDevice2*>(record);
      processDeviceActivity(device);
      printf("processed device activity\n");
      break;
    }
    default:
      DFTRACER_LOG_DEBUG("Unknown CUPTI activity kind: %d", record->kind);
      break;
  }
}

void CUPTIFunction::processKernelActivity(CUpti_ActivityKernel5* kernel,
                                          const char* kindString) {
  TimeResolution start_time = transform_timestamp(kernel->start);
  TimeResolution end_time = transform_timestamp(kernel->end);
  TimeResolution duration = transform_time(kernel->end, kernel->start);

  // Create metadata for kernel activity
  auto metadata = new std::unordered_map<std::string, std::any>();
  metadata->insert_or_assign("device_id",
                             static_cast<uint32_t>(kernel->deviceId));
  metadata->insert_or_assign("context_id",
                             static_cast<uint32_t>(kernel->contextId));
  metadata->insert_or_assign("stream_id",
                             static_cast<uint32_t>(kernel->streamId));
  metadata->insert_or_assign("correlation_id",
                             static_cast<uint32_t>(kernel->correlationId));
  metadata->insert_or_assign("grid_x", static_cast<uint32_t>(kernel->gridX));
  metadata->insert_or_assign("grid_y", static_cast<uint32_t>(kernel->gridY));
  metadata->insert_or_assign("grid_z", static_cast<uint32_t>(kernel->gridZ));
  metadata->insert_or_assign("block_x", static_cast<uint32_t>(kernel->blockX));
  metadata->insert_or_assign("block_y", static_cast<uint32_t>(kernel->blockY));
  metadata->insert_or_assign("block_z", static_cast<uint32_t>(kernel->blockZ));
  metadata->insert_or_assign("static_shared_memory",
                             static_cast<uint32_t>(kernel->staticSharedMemory));
  metadata->insert_or_assign(
      "dynamic_shared_memory",
      static_cast<uint32_t>(kernel->dynamicSharedMemory));

  std::string kind_name = std::string("CUDA_KERNEL_") + kernel->name;

  logger->enter_event();
  logger->log(kernel->name, kind_name.c_str(), start_time, duration, metadata);
  logger->exit_event();
}

void CUPTIFunction::processMemcpyActivity(CUpti_ActivityMemcpy4* memcpy) {
  TimeResolution start_time = transform_timestamp(memcpy->start);
  TimeResolution end_time = transform_timestamp(memcpy->end);
  TimeResolution duration = transform_time(memcpy->end, memcpy->start);

  // Create metadata for memcpy activity
  auto metadata = new std::unordered_map<std::string, std::any>();
  metadata->insert_or_assign("device_id",
                             static_cast<uint32_t>(memcpy->deviceId));
  metadata->insert_or_assign("context_id",
                             static_cast<uint32_t>(memcpy->contextId));
  metadata->insert_or_assign("stream_id",
                             static_cast<uint32_t>(memcpy->streamId));
  metadata->insert_or_assign("correlation_id",
                             static_cast<uint32_t>(memcpy->correlationId));
  metadata->insert_or_assign("bytes", static_cast<uint64_t>(memcpy->bytes));
  metadata->insert_or_assign("copy_kind", static_cast<int>(memcpy->copyKind));

  std::string event_name =
      std::string("MEMCPY_") +
      getMemcpyKindString(
          static_cast<CUpti_ActivityMemcpyKind>(memcpy->copyKind));

  logger->enter_event();
  logger->log(event_name.c_str(), "CUDA_MEMCPY", start_time, duration,
              metadata);
  logger->exit_event();
}

void CUPTIFunction::processMemsetActivity(CUpti_ActivityMemset3* memset) {
  TimeResolution start_time = transform_timestamp(memset->start);
  TimeResolution end_time = transform_timestamp(memset->end);
  TimeResolution duration = transform_time(memset->end, memset->start);

  // Create metadata for memset activity
  auto metadata = new std::unordered_map<std::string, std::any>();
  metadata->insert_or_assign("device_id",
                             static_cast<uint32_t>(memset->deviceId));
  metadata->insert_or_assign("context_id",
                             static_cast<uint32_t>(memset->contextId));
  metadata->insert_or_assign("stream_id",
                             static_cast<uint32_t>(memset->streamId));
  metadata->insert_or_assign("correlation_id",
                             static_cast<uint32_t>(memset->correlationId));
  metadata->insert_or_assign("value", static_cast<uint32_t>(memset->value));

  logger->enter_event();
  logger->log("MEMSET", "CUDA_MEMSET", start_time, duration, metadata);
  logger->exit_event();
}

void CUPTIFunction::processRuntimeActivity(CUpti_ActivityAPI* api,
                                           const char* apiType) {
  TimeResolution start_time = transform_timestamp(api->start);
  TimeResolution end_time = transform_timestamp(api->end);
  TimeResolution duration = transform_time(api->end, api->start);

  // Create metadata for runtime API activity
  auto metadata = new std::unordered_map<std::string, std::any>();
  metadata->insert_or_assign("cbid", static_cast<uint32_t>(api->cbid));
  metadata->insert_or_assign("process_id",
                             static_cast<uint32_t>(api->processId));
  metadata->insert_or_assign("thread_id", static_cast<uint32_t>(api->threadId));
  metadata->insert_or_assign("correlation_id",
                             static_cast<uint32_t>(api->correlationId));

  std::string event_name =
      "CUDA_" + std::string(apiType) + "_" + std::to_string(api->cbid);

  logger->enter_event();
  logger->log(event_name.c_str(), apiType, start_time, duration, metadata);
  logger->exit_event();
}

void CUPTIFunction::processDriverActivity(CUpti_ActivityAPI* api) {
  processRuntimeActivity(api, "DRIVER");
}

void CUPTIFunction::processContextActivity(CUpti_ActivityContext* context) {
  // Create metadata for context activity
  auto metadata = new std::unordered_map<std::string, std::any>();
  metadata->insert_or_assign("context_id",
                             static_cast<uint32_t>(context->contextId));
  metadata->insert_or_assign("device_id",
                             static_cast<uint32_t>(context->deviceId));
  metadata->insert_or_assign("compute_api_kind",
                             static_cast<int>(context->computeApiKind));
  metadata->insert_or_assign("null_stream_id",
                             static_cast<uint32_t>(context->nullStreamId));

  std::string event_name =
      std::string("CONTEXT_") + std::to_string(context->contextId);
  std::string kind_name =
      std::string("CUDA_CONTEXT_") +
      getComputeApiKindString(
          static_cast<CUpti_ActivityComputeApiKind>(context->computeApiKind));

  logger->enter_event();
  logger->log(event_name.c_str(), kind_name.c_str(), logger->get_time(), 0,
              metadata);
  logger->exit_event();
}

void CUPTIFunction::processDeviceActivity(CUpti_ActivityDevice2* device) {
  printf("Processing device activity: %d\n", device->id);

  // Create metadata for device activity
  auto metadata = new std::unordered_map<std::string, std::any>();
  metadata->insert_or_assign("device_id", static_cast<uint32_t>(device->id));
  metadata->insert_or_assign(
      "compute_capability_major",
      static_cast<uint32_t>(device->computeCapabilityMajor));
  metadata->insert_or_assign(
      "compute_capability_minor",
      static_cast<uint32_t>(device->computeCapabilityMinor));
  metadata->insert_or_assign(
      "global_memory_bandwidth",
      static_cast<uint32_t>(device->globalMemoryBandwidth / 1024 / 1024));
  metadata->insert_or_assign(
      "global_memory_size",
      static_cast<uint32_t>(device->globalMemorySize / 1024 / 1024));
  metadata->insert_or_assign("num_multiprocessors",
                             static_cast<uint32_t>(device->numMultiprocessors));
  metadata->insert_or_assign(
      "core_clock_rate", static_cast<uint32_t>(device->coreClockRate / 1000));

  std::string event_name = std::string("DEVICE_") + std::string(device->name);
  std::string kind_name = std::string("CUDA_DEVICE");

  logger->enter_event();
  logger->log(event_name.c_str(), kind_name.c_str(), 0, 0, metadata);
  logger->exit_event();
}
}  // namespace dftracer

#endif  // DFTRACER_CUPTI_TRACING_ENABLE