//
// Created by haridev on 10/8/23.
//

#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/dftracer_main.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/dftracer.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
struct RegionDebugEntry {
  std::size_t id;
  std::string name;
  std::string category;
  int event_type;
  void* profiler;
};

std::mutex g_region_lock;
std::unordered_set<DFTracerData*> g_live_regions;
std::unordered_map<DFTracerData*, RegionDebugEntry> g_region_debug_map;
std::once_flag g_region_atexit_once;
std::atomic<std::size_t> g_region_id{1};

bool region_debug_enabled() {
  static const bool enabled =
      (std::getenv("DFTRACER_DEBUG_REGION_TRACK") != nullptr);
  return enabled;
}

void dump_live_regions() {
  if (!region_debug_enabled()) return;
  std::lock_guard<std::mutex> lock(g_region_lock);
  dftracer_logging_real_fprintf()(
      stderr, "[DFTRACER_REGION_DEBUG] outstanding regions=%zu\n",
      g_region_debug_map.size());
  for (const auto& [data, entry] : g_region_debug_map) {
    dftracer_logging_real_fprintf()(
        stderr,
        "[DFTRACER_REGION_DEBUG] id=%zu data=%p profiler=%p event=%d "
        "name=%s category=%s\n",
        entry.id, static_cast<void*>(data), entry.profiler, entry.event_type,
        entry.name.c_str(), entry.category.c_str());
  }
}

void release_region_nolock(DFTracerData* data) {
  if (!data) return;
  auto profiler = static_cast<DFTracer*>(data->profiler);
  if (profiler) {
    profiler->finalize();
    delete profiler;
  }
  delete data;
}

void release_all_live_regions() {
  std::vector<DFTracerData*> pending;
  {
    std::lock_guard<std::mutex> lock(g_region_lock);
    pending.reserve(g_live_regions.size());
    for (auto* data : g_live_regions) {
      pending.push_back(data);
      g_region_debug_map.erase(data);
    }
    g_live_regions.clear();
  }

  for (auto* data : pending) {
    release_region_nolock(data);
  }
}
}  // namespace

DFTracer::DFTracer(ConstEventNameType _name, ConstEventNameType _cat,
                   int event_type, TraceEventType _type)
    : event_type(event_type),
      initialized(true),
      name(_name),
      cat(_cat),
      type(_type),
      metadata(nullptr) {
  DFTRACER_LOG_DEBUG("DFTracer::DFTracer event %s cat %s ", _name, _cat);
  auto dftracer_core = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                               ProfileType::PROFILER_CPP_APP);
  if (dftracer_core != nullptr) {
    if (event_type == DF_DATA_EVENT) {
      start_time = dftracer_core->get_time();
    }
    dftracer_core->enter_event();
  }
}

void DFTracer::update(const char* key, int value, MetadataType type) {
  DFTRACER_LOG_DEBUG("DFTracer::update event %s cat %s  key %s value %d ", name,
                     cat, key, value);
  if (event_type == DF_DATA_EVENT) {
    auto dftracer_core = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                                 ProfileType::PROFILER_CPP_APP);
    if (dftracer_core != nullptr && dftracer_core->is_active() &&
        dftracer_core->include_metadata) {
      if (metadata == nullptr) metadata = new dftracer::Metadata();
      metadata->insert_or_assign(key, value, type);
    }
  }
}

void DFTracer::update(const char* key, const char* value, MetadataType type) {
  DFTRACER_LOG_DEBUG("DFTracer::update event %s cat %s  key %s value %s ", name,
                     cat, key, value);
  if (event_type == DF_DATA_EVENT) {
    auto dftracer_core = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                                 ProfileType::PROFILER_CPP_APP);
    if (dftracer_core != nullptr && dftracer_core->is_active() &&
        dftracer_core->include_metadata) {
      if (metadata == nullptr) metadata = new dftracer::Metadata();
      metadata->insert_or_assign(key, value, type);
    }
  }
}

