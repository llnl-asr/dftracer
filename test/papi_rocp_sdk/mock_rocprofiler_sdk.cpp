// A stand-in for librocprofiler-sdk.so, so that PAPI's rocp_sdk component can
// be tested without a GPU. PAPI finds it through PAPI_ROCP_SDK_LIB.
//
// It reports two GPUs: GPU 0 is hidden from HSA (as ROCR_VISIBLE_DEVICES=1
// does) and GPU 1 is visible. Two behaviours of the real library are copied:
//   * starting a context fails when its device counting service holds an
//     agent that HSA does not know, so no agent counts at all;
//   * the tools' finalize functions run from the library's exit destructors.
#include <rocprofiler-sdk/registration.h>
#include <rocprofiler-sdk/rocprofiler.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint64_t kHiddenAgent = 100;
constexpr uint64_t kVisibleAgent = 101;
constexpr uint64_t kCounter = 7;
// A profile's handle is this plus its agent's handle.
constexpr uint64_t kProfileBase = 1000;
// Value of kCounter on the visible GPU, as the test expects it.
constexpr double kVisibleValue = 4242.0;

struct DeviceService {
  uint64_t context;
  rocprofiler_agent_id_t agent;
  rocprofiler_device_counting_service_callback_t callback;
  void* user_data;
  bool profiled;
};

struct Tool {
  rocprofiler_tool_finalize_t finalize;
  void* tool_data;
};

rocprofiler_agent_v0_t agents[2];
bool initialized = false;
uint64_t next_context = 1;
std::vector<DeviceService>* services = new std::vector<DeviceService>();
std::vector<uint64_t>* active = new std::vector<uint64_t>();
Tool tool{nullptr, nullptr};

void make_agents() {
  for (int i = 0; i < 2; ++i) {
    rocprofiler_agent_v0_t& a = agents[i];
    std::memset(&a, 0, sizeof(a));
    a.size = sizeof(a);
    a.id.handle = i == 0 ? kHiddenAgent : kVisibleAgent;
    a.type = ROCPROFILER_AGENT_TYPE_GPU;
    a.name = "gfx942";
    a.vendor_name = "AMD";
    a.product_name = "mock";
    a.model_name = "mock";
    a.node_id = static_cast<uint32_t>(i + 1);
    a.logical_node_id = i + 1;
    a.logical_node_type_id = i;
    a.runtime_visibility.hsa = i == 0 ? 0 : 1;
    a.runtime_visibility.hip = a.runtime_visibility.hsa;
  }
}

bool is_active(uint64_t context) {
  for (uint64_t c : *active)
    if (c == context) return true;
  return false;
}

// As the real library: an agent counts only once the tool set its profile.
rocprofiler_status_t set_profile(rocprofiler_context_id_t context_id,
                                 rocprofiler_profile_config_id_t config) {
  for (DeviceService& s : *services) {
    if (s.context == context_id.handle &&
        config.handle == kProfileBase + s.agent.handle) {
      s.profiled = true;
      return ROCPROFILER_STATUS_SUCCESS;
    }
  }
  return ROCPROFILER_STATUS_ERROR_PROFILE_NOT_FOUND;
}

void client_finalize(rocprofiler_client_id_t) {}

// Exit order of the real library: its static destructors run the tools'
// finalize functions, after the libraries loaded later are gone.
struct AtExit {
  ~AtExit() {
    if (tool.finalize != nullptr) tool.finalize(tool.tool_data);
  }
} at_exit;

}  // namespace

