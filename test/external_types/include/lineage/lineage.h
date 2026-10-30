/* lineage: a provenance-style API of an external project, written with
 * dftracer's event types (the C connector of lineage.hpp).
 *
 * Entities are (type, key) pairs with a 64-bit id. Entity types (LT records)
 * and entities (LE records) are written once per process; relations between
 * entities are LR records; activities are events on the lineage layer, never
 * aggregated, with args.relations.{used,generated,updated}. */
#ifndef LINEAGE_LINEAGE_H
#define LINEAGE_LINEAGE_H
#include <dftracer/dftracer.h>
#include <stdint.h>

/* lineage's own layer (the "type" column). */
#define LINEAGE_LAYER (DFTRACER_TRACE_TYPE_EXTERNAL + 2)

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t lineage_id;

enum lineage_relation {
  /* activity -> entity */
  LINEAGE_USED = 0,
  LINEAGE_GENERATED = 1,
  LINEAGE_UPDATED = 2,
  /* entity -> entity */
  LINEAGE_DERIVED_FROM = 3,
  LINEAGE_CONTAINS = 4
};

void lineage_entity_type(const char* type, const char* description);
/* uri may be NULL. */
lineage_id lineage_entity(const char* type, const char* key, const char* uri);
/* An entity -> entity relation. */
void lineage_relate(enum lineage_relation relation, lineage_id subject,
                    lineage_id object);

/* An activity: one event from begin to end. name must outlive it. */
struct lineage_activity;
struct lineage_activity* lineage_activity_begin(const char* name);
/* An activity -> entity relation (USED, GENERATED or UPDATED). */
void lineage_activity_relate(struct lineage_activity* activity,
                             enum lineage_relation relation, lineage_id id);
void lineage_activity_end(struct lineage_activity* activity);

#ifdef __cplusplus
}
#endif
#endif /* LINEAGE_LINEAGE_H */
