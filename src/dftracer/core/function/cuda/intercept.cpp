#include <dftracer/core/function/cuda/intercept.h>

#ifdef DFTRACER_CUDA_TRACING_ENABLE

#include <cupti.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// CUPTI activity record structs are versioned: NVIDIA appends a new numbered
// typedef whenever the layout changes, and drops the older typedefs entirely a
// few releases later (CUDA 12.6 removed everything below the current version).
// So neither "always use the newest" nor "always use the oldest" compiles
// across the supported range -- we must select per toolkit version.
//
// DFTRACER_CUDA_VERSION is baked into dftracer_config.hpp by CMake from
// CUDAToolkit_VERSION.  Selecting on it (rather than CUPTI_API_VERSION) is
// required because CUDA 11.5 and 11.6 share CUPTI_API_VERSION 16 but ship
// different newest-kernel structs (Kernel6 vs Kernel7).
//
// Verified against the headers of CUDA 10.1, 10.2, 11.1-11.8, 12.2, 12.6,
// 12.9 and 13.1.
// ─────────────────────────────────────────────────────────────────────────────
#if DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION(13, 0, 0)
#define DFT_CUPTI_KERNEL_T CUpti_ActivityKernel11
#define DFT_CUPTI_MEMCPY_T CUpti_ActivityMemcpy6
#define DFT_CUPTI_MEMCPY_P2P_T CUpti_ActivityMemcpyPtoP4
#define DFT_CUPTI_MEMSET_T CUpti_ActivityMemset4
#define DFT_CUPTI_MEMORY_T CUpti_ActivityMemory4
#define DFT_CUPTI_SYNC_T CUpti_ActivitySynchronization2
#define DFT_CUPTI_OVERHEAD_T CUpti_ActivityOverhead3
#define DFT_CUPTI_UM_T CUpti_ActivityUnifiedMemoryCounter3
#define DFT_CUPTI_GRAPH_T CUpti_ActivityGraphTrace2
#elif DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION(12, 9, 0)
#define DFT_CUPTI_KERNEL_T CUpti_ActivityKernel9
#define DFT_CUPTI_MEMCPY_T CUpti_ActivityMemcpy6
#define DFT_CUPTI_MEMCPY_P2P_T CUpti_ActivityMemcpyPtoP4
#define DFT_CUPTI_MEMSET_T CUpti_ActivityMemset4
#define DFT_CUPTI_MEMORY_T CUpti_ActivityMemory4
#define DFT_CUPTI_SYNC_T CUpti_ActivitySynchronization2
#define DFT_CUPTI_OVERHEAD_T CUpti_ActivityOverhead3
#define DFT_CUPTI_UM_T CUpti_ActivityUnifiedMemoryCounter3
#define DFT_CUPTI_GRAPH_T CUpti_ActivityGraphTrace2
#elif DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION(12, 6, 0)
#define DFT_CUPTI_KERNEL_T CUpti_ActivityKernel9
#define DFT_CUPTI_MEMCPY_T CUpti_ActivityMemcpy5
#define DFT_CUPTI_MEMCPY_P2P_T CUpti_ActivityMemcpyPtoP4
#define DFT_CUPTI_MEMSET_T CUpti_ActivityMemset4
#define DFT_CUPTI_MEMORY_T CUpti_ActivityMemory4
#define DFT_CUPTI_SYNC_T CUpti_ActivitySynchronization
#define DFT_CUPTI_OVERHEAD_T CUpti_ActivityOverhead3
#define DFT_CUPTI_UM_T CUpti_ActivityUnifiedMemoryCounter2
#define DFT_CUPTI_GRAPH_T CUpti_ActivityGraphTrace2
#elif DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION(12, 0, 0)
#define DFT_CUPTI_KERNEL_T CUpti_ActivityKernel9
#define DFT_CUPTI_MEMCPY_T CUpti_ActivityMemcpy5
#define DFT_CUPTI_MEMCPY_P2P_T CUpti_ActivityMemcpyPtoP4
#define DFT_CUPTI_MEMSET_T CUpti_ActivityMemset4
#define DFT_CUPTI_MEMORY_T CUpti_ActivityMemory3
#define DFT_CUPTI_SYNC_T CUpti_ActivitySynchronization
#define DFT_CUPTI_OVERHEAD_T CUpti_ActivityOverhead
#define DFT_CUPTI_UM_T CUpti_ActivityUnifiedMemoryCounter2
#define DFT_CUPTI_GRAPH_T CUpti_ActivityGraphTrace
#elif DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION(11, 8, 0)
#define DFT_CUPTI_KERNEL_T CUpti_ActivityKernel8
#define DFT_CUPTI_MEMCPY_T CUpti_ActivityMemcpy5
#define DFT_CUPTI_MEMCPY_P2P_T CUpti_ActivityMemcpyPtoP4
#define DFT_CUPTI_MEMSET_T CUpti_ActivityMemset4
#define DFT_CUPTI_MEMORY_T CUpti_ActivityMemory3
#define DFT_CUPTI_SYNC_T CUpti_ActivitySynchronization
#define DFT_CUPTI_OVERHEAD_T CUpti_ActivityOverhead
#define DFT_CUPTI_UM_T CUpti_ActivityUnifiedMemoryCounter2
#define DFT_CUPTI_GRAPH_T CUpti_ActivityGraphTrace
#elif DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION(11, 6, 0)
#define DFT_CUPTI_KERNEL_T CUpti_ActivityKernel7
#define DFT_CUPTI_MEMCPY_T CUpti_ActivityMemcpy5
#define DFT_CUPTI_MEMCPY_P2P_T CUpti_ActivityMemcpyPtoP4
#define DFT_CUPTI_MEMSET_T CUpti_ActivityMemset4
#define DFT_CUPTI_MEMORY_T CUpti_ActivityMemory3
#define DFT_CUPTI_SYNC_T CUpti_ActivitySynchronization
#define DFT_CUPTI_OVERHEAD_T CUpti_ActivityOverhead
#define DFT_CUPTI_UM_T CUpti_ActivityUnifiedMemoryCounter2
#elif DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION(11, 2, 0)
#define DFT_CUPTI_KERNEL_T CUpti_ActivityKernel6
#define DFT_CUPTI_MEMCPY_T CUpti_ActivityMemcpy4
#define DFT_CUPTI_MEMCPY_P2P_T CUpti_ActivityMemcpyPtoP3
#define DFT_CUPTI_MEMSET_T CUpti_ActivityMemset3
#define DFT_CUPTI_MEMORY_T CUpti_ActivityMemory2
#define DFT_CUPTI_SYNC_T CUpti_ActivitySynchronization
#define DFT_CUPTI_OVERHEAD_T CUpti_ActivityOverhead
#define DFT_CUPTI_UM_T CUpti_ActivityUnifiedMemoryCounter2
#elif DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION(11, 0, 0)
#define DFT_CUPTI_KERNEL_T CUpti_ActivityKernel5
#define DFT_CUPTI_MEMCPY_T CUpti_ActivityMemcpy4
#define DFT_CUPTI_MEMCPY_P2P_T CUpti_ActivityMemcpyPtoP3
#define DFT_CUPTI_MEMSET_T CUpti_ActivityMemset3
#define DFT_CUPTI_SYNC_T CUpti_ActivitySynchronization
#define DFT_CUPTI_OVERHEAD_T CUpti_ActivityOverhead
#define DFT_CUPTI_UM_T CUpti_ActivityUnifiedMemoryCounter2
#else  // CUDA 10.x
#define DFT_CUPTI_KERNEL_T CUpti_ActivityKernel4
#define DFT_CUPTI_MEMCPY_T CUpti_ActivityMemcpy2
#define DFT_CUPTI_MEMSET_T CUpti_ActivityMemset
#define DFT_CUPTI_SYNC_T CUpti_ActivitySynchronization
#define DFT_CUPTI_OVERHEAD_T CUpti_ActivityOverhead
#define DFT_CUPTI_UM_T CUpti_ActivityUnifiedMemoryCounter2
#endif

