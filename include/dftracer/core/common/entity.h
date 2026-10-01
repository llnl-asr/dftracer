#ifndef DFTRACER_CORE_COMMON_ENTITY_H
#define DFTRACER_CORE_COMMON_ENTITY_H
/*
 * Entities and relations -- shared definitions for the C, C++ and Python APIs.
 *
 * An entity is a typed instance of something an application works with (a
 * file, a dataset shard, a checkpoint, a protein structure, a GPU buffer).
 *
 *   identity   EntityID         64-bit FNV-1a of (type, key); serialized as
 *                               exactly 16 lowercase hex characters.
 *   type       fixed-length string (DFT_ENTITY_TYPE_LEN), application-defined
 *   store      EntityStore enum   -- where the instance lives
 *   uri        fixed-length string (DFT_ENTITY_URI_LEN), unique within the app
 *   role       EntityRole enum     -- per entity TYPE (input/output/...)
 *   description fixed-length string (DFT_ENTITY_DESC_LEN), per entity TYPE
 *   relation   EntityRelation enum -- event->entity and entity->entity
 *
 * Enums are serialized as integers. Strings are copied into fixed buffers,
 * truncated, and restricted to a JSON-safe character set (dft_entity_sanitize)
 * so records never need escaping.
 *
 * Trace records (metadata, like the FH/SH file/string hash records):
 *   EH  args: {"id":"<16-hex>","type":"<type>","store":<int>,"uri":"<uri>"}
 *   ET  args: {"type":"<type>","role":<int>,"description":"<text>"}
 *   ER  args: {"relation":<int>,"subject":"<16-hex>","object":"<16-hex>"}
 * Event->entity relations are event args keyed by dft_relation_name(rel)
 * whose value is a JSON array of 16-hex ids.
 */
#include <dftracer/core/common/typedef.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed string capacities, including the terminating NUL. */
#define DFT_ENTITY_TYPE_LEN 32
#define DFT_ENTITY_URI_LEN 256
#define DFT_ENTITY_DESC_LEN 256
/* 16 hex digits + NUL. */
#define DFT_ENTITY_HEX_LEN 17

#define DFT_ENTITY_NONE ((EntityID)0)

/* Where an entity instance lives. */
typedef enum {
  DFT_STORE_MEMORY = 0,
  DFT_STORE_GPU_MEMORY = 1,
  DFT_STORE_LOCAL_DISK = 2,
  DFT_STORE_PARALLEL_FS = 3,
  DFT_STORE_BURST_BUFFER = 4,
  DFT_STORE_OBJECT_STORE = 5,
  DFT_STORE_DATABASE = 6,
  DFT_STORE_NETWORK = 7,
  DFT_STORE_OTHER = 8
} EntityStore;

/* Role of an entity TYPE in the workflow. */
typedef enum {
  DFT_ROLE_UNKNOWN = 0,
  DFT_ROLE_INPUT = 1,        /* enters the workflow from outside */
  DFT_ROLE_OUTPUT = 2,       /* a deliverable / scientific artifact */
  DFT_ROLE_INTERMEDIATE = 3, /* produced and consumed inside the workflow */
  DFT_ROLE_PARAMETER = 4,    /* configuration, seeds, model settings */
  DFT_ROLE_REFERENCE = 5     /* versioned reference data (databases, weights) */
} EntityRole;

/* Relations. Values < 16 relate an EVENT to an entity (event args); values
 * >= 16 relate two ENTITIES (ER records). */
