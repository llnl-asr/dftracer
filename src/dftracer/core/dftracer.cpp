//
// Created by haridev on 10/8/23.
//

#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/dftracer_main.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/event_types.h>
#include <dftracer/core/utils/utils.h>
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

extern "C" void dftracer_register_event_type(
    const struct DFTracerEventType* type) {
  if (type == nullptr || type->layer < 0 || type->layer > 255) return;
  // Metadata events are never aggregated: their types leave the layer as is.
  if (type->kind == DF_DATA_EVENT && !type->aggregatable)
    dftracer::set_event_type_not_aggregatable(
        static_cast<TraceEventType>(type->layer));
}

DFTracer::DFTracer(ConstEventNameType _name, const DFTracerEventType& def)
    : DFTracer(_name, def.category, def.kind,
               static_cast<TraceEventType>(def.layer)) {
  dftracer_register_event_type(&def);
}

// A value for an event's args, or a metadata record's fields; kept only when
// tracing is active.
template <typename T>
static void update_value(int event_type, dftracer::Metadata*& metadata,
                         const char* key, const T& value, MetadataType type) {
  if (key == nullptr) return;
  auto dftracer_core = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                               ProfileType::PROFILER_CPP_APP);
  if (dftracer_core != nullptr && dftracer_core->is_active() &&
      (event_type == DF_METADATA_EVENT || dftracer_core->include_metadata)) {
    if (metadata == nullptr) metadata = new dftracer::Metadata();
    metadata->insert_or_assign(key, value, type);
  }
}

void DFTracer::update(const char* key, int value, MetadataType type) {
  DFTRACER_LOG_DEBUG("DFTracer::update event %s cat %s  key %s value %d ", name,
                     cat, key, value);
  update_value(event_type, metadata, key, value, type);
}

void DFTracer::update(const char* key, const char* value, MetadataType type) {
  DFTRACER_LOG_DEBUG("DFTracer::update event %s cat %s  key %s value %s ", name,
                     cat, key, value);
  // Kept by pointer: the string must outlive the region.
  update_value(event_type, metadata, key, value, type);
}

void DFTracer::update_number(const char* key, int64_t value,
                             MetadataType type) {
  update_value(event_type, metadata, key, value, type);
}

void DFTracer::update_number(const char* key, uint64_t value,
                             MetadataType type) {
  update_value(event_type, metadata, key, value, type);
}

void DFTracer::update_number(const char* key, double value, MetadataType type) {
  update_value(event_type, metadata, key, value, type);
}

// A list arg (dftracer::ArgList) in the event's Metadata under its key. The
// first append decides whether the key holds a list or an object of lists;
// appends of the other form, or to a key holding another value, are ignored.
static void append_value(dftracer::Metadata*& metadata, const char* key,
                         const char* sub, dftracer::ArgList::Value value) {
  if (key == nullptr) return;
  if (metadata == nullptr) metadata = new dftracer::Metadata();
  auto it = metadata->find(key);
  if (it == metadata->end())
    it = metadata->insert_or_assign(key, dftracer::ArgList{sub != nullptr, {}})
             .first;
  auto* list = std::any_cast<dftracer::ArgList>(&std::get<1>(it->second));
  if (list == nullptr || list->nested != (sub != nullptr)) return;
  for (auto& s : list->subs) {
    if (sub == nullptr || std::strcmp(s.name, sub) == 0) {
      s.values.push_back(value);
      return;
    }
  }
  list->subs.push_back({sub, {value}});
}

void DFTracer::append(const char* key, const char* value) {
  append(key, nullptr, value);
}

void DFTracer::append(const char* key, int64_t value) {
  append(key, nullptr, value);
}

void DFTracer::append(const char* key, const char* sub, const char* value) {
  dftracer::ArgList::Value v{dftracer::ArgList::Value::STRING, {}};
  v.s = value;
  if (initialized) append_value(metadata, key, sub, v);
}

void DFTracer::append(const char* key, const char* sub, int64_t value) {
  dftracer::ArgList::Value v{dftracer::ArgList::Value::INT, {}};
  v.i = value;
  if (initialized) append_value(metadata, key, sub, v);
}