extern "C" {

rocprofiler_status_t rocprofiler_is_initialized(int* status) {
  *status = initialized ? 1 : 0;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_is_finalized(int* status) {
  *status = 0;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_force_configure(
    rocprofiler_configure_func_t configure_func) {
  if (initialized) return ROCPROFILER_STATUS_ERROR_CONFIGURATION_LOCKED;
  initialized = true;
  make_agents();
  rocprofiler_client_id_t id{};
  rocprofiler_tool_configure_result_t* result =
      configure_func(ROCPROFILER_VERSION, "mock", 0, &id);
  if (result == nullptr) return ROCPROFILER_STATUS_SUCCESS;
  tool.finalize = result->finalize;
  tool.tool_data = result->tool_data;
  if (result->initialize != nullptr)
    result->initialize(client_finalize, result->tool_data);
  return ROCPROFILER_STATUS_SUCCESS;
}

const char* rocprofiler_get_status_string(rocprofiler_status_t) {
  return "mock status";
}

rocprofiler_status_t rocprofiler_get_thread_id(rocprofiler_thread_id_t* tid) {
  *tid = 1;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_query_available_agents(
    rocprofiler_agent_version_t,
    rocprofiler_query_available_agents_cb_t callback, size_t, void* user_data) {
  const void* list[2] = {&agents[0], &agents[1]};
  return callback(ROCPROFILER_AGENT_INFO_VERSION_0, list, 2, user_data);
}

rocprofiler_status_t rocprofiler_create_context(
    rocprofiler_context_id_t* context_id) {
  context_id->handle = next_context++;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_context_is_valid(
    rocprofiler_context_id_t context_id, int* status) {
  *status = context_id.handle != 0 && context_id.handle < next_context;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_context_is_active(
    rocprofiler_context_id_t context_id, int* status) {
  *status = is_active(context_id.handle);
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_start_context(
    rocprofiler_context_id_t context_id) {
  for (const DeviceService& s : *services) {
    if (s.context != context_id.handle) continue;
    if (s.agent.handle == kHiddenAgent) return ROCPROFILER_STATUS_ERROR;
  }
  for (DeviceService& s : *services) {
    if (s.context != context_id.handle) continue;
    s.profiled = false;
    s.callback(context_id, s.agent, set_profile, s.user_data);
  }
  if (!is_active(context_id.handle)) active->push_back(context_id.handle);
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_stop_context(
    rocprofiler_context_id_t context_id) {
  for (size_t i = 0; i < active->size(); ++i) {
    if ((*active)[i] == context_id.handle) {
      active->erase(active->begin() + static_cast<long>(i));
      break;
    }
  }
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_create_buffer(
    rocprofiler_context_id_t, size_t, size_t, rocprofiler_buffer_policy_t,
    rocprofiler_buffer_tracing_cb_t, void*,
    rocprofiler_buffer_id_t* buffer_id) {
  buffer_id->handle = 9;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_flush_buffer(rocprofiler_buffer_id_t) {
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_configure_device_counting_service(
    rocprofiler_context_id_t context_id, rocprofiler_buffer_id_t,
    rocprofiler_agent_id_t agent_id,
    rocprofiler_device_counting_service_callback_t cb, void* user_data) {
  services->push_back(
      DeviceService{context_id.handle, agent_id, cb, user_data, false});
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_configure_callback_dispatch_counting_service(
    rocprofiler_context_id_t, rocprofiler_dispatch_counting_service_callback_t,
    void*, rocprofiler_profile_counting_record_callback_t, void*) {
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_configure_callback_tracing_service(
    rocprofiler_context_id_t, rocprofiler_callback_tracing_kind_t,
    const rocprofiler_tracing_operation_t*, size_t,
    rocprofiler_callback_tracing_cb_t, void*) {
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_iterate_agent_supported_counters(
    rocprofiler_agent_id_t agent_id, rocprofiler_available_counters_cb_t cb,
    void* user_data) {
  rocprofiler_counter_id_t counter{kCounter};
  return cb(agent_id, &counter, 1, user_data);
}

rocprofiler_status_t rocprofiler_query_counter_info(
    rocprofiler_counter_id_t counter_id, rocprofiler_counter_info_version_id_t,
    void* info) {
  auto* out = static_cast<rocprofiler_counter_info_v0_t*>(info);
  std::memset(out, 0, sizeof(*out));
  out->id = counter_id;
  out->name = "SQ_WAVES";
  out->description = "mock counter";
  out->block = "SQ";
  out->expression = "";
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_iterate_counter_dimensions(
    rocprofiler_counter_id_t id, rocprofiler_available_dimensions_cb_t info_cb,
    void* user_data) {
  return info_cb(id, nullptr, 0, user_data);
}

rocprofiler_status_t rocprofiler_query_counter_instance_count(
    rocprofiler_agent_id_t, rocprofiler_counter_id_t, size_t* instance_count) {
  *instance_count = 1;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_create_profile_config(
    rocprofiler_agent_id_t agent_id, rocprofiler_counter_id_t*, size_t,
    rocprofiler_profile_config_id_t* config_id) {
  config_id->handle = kProfileBase + agent_id.handle;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_destroy_profile_config(
    rocprofiler_profile_config_id_t) {
  return ROCPROFILER_STATUS_SUCCESS;
}

// One record per agent with a profile, only while its context counts.
rocprofiler_status_t rocprofiler_sample_device_counting_service(
    rocprofiler_context_id_t context_id, rocprofiler_user_data_t,
    rocprofiler_counter_flag_t, rocprofiler_record_counter_t* output_records,
    size_t* rec_count) {
  size_t n = 0;
  if (is_active(context_id.handle)) {
    for (const DeviceService& s : *services) {
      if (s.context != context_id.handle || !s.profiled || n >= *rec_count)
        continue;
      rocprofiler_record_counter_t& r = output_records[n++];
      std::memset(&r, 0, sizeof(r));
      r.id = kCounter;
      r.counter_value = kVisibleValue;
      r.agent_id = s.agent;
    }
  }
  *rec_count = n;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_query_record_counter_id(
    rocprofiler_counter_instance_id_t id,
    rocprofiler_counter_id_t* counter_id) {
  counter_id->handle = id;
  return ROCPROFILER_STATUS_SUCCESS;
}

rocprofiler_status_t rocprofiler_query_record_dimension_position(
    rocprofiler_counter_instance_id_t, rocprofiler_counter_dimension_id_t,
    size_t* pos) {
  *pos = 0;
  return ROCPROFILER_STATUS_SUCCESS;
}

}  // extern "C"
