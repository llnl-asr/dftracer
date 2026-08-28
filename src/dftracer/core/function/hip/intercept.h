// Created by druva on 6/9/25

#ifndef DFTRACER_HIP_INTERCEPT_H
#define DFTRACER_HIP_INTERCEPT_H

#ifdef DFTRACER_DEBUG
#include <atomic>
#include <dftracer/core/dftracer_config_dbg.hpp>
#else
#include <dftracer/core/dftracer_config.hpp>
#endif
#ifdef DFTRACER_HIP_TRACING_ENABLE

#include <dftracer/core/common/logging.h>
#include <dftracer/core/function/generic_function.h>
#include <rocprofiler-sdk/buffer.h>
#include <rocprofiler-sdk/buffer_tracing.h>
#include <rocprofiler-sdk/registration.h>
#include <rocprofiler-sdk/rocprofiler.h>

#include <rocprofiler-sdk/cxx/name_info.hpp>

namespace conf {

extern "C" rocprofiler_tool_configure_result_t* roc_conf(
    uint32_t version, const char* runtime_version, uint32_t priority,
    rocprofiler_client_id_t* id);
}

namespace dftracer {

using kernel_symbol_data_t =
    rocprofiler_callback_tracing_code_object_kernel_symbol_register_data_t;

// Used to trace AMD GPU APIS - HIP, HSA, RCCL, and the memory APIS - counters
// not implemented
class HIPFunction : public dftracer::GenericFunction {
 private:
  rocprofiler::sdk::buffer_name_info client_name_info;
  // Zero-initialised on purpose. rocprofiler defers tool_init() (which is what
  // actually creates these) until the HSA runtime is loaded, so both ids are
  // read before they are assigned whenever the application has not touched the
  // GPU yet. Leaving them indeterminate meant initialize() handed a garbage
  // handle to rocprofiler_start_context().
  rocprofiler_buffer_id_t client_buffer = {0};
  rocprofiler_context_id_t client_ctx = {0};
  // Guards against the double teardown described in finalize().
  // ATOMIC: read from rocprofiler's own PTL thread-pool threads (which run the
  // buffer-flush tracing callback), written by the thread running finalize().
  // A plain bool here is a data race and, worse, lets a late callback sail past
  // the guard below into a destroyed logger.
  std::atomic<bool> finalized{false};
  std::unordered_map<rocprofiler_kernel_id_t, kernel_symbol_data_t>
      client_kernels;

  TimeResolution time_diff;
  TimeResolution transform_timestamp(rocprofiler_timestamp_t timestamp);
  TimeResolution transform_time(rocprofiler_timestamp_t end_time,
                                rocprofiler_timestamp_t start_time);

 public:
  HIPFunction() : dftracer::GenericFunction() {
    DFTRACER_LOG_DEBUG("Creating HIPFunction instance",
                       "");  // Initialize parent
    time_diff = 0;
  }

  static void tool_code_object_callback(
      rocprofiler_callback_tracing_record_t record,
      rocprofiler_user_data_t* user_data, void* callback_data);
  static void tool_tracing_callback(rocprofiler_context_id_t context,
                                    rocprofiler_buffer_id_t buffer_id,
                                    rocprofiler_record_header_t** headers,
                                    size_t num_headers, void* user_data,
                                    uint64_t drop_count);

  static void thread_precreate(rocprofiler_runtime_library_t lib,
                               void* tool_data);

  static void thread_postcreate(rocprofiler_runtime_library_t lib,
                                void* tool_data);

  static int tool_init(rocprofiler_client_finalize_t fini_func,
                       void* tool_data);
  static void tool_fini(void* tool_data);