// GraphTrace landed in 11.7, i.e. inside the 11.6 band above, so it gets its
// own gate rather than a band of its own.
#if !defined(DFT_CUPTI_GRAPH_T) && \
    DFTRACER_CUDA_VERSION >= DFTRACER_GET_VERSION(11, 7, 0)
#define DFT_CUPTI_GRAPH_T CUpti_ActivityGraphTrace
#endif

// CUPTI activity buffers: 8 MiB is NVIDIA's own sample default and keeps the
// number of buffer_completed round-trips low for kernel-heavy workloads.
#define DFT_CUPTI_BUF_SIZE (8 * 1024 * 1024)
#define DFT_CUPTI_ALIGN_SIZE 8
#define DFT_CUPTI_ALIGN_BUFFER(buffer, align)                         \
  (((uintptr_t)(buffer) & ((align) - 1))                              \
       ? ((buffer) + (align) - ((uintptr_t)(buffer) & ((align) - 1))) \
       : (buffer))

// Truncate long kernel/marker names, matching the HIP backend's behaviour so
// both vendors produce comparably sized event names.
#define DFT_CUPTI_MAX_EVENT_NAME_LENGTH 64

template <>
std::shared_ptr<dftracer::CUDAFunction>
    dftracer::Singleton<dftracer::CUDAFunction>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::CUDAFunction>::stop_creating_instances =
    false;

