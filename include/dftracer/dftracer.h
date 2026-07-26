//
// Created by haridev on 10/7/23.
//

#ifndef DFTRACER_DFTRACER_H
#define DFTRACER_DFTRACER_H

/**
 * Common to both C and CPP
 */
#include <dftracer/core/common/constants.h>
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

 public:
  DFTracer(ConstEventNameType _name, ConstEventNameType _cat,
           int event_type = DF_DATA_EVENT,
           TraceEventType _type = TraceEventType::TRACE_TYPE_CPP_APP);

  void update(const char* key, int value,
              MetadataType type = MetadataType::MT_KEY);

  void update(const char* key, const char* value,
              MetadataType type = MetadataType::MT_KEY);

  void finalize();

  ~DFTracer();
};

#define DFTRACER_CPP_INIT(log_file, data_dirs, process_id) \
  initialize_main(log_file, data_dirs, process_id);
#define DFTRACER_CPP_INIT_NO_BIND(log_file, data_dirs, process_id) \
  initialize_no_bind(log_file, data_dirs, process_id);
#define DFTRACER_CPP_FINI() finalize()
#define DFTRACER_CPP_APP_METADATA_INT(key, val) set_app_metadata_int(key, val);
#define DFTRACER_CPP_APP_METADATA_STR(key, val) \
  set_app_metadata_string(key, val);
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

#define DFTRACER_C_INIT(log_file, data_dirs, process_id) \
  initialize_main(log_file, data_dirs, process_id);
#define DFTRACER_C_INIT_NO_BIND(log_file, data_dirs, process_id) \
  initialize_no_bind(log_file, data_dirs, process_id);
#define DFTRACER_C_FINI() finalize()
#define DFTRACER_C_APP_METADATA_INT(key, val) set_app_metadata_int(key, val);
#define DFTRACER_C_APP_METADATA_STR(key, val) set_app_metadata_string(key, val);

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

#ifdef __cplusplus
}
#endif

#endif  // DFTRACER_DFTRACER_H