  void initialize() override {
    DFTRACER_LOG_DEBUG("Initializing HIPFunction instance");

    // Late-registration fallback. The PRIMARY path is the global
    // `rocprofiler_configure` symbol exported from intercept.cpp, which
    // rocprofiler finds and calls itself at the right moment. This
    // force_configure only matters for the case where rocprofiler had not yet
    // scanned for clients.
    //
    // CONFIGURATION_LOCKED here is normal, not a failure: on Cray PE + ROCm the
    // load-time constructors of librocprofiler-register / libamdhip64 / the
    // MPICH GTL bring rocprofiler up before main() runs, so configuration is
    // always already locked by the time any application code executes. It is
    // only a real problem if we ALSO never got registered via
    // rocprofiler_configure, which shows up as tool_init never running.
    rocprofiler_status_t cfg_status =
        rocprofiler_force_configure(&conf::roc_conf);
    if (cfg_status != ROCPROFILER_STATUS_SUCCESS &&
        cfg_status != ROCPROFILER_STATUS_ERROR_CONFIGURATION_LOCKED) {
      DFTRACER_LOG_ERROR(
          "HIP Intercept rocprofiler_force_configure failed: status %d (%s)\n",
          cfg_status, rocprofiler_get_status_name(cfg_status));
    } else if (cfg_status == ROCPROFILER_STATUS_ERROR_CONFIGURATION_LOCKED) {
      DFTRACER_LOG_DEBUG(
          "HIP Intercept: rocprofiler configuration already locked (expected); "
          "relying on the exported rocprofiler_configure registration");
    }

    // Do NOT unconditionally start the context here. rocprofiler calls
    // tool_init() -- which creates the context AND starts it -- lazily, when
    // the HSA runtime comes up. When the application has not touched the GPU
    // yet (e.g. DFTRACER_CPP_INIT at the top of main, before MFEM configures
    // its device) tool_init has not run at this point, client_ctx is still 0,
    // and starting it fails with ROCPROFILER_STATUS_ERROR_CONTEXT_NOT_FOUND
    // (2). That is the expected, healthy ordering, so treating it as an error
    // produced a scary log line on every correct run while the real tracing
    // was set up later by tool_init.
    if (client_ctx.handle == 0) {
      DFTRACER_LOG_DEBUG(
          "HIP Intercept: context not created yet; rocprofiler will start it "
          "from tool_init once the HSA runtime initialises");
      return;
    }

    // If tool_init already ran it also already started the context; starting a
    // second time is redundant.
    int is_active = 0;
    if (rocprofiler_context_is_active(client_ctx, &is_active) ==
            ROCPROFILER_STATUS_SUCCESS &&
        is_active != 0) {
      DFTRACER_LOG_DEBUG("HIP Intercept context already active");
      return;
    }

    rocprofiler_status_t status = rocprofiler_start_context(client_ctx);
    if (status != ROCPROFILER_STATUS_SUCCESS) {
      DFTRACER_LOG_ERROR(
          "HIP Intercept context start failed in initialize(): status %d "
          "(%s)\n",
          status, rocprofiler_get_status_name(status));
    }
  }

  void finalize() override {
    DFTRACER_LOG_DEBUG("Finalizing HIPFunction instance");

    // Only ever tear down once. Now that dftracer registers through the
    // exported rocprofiler_configure symbol, rocprofiler owns the tool lifetime
    // and calls tool_fini() during its own atexit shutdown -- while the
    // application ALSO reaches here via DFTRACER_*_FINI. Running the teardown
    // twice double-frees rocprofiler-side state and segfaults at exit, which
    // truncates the gzip trace stream mid-write (measured: 11 of 16 ranks left
    // unreadable "Compressed file ended before the end-of-stream marker").
    if (finalized) {
      DFTRACER_LOG_DEBUG("HIP Intercept already finalized; skipping");
      return;
    }
    finalized = true;

    // If rocprofiler has already finalized, its context/buffer objects are gone
    // and touching them is a use-after-free.
    int roc_finalized = 0;
    if (rocprofiler_is_finalized(&roc_finalized) ==
            ROCPROFILER_STATUS_SUCCESS &&
        roc_finalized != 0) {
      DFTRACER_LOG_DEBUG(
          "HIP Intercept: rocprofiler already finalized; nothing to flush");
      return;
    }

    // As in initialize(): a zero id means tool_init never ran (no GPU touched).
    // Flush BEFORE stopping, so records still buffered when the context stops
    // are handed to the tracing callback while the logger is alive.
    if (client_buffer.handle != 0) {
      rocprofiler_flush_buffer(client_buffer);
    }
    if (client_ctx.handle != 0) {
      rocprofiler_stop_context(client_ctx);
    }
  }
};

}  // namespace dftracer

#endif
#endif