void DFTracer::finalize() {
  DFTRACER_LOG_DEBUG("DFTracer::finalize event %s cat %s", name, cat);
  auto dftracer_core = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                               ProfileType::PROFILER_CPP_APP);
  if (dftracer_core != nullptr && dftracer_core->is_active()) {
    if (event_type == DF_DATA_EVENT) {
      TimeResolution end_time = dftracer_core->get_time();
      bool consumed = dftracer_core->log(name, cat, type, start_time,
                                         end_time - start_time, metadata);
      if (consumed) {
        metadata = nullptr;
      }
    } else if (event_type == DF_METADATA_EVENT) {
      dftracer_core->log_metadata(name, cat, type);
    }

    dftracer_core->exit_event();
  }
  if (metadata != nullptr) {
    delete metadata;
    metadata = nullptr;
  }
  initialized = false;
}

DFTracer::~DFTracer() {
  DFTRACER_LOG_DEBUG("DFTracer::~DFTracer event %s cat %s", name, cat);
  if (initialized) finalize();
}

void initialize_main(const char* log_file, const char* data_dirs,
                     int* process_id) {
  DFTRACER_LOG_DEBUG("dftracer.initialize_main");
  DFTRACER_MAIN_SINGLETON_INIT(ProfilerStage::PROFILER_INIT,
                               ProfileType::PROFILER_C_APP, log_file, data_dirs,
                               process_id);
}

void initialize_no_bind(const char* log_file, const char* data_dirs,
                        int* process_id) {
  DFTRACER_LOG_DEBUG("dftracer.initialize_no_bind");
  DFTRACER_MAIN_SINGLETON_INIT(ProfilerStage::PROFILER_OTHER,
                               ProfileType::PROFILER_C_APP, log_file, data_dirs,
                               process_id);
}

struct DFTracerData* initialize_region(ConstEventNameType name,
                                       ConstEventNameType cat, int event_type) {
  DFTRACER_LOG_DEBUG("dftracer.initialize_region event %s cat %s", name, cat);
  if (region_debug_enabled()) {
    std::call_once(g_region_atexit_once, []() {
      std::atexit(release_all_live_regions);
      std::atexit(dump_live_regions);
    });
  }

  auto data = new DFTracerData();
  data->profiler =
      new DFTracer(name, cat, event_type, TraceEventType::TRACE_TYPE_C_APP);

  {
    std::lock_guard<std::mutex> lock(g_region_lock);
    g_live_regions.insert(data);
    if (region_debug_enabled()) {
      RegionDebugEntry entry{g_region_id.fetch_add(1), name ? name : "",
                             cat ? cat : "", event_type, data->profiler};
      g_region_debug_map.insert_or_assign(data, std::move(entry));
      const auto& e = g_region_debug_map[data];
      dftracer_logging_real_fprintf()(
          stderr,
          "[DFTRACER_REGION_DEBUG] alloc id=%zu data=%p profiler=%p "
          "event=%d name=%s category=%s\n",
          e.id, static_cast<void*>(data), e.profiler, e.event_type,
          e.name.c_str(), e.category.c_str());
    }
  }
  return data;
}

void finalize_region(struct DFTracerData* data) {
  DFTRACER_LOG_DEBUG("dftracer.finalize_region");
  if (data == nullptr) return;

  {
    std::lock_guard<std::mutex> lock(g_region_lock);
    auto iter = g_live_regions.find(data);
    if (iter == g_live_regions.end()) {
      return;
    }
    if (region_debug_enabled()) {
      auto dbg = g_region_debug_map.find(data);
      if (dbg != g_region_debug_map.end()) {
        dftracer_logging_real_fprintf()(
            stderr,
            "[DFTRACER_REGION_DEBUG] free id=%zu data=%p profiler=%p "
            "event=%d name=%s category=%s\n",
            dbg->second.id, static_cast<void*>(data), dbg->second.profiler,
            dbg->second.event_type, dbg->second.name.c_str(),
            dbg->second.category.c_str());
        g_region_debug_map.erase(dbg);
      }
    }
    g_live_regions.erase(iter);
  }

  release_region_nolock(data);
}