namespace dftracer {

// ── Time base alignment ─────────────────────────────────────────────────────
//
// CUPTI activity timestamps are nanoseconds on CUPTI's own monotonic clock,
// whereas DFTLogger::get_time() is gettimeofday() rendered in the configured
// time_metric unit.  Scale CUPTI ns into that unit, then add a fixed offset
// sampled once by reading both clocks back to back.  This is exactly what the
// HIP/rocprofiler backend does, so CUDA, HIP and CPU-side events all share one
// timeline.
static double cupti_ns_to_time_metric_factor() {
  auto config =
      dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
  return time_metric_units_per_second(config->time_metric) / 1e9;
}

TimeResolution CUDAFunction::transform_time(uint64_t end_time,
                                            uint64_t start_time) {
  double factor = cupti_ns_to_time_metric_factor();
  return std::floor(end_time * factor) - std::floor(start_time * factor);
}

TimeResolution CUDAFunction::transform_timestamp(uint64_t timestamp) {
  double factor = cupti_ns_to_time_metric_factor();
  if (time_diff == 0) {
    uint64_t cupti_now = 0;
    // Deliberately sampled lazily: cuptiGetTimestamp() only returns a usable
    // value once CUPTI is initialized.  If it is not yet, leave time_diff
    // unresolved and retry on the next record.
    if (cuptiGetTimestamp(&cupti_now) == CUPTI_SUCCESS && cupti_now != 0) {
      time_diff = logger->get_time() - std::floor(cupti_now * factor);
    }
  }
  return std::floor(timestamp * factor) + time_diff;
}

// ── Small enum → string helpers, so event names read like the CUDA API ──────
static const char *memcpy_kind_name(uint8_t kind) {
  switch (kind) {
    case CUPTI_ACTIVITY_MEMCPY_KIND_HTOD:
      return "MemcpyHtoD";
    case CUPTI_ACTIVITY_MEMCPY_KIND_DTOH:
      return "MemcpyDtoH";
    case CUPTI_ACTIVITY_MEMCPY_KIND_HTOA:
      return "MemcpyHtoA";
    case CUPTI_ACTIVITY_MEMCPY_KIND_ATOH:
      return "MemcpyAtoH";
    case CUPTI_ACTIVITY_MEMCPY_KIND_ATOA:
      return "MemcpyAtoA";
    case CUPTI_ACTIVITY_MEMCPY_KIND_ATOD:
      return "MemcpyAtoD";
    case CUPTI_ACTIVITY_MEMCPY_KIND_DTOA:
      return "MemcpyDtoA";
    case CUPTI_ACTIVITY_MEMCPY_KIND_DTOD:
      return "MemcpyDtoD";
    case CUPTI_ACTIVITY_MEMCPY_KIND_HTOH:
      return "MemcpyHtoH";
    case CUPTI_ACTIVITY_MEMCPY_KIND_PTOP:
      return "MemcpyPtoP";
    default:
      return "MemcpyUnknown";
  }
}

static const char *sync_type_name(uint8_t type) {
  switch (type) {
    case CUPTI_ACTIVITY_SYNCHRONIZATION_TYPE_EVENT_SYNCHRONIZE:
      return "EventSynchronize";
    case CUPTI_ACTIVITY_SYNCHRONIZATION_TYPE_STREAM_WAIT_EVENT:
      return "StreamWaitEvent";
    case CUPTI_ACTIVITY_SYNCHRONIZATION_TYPE_STREAM_SYNCHRONIZE:
      return "StreamSynchronize";
    case CUPTI_ACTIVITY_SYNCHRONIZATION_TYPE_CONTEXT_SYNCHRONIZE:
      return "ContextSynchronize";
    default:
      return "SynchronizeUnknown";
  }
}

static const char *overhead_kind_name(CUpti_ActivityOverheadKind kind) {
  switch (kind) {
    case CUPTI_ACTIVITY_OVERHEAD_DRIVER_COMPILER:
      return "OverheadDriverCompiler";
    case CUPTI_ACTIVITY_OVERHEAD_CUPTI_BUFFER_FLUSH:
      return "OverheadBufferFlush";
    case CUPTI_ACTIVITY_OVERHEAD_CUPTI_INSTRUMENTATION:
      return "OverheadInstrumentation";
    case CUPTI_ACTIVITY_OVERHEAD_CUPTI_RESOURCE:
      return "OverheadResource";
    default:
      return "OverheadUnknown";
  }
}

#ifdef DFT_CUPTI_MEMORY_T
static const char *memory_operation_name(uint8_t op) {
  switch (op) {
    case CUPTI_ACTIVITY_MEMORY_OPERATION_TYPE_ALLOCATION:
      return "MemoryAllocation";
    case CUPTI_ACTIVITY_MEMORY_OPERATION_TYPE_RELEASE:
      return "MemoryRelease";
    default:
      return "MemoryOperationUnknown";
  }
}
#endif

// Resolve a runtime/driver API callback id to its function name (e.g.
// "cudaMemcpy_v3020").  Falls back to the numeric id when CUPTI cannot name it.
static std::string api_callback_name(CUpti_CallbackDomain domain,
                                     uint32_t cbid) {
  const char *name = nullptr;
  if (cuptiGetCallbackName(domain, cbid, &name) == CUPTI_SUCCESS &&
      name != nullptr) {
    return std::string(name);
  }
  return std::string("cbid_") + std::to_string(cbid);
}

static std::string truncate_name(const char *name, const char *fallback) {
  if (name == nullptr || name[0] == '\0') return std::string(fallback);
  std::string result(name);
  if (result.length() > DFT_CUPTI_MAX_EVENT_NAME_LENGTH)
    result = result.substr(0, DFT_CUPTI_MAX_EVENT_NAME_LENGTH);
  return result;
}

// ── Record translation ──────────────────────────────────────────────────────
void CUDAFunction::process_record(CUpti_Activity *record) {
  auto function = dftracer::Singleton<dftracer::CUDAFunction>::get_instance();
  if (function == nullptr || function->logger == nullptr) return;

  switch (record->kind) {
    // ── CUDA runtime / driver API calls ─────────────────────────────────────
    case CUPTI_ACTIVITY_KIND_RUNTIME:
    case CUPTI_ACTIVITY_KIND_DRIVER: {
      auto *api = reinterpret_cast<CUpti_ActivityAPI *>(record);
      bool is_runtime = (record->kind == CUPTI_ACTIVITY_KIND_RUNTIME);
      CUpti_CallbackDomain domain =
          is_runtime ? CUPTI_CB_DOMAIN_RUNTIME_API : CUPTI_CB_DOMAIN_DRIVER_API;

      auto metadata = new Metadata();
      metadata->insert_or_assign("cbid", (unsigned int)api->cbid);
      metadata->insert_or_assign("correlation_id",
                                 (unsigned int)api->correlationId);
      metadata->insert_or_assign("return_value",
                                 (unsigned int)api->returnValue);
      metadata->insert_or_assign("pid", (unsigned int)api->processId);
      metadata->insert_or_assign("tid", (ThreadID)api->threadId);

      std::string event_name = api_callback_name(domain, api->cbid);
      function->logger->enter_event();
      function->logger->log(event_name.c_str(),
                            is_runtime ? "CUDA_RUNTIME_API" : "CUDA_DRIVER_API",
                            TraceEventType::TRACE_TYPE_CUDA,
                            function->transform_timestamp(api->start),
                            function->transform_time(api->end, api->start),
                            metadata);
      function->logger->exit_event();
      break;
    }

    // ── Kernel launches ─────────────────────────────────────────────────────
    case CUPTI_ACTIVITY_KIND_KERNEL:
    case CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL: {
      auto *kernel = reinterpret_cast<DFT_CUPTI_KERNEL_T *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("device_id", (unsigned int)kernel->deviceId);
      metadata->insert_or_assign("context_id", (unsigned int)kernel->contextId);
      metadata->insert_or_assign("stream_id", (unsigned int)kernel->streamId);
      metadata->insert_or_assign("correlation_id",
                                 (unsigned int)kernel->correlationId);
      metadata->insert_or_assign("grid_id", (long)kernel->gridId);
      metadata->insert_or_assign("grid_x", (int)kernel->gridX);
      metadata->insert_or_assign("grid_y", (int)kernel->gridY);
      metadata->insert_or_assign("grid_z", (int)kernel->gridZ);
      metadata->insert_or_assign("block_x", (int)kernel->blockX);
      metadata->insert_or_assign("block_y", (int)kernel->blockY);
      metadata->insert_or_assign("block_z", (int)kernel->blockZ);
      metadata->insert_or_assign("static_shared_memory",
                                 (int)kernel->staticSharedMemory);
      metadata->insert_or_assign("dynamic_shared_memory",
                                 (int)kernel->dynamicSharedMemory);
      metadata->insert_or_assign("registers_per_thread",
                                 (unsigned int)kernel->registersPerThread);
      metadata->insert_or_assign("local_memory_per_thread",
                                 (unsigned int)kernel->localMemoryPerThread);

      std::string event_name = truncate_name(kernel->name, "kernel");
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), "CUDA_KERNEL", TraceEventType::TRACE_TYPE_CUDA,
          function->transform_timestamp(kernel->start),
          function->transform_time(kernel->end, kernel->start), metadata);
      function->logger->exit_event();
      break;
    }