void DFTracer::append_id(const char* key, const char* sub, uint64_t id) {
  dftracer::ArgList::Value v{dftracer::ArgList::Value::ID, {}};
  v.u = id;
  if (initialized) append_value(metadata, key, sub, v);
}

void DFTracer::relate(EntityRelation relation, EntityID entity) {
  if (event_type != DF_DATA_EVENT || entity == DFT_ENTITY_NONE ||
      !dft_relation_is_event(relation))
    return;
  append_id(DFT_RELATIONS_ARG, dft_relation_name(relation), entity);
}

EntityID DFTracer::relate(EntityRelation relation, ConstEntityTypeName type,
                          ConstEntityKey key, EntityStore store,
                          ConstEntityURI uri) {
  EntityID id = dftracer_declare_entity(type, key, store, uri);
  relate(relation, id);
  return id;
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
      // With fields, a record named after the region; without, the
      // key/value pair (name, cat).
      if (metadata != nullptr && !metadata->empty()) {
        dftracer_core->log_record(name, metadata, type);
        metadata = nullptr;
      } else {
        dftracer_core->log_metadata(name, cat, type);
      }
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

// Registers a C region (see release_all_live_regions).
static struct DFTracerData* new_region(ConstEventNameType name,
                                       ConstEventNameType cat, int event_type,
                                       DFTracer* profiler) {
  if (region_debug_enabled()) {
    std::call_once(g_region_atexit_once, []() {
      std::atexit(release_all_live_regions);
      std::atexit(dump_live_regions);
    });
  }

  auto data = new DFTracerData();
  data->profiler = profiler;

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

struct DFTracerData* initialize_region(ConstEventNameType name,
                                       ConstEventNameType cat, int event_type) {
  DFTRACER_LOG_DEBUG("dftracer.initialize_region event %s cat %s", name, cat);
  return new_region(
      name, cat, event_type,
      new DFTracer(name, cat, event_type, TraceEventType::TRACE_TYPE_C_APP));
}

struct DFTracerData* initialize_typed_region(
    ConstEventNameType name, const struct DFTracerEventType* type) {
  if (type == nullptr)
    return initialize_region(name, C_LOG_CATEGORY, DF_DATA_EVENT);
  DFTRACER_LOG_DEBUG("dftracer.initialize_typed_region event %s cat %s", name,
                     type->category);
  return new_region(name, type->category, type->kind,
                    new DFTracer(name, *type));
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

void update_relation(struct DFTracerData* data, EntityRelation relation,
                     EntityID entity) {
  if (data && data->profiler)
    static_cast<DFTracer*>(data->profiler)->relate(relation, entity);
}

EntityID update_relation_entity(struct DFTracerData* data,
                                EntityRelation relation,
                                ConstEntityTypeName type, ConstEntityKey key,
                                EntityStore store, ConstEntityURI uri) {
  EntityID id = dftracer_declare_entity(type, key, store, uri);
  update_relation(data, relation, id);
  return id;
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

void update_metadata_int64(struct DFTracerData* data, const char* key,
                           int64_t value) {
  if (data && data->profiler)
    static_cast<DFTracer*>(data->profiler)->update(key, value);
}

void update_metadata_uint64(struct DFTracerData* data, const char* key,
                            uint64_t value) {
  if (data && data->profiler)
    static_cast<DFTracer*>(data->profiler)->update(key, value);
}

void update_metadata_double(struct DFTracerData* data, const char* key,
                            double value) {
  if (data && data->profiler)
    static_cast<DFTracer*>(data->profiler)->update(key, value);
}

void update_metadata_append_string(struct DFTracerData* data, const char* key,
                                   const char* sub, const char* value) {
  if (data && data->profiler)
    static_cast<DFTracer*>(data->profiler)->append(key, sub, value);
}

void update_metadata_append_int(struct DFTracerData* data, const char* key,
                                const char* sub, int64_t value) {
  if (data && data->profiler)
    static_cast<DFTracer*>(data->profiler)->append(key, sub, value);
}

void update_metadata_append_id(struct DFTracerData* data, const char* key,
                               const char* sub, uint64_t id) {
  if (data && data->profiler)
    static_cast<DFTracer*>(data->profiler)->append_id(key, sub, id);
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

// Entities and relations are metadata records (DF_METADATA_EVENT regions with
// fields), written once per process per entity and per type.
static std::shared_ptr<dftracer::DFTracerCore> active_core() {
  auto core = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                      ProfileType::PROFILER_CPP_APP);
  return core != nullptr && core->is_active() ? core : nullptr;
}

extern "C" EntityID dftracer_declare_entity(ConstEntityTypeName type,
                                            ConstEntityKey key,
                                            EntityStore store,
                                            ConstEntityURI uri) {
  DFTRACER_LOG_DEBUG("dftracer.declare_entity");
  if (type == nullptr || key == nullptr) return DFT_ENTITY_NONE;
  auto core = active_core();
  if (core == nullptr) return DFT_ENTITY_NONE;
  char t[DFT_ENTITY_TYPE_LEN];
  dft_entity_sanitize(t, sizeof(t), type);
  EntityID id = dft_entity_hash(t, key);
  if (!core->first_time(METADATA_NAME_ENTITY_HASH, id)) return id;
  char u[DFT_ENTITY_URI_LEN];
  dft_entity_sanitize(u, sizeof(u), uri);
  char hex[DFT_ENTITY_HEX_LEN];
  dft_entity_hex(id, hex);
  DFTracer record(METADATA_NAME_ENTITY_HASH, "", DF_METADATA_EVENT,
                  TraceEventType::TRACE_TYPE_DFTRACER);
  record.update("id", hex);
  record.update("type", t);
  record.update("store", static_cast<int>(store));
  record.update("uri", u);
  return id;
}

extern "C" void dftracer_declare_entity_type(
    ConstEntityTypeName type, EntityRole role,
    ConstEntityDescription description) {
  DFTRACER_LOG_DEBUG("dftracer.declare_entity_type");
  if (type == nullptr) return;
  auto core = active_core();
  if (core == nullptr) return;
  char t[DFT_ENTITY_TYPE_LEN];
  dft_entity_sanitize(t, sizeof(t), type);
  if (!core->first_time(METADATA_NAME_ENTITY_TYPE, dft_entity_hash(t, "")))
    return;
  char d[DFT_ENTITY_DESC_LEN];
  dft_entity_sanitize(d, sizeof(d), description);
  DFTracer record(METADATA_NAME_ENTITY_TYPE, "", DF_METADATA_EVENT,
                  TraceEventType::TRACE_TYPE_DFTRACER);
  record.update("type", t);
  record.update("role", static_cast<int>(role));
  record.update("description", d);
}

extern "C" void dftracer_relate_entities(EntityRelation relation,
                                         EntityID subject, EntityID object) {
  DFTRACER_LOG_DEBUG("dftracer.relate_entities");
  if (dft_relation_is_event(relation)) return;  // event relations go on events
  if (subject == DFT_ENTITY_NONE || object == DFT_ENTITY_NONE) return;
  if (active_core() == nullptr) return;
  char s_hex[DFT_ENTITY_HEX_LEN], o_hex[DFT_ENTITY_HEX_LEN];
  dft_entity_hex(subject, s_hex);
  dft_entity_hex(object, o_hex);
  DFTracer record(METADATA_NAME_ENTITY_RELATION, "", DF_METADATA_EVENT,
                  TraceEventType::TRACE_TYPE_DFTRACER);
  record.update("relation", static_cast<int>(relation));
  record.update("subject", s_hex);
  record.update("object", o_hex);
}

extern "C" void mark_used(const char* name) {
  DFTRACER_LOG_DEBUG("dftracer.cpp.mark_used");
  auto dftracer = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_OTHER,
                                          ProfileType::PROFILER_ANY);
  if (dftracer != nullptr)
    dftracer->mark_used(name);
  else
    DFTRACER_LOG_ERROR("dftracer.cpp.mark_used dftracer not initialized");
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