// GzipCompression writes complete gzip members that decompress alone and
// joined, with a deflate block end every kBlockBytes, and
// NodeTraceFile::complete_members() counts only whole members.
#include <dftracer/core/compression/gzip_compression.h>
#include <dftracer/service/pipeline/node_trace_file.h>
#include <isa-l/igzip_lib.h>

#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "check.h"

using namespace dftracer;

namespace {

std::string trace_text(size_t bytes, uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::string s;
  char line[256];
  for (int i = 0; s.size() < bytes; ++i) {
    const int n = std::snprintf(
        line, sizeof(line),
        "{\"id\":%d,\"name\":\"pread\",\"cat\":\"POSIX\",\"pid\":4242,"
        "\"tid\":4243,\"ts\":%llu,\"dur\":%u,\"ph\":\"X\",\"args\":{"
        "\"fhash\":\"%016llx\",\"count\":4096,\"offset\":%llu}}\n",
        i, static_cast<unsigned long long>(1763000000000000ULL + i * 7),
        static_cast<unsigned>(rng() % 50),
        static_cast<unsigned long long>(rng() % 8),
        static_cast<unsigned long long>(i) * 4096);
    s.append(line, static_cast<size_t>(n));
  }
  s.resize(bytes);
  return s;
}

std::string compress(GzipCompression& gz, const std::string& text) {
  // compress() copies the member back into the buffer, which must hold it
  // even for data that does not compress.
  std::vector<char> buf(text.begin(), text.end());
  buf.resize(text.size() + text.size() / 8 + 65536);
  const size_t n = gz.compress(buf.data(), text.size());
  DFT_CHECK(n > 0);
  return std::string(buf.data(), n);
}

// Every member of `z`, decompressed one at a time.
std::string inflate_all(const std::string& z, size_t* members) {
  std::string out;
  std::vector<uint8_t> tmp(1u << 16);
  inflate_state s;
  size_t at = 0;
  *members = 0;
  while (at < z.size()) {
    isal_inflate_init(&s);
    s.crc_flag = ISAL_GZIP;
    s.next_in = reinterpret_cast<uint8_t*>(const_cast<char*>(z.data() + at));
    s.avail_in = static_cast<uint32_t>(z.size() - at);
    while (s.block_state != ISAL_BLOCK_FINISH) {
      s.next_out = tmp.data();
      s.avail_out = static_cast<uint32_t>(tmp.size());
      DFT_CHECK(isal_inflate(&s) == ISAL_DECOMP_OK);
      out.append(reinterpret_cast<char*>(tmp.data()), tmp.size() - s.avail_out);
    }
    at = static_cast<size_t>(reinterpret_cast<const char*>(s.next_in) -
                             z.data());
    ++*members;
  }
  return out;
}

// A SYNC_FLUSH block end leaves the empty stored block 00 00 ff ff.
size_t sync_marks(const std::string& z) {
  size_t n = 0;
  for (size_t i = 0; i + 4 <= z.size(); ++i)
    n += z.compare(i, 4, std::string("\x00\x00\xff\xff", 4)) == 0;
  return n;
}

void test_members(int level) {
  GzipCompression gz;
  DFT_CHECK(gz.initialize(4u << 20, level) == 0);
  std::string all_text, all_z;
  const size_t sizes[] = {0,
                          1,
                          100,
                          GzipCompression::kBlockBytes,
                          GzipCompression::kBlockBytes + 1,
                          4u << 20,
                          (4u << 20) + 12345};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    const std::string text = trace_text(sizes[i], i + 1);
    const std::string z = compress(gz, text);
    size_t members = 0;
    DFT_CHECK(inflate_all(z, &members) == text);
    DFT_CHECK(members == 1);
    // One block end per kBlockBytes of input but the last.
    const size_t blocks = (text.size() + GzipCompression::kBlockBytes - 1) /
                          GzipCompression::kBlockBytes;
    DFT_CHECK(sync_marks(z) + 1 >= blocks);
    all_text += text;
    all_z += z;
  }
  size_t members = 0;
  DFT_CHECK(inflate_all(all_z, &members) == all_text);
  DFT_CHECK(members == sizeof(sizes) / sizeof(sizes[0]));
  DFT_CHECK(NodeTraceFile::complete_members(all_z.data(), all_z.size()) ==
            all_z.size());
}

void test_incompressible() {
  GzipCompression gz;
  DFT_CHECK(gz.initialize(1u << 20) == 0);
  std::mt19937_64 rng(9);
  std::string text(3u << 20, '\0');
  for (char& c : text) c = static_cast<char>(rng());
  const std::string z = compress(gz, text);
  size_t members = 0;
  DFT_CHECK(inflate_all(z, &members) == text);
}

void test_cut_member() {
  GzipCompression gz;
  DFT_CHECK(gz.initialize(1u << 20) == 0);
  const std::string a = compress(gz, trace_text(1u << 20, 3));
  const std::string b = compress(gz, trace_text(1u << 20, 4));
  const std::string both = a + b;
  for (size_t cut : {size_t{1}, size_t{8}, b.size() / 2, b.size() - 1}) {
    const std::string partial = both.substr(0, a.size() + cut);
    DFT_CHECK(NodeTraceFile::complete_members(partial.data(), partial.size()) ==
              a.size());
  }
  DFT_CHECK(NodeTraceFile::complete_members(both.data(), both.size()) ==
            both.size());
}

}  // namespace

int main() {
  test_members(1);
  test_members(0);
  test_members(5);
  test_incompressible();
  test_cut_member();
  std::printf("test_gzip_compression passed\n");
  return 0;
}