    // ── Memory copies ───────────────────────────────────────────────────────
    case CUPTI_ACTIVITY_KIND_MEMCPY: {
      auto *memcpy_rec = reinterpret_cast<DFT_CUPTI_MEMCPY_T *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("device_id",
                                 (unsigned int)memcpy_rec->deviceId);
      metadata->insert_or_assign("context_id",
                                 (unsigned int)memcpy_rec->contextId);
      metadata->insert_or_assign("stream_id",
                                 (unsigned int)memcpy_rec->streamId);
      metadata->insert_or_assign("correlation_id",
                                 (unsigned int)memcpy_rec->correlationId);
      metadata->insert_or_assign("bytes", (unsigned long)memcpy_rec->bytes);
      metadata->insert_or_assign("copy_kind",
                                 (unsigned int)memcpy_rec->copyKind);
      metadata->insert_or_assign("src_kind", (unsigned int)memcpy_rec->srcKind);
      metadata->insert_or_assign("dst_kind", (unsigned int)memcpy_rec->dstKind);

      function->logger->enter_event();
      function->logger->log(
          memcpy_kind_name(memcpy_rec->copyKind), "CUDA_MEMCPY",
          TraceEventType::TRACE_TYPE_CUDA,
          function->transform_timestamp(memcpy_rec->start),
          function->transform_time(memcpy_rec->end, memcpy_rec->start),
          metadata);
      function->logger->exit_event();
      break;
    }

#ifdef DFT_CUPTI_MEMCPY_P2P_T
    // ── Peer-to-peer memory copies ──────────────────────────────────────────
    case CUPTI_ACTIVITY_KIND_MEMCPY2: {
      auto *p2p = reinterpret_cast<DFT_CUPTI_MEMCPY_P2P_T *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("device_id", (unsigned int)p2p->deviceId);
      metadata->insert_or_assign("context_id", (unsigned int)p2p->contextId);
      metadata->insert_or_assign("stream_id", (unsigned int)p2p->streamId);
      metadata->insert_or_assign("correlation_id",
                                 (unsigned int)p2p->correlationId);
      metadata->insert_or_assign("bytes", (unsigned long)p2p->bytes);
      metadata->insert_or_assign("copy_kind", (unsigned int)p2p->copyKind);
      metadata->insert_or_assign("src_device_id",
                                 (unsigned int)p2p->srcDeviceId);
      metadata->insert_or_assign("dst_device_id",
                                 (unsigned int)p2p->dstDeviceId);
      metadata->insert_or_assign("src_context_id",
                                 (unsigned int)p2p->srcContextId);
      metadata->insert_or_assign("dst_context_id",
                                 (unsigned int)p2p->dstContextId);

      function->logger->enter_event();
      function->logger->log(
          "MemcpyPtoP", "CUDA_MEMCPY_P2P", TraceEventType::TRACE_TYPE_CUDA,
          function->transform_timestamp(p2p->start),
          function->transform_time(p2p->end, p2p->start), metadata);
      function->logger->exit_event();
      break;
    }
#endif

