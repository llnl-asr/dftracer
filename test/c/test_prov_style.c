/* A provenance-style API defined in C on top of dftracer's public API only --
 * event types, records with fields and lists. Run with aggregation on;
 * checked by test/check_prov_style_trace.py. */
#include <dftracer/dftracer.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Activities are data events on a layer of their own that is never
 * aggregated; entities, entity types and relations are metadata records. */
#define PROV_LAYER (DFTRACER_TRACE_TYPE_EXTERNAL + 1)
static const struct DFTracerEventType PROV_ACTIVITY = {"lineage", PROV_LAYER,
                                                       DF_DATA_EVENT, 0};
static const struct DFTracerEventType PROV_RECORD = {"lineage", PROV_LAYER,
                                                     DF_METADATA_EVENT, 1};

typedef uint64_t prov_id;

/* Each entity and type is written once per process (single-threaded test). */
static prov_id seen[64];
static int n_seen;
static int first_time(prov_id id) {
  int i;
  for (i = 0; i < n_seen; ++i)
    if (seen[i] == id) return 0;
  seen[n_seen++] = id;
  return 1;
}

static prov_id prov_hash(const char* type, const char* key) {
  prov_id h = 1469598103934665603ULL; /* FNV-1a */
  const char* parts[3];
  int p;
  parts[0] = type;
  parts[1] = "\x1f";
  parts[2] = key;
  for (p = 0; p < 3; ++p) {
    const char* s;
    for (s = parts[p]; *s; ++s) h = (h ^ (unsigned char)*s) * 1099511628211ULL;
  }
  return h;
}

static void prov_entity_type(const char* type) {
  if (!first_time(prov_hash(type, ""))) return;
  {
    struct DFTracerData* r = initialize_typed_region("LT", &PROV_RECORD);
    update_metadata_string(r, "type", type);
    finalize_region(r);
  }
}

static prov_id prov_entity(const char* type, const char* key) {
  prov_id id = prov_hash(type, key);
  char hex[17];
  struct DFTracerData* r;
  if (!first_time(id)) return id;
  snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)id);
  r = initialize_typed_region("LE", &PROV_RECORD);
  update_metadata_string(r, "id", hex);
  update_metadata_string(r, "type", type);
  finalize_region(r);
  return id;
}

static void prov_derived_from(prov_id subject, prov_id object) {
  char s[17], o[17];
  struct DFTracerData* r;
  snprintf(s, sizeof(s), "%016llx", (unsigned long long)subject);
  snprintf(o, sizeof(o), "%016llx", (unsigned long long)object);
  r = initialize_typed_region("LR", &PROV_RECORD);
  update_metadata_string(r, "subject", s);
  update_metadata_string(r, "object", o);
  finalize_region(r);
}

int main(int argc, char* argv[]) {
  int i;
  (void)argc;
  (void)argv;
  DFTRACER_C_INIT(NULL, NULL, NULL);
  prov_entity_type("dataset");
  prov_entity_type("dataset"); /* once per process */
  for (i = 0; i < 10; ++i) {
    char in[16], out[16];
    prov_id src, dst;
    snprintf(in, sizeof(in), "in%d", i % 3);
    snprintf(out, sizeof(out), "out%d", i);
    src = prov_entity("dataset", in);
    dst = prov_entity("dataset", out);
    {
      DFTRACER_C_TYPED_REGION_START(PROV_ACTIVITY, activity);
      update_metadata_append_id(data_activity, "relations", "used", src);
      update_metadata_append_id(data_activity, "relations", "generated", dst);
      DFTRACER_C_REGION_END(activity);
    }
    prov_derived_from(dst, src);
    {
      DFTRACER_C_REGION_START(work);
      DFTRACER_C_REGION_END(work);
    }
  }
  DFTRACER_C_FINI();
  return 0;
}
