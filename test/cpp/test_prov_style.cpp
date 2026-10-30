// A provenance-style API defined on top of dftracer's public API only -- event
// types, records with fields and lists -- the way moirai builds its own. Run
// with aggregation on; checked by test/check_prov_style_trace.py.
#include <dftracer/dftracer.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_set>

namespace prov {
// Activities are data events on a layer of their own that is never
// aggregated; entities, entity types and relations are metadata records.
struct Activity : dftracer::EventType {
  static constexpr const char* category = "lineage";
  static constexpr TraceEventType layer =
      static_cast<TraceEventType>(DFTRACER_TRACE_TYPE_EXTERNAL + 1);
  static constexpr bool aggregatable = false;
};
struct Record : dftracer::EventType {
  static constexpr const char* category = "lineage";
  static constexpr TraceEventType layer = Activity::layer;
  static constexpr int kind = DF_METADATA_EVENT;
};

using Id = uint64_t;

// Each entity and type is written once per process.
static bool first_time(Id id) {
  static std::mutex mtx;
  static std::unordered_set<Id> seen;
  std::lock_guard<std::mutex> lock(mtx);
  return seen.insert(id).second;
}

static Id hash(const char* type, const char* key) {
  Id h = 1469598103934665603ULL;  // FNV-1a
  for (const char* s : {type, "\x1f", key})
    for (; *s; ++s) h = (h ^ static_cast<unsigned char>(*s)) * 1099511628211ULL;
  return h;
}

static void hex(Id id, char out[17]) {
  snprintf(out, 17, "%016llx", (unsigned long long)id);
}

void entity_type(const char* type) {
  if (!first_time(hash(type, ""))) return;
  DFTracer record("LT", Record{});
  record.update("type", type);
}

Id entity(const char* type, const char* key) {
  Id id = hash(type, key);
  if (!first_time(id)) return id;
  char h[17];
  hex(id, h);
  DFTracer record("LE", Record{});
  record.update("id", h);
  record.update("type", type);
  return id;
}

void derived_from(Id subject, Id object) {
  char s[17], o[17];
  hex(subject, s);
  hex(object, o);
  DFTracer record("LR", Record{});
  record.update("subject", s);
  record.update("object", o);
}

class Scope {
  DFTracer event;

 public:
  explicit Scope(const char* name) : event(name, Activity{}) {}
  void used(Id id) { event.append_id("relations", "used", id); }
  void generated(Id id) { event.append_id("relations", "generated", id); }
};
}  // namespace prov

int main() {
  DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
  prov::entity_type("dataset");
  prov::entity_type("dataset");  // once per process
  for (int i = 0; i < 10; ++i) {
    std::string in = "in" + std::to_string(i % 3);
    std::string out = "out" + std::to_string(i);
    prov::Id src = prov::entity("dataset", in.c_str());
    prov::Id dst = prov::entity("dataset", out.c_str());
    {
      prov::Scope activity("activity");
      activity.used(src);
      activity.generated(dst);
    }
    prov::derived_from(dst, src);
    DFTRACER_CPP_REGION(work);
  }
  DFTRACER_CPP_FINI();
  return 0;
}