    // ── Memsets ─────────────────────────────────────────────────────────────
    case CUPTI_ACTIVITY_KIND_MEMSET: {
      auto *memset_rec = reinterpret_cast<DFT_CUPTI_MEMSET_T *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("device_id",
                                 (unsigned int)memset_rec->deviceId);
      metadata->insert_or_assign("context_id",
                                 (unsigned int)memset_rec->contextId);
      metadata->insert_or_assign("stream_id",
                                 (unsigned int)memset_rec->streamId);
      metadata->insert_or_assign("correlation_id",
                                 (unsigned int)memset_rec->correlationId);
      metadata->insert_or_assign("bytes", (unsigned long)memset_rec->bytes);
      metadata->insert_or_assign("value", (unsigned int)memset_rec->value);

      function->logger->enter_event();
      function->logger->log(
          "Memset", "CUDA_MEMSET", TraceEventType::TRACE_TYPE_CUDA,
          function->transform_timestamp(memset_rec->start),
          function->transform_time(memset_rec->end, memset_rec->start),
          metadata);
      function->logger->exit_event();
      break;
    }

#ifdef DFT_CUPTI_MEMORY_T
    // ── Allocations and frees.  These carry a single timestamp, not a span. ──
    case CUPTI_ACTIVITY_KIND_MEMORY2: {
      auto *mem = reinterpret_cast<DFT_CUPTI_MEMORY_T *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("device_id", (unsigned int)mem->deviceId);
      metadata->insert_or_assign("context_id", (unsigned int)mem->contextId);
      metadata->insert_or_assign("stream_id", (unsigned int)mem->streamId);
      metadata->insert_or_assign("correlation_id",
                                 (unsigned int)mem->correlationId);
      metadata->insert_or_assign("bytes", (unsigned long)mem->bytes);
      metadata->insert_or_assign("address", (unsigned long)mem->address);
      metadata->insert_or_assign("memory_kind", (unsigned int)mem->memoryKind);
      metadata->insert_or_assign("pid", (unsigned int)mem->processId);

      function->logger->enter_event();
      function->logger->log(memory_operation_name(mem->memoryOperationType),
                            "CUDA_MEMORY", TraceEventType::TRACE_TYPE_CUDA,
                            function->transform_timestamp(mem->timestamp), 0,
                            metadata);
      function->logger->exit_event();
      break;
    }
#endif

