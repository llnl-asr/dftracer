#include <dftracer/core/function/hip/intercept.h>
#ifdef DFTRACER_HIP_TRACING_ENABLE

#include <dftracer/core/common/logging.h>
#include <rocprofiler-sdk/buffer.h>
#include <rocprofiler-sdk/buffer_tracing.h>
#include <rocprofiler-sdk/external_correlation.h>
#include <rocprofiler-sdk/fwd.h>
#include <rocprofiler-sdk/internal_threading.h>
#include <rocprofiler-sdk/registration.h>
#include <rocprofiler-sdk/rocprofiler.h>
#include <rocprofiler-sdk/version.h>

#include <any>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>

#define MAX_EVENT_NAME_LENGTH 15

// Compute the numeric API version we are compiling against.
// ROCPROFILER_VERSION_MAJOR/MINOR/PATCH exist in all supported SDK versions:
//   0.4.0 (ROCm 6.2) through 1.x.x (ROCm 7.x).
#if defined(ROCPROFILER_VERSION_MAJOR) && \
    defined(ROCPROFILER_VERSION_MINOR) && defined(ROCPROFILER_VERSION_PATCH)
#define DFTRACER_ROCPROFILER_API_VERSION                                     \
  DFTRACER_GET_VERSION(ROCPROFILER_VERSION_MAJOR, ROCPROFILER_VERSION_MINOR, \
                       ROCPROFILER_VERSION_PATCH)
#else
// Fallback: use the version baked into the dftracer config header at cmake
// time.
#define DFTRACER_ROCPROFILER_API_VERSION DFTRACER_ROCPROFILER_VERSION
#endif

// SDK 1.0.0+ (ROCm 7.0+) ships the KFD event tracing header.
#if DFTRACER_ROCPROFILER_API_VERSION >= DFTRACER_GET_VERSION(1, 0, 0)
#include <rocprofiler-sdk/kfd/kfd_id.h>
#endif

namespace conf {
extern "C" rocprofiler_tool_configure_result_t* roc_conf(
    uint32_t version, const char* runtime_version, uint32_t priority,
    rocprofiler_client_id_t* id) {
  DFTRACER_LOG_DEBUG("HIPFunction configured");
  void* client_tool_data = nullptr;
  rocprofiler_at_internal_thread_create(
      dftracer::HIPFunction::thread_precreate,
      dftracer::HIPFunction::thread_postcreate,
      ROCPROFILER_LIBRARY | ROCPROFILER_HSA_LIBRARY | ROCPROFILER_HIP_LIBRARY |
          ROCPROFILER_MARKER_LIBRARY,
      static_cast<void*>(client_tool_data));

  // create configure data
  static auto cfg = rocprofiler_tool_configure_result_t{
      sizeof(rocprofiler_tool_configure_result_t),
      &dftracer::HIPFunction::tool_init, &dftracer::HIPFunction::tool_fini,
      static_cast<void*>(client_tool_data)};

  // return pointer to configure data
  return &cfg;
}

}  // namespace conf

// rocprofiler-sdk's SUPPORTED tool-registration entry point.
//
// Why this exists in addition to the rocprofiler_force_configure() call in
// HIPFunction::initialize(): force_configure only works if it runs before
// rocprofiler locks its configuration, and on a Cray PE + ROCm system it never
// does. librocprofiler-register / libamdhip64 / the Cray MPICH GTL are pulled
// in as link-time dependencies, and their load-time constructors bring
// rocprofiler up before main() is entered -- so by the time ANY application
// code runs, including DFTRACER_CPP_INIT as the very first statement of main(),
// force_configure returns ROCPROFILER_STATUS_ERROR_CONFIGURATION_LOCKED (16)
// and no GPU tracing is ever configured. Measured on Tuolumne (MI300A,
// ROCm 6.4.2, rocprofiler-sdk 0.6.0) with Laghos/MFEM.
//
// rocprofiler instead SCANS the loaded libraries for a symbol named exactly
// `rocprofiler_configure` and calls it at the correct point in its own
// initialisation, which removes the race entirely. Defining it here means
// libdftracer_core.so is discovered as a rocprofiler client no matter when the
// application gets around to calling DFTRACER_*_INIT.
//
// Because this runs for EVERY process that merely links libdftracer_core (not
// just traced ones), decline politely unless dftracer is actually switched on.
// Returning nullptr is the documented way for a client to say "not interested".
// getenv is used rather than the ConfigurationManager singleton because this
// can be called before any dftracer object has been constructed.
extern "C" __attribute__((visibility("default")))
rocprofiler_tool_configure_result_t*
rocprofiler_configure(uint32_t version, const char* runtime_version,
                      uint32_t priority, rocprofiler_client_id_t* id) {
  const char* enabled = getenv("DFTRACER_ENABLE");
  if (enabled == nullptr || enabled[0] == '0' || enabled[0] == '\0') {
    return nullptr;
  }
  if (id != nullptr) {
    id->name = "dftracer";
  }
  DFTRACER_LOG_DEBUG(
      "dftracer registered with rocprofiler via rocprofiler_configure");
  return conf::roc_conf(version, runtime_version, priority, id);
}