void finalize_region_cleanup(struct DFTracerData** data) {
  if (data == nullptr || *data == nullptr) return;
  finalize_region(*data);
  *data = nullptr;
}

void update_metadata_int(struct DFTracerData* data, const char* key,
                         int value) {
  DFTRACER_LOG_DEBUG("dftracer.update_metadata_int");
  if (data && data->profiler) {
    auto profiler = (DFTracer*)data->profiler;
    profiler->update(key, value);
  }
}

void update_metadata_string(struct DFTracerData* data, const char* key,
                            const char* value) {
  DFTRACER_LOG_DEBUG("dftracer.update_metadata_string");
  if (data && data->profiler) {
    auto profiler = (DFTracer*)data->profiler;
    profiler->update(key, value);
  }
}

void update_metadata_int_type(struct DFTracerData* data, const char* key,
                              int value, int type) {
  DFTRACER_LOG_DEBUG("dftracer.update_metadata_int_type");
  if (data && data->profiler) {
    auto profiler = (DFTracer*)data->profiler;
    MetadataType meta_type;
    convert(type, meta_type);
    profiler->update(key, value, meta_type);
  }
}

void update_metadata_string_type(struct DFTracerData* data, const char* key,
                                 const char* value, int type) {
  DFTRACER_LOG_DEBUG("dftracer.update_metadata_string_type");
  if (data && data->profiler) {
    auto profiler = (DFTracer*)data->profiler;
    MetadataType meta_type;
    convert(type, meta_type);
    profiler->update(key, value, meta_type);
  }
}

TimeResolution get_time() {
  DFTRACER_LOG_DEBUG("dftracer.cpp.get_time");
  auto dftracer = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                          ProfileType::PROFILER_C_APP);
  if (dftracer != nullptr)
    return dftracer->get_time();
  else
    DFTRACER_LOG_ERROR("dftracer.cpp.get_time dftracer not initialized");
  return 0;
}

void log_event(ConstEventNameType name, ConstEventNameType cat,
               TimeResolution start_time, TimeResolution duration) {
  DFTRACER_LOG_DEBUG("dftracer.cpp.log_event");
  auto dftracer = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                          ProfileType::PROFILER_C_APP);
  if (dftracer != nullptr)
    dftracer->log(name, cat, TraceEventType::TRACE_TYPE_C_APP, start_time,
                  duration, nullptr);
  else
    DFTRACER_LOG_ERROR("dftracer.cpp.log_event dftracer not initialized");
}

extern "C" void set_app_metadata_int(const char* key, int value) {
  DFTRACER_LOG_DEBUG("dftracer.cpp.set_app_metadata_int");
  auto dftracer = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                          ProfileType::PROFILER_ANY);
  if (dftracer != nullptr)
    dftracer->set_app_metadata(key, (int64_t)value);
  else
    DFTRACER_LOG_ERROR(
        "dftracer.cpp.set_app_metadata_int dftracer not initialized");
}

extern "C" void set_app_metadata_string(const char* key, const char* value) {
  DFTRACER_LOG_DEBUG("dftracer.cpp.set_app_metadata_string");
  auto dftracer = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                          ProfileType::PROFILER_ANY);
  if (dftracer != nullptr)
    dftracer->set_app_metadata(key, value);
  else
    DFTRACER_LOG_ERROR(
        "dftracer.cpp.set_app_metadata_string dftracer not initialized");
}

void finalize() {
  DFTRACER_LOG_DEBUG("dftracer.cpp.finalize");
  release_all_live_regions();
  auto dftracer = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_FINI,
                                          ProfileType::PROFILER_C_APP);
  if (dftracer != nullptr) {
    dftracer->finalize();
    dftracer::Singleton<dftracer::DFTracerCore>::finalize();
  }
}