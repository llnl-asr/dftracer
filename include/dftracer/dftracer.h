//
// Created by haridev on 10/7/23.
//

#ifndef DFTRACER_DFTRACER_H
#define DFTRACER_DFTRACER_H

/**
 * Common to both C and CPP
 */
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/entity.h>
#include <dftracer/core/common/typedef.h>
#define DF_DATA_EVENT 0
#define DF_METADATA_EVENT 1
#ifdef __cplusplus
extern "C" {
#endif
void initialize_main(const char* log_file, const char* data_dirs,
                     int* process_id);
void initialize_no_bind(const char* log_file, const char* data_dirs,
                        int* process_id);
void finalize();

// App-supplied, process-global metadata (as opposed to update_metadata_*,
// which is scoped to a single region/DFTracer instance). Folded into the
// trace's single "end" event at finalize(), alongside the effective
// configuration and which instrumentation layers were used. Last write for a
// given key wins; safe to call at any point before finalize(). No-op if
// tracing isn't enabled.
void set_app_metadata_int(const char* key, int value);
void set_app_metadata_string(const char* key, const char* value);

// Reports that a named sub-layer/integration was exercised this run, folded
// into the "end" event's "used" object alongside the automatically-tracked
// TraceEventType layers (see docs/trace_format.rst). Use this to distinguish
// integrations that all log through the same TraceEventType — e.g. the
// PyTorch profiler, torch.compile/dynamo, and the AI decorator framework all
// log as PYTHON, so each calls this with its own name to tell them apart.
// Idempotent; safe to call on every invocation.
void mark_used(const char* name);

// ---------------------------------------------------------------------------
// Entities and relations (types, enums and limits: core/common/entity.h).
//
// Declare an entity instance: (type, key) -> 64-bit id, recorded once per
// process as an EH record. `type` is application-defined (truncated to
// DFT_ENTITY_TYPE_LEN), `key` is any application identifier (hashed, never
// stored), `uri` is an optional locator unique within the app (truncated to
// DFT_ENTITY_URI_LEN). Returns DFT_ENTITY_NONE when tracing is off.
EntityID dftracer_declare_entity(ConstEntityTypeName type, ConstEntityKey key,
                                 EntityStore store, ConstEntityURI uri);
// Describe an entity type once per process: its role and what it represents
// (description truncated to DFT_ENTITY_DESC_LEN). One ET record.
void dftracer_declare_entity_type(ConstEntityTypeName type, EntityRole role,
                                  ConstEntityDescription description);
// Relate two entities (entity -> entity relations, e.g. DFT_REL_CONTAINS,
// DFT_REL_DERIVED_FROM). One ER record.
void dftracer_relate_entities(EntityRelation relation, EntityID subject,
                              EntityID object);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/**
 * CPP Only
 */
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/enumeration.h>

// External Headers

// constants defined
__attribute__((unused)) static ConstEventNameType CPP_LOG_CATEGORY = "CPP_APP";

class DFTracer {
  int event_type;  // 0->event  1->metadata
  bool initialized;
  ConstEventNameType name;
  ConstEventNameType cat;
  TraceEventType type;
  TimeResolution start_time;
  dftracer::Metadata* metadata;
  void* relations;  // relation name -> entity hashes; see relate()

 public:
  DFTracer(ConstEventNameType _name, ConstEventNameType _cat,
           int event_type = DF_DATA_EVENT,
           TraceEventType _type = TraceEventType::TRACE_TYPE_CPP_APP);

  void update(const char* key, int value,
              MetadataType type = MetadataType::MT_KEY);

  void update(const char* key, const char* value,
              MetadataType type = MetadataType::MT_KEY);

  // Relate this event to an entity under an event relation (DFT_REL_USED,
  // DFT_REL_GENERATED, DFT_REL_INVALIDATED, DFT_REL_UPDATED). Ids are kept
  // as integers and rendered to hex only when the event is written.
  void relate(EntityRelation relation, EntityID entity);
  // Declare (type, key) and relate it in one step; returns the entity id.
  EntityID relate(EntityRelation relation, ConstEntityTypeName type,
                  ConstEntityKey key, EntityStore store = DFT_STORE_MEMORY,
                  ConstEntityURI uri = nullptr);
  EntityID uses(ConstEntityTypeName type, ConstEntityKey key,
                EntityStore store = DFT_STORE_MEMORY,
                ConstEntityURI uri = nullptr) {
    return relate(DFT_REL_USED, type, key, store, uri);
  }
  EntityID generates(ConstEntityTypeName type, ConstEntityKey key,
                     EntityStore store = DFT_STORE_MEMORY,
                     ConstEntityURI uri = nullptr) {
    return relate(DFT_REL_GENERATED, type, key, store, uri);
  }

  void finalize();

  ~DFTracer();
};

#define DFTRACER_CPP_INIT(log_file, data_dirs, process_id) \
  initialize_main(log_file, data_dirs, process_id);        \
  mark_used("CPP_APP");
#define DFTRACER_CPP_INIT_NO_BIND(log_file, data_dirs, process_id) \
  initialize_no_bind(log_file, data_dirs, process_id);             \
  mark_used("CPP_APP");
#define DFTRACER_CPP_FINI() finalize()
#define DFTRACER_CPP_APP_METADATA_INT(key, val) set_app_metadata_int(key, val);
#define DFTRACER_CPP_APP_METADATA_STR(key, val) \
  set_app_metadata_string(key, val);
#define DFTRACER_CPP_MARK_USED(name) mark_used(name);
#define DFTRACER_CPP_FUNCTION() \
  DFTracer profiler_dft_fn =    \
      DFTracer((char*)__FUNCTION__, CPP_LOG_CATEGORY, DF_DATA_EVENT);

#define DFTRACER_CPP_METADATA(name, key, value)                         \
  {                                                                     \
    DFTracer profiler_##name = DFTracer(key, value, DF_METADATA_EVENT); \
  }

#define DFTRACER_CPP_REGION(name) \
  DFTracer profiler_##name = DFTracer(#name, CPP_LOG_CATEGORY, DF_DATA_EVENT);

#define DFTRACER_CPP_REGION_START(name) \
  DFTracer* profiler_##name =           \
      new DFTracer(#name, CPP_LOG_CATEGORY, DF_DATA_EVENT);

#define DFTRACER_CPP_REGION_END(name) delete profiler_##name

#define DFTRACER_CPP_FUNCTION_UPDATE(key, val) profiler_dft_fn.update(key, val);

#define DFTRACER_CPP_FUNCTION_UPDATE_TYPE(key, val, type) \
  profiler_dft_fn.update(key, val, type);

#define DFTRACER_CPP_REGION_UPDATE(name, key, val) \
  profiler_##name.update(key, val);

#define DFTRACER_CPP_REGION_DYN_UPDATE(name, key, val) \
  profiler_##name->update(key, val);

#define DFTRACER_CPP_REGION_UPDATE_TYPE(name, key, val, type) \
  profiler_##name.update(key, val, type);

#define DFTRACER_CPP_REGION_DYN_UPDATE_TYPE(name, key, val, type) \
  profiler_##name->update(key, val, type);

// Entities and relations (see dftracer_declare_entity, core/common/entity.h).
#define DFTRACER_CPP_ENTITY(type, key, store, uri) \
  dftracer_declare_entity(type, key, store, uri)
#define DFTRACER_CPP_ENTITY_TYPE(type, role, description) \
  dftracer_declare_entity_type(type, role, description);
#define DFTRACER_CPP_ENTITY_RELATE(relation, subject, object) \
  dftracer_relate_entities(relation, subject, object);
#define DFTRACER_CPP_FUNCTION_RELATE(relation, entity) \
  profiler_dft_fn.relate(relation, entity);
#define DFTRACER_CPP_FUNCTION_USES(type, key) profiler_dft_fn.uses(type, key);
#define DFTRACER_CPP_FUNCTION_GENERATES(type, key) \
  profiler_dft_fn.generates(type, key);
#define DFTRACER_CPP_REGION_RELATE(name, relation, entity) \
  profiler_##name.relate(relation, entity);
#define DFTRACER_CPP_REGION_DYN_RELATE(name, relation, entity) \
  profiler_##name->relate(relation, entity);

extern "C" {
#endif
// C APIs

struct DFTracerData {
  void* profiler;
};

__attribute__((unused)) static ConstEventNameType C_LOG_CATEGORY = "C_APP";
struct DFTracerData* initialize_region(ConstEventNameType name,
                                       ConstEventNameType cat, int event_type);
void finalize_region(struct DFTracerData* data);
void finalize_region_cleanup(struct DFTracerData** data);
void update_metadata_int(struct DFTracerData* data, const char* key, int value);
void update_metadata_string(struct DFTracerData* data, const char* key,
                            const char* value);

void update_metadata_int_type(struct DFTracerData* data, const char* key,
                              int value, int type);
void update_metadata_string_type(struct DFTracerData* data, const char* key,
                                 const char* value, int type);

// Relate a region to an entity under an event relation, or declare
// (type, key) and relate it in one call (returns the entity id).
void update_relation(struct DFTracerData* data, EntityRelation relation,
                     EntityID entity);
EntityID update_relation_entity(struct DFTracerData* data,
                                EntityRelation relation,
                                ConstEntityTypeName type, ConstEntityKey key,
                                EntityStore store, ConstEntityURI uri);

#define DFTRACER_C_INIT(log_file, data_dirs, process_id) \
  initialize_main(log_file, data_dirs, process_id);      \
  mark_used("C_APP");
#define DFTRACER_C_INIT_NO_BIND(log_file, data_dirs, process_id) \
  initialize_no_bind(log_file, data_dirs, process_id);           \
  mark_used("C_APP");
#define DFTRACER_C_FINI() finalize()
#define DFTRACER_C_APP_METADATA_INT(key, val) set_app_metadata_int(key, val);
#define DFTRACER_C_APP_METADATA_STR(key, val) set_app_metadata_string(key, val);
#define DFTRACER_C_MARK_USED(name) mark_used(name);

#if defined(__GNUC__) || defined(__clang__)
#define DFTRACER_C_REGION_CLEANUP \
  __attribute__((cleanup(finalize_region_cleanup)))
#else
#define DFTRACER_C_REGION_CLEANUP
#endif

#define DFTRACER_C_FUNCTION_START()                        \
  struct DFTracerData* data_fn DFTRACER_C_REGION_CLEANUP = \
      initialize_region(__func__, C_LOG_CATEGORY, DF_DATA_EVENT);

#define DFTRACER_C_FUNCTION_END() \
  finalize_region(data_fn);       \
  data_fn = NULL;

#define DFTRACER_C_REGION_START(name)                          \
  struct DFTracerData* data_##name DFTRACER_C_REGION_CLEANUP = \
      initialize_region(#name, C_LOG_CATEGORY, DF_DATA_EVENT);

#define DFTRACER_C_REGION_END(name) \
  finalize_region(data_##name);     \
  data_##name = NULL;

#define DFTRACER_C_METADATA(name, key, val)             \
  {                                                     \
    struct DFTracerData* data_##name =                  \
        initialize_region(key, val, DF_METADATA_EVENT); \
    finalize_region(data_##name);                       \
  }

#define DFTRACER_C_FUNCTION_UPDATE_INT(key, val) \
  update_metadata_int(data_fn, key, val);

#define DFTRACER_C_FUNCTION_UPDATE_STR(key, val) \
  update_metadata_string(data_fn, key, val);

#define DFTRACER_C_REGION_UPDATE_INT(name, key, val) \
  update_metadata_int(data_##name, key, val);

#define DFTRACER_C_REGION_UPDATE_STR(name, key, val) \
  update_metadata_string(data_##name, key, val);

#define DFTRACER_C_FUNCTION_UPDATE_INT_TYPE(key, val, type) \
  update_metadata_int_type(data_fn, key, val, type);

#define DFTRACER_C_FUNCTION_UPDATE_STR_TYPE(key, val, type) \
  update_metadata_string_type(data_fn, key, val, type);

#define DFTRACER_C_REGION_UPDATE_INT_TYPE(name, key, val, type) \
  update_metadata_int_type(data_##name, key, val, type);

#define DFTRACER_C_REGION_UPDATE_STR_TYPE(name, key, val, type) \
  update_metadata_string_type(data_##name, key, val, type);

// Entities and relations (see dftracer_declare_entity, core/common/entity.h).
#define DFTRACER_C_ENTITY(type, key, store, uri) \
  dftracer_declare_entity(type, key, store, uri)
#define DFTRACER_C_ENTITY_TYPE(type, role, description) \
  dftracer_declare_entity_type(type, role, description);
#define DFTRACER_C_ENTITY_RELATE(relation, subject, object) \
  dftracer_relate_entities(relation, subject, object);
#define DFTRACER_C_FUNCTION_RELATE(relation, entity) \
  update_relation(data_fn, relation, entity);
#define DFTRACER_C_FUNCTION_USES(type, key)                                  \
  update_relation_entity(data_fn, DFT_REL_USED, type, key, DFT_STORE_MEMORY, \
                         NULL);
#define DFTRACER_C_FUNCTION_GENERATES(type, key)                \
  update_relation_entity(data_fn, DFT_REL_GENERATED, type, key, \
                         DFT_STORE_MEMORY, NULL);
#define DFTRACER_C_REGION_RELATE(name, relation, entity) \
  update_relation(data_##name, relation, entity);

#ifdef __cplusplus
}
#endif

#endif  // DFTRACER_DFTRACER_H