template <>
std::shared_ptr<dftracer::HIPFunction>
    dftracer::Singleton<dftracer::HIPFunction>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::HIPFunction>::stop_creating_instances =
    false;
namespace dftracer {

// rocprofiler timestamps are always nanoseconds; scale them into whatever
// unit DFTLogger::get_time()/config->time_metric is currently using so GPU
// events line up with CPU-side events in the same trace.
static double roc_ns_to_time_metric_factor() {
  auto config =
      dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
  return time_metric_units_per_second(config->time_metric) / 1e9;
}

TimeResolution HIPFunction::transform_time(rocprofiler_timestamp_t end_time,
                                           rocprofiler_timestamp_t start_time) {
  double factor = roc_ns_to_time_metric_factor();
  return std::floor(end_time * factor) - std::floor(start_time * factor);
}

TimeResolution HIPFunction::transform_timestamp(
    rocprofiler_timestamp_t timestamp) {
  double factor = roc_ns_to_time_metric_factor();
  if (time_diff == 0) {
    rocprofiler_timestamp_t roctime;
    rocprofiler_get_timestamp(&roctime);
    time_diff = logger->get_time() - std::floor(roctime * factor);
  }
  return std::floor(timestamp * factor) + time_diff;
}

void HIPFunction::tool_code_object_callback(
    rocprofiler_callback_tracing_record_t record,
    rocprofiler_user_data_t* user_data, void* callback_data) {
  DFTRACER_LOG_DEBUG("HIPFunction::tool_code_object_callback");
  auto function = dftracer::Singleton<dftracer::HIPFunction>::get_instance();
  if (record.kind == ROCPROFILER_CALLBACK_TRACING_CODE_OBJECT &&
      record.operation == ROCPROFILER_CODE_OBJECT_LOAD) {
    if (record.phase == ROCPROFILER_CALLBACK_PHASE_UNLOAD) {
      auto flush_status = rocprofiler_flush_buffer(function->client_buffer);
      if (flush_status != ROCPROFILER_STATUS_ERROR_BUFFER_BUSY)
        DFTRACER_LOG_ERROR(
            "HIPFunction::tool_code_object_callback flush failed status: %d",
            flush_status);
    }
  } else if (record.kind == ROCPROFILER_CALLBACK_TRACING_CODE_OBJECT &&
             record.operation ==
                 ROCPROFILER_CODE_OBJECT_DEVICE_KERNEL_SYMBOL_REGISTER) {
    auto* data = static_cast<kernel_symbol_data_t*>(record.payload);
    if (record.phase == ROCPROFILER_CALLBACK_PHASE_LOAD) {
      function->client_kernels.emplace(data->kernel_id, *data);
    } else if (record.phase == ROCPROFILER_CALLBACK_PHASE_UNLOAD) {
      function->client_kernels.erase(data->kernel_id);
    }
  }

  (void)user_data;
  (void)callback_data;
}

// Dispatch callback: all registered buffer tracing kinds funnel here.
void HIPFunction::tool_tracing_callback(rocprofiler_context_id_t context,
                                        rocprofiler_buffer_id_t buffer_id,
                                        rocprofiler_record_header_t** headers,
                                        size_t num_headers, void* user_data,
                                        uint64_t drop_count) {
  DFTRACER_LOG_DEBUG("HIPFunction::tool_tracing_callback");
  auto function = dftracer::Singleton<dftracer::HIPFunction>::get_instance();
  auto client_name_info = function->client_name_info;
  assert(user_data != nullptr);
  assert(drop_count == 0 && "drop count should be zero for lossless policy");

  if (num_headers == 0 || headers == nullptr) return;

  for (size_t i = 0; i < num_headers; ++i) {
    auto* header = headers[i];

    auto kind_name = std::string{};
    if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING) {
      const char* _name = nullptr;
      auto _kind = static_cast<rocprofiler_buffer_tracing_kind_t>(header->kind);
      rocprofiler_query_buffer_tracing_kind_name(_kind, &_name, nullptr);
      if (_name) kind_name = std::string{_name};
    }

    // ── HSA core / extension APIs ─────────────────────────────────────────
    if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
        (header->kind == ROCPROFILER_BUFFER_TRACING_HSA_CORE_API ||
         header->kind == ROCPROFILER_BUFFER_TRACING_HSA_AMD_EXT_API ||
         header->kind == ROCPROFILER_BUFFER_TRACING_HSA_IMAGE_EXT_API ||
         header->kind == ROCPROFILER_BUFFER_TRACING_HSA_FINALIZE_EXT_API)) {
      auto* record = static_cast<rocprofiler_buffer_tracing_hsa_api_record_t*>(
          header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("context", context.handle);
      metadata->insert_or_assign("buffer_id", buffer_id.handle);
      metadata->insert_or_assign("extern_cid",
                                 record->correlation_id.external.value);
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("tid", record->thread_id);

      std::string event_name =
          std::string(client_name_info[record->kind][record->operation]);
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

      // ── HIP runtime API ───────────────────────────────────────────────────
    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_HIP_RUNTIME_API) {
      auto* record = static_cast<rocprofiler_buffer_tracing_hip_api_record_t*>(
          header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("context", context.handle);
      metadata->insert_or_assign("buffer_id", buffer_id.handle);
      metadata->insert_or_assign("extern_cid",
                                 record->correlation_id.external.value);
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("tid", record->thread_id);

      std::string event_name =
          std::string(client_name_info[record->kind][record->operation]);
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

      // ── Kernel dispatch ───────────────────────────────────────────────────
    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_KERNEL_DISPATCH) {
      auto* record =
          static_cast<rocprofiler_buffer_tracing_kernel_dispatch_record_t*>(
              header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("tid", record->thread_id);
      metadata->insert_or_assign("correlation_id",
                                 record->correlation_id.external.value);
      std::string event_name = std::string(
          function->client_kernels.at(record->dispatch_info.kernel_id)
              .kernel_name);
      if (event_name.length() > MAX_EVENT_NAME_LENGTH)
        event_name = event_name.substr(0, MAX_EVENT_NAME_LENGTH);
      event_name = std::to_string(record->dispatch_info.kernel_id) + event_name;
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

      // ── Memory copy ───────────────────────────────────────────────────────
    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_MEMORY_COPY) {
      auto* record =
          static_cast<rocprofiler_buffer_tracing_memory_copy_record_t*>(
              header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("context", context.handle);
      metadata->insert_or_assign("buffer_id", buffer_id.handle);
      metadata->insert_or_assign("extern_cid",
                                 record->correlation_id.external.value);
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("src_agent_id", record->src_agent_id.handle);
      metadata->insert_or_assign("dst_agent_id", record->dst_agent_id.handle);
      metadata->insert_or_assign("tid", record->thread_id);

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

      // ── Scratch memory ────────────────────────────────────────────────────
    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_SCRATCH_MEMORY) {
      auto* record =
          static_cast<rocprofiler_buffer_tracing_scratch_memory_record_t*>(
              header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("context", context.handle);
      metadata->insert_or_assign("buffer_id", buffer_id.handle);
      metadata->insert_or_assign("extern_cid",
                                 record->correlation_id.external.value);
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("agent_id", record->agent_id.handle);
      metadata->insert_or_assign("queue_id", record->queue_id.handle);
      metadata->insert_or_assign("flags", record->flags);
      metadata->insert_or_assign("tid", record->thread_id);
      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();

      // ── Page migration
      // ────────────────────────────────────────────────────────
      //
      // SDK 0.4.0–0.5.0 (ROCm 6.2–6.3): one enum
      // ROCPROFILER_BUFFER_TRACING_PAGE_MIGRATION,
      //   record union with PAGE_MIGRATE / PAGE_FAULT / QUEUE_SUSPEND /
      //   UNMAP_FROM_GPU ops.
      //
      // SDK 0.6.0       (ROCm 6.4): same enum, but struct redesigned to use
      // args union,
      //   renamed ops (split MIGRATE→START/END, FAULT→START/END,
      //   SUSPEND→EVICTION/RESTORE), single timestamp (not start/end).
      //
      // SDK 1.0.0+      (ROCm 7.0+): ROCPROFILER_BUFFER_TRACING_PAGE_MIGRATION
      // removed;
      //   replaced by per-kind KFD event enums and dedicated record structs.

#if DFTRACER_ROCPROFILER_API_VERSION < DFTRACER_GET_VERSION(1, 0, 0)
      // ── SDK 0.4.0–0.6.x: unified PAGE_MIGRATION kind ─────────────────────
    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_PAGE_MIGRATION) {
      auto* record =
          static_cast<rocprofiler_buffer_tracing_page_migration_record_t*>(
              header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("kind", record->kind);
      metadata->insert_or_assign("operation", record->operation);

#if DFTRACER_ROCPROFILER_API_VERSION >= DFTRACER_GET_VERSION(0, 6, 0)
      // SDK 0.6.0: redesigned args union, renamed ops, single timestamp.
      switch (record->operation) {
        case ROCPROFILER_PAGE_MIGRATION_PAGE_MIGRATE_START: {
          metadata->insert_or_assign(
              "start_addr", record->args.page_migrate_start.start_addr);
          metadata->insert_or_assign("end_addr",
                                     record->args.page_migrate_start.end_addr);
          metadata->insert_or_assign(
              "from_agent", record->args.page_migrate_start.from_agent.handle);
          metadata->insert_or_assign(
              "to_agent", record->args.page_migrate_start.to_agent.handle);
          metadata->insert_or_assign(
              "prefetch_agent",
              record->args.page_migrate_start.prefetch_agent.handle);
          metadata->insert_or_assign(
              "preferred_agent",
              record->args.page_migrate_start.preferred_agent.handle);
          metadata->insert_or_assign("trigger",
                                     record->args.page_migrate_start.trigger);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_PAGE_MIGRATE_END: {
          metadata->insert_or_assign("start_addr",
                                     record->args.page_migrate_end.start_addr);
          metadata->insert_or_assign("end_addr",
                                     record->args.page_migrate_end.end_addr);
          metadata->insert_or_assign(
              "from_agent", record->args.page_migrate_end.from_agent.handle);
          metadata->insert_or_assign(
              "to_agent", record->args.page_migrate_end.to_agent.handle);
          metadata->insert_or_assign("trigger",
                                     record->args.page_migrate_end.trigger);
          metadata->insert_or_assign("error_code",
                                     record->args.page_migrate_end.error_code);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_PAGE_FAULT_START: {
          metadata->insert_or_assign(
              "agent_id", record->args.page_fault_start.agent_id.handle);
          metadata->insert_or_assign(
              "read_fault",
              static_cast<uint32_t>(record->args.page_fault_start.read_fault));
          metadata->insert_or_assign("address",
                                     record->args.page_fault_start.address);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_PAGE_FAULT_END: {
          metadata->insert_or_assign(
              "agent_id", record->args.page_fault_end.agent_id.handle);
          metadata->insert_or_assign(
              "migrated",
              static_cast<uint32_t>(record->args.page_fault_end.migrated));
          metadata->insert_or_assign("address",
                                     record->args.page_fault_end.address);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_QUEUE_EVICTION: {
          metadata->insert_or_assign(
              "agent_id", record->args.queue_eviction.agent_id.handle);
          metadata->insert_or_assign("trigger",
                                     record->args.queue_eviction.trigger);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_QUEUE_RESTORE: {
          metadata->insert_or_assign(
              "agent_id", record->args.queue_restore.agent_id.handle);
          metadata->insert_or_assign(
              "rescheduled",
              static_cast<uint32_t>(record->args.queue_restore.rescheduled));
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_UNMAP_FROM_GPU: {
          metadata->insert_or_assign(
              "agent_id", record->args.unmap_from_gpu.agent_id.handle);
          metadata->insert_or_assign("start_addr",
                                     record->args.unmap_from_gpu.start_addr);
          metadata->insert_or_assign("end_addr",
                                     record->args.unmap_from_gpu.end_addr);
          metadata->insert_or_assign("trigger",
                                     record->args.unmap_from_gpu.trigger);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_DROPPED_EVENT: {
          metadata->insert_or_assign(
              "dropped_events_count",
              record->args.dropped_event.dropped_events_count);
          break;
        }
        default:
          delete metadata;
          continue;
      }

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->timestamp), 0, metadata);
      function->logger->exit_event();

#else
      // SDK 0.4.0–0.5.0: original union layout, start/end timestamps.
      switch (record->operation) {
        case ROCPROFILER_PAGE_MIGRATION_PAGE_MIGRATE: {
          metadata->insert_or_assign("node_id", record->page_fault.node_id);
          metadata->insert_or_assign("address", record->page_fault.address);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_PAGE_FAULT: {
          metadata->insert_or_assign("start_addr",
                                     record->page_migrate.start_addr);
          metadata->insert_or_assign("end_addr", record->page_migrate.end_addr);
          metadata->insert_or_assign("from_node",
                                     record->page_migrate.from_node);
          metadata->insert_or_assign("to_node", record->page_migrate.to_node);
          metadata->insert_or_assign("prefetch_node",
                                     record->page_migrate.prefetch_node);
          metadata->insert_or_assign("preferred_node",
                                     record->page_migrate.preferred_node);
          metadata->insert_or_assign("trigger", record->page_migrate.trigger);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_QUEUE_SUSPEND: {
          metadata->insert_or_assign("node_id", record->queue_suspend.node_id);
          metadata->insert_or_assign("trigger", record->queue_suspend.trigger);
          break;
        }
        case ROCPROFILER_PAGE_MIGRATION_UNMAP_FROM_GPU: {
          metadata->insert_or_assign("node_id", record->unmap_from_gpu.node_id);
          metadata->insert_or_assign("start_addr",
                                     record->unmap_from_gpu.start_addr);
          metadata->insert_or_assign("end_addr",
                                     record->unmap_from_gpu.end_addr);
          metadata->insert_or_assign("trigger", record->unmap_from_gpu.trigger);
          break;
        }
        default:
          delete metadata;
          continue;
      }

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();
#endif  // SDK < 1.0.0 inner split

#else  // DFTRACER_ROCPROFILER_API_VERSION >= 1.0.0
      // ── SDK 1.0.0+ (ROCm 7.0+): per-kind KFD event records ──────────────

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind ==
                   ROCPROFILER_BUFFER_TRACING_KFD_EVENT_PAGE_MIGRATE) {
      auto* record = static_cast<
          rocprofiler_buffer_tracing_kfd_event_page_migrate_record_t*>(
          header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("pid", record->pid);
      metadata->insert_or_assign("start_address", record->start_address.handle);
      metadata->insert_or_assign("end_address", record->end_address.handle);
      metadata->insert_or_assign("src_agent", record->src_agent.handle);
      metadata->insert_or_assign("dst_agent", record->dst_agent.handle);
      metadata->insert_or_assign("prefetch_agent",
                                 record->prefetch_agent.handle);
      metadata->insert_or_assign("preferred_agent",
                                 record->preferred_agent.handle);
      metadata->insert_or_assign("error_code", record->error_code);

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->timestamp), 0, metadata);
      function->logger->exit_event();

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind ==
                   ROCPROFILER_BUFFER_TRACING_KFD_EVENT_PAGE_FAULT) {
      auto* record = static_cast<
          rocprofiler_buffer_tracing_kfd_event_page_fault_record_t*>(
          header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("pid", record->pid);
      metadata->insert_or_assign("agent_id", record->agent_id.handle);
      metadata->insert_or_assign("address", record->address.handle);

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->timestamp), 0, metadata);
      function->logger->exit_event();

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind == ROCPROFILER_BUFFER_TRACING_KFD_EVENT_QUEUE) {
      auto* record =
          static_cast<rocprofiler_buffer_tracing_kfd_event_queue_record_t*>(
              header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("pid", record->pid);
      metadata->insert_or_assign("agent_id", record->agent_id.handle);

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->timestamp), 0, metadata);
      function->logger->exit_event();

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind ==
                   ROCPROFILER_BUFFER_TRACING_KFD_EVENT_UNMAP_FROM_GPU) {
      auto* record = static_cast<
          rocprofiler_buffer_tracing_kfd_event_unmap_from_gpu_record_t*>(
          header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("pid", record->pid);
      metadata->insert_or_assign("agent_id", record->agent_id.handle);
      metadata->insert_or_assign("start_address", record->start_address.handle);
      metadata->insert_or_assign("end_address", record->end_address.handle);

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->timestamp), 0, metadata);
      function->logger->exit_event();

    } else if (header->category == ROCPROFILER_BUFFER_CATEGORY_TRACING &&
               header->kind ==
                   ROCPROFILER_BUFFER_TRACING_KFD_EVENT_DROPPED_EVENTS) {
      auto* record = static_cast<
          rocprofiler_buffer_tracing_kfd_event_dropped_events_record_t*>(
          header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("pid", record->pid);
      metadata->insert_or_assign("dropped_events_count", record->count);

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->timestamp), 0, metadata);
      function->logger->exit_event();

#endif  // page migration version split

// ── RCCL API (added in SDK 0.5.0 / ROCm 6.3) ────────────────────────────
#if DFTRACER_ROCPROFILER_API_VERSION >= DFTRACER_GET_VERSION(0, 5, 0)
    } else if (header->kind == ROCPROFILER_BUFFER_TRACING_RCCL_API) {
      auto* record = static_cast<rocprofiler_buffer_tracing_rccl_api_record_t*>(
          header->payload);

      auto metadata = new Metadata();
      metadata->insert_or_assign("operation", record->operation);
      metadata->insert_or_assign("tid", record->thread_id);
      metadata->insert_or_assign("correlation_id",
                                 record->correlation_id.external.value);

      std::string event_name =
          std::string(client_name_info.at(record->kind, record->operation));
      function->logger->enter_event();
      function->logger->log(
          event_name.c_str(), kind_name.c_str(), TraceEventType::TRACE_TYPE_HIP,
          function->transform_timestamp(record->start_timestamp),
          function->transform_time(record->end_timestamp,
                                   record->start_timestamp),
          metadata);
      function->logger->exit_event();
#endif  // SDK >= 0.5.0

    } else {
      continue;
    }
  }
}

void HIPFunction::thread_precreate(rocprofiler_runtime_library_t lib,
                                   void* tool_data) {
  DFTRACER_LOG_DEBUG(
      "internal thread about to be created by rocprofiler: lib=%u",
      static_cast<unsigned int>(lib));
}

void HIPFunction::thread_postcreate(rocprofiler_runtime_library_t lib,
                                    void* tool_data) {
  DFTRACER_LOG_DEBUG("internal thread was created by rocprofiler: lib=%u",
                     static_cast<unsigned int>(lib));
}

// Register all buffer tracing services and start the context.
int HIPFunction::tool_init(rocprofiler_client_finalize_t fini_func,
                           void* tool_data) {
  DFTRACER_LOG_DEBUG("HIP Intercept class initialized");
  auto function = dftracer::Singleton<dftracer::HIPFunction>::get_instance();
  function->client_ctx = {0};
  function->client_name_info = rocprofiler::sdk::get_buffer_tracing_names();

  rocprofiler_status_t status =
      rocprofiler_create_context(&function->client_ctx);
  if (status != ROCPROFILER_STATUS_SUCCESS) {
    DFTRACER_LOG_ERROR("HIP Intercept context creation failed: status, %d\n",
                       status);
    return -1;
  }

  auto code_object_ops = std::vector<rocprofiler_tracing_operation_t>{
      ROCPROFILER_CODE_OBJECT_DEVICE_KERNEL_SYMBOL_REGISTER};

  rocprofiler_configure_callback_tracing_service(
      function->client_ctx, ROCPROFILER_CALLBACK_TRACING_CODE_OBJECT,
      code_object_ops.data(), code_object_ops.size(), tool_code_object_callback,
      nullptr);

  constexpr auto buffer_size_bytes = 4096;
  constexpr auto buffer_watermark_bytes =
      buffer_size_bytes - (buffer_size_bytes / 8);

  rocprofiler_create_buffer(function->client_ctx, buffer_size_bytes,
                            buffer_watermark_bytes,
                            ROCPROFILER_BUFFER_POLICY_LOSSLESS,
                            dftracer::HIPFunction::tool_tracing_callback,
                            tool_data, &function->client_buffer);

  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_HIP_RUNTIME_API, nullptr,
      0, function->client_buffer);

  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_KERNEL_DISPATCH, nullptr,
      0, function->client_buffer);

  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_MEMORY_COPY, nullptr, 0,
      function->client_buffer);

  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_SCRATCH_MEMORY, nullptr,
      0, function->client_buffer);

  // Page migration service registration differs by SDK version.
#if DFTRACER_ROCPROFILER_API_VERSION < DFTRACER_GET_VERSION(1, 0, 0)
  // SDK 0.4.0–0.6.x: single PAGE_MIGRATION kind covers all sub-events.
  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_PAGE_MIGRATION, nullptr,
      0, function->client_buffer);
#else
  // SDK 1.0.0+ (ROCm 7.0+): each KFD event kind is registered separately.
  for (auto kfd_kind : {ROCPROFILER_BUFFER_TRACING_KFD_EVENT_PAGE_MIGRATE,
                        ROCPROFILER_BUFFER_TRACING_KFD_EVENT_PAGE_FAULT,
                        ROCPROFILER_BUFFER_TRACING_KFD_EVENT_QUEUE,
                        ROCPROFILER_BUFFER_TRACING_KFD_EVENT_UNMAP_FROM_GPU,
                        ROCPROFILER_BUFFER_TRACING_KFD_EVENT_DROPPED_EVENTS}) {
    rocprofiler_configure_buffer_tracing_service(
        function->client_ctx, kfd_kind, nullptr, 0, function->client_buffer);
  }
#endif

  // RCCL tracing was added in SDK 0.5.0 (ROCm 6.3).
#if DFTRACER_ROCPROFILER_API_VERSION >= DFTRACER_GET_VERSION(0, 5, 0)
  rocprofiler_configure_buffer_tracing_service(
      function->client_ctx, ROCPROFILER_BUFFER_TRACING_RCCL_API, nullptr, 0,
      function->client_buffer);
#endif

  auto client_thread = rocprofiler_callback_thread_t{};
  rocprofiler_create_callback_thread(&client_thread);
  rocprofiler_assign_callback_thread(function->client_buffer, client_thread);

  int valid_ctx = 0;
  rocprofiler_context_is_valid(function->client_ctx, &valid_ctx);
  if (valid_ctx == 0) {
    DFTRACER_LOG_DEBUG("HIP Intercept initialization failed");
    return -1;
  }

  status = rocprofiler_start_context(function->client_ctx);
  if (status != ROCPROFILER_STATUS_SUCCESS) {
    DFTRACER_LOG_ERROR(
        "HIP Intercept context start failed in tool_init(): status %d (%s)\n",
        status, rocprofiler_get_status_name(status));
    return -1;
  }
  DFTRACER_LOG_DEBUG("HIP Intercept context started from tool_init");
  return 0;
}

void HIPFunction::tool_fini(void* tool_data) {
  DFTRACER_LOG_DEBUG("HIP Intercept class finalized");
  return;
}

}  // namespace dftracer

#endif