    // ── Stream / event / context synchronization ────────────────────────────
    case CUPTI_ACTIVITY_KIND_SYNCHRONIZATION: {
      auto *sync = reinterpret_cast<DFT_CUPTI_SYNC_T *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("context_id", (unsigned int)sync->contextId);
      metadata->insert_or_assign("stream_id", (unsigned int)sync->streamId);
      metadata->insert_or_assign("correlation_id",
                                 (unsigned int)sync->correlationId);
      metadata->insert_or_assign("cuda_event_id",
                                 (unsigned int)sync->cudaEventId);

      function->logger->enter_event();
      function->logger->log(sync_type_name(sync->type), "CUDA_SYNC",
                            TraceEventType::TRACE_TYPE_CUDA,
                            function->transform_timestamp(sync->start),
                            function->transform_time(sync->end, sync->start),
                            metadata);
      function->logger->exit_event();
      break;
    }

    // ── CUPTI's own tracing overhead ────────────────────────────────────────
    case CUPTI_ACTIVITY_KIND_OVERHEAD: {
      auto *overhead = reinterpret_cast<DFT_CUPTI_OVERHEAD_T *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("object_kind",
                                 (unsigned int)overhead->objectKind);
      metadata->insert_or_assign("overhead_kind",
                                 (unsigned int)overhead->overheadKind);

      function->logger->enter_event();
      function->logger->log(
          overhead_kind_name(overhead->overheadKind), "CUDA_OVERHEAD",
          TraceEventType::TRACE_TYPE_CUDA,
          function->transform_timestamp(overhead->start),
          function->transform_time(overhead->end, overhead->start), metadata);
      function->logger->exit_event();
      break;
    }

