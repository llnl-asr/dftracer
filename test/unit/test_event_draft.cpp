// EventDraft keeps Metadata::insert_or_assign order: a key set again keeps
// its place, and a value of a type Metadata does not store removes the key.
#include <dftracer/core/common/event_draft.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "check.h"

using namespace dftracer;

namespace {

struct Arg {
  std::string key;
  uint8_t tag;
  std::string value;
};

std::vector<Arg> args_of(const EventDraft& d) {
  std::vector<Arg> out;
  const uint8_t* p = d.data();
  const uint8_t* end = p + d.size();
  while (p < end) {
    uint16_t key_len;
    std::memcpy(&key_len, p + 2, sizeof(key_len));
    Arg a{std::string(reinterpret_cast<const char*>(p + 4), key_len), p[0], {}};
    p += 4 + key_len;
    if (a.tag == SHM_VALUE_STRING || a.tag == SHM_VALUE_RAW_JSON) {
      uint32_t n;
      std::memcpy(&n, p, sizeof(n));
      a.value.assign(reinterpret_cast<const char*>(p + sizeof(n)), n);
      p += sizeof(n) + n;
    } else {
      int64_t v;
      std::memcpy(&v, p, sizeof(v));
      a.value = std::to_string(v);
      p += 8;
    }
    out.push_back(std::move(a));
  }
  DFT_CHECK(p == end);
  return out;
}

std::vector<std::string> keys_of(const EventDraft& d) {
  std::vector<std::string> keys;
  for (const auto& a : args_of(d)) keys.push_back(a.key);
  return keys;
}

struct NotStored {};

}  // namespace

int main() {
  EventDraft d;
  d.reset();
  d.set("a", 1);
  d.set("b", std::string("x"));
  d.set("c", 3);
  DFT_CHECK(d.count() == 3);
  DFT_CHECK((keys_of(d) == std::vector<std::string>{"a", "b", "c"}));

  d.set("b", 7);
  auto args = args_of(d);
  DFT_CHECK(d.count() == 3);
  DFT_CHECK((keys_of(d) == std::vector<std::string>{"a", "b", "c"}));
  DFT_CHECK(args[1].tag == SHM_VALUE_INT && args[1].value == "7");

  d.set("a", "longer string value");
  d.set("c", 9);
  args = args_of(d);
  DFT_CHECK((keys_of(d) == std::vector<std::string>{"a", "b", "c"}));
  DFT_CHECK(args[0].value == "longer string value");
  DFT_CHECK(args[2].value == "9");

  d.set("b", NotStored{});
  DFT_CHECK(d.count() == 2);
  DFT_CHECK((keys_of(d) == std::vector<std::string>{"a", "c"}));

  // More keys than the 64 bits that skip the search, so some collide.
  d.reset();
  std::vector<std::string> keys;
  for (int i = 0; i < 100; ++i) keys.push_back("key" + std::to_string(i));
  for (int i = 0; i < 100; ++i) d.set(keys[i], i);
  DFT_CHECK(d.count() == 100);
  DFT_CHECK(keys_of(d) == keys);
  d.set(keys[40], std::string("replaced"));
  args = args_of(d);
  DFT_CHECK(d.count() == 100);
  DFT_CHECK(keys_of(d) == keys);
  DFT_CHECK(args[40].value == "replaced");
  DFT_CHECK(args[41].value == "41");

  d.reset();
  DFT_CHECK(d.empty() && d.size() == 0);
  d.set("a", 1);
  DFT_CHECK((keys_of(d) == std::vector<std::string>{"a"}));
  return 0;
}
