// The compile-time line formatter (serialization/line_format.h), and the
// exact bytes JsonLines writes for each kind of line: they must not change
// when the way a line is built does. Metadata has one key where it appears,
// since a Metadata is an unordered_map and its order is not fixed.
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/serialization/line_format.h>

#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "check.h"

using namespace dftracer;

namespace {
DFTRACER_LINE_FORMAT(Plain, "no placeholders");
DFTRACER_LINE_FORMAT(Json, R"({"a":{},"b":"{}","c":{}})");
DFTRACER_LINE_FORMAT(Literal, "x{{}}y{}");
DFTRACER_LINE_FORMAT(Leading, "{}-{}");

// The pieces are split at compile time.
static_assert(line_format::Compiled<Plain>::holes == 0);
static_assert(line_format::Compiled<Json>::holes == 3);
static_assert(line_format::Compiled<Json>::piece<0>() == R"({"a":)");
static_assert(line_format::Compiled<Json>::piece<1>() == R"(,"b":")");
static_assert(line_format::Compiled<Json>::piece<3>() == "}");
static_assert(line_format::Compiled<Literal>::holes == 1);
static_assert(line_format::Compiled<Literal>::piece<0>() == "x{}y");
static_assert(line_format::Compiled<Leading>::piece<0>().empty());

template <typename F, typename... A>
std::string format(const A&... a) {
  static char buf[DFTRACER_SERIALIZATION_EVENT_MAX + 16];
  line_format::Line w(buf);
  line_format::append<F>(w, a...);
  return std::string(buf, w.size());
}

void test_formatter() {
  DFT_CHECK(format<Plain>() == "no placeholders");
  DFT_CHECK(format<Json>(-7, "str", 18446744073709551615ULL) ==
            R"({"a":-7,"b":"str","c":18446744073709551615})");
  DFT_CHECK(format<Json>(0, std::string("s"), 1u) ==
            R"({"a":0,"b":"s","c":1})");
  DFT_CHECK(format<Json>(1, std::string_view("v"), 2L) ==
            R"({"a":1,"b":"v","c":2})");
  const char* null_str = nullptr;
  DFT_CHECK(format<Json>(1, null_str, 2) == R"({"a":1,"b":"","c":2})");
  DFT_CHECK(format<Literal>(5) == "x{}y5");
  DFT_CHECK(format<Leading>(1, 2) == "1-2");

  // A line longer than the limit is cut, and still ends in a newline.
  static char buf[DFTRACER_SERIALIZATION_EVENT_MAX + 16];
  std::string big(DFTRACER_SERIALIZATION_EVENT_MAX * 2, 'z');
  line_format::Line w(buf);
  line_format::append<Json>(w, 1, big, 2);
  DFT_CHECK(w.cut());
  const size_t n = w.finish("test", 0);
  DFT_CHECK(n == DFTRACER_SERIALIZATION_EVENT_MAX - 1);
  DFT_CHECK(buf[n - 1] == '\n' && buf[n] == '\0');
  std::cout << "✓ line_format" << std::endl;
}

std::string line(const char* buf, size_t n) { return std::string(buf, n); }

void test_json_lines_bytes() {
  JsonLines js;
  js.set_include_metadata(true);
  char host[] = "hh";
  js.initialize(nullptr, host);
  static char buf[DFTRACER_SERIALIZATION_EVENT_MAX + 16];

  size_t n =
      js.data(buf, 3, "read", "POSIX", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
              20, nullptr, 42, 7);
  DFT_CHECK(line(buf, n) ==
            "{\"id\":3,\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,"
            "\"pid\":42,\"tid\":7,\"ts\":100,\"dur\":20,\"ph\":1}\n");

  auto* m = new Metadata();
  m->insert_or_assign("size", static_cast<int64_t>(4096));
  n = js.data(buf, 4, "write", "POSIX", TraceEventType::TRACE_TYPE_LIBC_IO, 5,
              6, m, 42, 7);
  DFT_CHECK(line(buf, n) ==
            "{\"id\":4,\"name\":\"write\",\"cat\":\"POSIX\",\"type\":3,"
            "\"pid\":42,\"tid\":7,\"ts\":5,\"dur\":6,\"ph\":1,"
            "\"args\":{\"hhash\":\"hh\",\"size\":4096}}\n");

  m = new Metadata();
  m->insert_or_assign("fname", std::string("/a/b"));
  n = js.counter(buf, 1, "mem", "PAPI", TraceEventType::TRACE_TYPE_PAPI, 9, 42,
                 7, m);
  DFT_CHECK(line(buf, n) ==
            "{\"name\":\"mem\",\"cat\":\"PAPI\",\"type\":11,\"ts\":9,"
            "\"ph\":2,\"pid\":42,\"tid\":7,"
            "\"args\":{\"hhash\":\"hh\",\"fname\":\"/a/b\"}}\n");
  n = js.counter(buf, 1, "mem", "PAPI", TraceEventType::TRACE_TYPE_PAPI, 9, 42,
                 7, nullptr);
  DFT_CHECK(line(buf, n) ==
            "{\"name\":\"mem\",\"cat\":\"PAPI\",\"type\":11,\"ts\":9,"
            "\"ph\":2,\"pid\":42,\"tid\":7}\n");

  n = js.metadata(buf, "/a/b", "f00d", "FH",
                  TraceEventType::TRACE_TYPE_DFTRACER, 42, 7, true);
  DFT_CHECK(line(buf, n) ==
            "{\"name\":\"FH\",\"cat\":\"dftracer\",\"type\":1,\"pid\":42,"
            "\"tid\":7,\"ph\":4,\"args\":{\"hhash\":\"hh\",\"name\":\"/a/b\","
            "\"value\":\"f00d\"}}\n");
  n = js.metadata(buf, "rank", "3", "PR", TraceEventType::TRACE_TYPE_DFTRACER,
                  42, 7, false);
  DFT_CHECK(line(buf, n) ==
            "{\"name\":\"PR\",\"cat\":\"dftracer\",\"type\":1,\"pid\":42,"
            "\"tid\":7,\"ph\":4,\"args\":{\"hhash\":\"hh\",\"name\":\"rank\","
            "\"value\":3}}\n");

  n = js.record(buf, "FS", "\"level\":\"file\"",
                TraceEventType::TRACE_TYPE_DFTRACER, 42, 7);
  DFT_CHECK(line(buf, n) ==
            "{\"name\":\"FS\",\"cat\":\"dftracer\",\"type\":1,\"pid\":42,"
            "\"tid\":7,\"ph\":4,\"args\":{\"hhash\":\"hh\","
            "\"level\":\"file\"}}\n");
  Metadata fields;
  fields.insert_or_assign("level", std::string("dir"));
  n = js.record(buf, "FS", &fields, TraceEventType::TRACE_TYPE_DFTRACER, 42, 7);
  DFT_CHECK(line(buf, n) ==
            "{\"name\":\"FS\",\"cat\":\"dftracer\",\"type\":1,\"pid\":42,"
            "\"tid\":7,\"ph\":4,\"args\":{\"hhash\":\"hh\","
            "\"level\":\"dir\"}}\n");

  // Without DFTRACER_INC_METADATA an event keeps no optional args.
  js.set_include_metadata(false);
  m = new Metadata();
  m->insert_or_assign("size", static_cast<int64_t>(1));
  n = js.data(buf, 5, "read", "POSIX", TraceEventType::TRACE_TYPE_LIBC_IO, 1, 2,
              m, 42, 7);
  DFT_CHECK(line(buf, n) ==
            "{\"id\":5,\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,"
            "\"pid\":42,\"tid\":7,\"ts\":1,\"dur\":2,\"ph\":1}\n");
  std::cout << "✓ JsonLines bytes" << std::endl;
}
}  // namespace

int main() {
  test_formatter();
  test_json_lines_bytes();
  return 0;
}