    // ── Unified memory page faults / migrations / thrashing ─────────────────
    case CUPTI_ACTIVITY_KIND_UNIFIED_MEMORY_COUNTER: {
      auto *um = reinterpret_cast<DFT_CUPTI_UM_T *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("counter_kind", (unsigned int)um->counterKind);
      metadata->insert_or_assign("value", (unsigned long)um->value);
      metadata->insert_or_assign("address", (unsigned long)um->address);
      metadata->insert_or_assign("src_id", (unsigned int)um->srcId);
      metadata->insert_or_assign("dst_id", (unsigned int)um->dstId);
      metadata->insert_or_assign("stream_id", (unsigned int)um->streamId);
      metadata->insert_or_assign("pid", (unsigned int)um->processId);

      // Some unified-memory counters report a point in time rather than a
      // span; CUPTI then sets end == start, which yields a zero duration.
      function->logger->enter_event();
      function->logger->log("UnifiedMemoryCounter", "CUDA_UNIFIED_MEMORY",
                            TraceEventType::TRACE_TYPE_CUDA,
                            function->transform_timestamp(um->start),
                            function->transform_time(um->end, um->start),
                            metadata);
      function->logger->exit_event();
      break;
    }

#ifdef DFT_CUPTI_GRAPH_T
    // ── CUDA graph execution ────────────────────────────────────────────────
    case CUPTI_ACTIVITY_KIND_GRAPH_TRACE: {
      auto *graph = reinterpret_cast<DFT_CUPTI_GRAPH_T *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("graph_id", (unsigned int)graph->graphId);
      metadata->insert_or_assign("device_id", (unsigned int)graph->deviceId);
      metadata->insert_or_assign("context_id", (unsigned int)graph->contextId);
      metadata->insert_or_assign("stream_id", (unsigned int)graph->streamId);
      metadata->insert_or_assign("correlation_id",
                                 (unsigned int)graph->correlationId);

      function->logger->enter_event();
      function->logger->log(
          "GraphTrace", "CUDA_GRAPH", TraceEventType::TRACE_TYPE_CUDA,
          function->transform_timestamp(graph->start),
          function->transform_time(graph->end, graph->start), metadata);
      function->logger->exit_event();
      break;
    }
#endif

    // ── NVTX markers ────────────────────────────────────────────────────────
    case CUPTI_ACTIVITY_KIND_MARKER: {
      auto *marker = reinterpret_cast<CUpti_ActivityMarker2 *>(record);

      auto metadata = new Metadata();
      metadata->insert_or_assign("marker_id", (unsigned int)marker->id);
      metadata->insert_or_assign("flags", (unsigned int)marker->flags);
      metadata->insert_or_assign("object_kind",
                                 (unsigned int)marker->objectKind);
      if (marker->domain != nullptr)
        metadata->insert_or_assign("domain", std::string(marker->domain));

      std::string event_name = truncate_name(marker->name, "marker");
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), "CUDA_MARKER", TraceEventType::TRACE_TYPE_CUDA,
          function->transform_timestamp(marker->timestamp), 0, metadata);
      function->logger->exit_event();
      break;
    }

    default:
      // Kinds we register only for correlation context (DEVICE, CONTEXT,
      // STREAM, NAME, EXTERNAL_CORRELATION) carry no timespan of their own.
      break;
  }
}

