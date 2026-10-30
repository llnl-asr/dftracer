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

// Layers (the "type" column) from here up to 255 are for other projects'
// event types; below it are dftracer's own TraceEventType values.
#define DFTRACER_TRACE_TYPE_EXTERNAL 128

// An event type, defined at compile time by the project whose events it
// describes: the category and layer its events carry, whether they are data
// events or metadata records, and whether aggregation may fold them.
// Aggregatability belongs to the layer: once a data-event type on a layer is
// registered as not aggregatable, every event on that layer is traced in full,
// also when DFTRACER_AGGREGATION_ENABLE is on. Metadata events are never
// aggregated, so `aggregatable` means nothing for them. A type is registered
// when its first region is set up, or with dftracer_register_event_type.
// In C++ derive from dftracer::EventType.
struct DFTracerEventType {
  const char* category;
  int layer;         // a TraceEventType, or DFTRACER_TRACE_TYPE_EXTERNAL + n
  int kind;          // DF_DATA_EVENT or DF_METADATA_EVENT
  int aggregatable;  // 0: the layer's events are never aggregated
};
#ifdef __cplusplus
extern "C" {
#endif
void initialize_main(const char* log_file, const char* data_dirs,
                     int* process_id);
// Register an event type up front, e.g. before events of its layer are logged
// by other means (see DFTracerEventType).
void dftracer_register_event_type(const struct DFTracerEventType* type);
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
#include <type_traits>

// constants defined
__attribute__((unused)) static ConstEventNameType CPP_LOG_CATEGORY = "CPP_APP";

namespace dftracer {
// The compile-time form of DFTracerEventType. A project derives its types
// from it and overrides what differs; the defaults are dftracer's own C++
// region (an aggregatable CPP_APP data event):
//
//   struct Provenance : dftracer::EventType {
//     static constexpr const char* category = "provenance";
//     static constexpr bool aggregatable = false;
//   };
//   DFTracer ev("load", Provenance{});   // or DFTRACER_CPP_TYPED_REGION
struct EventType {
  static constexpr const char* category = "CPP_APP";
  static constexpr TraceEventType layer = TraceEventType::TRACE_TYPE_CPP_APP;
  static constexpr int kind = DF_DATA_EVENT;
  static constexpr bool aggregatable = true;
};

template <typename T>
constexpr DFTracerEventType event_type_of() {
  static_assert(std::is_base_of<EventType, T>::value,
                "an event type derives from dftracer::EventType");
  static_assert(T::kind == DF_DATA_EVENT || T::kind == DF_METADATA_EVENT,
                "kind is DF_DATA_EVENT or DF_METADATA_EVENT");
  return DFTracerEventType{T::category, static_cast<int>(T::layer), T::kind,
                           T::kind == DF_METADATA_EVENT || T::aggregatable};
}
}  // namespace dftracer

// A traced region. A DF_DATA_EVENT region is written as one event with its
// duration and args. A DF_METADATA_EVENT region is written as one metadata
// record: with update()/append() fields, a record named `_name` holding them
// as args; without, the plain key/value pair (_name, _cat) as before.
class DFTracer {
  int event_type;  // 0->event  1->metadata
  bool initialized;
  ConstEventNameType name;
  ConstEventNameType cat;
  TraceEventType type;
  TimeResolution start_time;
  dftracer::Metadata* metadata;

  void update_number(const char* key, int64_t value, MetadataType type);
  void update_number(const char* key, uint64_t value, MetadataType type);
  void update_number(const char* key, double value, MetadataType type);

 public:
  DFTracer(ConstEventNameType _name, ConstEventNameType _cat,
           int event_type = DF_DATA_EVENT,
           TraceEventType _type = TraceEventType::TRACE_TYPE_CPP_APP);
  // A region of the given event type.
  DFTracer(ConstEventNameType _name, const DFTracerEventType& event_type);
  template <typename T,
            typename std::enable_if<
                std::is_base_of<dftracer::EventType, T>::value, int>::type = 0>
  DFTracer(ConstEventNameType _name, T)
      : DFTracer(_name, dftracer::event_type_of<T>()) {}

  void update(const char* key, int value,
              MetadataType type = MetadataType::MT_KEY);

  // The string is kept by pointer and must outlive the region.
  void update(const char* key, const char* value,
              MetadataType type = MetadataType::MT_KEY);

  // Any other number: 64-bit integers, unsigned values, doubles; bool is
  // written as 0 or 1.
  template <typename T,
            typename std::enable_if<std::is_arithmetic<T>::value &&
                                        !std::is_same<T, int>::value,
                                    int>::type = 0>
  void update(const char* key, T value,
              MetadataType type = MetadataType::MT_KEY) {
    if (std::is_floating_point<T>::value)
      update_number(key, static_cast<double>(value), type);
    else if (std::is_signed<T>::value)
      update_number(key, static_cast<int64_t>(value), type);
    else
      update_number(key, static_cast<uint64_t>(value), type);
  }

  // Add a value to the list args.<key>, or with `sub` to args.<key>.<sub>
  // (an object of lists, e.g. "relations":{"used":[...]}). Values are kept in
  // the order added and written with the event. Nothing is copied: strings,
  // like sub, must outlive the region, as with update().
  void append(const char* key, const char* value);
  void append(const char* key, int64_t value);
  void append(const char* key, const char* sub, const char* value);
  void append(const char* key, const char* sub, int64_t value);
  // A 64-bit id, written as a string of 16 hex digits (e.g. an entity id).
  void append_id(const char* key, const char* sub, uint64_t id);

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

// Regions of an event type (a struct derived from dftracer::EventType).
#define DFTRACER_CPP_TYPED_FUNCTION(event_type) \
  DFTracer profiler_dft_fn = DFTracer((char*)__FUNCTION__, event_type{});
#define DFTRACER_CPP_TYPED_REGION(event_type, name) \
  DFTracer profiler_##name = DFTracer(#name, event_type{});
#define DFTRACER_CPP_TYPED_REGION_START(event_type, name) \
  DFTracer* profiler_##name = new DFTracer(#name, event_type{});

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

// append(key, value) or append(key, sub, value); see DFTracer::append.
#define DFTRACER_CPP_FUNCTION_APPEND(...) profiler_dft_fn.append(__VA_ARGS__);
#define DFTRACER_CPP_REGION_APPEND(name, ...) \
  profiler_##name.append(__VA_ARGS__);
#define DFTRACER_CPP_REGION_DYN_APPEND(name, ...) \
  profiler_##name->append(__VA_ARGS__);

// A metadata record named `name` whose args are the fields set on it with
// DFTRACER_CPP_REGION_UPDATE / _APPEND before the region ends.
#define DFTRACER_CPP_RECORD(name) \
  DFTracer profiler_##name =      \
      DFTracer(#name, CPP_LOG_CATEGORY, DF_METADATA_EVENT);

extern "C" {
#endif
// C APIs

struct DFTracerData {
  void* profiler;
};

__attribute__((unused)) static ConstEventNameType C_LOG_CATEGORY = "C_APP";
struct DFTracerData* initialize_region(ConstEventNameType name,
                                       ConstEventNameType cat, int event_type);
// A region of the given event type (see DFTracerEventType).
struct DFTracerData* initialize_typed_region(
    ConstEventNameType name, const struct DFTracerEventType* type);
void finalize_region(struct DFTracerData* data);
void finalize_region_cleanup(struct DFTracerData** data);
void update_metadata_int(struct DFTracerData* data, const char* key, int value);
void update_metadata_string(struct DFTracerData* data, const char* key,
                            const char* value);

void update_metadata_int_type(struct DFTracerData* data, const char* key,
                              int value, int type);
void update_metadata_string_type(struct DFTracerData* data, const char* key,
                                 const char* value, int type);

void update_metadata_int64(struct DFTracerData* data, const char* key,
                           int64_t value);
void update_metadata_uint64(struct DFTracerData* data, const char* key,
                            uint64_t value);
void update_metadata_double(struct DFTracerData* data, const char* key,
                            double value);

// Add a value to the list args.<key>, or to args.<key>.<sub> when `sub` is
// not NULL (see DFTracer::append).
void update_metadata_append_string(struct DFTracerData* data, const char* key,
                                   const char* sub, const char* value);
void update_metadata_append_int(struct DFTracerData* data, const char* key,
                                const char* sub, int64_t value);
void update_metadata_append_id(struct DFTracerData* data, const char* key,
                               const char* sub, uint64_t id);

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

// Regions of an event type, a struct DFTracerEventType; end them with
// DFTRACER_C_FUNCTION_END / DFTRACER_C_REGION_END.
#define DFTRACER_C_TYPED_FUNCTION_START(event_type)        \
  struct DFTracerData* data_fn DFTRACER_C_REGION_CLEANUP = \
      initialize_typed_region(__func__, &(event_type));
#define DFTRACER_C_TYPED_REGION_START(event_type, name)        \
  struct DFTracerData* data_##name DFTRACER_C_REGION_CLEANUP = \
      initialize_typed_region(#name, &(event_type));

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

#define DFTRACER_C_FUNCTION_UPDATE_INT64(key, val) \
  update_metadata_int64(data_fn, key, val);
#define DFTRACER_C_FUNCTION_UPDATE_UINT64(key, val) \
  update_metadata_uint64(data_fn, key, val);
#define DFTRACER_C_FUNCTION_UPDATE_DOUBLE(key, val) \
  update_metadata_double(data_fn, key, val);
#define DFTRACER_C_REGION_UPDATE_INT64(name, key, val) \
  update_metadata_int64(data_##name, key, val);
#define DFTRACER_C_REGION_UPDATE_UINT64(name, key, val) \
  update_metadata_uint64(data_##name, key, val);
#define DFTRACER_C_REGION_UPDATE_DOUBLE(name, key, val) \
  update_metadata_double(data_##name, key, val);

// sub is NULL for a plain list args.<key>, or a name for args.<key>.<sub>.
#define DFTRACER_C_FUNCTION_APPEND_STR(key, sub, val) \
  update_metadata_append_string(data_fn, key, sub, val);
#define DFTRACER_C_FUNCTION_APPEND_INT(key, sub, val) \
  update_metadata_append_int(data_fn, key, sub, val);
#define DFTRACER_C_REGION_APPEND_STR(name, key, sub, val) \
  update_metadata_append_string(data_##name, key, sub, val);
#define DFTRACER_C_REGION_APPEND_INT(name, key, sub, val) \
  update_metadata_append_int(data_##name, key, sub, val);

// A metadata record named `name`: start it, set its fields with
// DFTRACER_C_REGION_UPDATE_* / _APPEND_*, and end it to write it.
#define DFTRACER_C_RECORD_START(name)                          \
  struct DFTracerData* data_##name DFTRACER_C_REGION_CLEANUP = \
      initialize_region(#name, C_LOG_CATEGORY, DF_METADATA_EVENT);
#define DFTRACER_C_RECORD_END(name) DFTRACER_C_REGION_END(name)

#ifdef __cplusplus
}
#endif

#endif  // DFTRACER_DFTRACER_H
