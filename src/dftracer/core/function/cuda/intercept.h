// CUPTI-based NVIDIA GPU tracing for DFTracer.
//
// Mirrors src/dftracer/core/function/hip/intercept.h (rocprofiler-sdk) so both
// GPU vendors surface the same shape of events into the same trace.

#ifndef DFTRACER_CUDA_INTERCEPT_H
#define DFTRACER_CUDA_INTERCEPT_H

#ifdef DFTRACER_DEBUG
#include <dftracer/core/dftracer_config_dbg.hpp>
#else
#include <dftracer/core/dftracer_config.hpp>
#endif

#ifdef DFTRACER_CUDA_TRACING_ENABLE

#include <cupti.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/function/generic_function.h>

#include <atomic>

namespace dftracer {

// Traces NVIDIA GPU activity via the CUPTI Activity API: CUDA runtime and
// driver API calls, kernel launches, memory copies (incl. peer-to-peer),
// memsets, allocations/frees, stream/device synchronization, unified-memory
// counters, CUDA graph traces, NVTX markers and CUPTI's own overhead.
//
// Records are collected into CUPTI-owned buffers and handed back on a CUPTI
// worker thread; each record is translated into a DFTracer duration event on
// the shared DFTLogger.
class CUDAFunction : public dftracer::GenericFunction {
 private:
  // Offset added to CUPTI timestamps to land them on the same clock and unit as
  // DFTLogger::get_time(). Resolved once, lazily, on first use.
  //
  // SIGNED: CUPTI's clock shares gettimeofday()'s epoch on this platform, so
  // the offset is ~0 and may legitimately be negative. As an unsigned type it
  // wrapped to ~2^64 and zeroed every subsequent timestamp.
  int64_t time_diff;
  // Explicit resolution flag: 0 is a VALID offset here, so it cannot double as
  // the "not yet resolved" sentinel the way it used to.
  bool time_diff_resolved;
  std::atomic<bool> started;

 public:
  CUDAFunction() : dftracer::GenericFunction() {
    DFTRACER_LOG_DEBUG("Creating CUDAFunction instance", "");
    time_diff = 0;
    time_diff_resolved = false;
    started.store(false);
  }

  TimeResolution transform_timestamp(uint64_t timestamp);
  TimeResolution transform_time(uint64_t end_time, uint64_t start_time);

  // CUPTI activity buffer lifecycle callbacks.
  static void CUPTIAPI buffer_requested(uint8_t **buffer, size_t *size,
                                        size_t *max_num_records);
  static void CUPTIAPI buffer_completed(CUcontext ctx, uint32_t stream_id,
                                        uint8_t *buffer, size_t size,
                                        size_t valid_size);

  // Translates a single CUPTI activity record into a DFTracer event.
  static void process_record(CUpti_Activity *record);

  void initialize() override;
  void finalize() override;
};

}  // namespace dftracer

#endif  // DFTRACER_CUDA_TRACING_ENABLE
#endif  // DFTRACER_CUDA_INTERCEPT_H