// ── CUPTI buffer lifecycle ──────────────────────────────────────────────────
void CUPTIAPI CUDAFunction::buffer_requested(uint8_t **buffer, size_t *size,
                                             size_t *max_num_records) {
  uint8_t *raw = (uint8_t *)malloc(DFT_CUPTI_BUF_SIZE + DFT_CUPTI_ALIGN_SIZE);
  if (raw == nullptr) {
    DFTRACER_LOG_ERROR("CUPTI buffer allocation of %d bytes failed",
                       DFT_CUPTI_BUF_SIZE);
    *buffer = nullptr;
    *size = 0;
    *max_num_records = 0;
    return;
  }
  *buffer = DFT_CUPTI_ALIGN_BUFFER(raw, DFT_CUPTI_ALIGN_SIZE);
  *size = DFT_CUPTI_BUF_SIZE;
  // 0 means "fill the buffer completely" rather than capping record count.
  *max_num_records = 0;
}

void CUPTIAPI CUDAFunction::buffer_completed(CUcontext ctx, uint32_t stream_id,
                                             uint8_t *buffer, size_t size,
                                             size_t valid_size) {
  (void)ctx;
  (void)stream_id;
  (void)size;

  if (valid_size > 0) {
    CUpti_Activity *record = nullptr;
    CUptiResult status;
    do {
      status = cuptiActivityGetNextRecord(buffer, valid_size, &record);
      if (status == CUPTI_SUCCESS) {
        process_record(record);
      } else if (status == CUPTI_ERROR_MAX_LIMIT_REACHED) {
        break;
      } else {
        DFTRACER_LOG_ERROR("cuptiActivityGetNextRecord failed: status %d",
                           status);
        break;
      }
    } while (true);

    size_t dropped = 0;
    if (cuptiActivityGetNumDroppedRecords(ctx, stream_id, &dropped) ==
            CUPTI_SUCCESS &&
        dropped > 0) {
      DFTRACER_LOG_WARN("CUPTI dropped %zu activity records", dropped);
    }
  }

  free(buffer);
}

// ── Lifecycle ───────────────────────────────────────────────────────────────
static void enable_activity_kind(CUpti_ActivityKind kind, const char *name) {
  CUptiResult status = cuptiActivityEnable(kind);
  if (status != CUPTI_SUCCESS) {
    // Not fatal: some kinds are unsupported on older drivers or on the
    // particular device, and CUPTI reports that per-kind.
    DFTRACER_LOG_DEBUG("CUPTI activity kind %s not enabled: status %d", name,
                       status);
  }
}

#define DFT_CUPTI_ENABLE(kind) enable_activity_kind(kind, #kind)

void CUDAFunction::initialize() {
  DFTRACER_LOG_DEBUG("Initializing CUDAFunction instance");
  if (started.exchange(true)) return;

  CUptiResult status = cuptiActivityRegisterCallbacks(
      CUDAFunction::buffer_requested, CUDAFunction::buffer_completed);
  if (status != CUPTI_SUCCESS) {
    DFTRACER_LOG_ERROR("cuptiActivityRegisterCallbacks failed: status %d",
                       status);
    started.store(false);
    return;
  }

  // API-level events.
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_RUNTIME);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_DRIVER);
  // GPU-side work.
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_MEMCPY);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_MEMCPY2);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_MEMSET);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_SYNCHRONIZATION);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_OVERHEAD);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_MARKER);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_UNIFIED_MEMORY_COUNTER);
  // Correlation context: not emitted as events, but keeps CUPTI's internal
  // name/id tables populated for the records that are.
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_NAME);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_DEVICE);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_CONTEXT);
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_EXTERNAL_CORRELATION);
#ifdef DFT_CUPTI_MEMORY_T
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_MEMORY2);
#endif
#ifdef DFT_CUPTI_GRAPH_T
  DFT_CUPTI_ENABLE(CUPTI_ACTIVITY_KIND_GRAPH_TRACE);
#endif

  DFTRACER_LOG_INFO("CUDA (CUPTI) tracing enabled", "");
}

void CUDAFunction::finalize() {
  DFTRACER_LOG_DEBUG("Finalizing CUDAFunction instance");
  if (!started.exchange(false)) return;

  // Force-flush so in-flight buffers are drained through buffer_completed()
  // while the logger is still alive.
  CUptiResult status = cuptiActivityFlushAll(1);
  if (status != CUPTI_SUCCESS) {
    DFTRACER_LOG_DEBUG("cuptiActivityFlushAll failed: status %d", status);
  }
}

}  // namespace dftracer

#endif  // DFTRACER_CUDA_TRACING_ENABLE
