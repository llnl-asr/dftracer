/* Entity/relation C API: declare typed entities, relate them to events and to
 * each other. Checked by test/check_entity_trace.py. */
#include <dftracer/dftracer.h>
#include <stdio.h>

static void transform(int i) {
  char key[32];
  DFTRACER_C_FUNCTION_START();
  snprintf(key, sizeof(key), "sample-%d", i);
  DFTRACER_C_FUNCTION_USES("raw_sample", key);
  DFTRACER_C_FUNCTION_GENERATES("protein_structure", "MGYP0001");
  DFTRACER_C_FUNCTION_END();
}

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;
  DFTRACER_C_INIT(NULL, NULL, NULL);
  DFTRACER_C_ENTITY_TYPE("raw_sample", DFT_ROLE_INPUT, "An input sample");
  DFTRACER_C_ENTITY_TYPE("protein_structure", DFT_ROLE_OUTPUT,
                         "A predicted \"structure\" | with bad chars");
  for (int i = 0; i < 3; ++i) transform(i);
  {
    EntityID archive = DFTRACER_C_ENTITY("archive", "a.tar",
                                         DFT_STORE_PARALLEL_FS, "/p/out/a.tar");
    EntityID member = DFTRACER_C_ENTITY("protein_structure", "MGYP0001",
                                        DFT_STORE_MEMORY, NULL);
    DFTRACER_C_REGION_START(pack);
    DFTRACER_C_REGION_RELATE(pack, DFT_REL_GENERATED, archive);
    DFTRACER_C_REGION_RELATE(pack, DFT_REL_USED, member);
    DFTRACER_C_REGION_END(pack);
    DFTRACER_C_ENTITY_RELATE(DFT_REL_CONTAINS, archive, member);
    /* An event relation is not an entity relation: ignored. */
    DFTRACER_C_ENTITY_RELATE(DFT_REL_USED, archive, member);
  }
  DFTRACER_C_FINI();
  return 0;
}
