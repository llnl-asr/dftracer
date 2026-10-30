#include <cstdio>
#include <lineage/lineage.hpp>
#include <mutex>
#include <unordered_set>

namespace lineage {
namespace {
const char* relation_name(Relation relation) {
  switch (relation) {
    case LINEAGE_USED:
      return "used";
    case LINEAGE_GENERATED:
      return "generated";
    case LINEAGE_UPDATED:
      return "updated";
    case LINEAGE_DERIVED_FROM:
      return "derived_from";
    case LINEAGE_CONTAINS:
      return "contains";
  }
  return "unknown";
}

Id hash(const char* type, const char* key) {
  Id h = 1469598103934665603ULL;  // FNV-1a over type, a separator, key
  for (const char* part : {type, "\x1f", key})
    for (const char* s = part; s != nullptr && *s; ++s)
      h = (h ^ static_cast<unsigned char>(*s)) * 1099511628211ULL;
  return h;
}

// Entities and entity types are written once per process.
bool first_time(Id id) {
  static std::mutex mtx;
  static std::unordered_set<Id> seen;
  std::lock_guard<std::mutex> lock(mtx);
  return seen.insert(id).second;
}

struct Hex {
  char s[17];
  explicit Hex(Id id) {
    std::snprintf(s, sizeof(s), "%016llx", static_cast<unsigned long long>(id));
  }
};
}  // namespace

void entity_type(const char* type, const char* description) {
  if (type == nullptr || !first_time(hash("\x1e", type))) return;
  DFTracer record("LT", RecordType{});
  record.update("type", type);
  record.update("description", description ? description : "");
}

Id entity(const char* type, const char* key, const char* uri) {
  if (type == nullptr || key == nullptr) return 0;
  Id id = hash(type, key);
  if (!first_time(id)) return id;
  Hex hex(id);
  DFTracer record("LE", RecordType{});
  record.update("id", hex.s);
  record.update("type", type);
  record.update("uri", uri ? uri : "");
  return id;
}

void relate(Relation relation, Id subject, Id object) {
  Hex s(subject), o(object);
  DFTracer record("LR", RecordType{});
  record.update("relation", relation_name(relation));
  record.update("subject", s.s);
  record.update("object", o.s);
}

void Activity::relate(Relation relation, Id id) {
  if (relation <= LINEAGE_UPDATED)
    event_.append_id("relations", relation_name(relation), id);
}
}  // namespace lineage

// The C connector.
struct lineage_activity {
  lineage::Activity activity;
  explicit lineage_activity(const char* name) : activity(name) {}
};

extern "C" {
void lineage_entity_type(const char* type, const char* description) {
  lineage::entity_type(type, description);
}
lineage_id lineage_entity(const char* type, const char* key, const char* uri) {
  return lineage::entity(type, key, uri);
}
void lineage_relate(enum lineage_relation relation, lineage_id subject,
                    lineage_id object) {
  lineage::relate(relation, subject, object);
}
struct lineage_activity* lineage_activity_begin(const char* name) {
  return new lineage_activity(name);
}
void lineage_activity_relate(struct lineage_activity* activity,
                             enum lineage_relation relation, lineage_id id) {
  if (activity != nullptr) activity->activity.relate(relation, id);
}
void lineage_activity_end(struct lineage_activity* activity) {
  delete activity;
}
}