typedef enum {
  /* event -> entity (W3C PROV style) */
  DFT_REL_USED = 0,        /* the event consumed the entity (cause) */
  DFT_REL_GENERATED = 1,   /* the event produced the entity (effect) */
  DFT_REL_INVALIDATED = 2, /* the event ended the entity (delete, consume) */
  DFT_REL_UPDATED = 3,     /* the event modified the entity in place */
  /* entity -> entity (class-style) */
  DFT_REL_DERIVED_FROM = 16,      /* subject was derived from object */
  DFT_REL_REVISION_OF = 17,       /* subject is a newer version of object */
  DFT_REL_CONTAINS = 18,          /* subject has object as a member (has-a) */
  DFT_REL_PART_OF = 19,           /* subject is part of object (is-in) */
  DFT_REL_SPECIALIZATION_OF = 20, /* subject is a kind of object (inherits) */
  DFT_REL_ALTERNATE_OF = 21,      /* same thing, another representation */
  DFT_REL_DEPENDS_ON = 22         /* subject requires object */
} EntityRelation;

static inline int dft_relation_is_event(EntityRelation r) { return r < 16; }

/* Event arg holding an event's relations:
 *   "relations": {"used":["<16-hex>",...], "generated":[...], ...}
 * One nested object, so relation names never collide with other event
 * args (the lifecycle "end" event has its own "used" object). */
#define DFT_RELATIONS_ARG "relations"

/* Stable relation names (part of the trace format). */
static inline const char* dft_relation_name(EntityRelation r) {
  switch (r) {
    case DFT_REL_USED:
      return "used";
    case DFT_REL_GENERATED:
      return "generated";
    case DFT_REL_INVALIDATED:
      return "invalidated";
    case DFT_REL_UPDATED:
      return "updated";
    case DFT_REL_DERIVED_FROM:
      return "derived_from";
    case DFT_REL_REVISION_OF:
      return "revision_of";
    case DFT_REL_CONTAINS:
      return "contains";
    case DFT_REL_PART_OF:
      return "part_of";
    case DFT_REL_SPECIALIZATION_OF:
      return "specialization_of";
    case DFT_REL_ALTERNATE_OF:
      return "alternate_of";
    case DFT_REL_DEPENDS_ON:
      return "depends_on";
  }
  return "related";
}

/* JSON-safe character set; everything else (quotes, backslash, control
 * characters, '|', ...) becomes '_', so fields never need escaping. */
static inline int dft_entity_safe_char(char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9'))
    return 1;
  switch (c) {
    case '_':
    case '-':
    case '.':
    case ':':
    case '/':
    case '@':
    case '+':
    case '=':
    case ',':
    case '~':
    case ' ':
      return 1;
    default:
      return 0;
  }
}

/* Copy src into dst[cap] (truncating) with the safe character set. */
static inline void dft_entity_sanitize(char* dst, size_t cap, const char* src) {
  size_t i = 0;
  if (cap == 0) return;
  if (src != NULL)
    for (; src[i] != '\0' && i + 1 < cap; ++i)
      dst[i] = dft_entity_safe_char(src[i]) ? src[i] : '_';
  dst[i] = '\0';
}

/* 64-bit FNV-1a of type, a 0x1f separator, and key. Same value in every
 * language and process; 0 is reserved for DFT_ENTITY_NONE. */
static inline EntityID dft_entity_hash(const char* type, const char* key) {
  uint64_t h = 1469598103934665603ULL;
  const unsigned char* p;
  for (p = (const unsigned char*)(type ? type : ""); *p; ++p) {
    h ^= *p;
    h *= 1099511628211ULL;
  }
  h ^= 0x1fu;
  h *= 1099511628211ULL;
  for (p = (const unsigned char*)(key ? key : ""); *p; ++p) {
    h ^= *p;
    h *= 1099511628211ULL;
  }
  return h == DFT_ENTITY_NONE ? 1 : h;
}

/* Render an id as exactly 16 lowercase hex digits into out[DFT_ENTITY_HEX_LEN].
 */
static inline void dft_entity_hex(EntityID id, char* out) {
  static const char digits[] = "0123456789abcdef";
  int i;
  for (i = 15; i >= 0; --i) {
    out[i] = digits[id & 0xfu];
    id >>= 4;
  }
  out[16] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* DFTRACER_CORE_COMMON_ENTITY_H */
