// lineage: a provenance-style API of an external project, written with
// dftracer's event types. The event types are defined here at compile time;
// lineage.h is the C connector.
#ifndef LINEAGE_LINEAGE_HPP
#define LINEAGE_LINEAGE_HPP
#include <dftracer/dftracer.h>
#include <lineage/lineage.h>

namespace lineage {
using Id = lineage_id;
using Relation = lineage_relation;

// Activities: data events on lineage's layer, never aggregated.
struct ActivityType : dftracer::EventType {
  static constexpr const char* category = "lineage";
  static constexpr TraceEventType layer =
      static_cast<TraceEventType>(LINEAGE_LAYER);
  static constexpr bool aggregatable = false;
};
// Entity types, entities and entity relations: metadata records.
struct RecordType : dftracer::EventType {
  static constexpr const char* category = "lineage";
  static constexpr TraceEventType layer = ActivityType::layer;
  static constexpr int kind = DF_METADATA_EVENT;
};

void entity_type(const char* type, const char* description);
Id entity(const char* type, const char* key, const char* uri = nullptr);
void relate(Relation relation, Id subject, Id object);

class Activity {
  DFTracer event_;

 public:
  // name must outlive the activity.
  explicit Activity(const char* name) : event_(name, ActivityType{}) {}
  void relate(Relation relation, Id id);
  void used(Id id) { relate(LINEAGE_USED, id); }
  void generated(Id id) { relate(LINEAGE_GENERATED, id); }
  void updated(Id id) { relate(LINEAGE_UPDATED, id); }
};
}  // namespace lineage
#endif  // LINEAGE_LINEAGE_HPP
