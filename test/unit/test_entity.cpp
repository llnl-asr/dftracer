// Unit tests for the entity helpers in core/common/entity.h (no tracer):
// hashing, hex rendering, sanitizing and relation names are part of the trace
// format and must stay stable across languages and releases.
#include <dftracer/core/common/entity.h>

#include <cstring>
#include <iostream>
#include <string>

#include "check.h"

static void test_hash() {
  std::cout << "entity hash..." << std::endl;
  // Deterministic and order-sensitive.
  DFT_CHECK(dft_entity_hash("sequence", "MGYP0001") ==
            dft_entity_hash("sequence", "MGYP0001"));
  DFT_CHECK(dft_entity_hash("sequence", "MGYP0001") !=
            dft_entity_hash("MGYP0001", "sequence"));
  // The 0x1f separator keeps (type, key) splits distinct.
  DFT_CHECK(dft_entity_hash("ab", "c") != dft_entity_hash("a", "bc"));
  // 0 is reserved for DFT_ENTITY_NONE.
  DFT_CHECK(dft_entity_hash("", "") != DFT_ENTITY_NONE);
  DFT_CHECK(dft_entity_hash(nullptr, nullptr) == dft_entity_hash("", ""));
  // Pinned value: FNV-1a-64 of "protein_structure" 0x1f "MGYP0001". Python,
  // C and C++ producers must all emit this id for this pair.
  char hex[DFT_ENTITY_HEX_LEN];
  dft_entity_hex(dft_entity_hash("protein_structure", "MGYP0001"), hex);
  DFT_CHECK(std::string(hex) == "8f0de68c04eea0bf");
}

static void test_hex() {
  std::cout << "entity hex..." << std::endl;
  char hex[DFT_ENTITY_HEX_LEN];
  dft_entity_hex(0x1ULL, hex);
  DFT_CHECK(std::string(hex) == "0000000000000001");
  dft_entity_hex(0xffffffffffffffffULL, hex);
  DFT_CHECK(std::string(hex) == "ffffffffffffffff");
  DFT_CHECK(std::strlen(hex) == 16);
}

static void test_sanitize() {
  std::cout << "entity sanitize..." << std::endl;
  char out[DFT_ENTITY_TYPE_LEN];
  dft_entity_sanitize(out, sizeof(out), "relaxed_structure");
  DFT_CHECK(std::string(out) == "relaxed_structure");
  // JSON-unsafe characters are replaced, never escaped.
  dft_entity_sanitize(out, sizeof(out), "a\"b\\c|d\ne");
  DFT_CHECK(std::string(out) == "a_b_c_d_e");
  // Truncated to capacity - 1 characters plus NUL.
  std::string longname(100, 'x');
  dft_entity_sanitize(out, sizeof(out), longname.c_str());
  DFT_CHECK(std::strlen(out) == DFT_ENTITY_TYPE_LEN - 1);
  // NULL source gives an empty string.
  dft_entity_sanitize(out, sizeof(out), nullptr);
  DFT_CHECK(out[0] == '\0');
  // Paths keep their separators.
  char uri[DFT_ENTITY_URI_LEN];
  dft_entity_sanitize(uri, sizeof(uri), "/p/lustre5/run/out-1.tar");
  DFT_CHECK(std::string(uri) == "/p/lustre5/run/out-1.tar");
}

static void test_relations() {
  std::cout << "entity relations..." << std::endl;
  DFT_CHECK(dft_relation_is_event(DFT_REL_USED));
  DFT_CHECK(dft_relation_is_event(DFT_REL_UPDATED));
  DFT_CHECK(!dft_relation_is_event(DFT_REL_CONTAINS));
  DFT_CHECK(std::string(dft_relation_name(DFT_REL_USED)) == "used");
  DFT_CHECK(std::string(dft_relation_name(DFT_REL_GENERATED)) == "generated");
  DFT_CHECK(std::string(dft_relation_name(DFT_REL_INVALIDATED)) ==
            "invalidated");
  DFT_CHECK(std::string(dft_relation_name(DFT_REL_CONTAINS)) == "contains");
  DFT_CHECK(std::string(dft_relation_name(DFT_REL_SPECIALIZATION_OF)) ==
            "specialization_of");
  // Enum values are part of the trace format.
  DFT_CHECK(DFT_REL_DERIVED_FROM == 16 && DFT_REL_DEPENDS_ON == 22);
  DFT_CHECK(DFT_STORE_BURST_BUFFER == 4 && DFT_ROLE_OUTPUT == 2);
}

int main() {
  test_hash();
  test_hex();
  test_sanitize();
  test_relations();
  std::cout << "entity unit tests passed" << std::endl;
  return 0;
}
