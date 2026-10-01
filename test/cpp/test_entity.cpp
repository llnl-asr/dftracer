// Entity/relation C++ API: DFTracer::uses/generates/relate and the C++
// macros. Uses the same (type, key) pairs as test/c/test_entity.c so both
// traces must agree on entity ids. Checked by test/check_entity_trace.py.
#include <dftracer/dftracer.h>

#include <string>

static void transform(int i) {
  DFTRACER_CPP_FUNCTION();
  std::string key = "sample-" + std::to_string(i);
  DFTRACER_CPP_FUNCTION_USES("raw_sample", key.c_str());
  DFTRACER_CPP_FUNCTION_GENERATES("protein_structure", "MGYP0001");
}

int main() {
  DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
  DFTRACER_CPP_ENTITY_TYPE("raw_sample", DFT_ROLE_INPUT, "An input sample");
  DFTRACER_CPP_ENTITY_TYPE("protein_structure", DFT_ROLE_OUTPUT,
                           "A predicted \"structure\" | with bad chars");
  for (int i = 0; i < 3; ++i) transform(i);
  {
    DFTracer pack("pack", CPP_LOG_CATEGORY);
    EntityID archive = pack.relate(DFT_REL_GENERATED, "archive", "a.tar",
                                   DFT_STORE_PARALLEL_FS, "/p/out/a.tar");
    EntityID member = pack.uses("protein_structure", "MGYP0001");
    DFTRACER_CPP_ENTITY_RELATE(DFT_REL_CONTAINS, archive, member);
    // An entity relation is not an event relation: ignored on the event.
    pack.relate(DFT_REL_CONTAINS, member);
  }
  DFTRACER_CPP_FINI();
  return 0;
}
