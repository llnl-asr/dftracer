// Adversarial load for the producer path (check_pc_stress.py checks its
// trace; its pc_ twin checks that producer/consumer mode traces the same):
//   long   event names of 1 .. 8000 characters
//   bigval string args of 100 .. 9000 bytes: records larger than the 4 KB
//          scratch buffer and than what a ring block has left
//   many   40 keys per event, the same keys every time
//   fresh  40 new keys per event
//   unique a new name and 8 new keys per event, more distinct strings than
//          a ring block has interning ids
//   addr   one buffer reused for different names (same address, other text)
//   posix  pwrite/pread on files under a long nested directory
// on THREADS threads at once, plus regions left open at finalize and a
// region finalized twice. Usage: test_pc_stress <data dir> [scale]
#include <dftracer/dftracer.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int kThreads = 4;
int scale = 1;
std::string root;

struct Region {
  explicit Region(const char* name)
      : data(initialize_region(name, C_LOG_CATEGORY, DF_DATA_EVENT)) {}
  ~Region() { finalize_region(data); }
  DFTracerData* data;
};

void worker(int t) {
  // long: a name of each length, 3 times (the second and third by
  // reference in producer/consumer mode).
  for (size_t len : {1, 15, 16, 17, 255, 256, 4095, 4096, 5000, 8000}) {
    const std::string name(len, static_cast<char>('a' + t));
    for (int i = 0; i < 3; ++i) Region r(name.c_str());
  }
  // bigval
  std::vector<std::string> values;
  for (int len : {100, 3000, 5000, 9000}) values.emplace_back(len, 'v');
  for (int rep = 0; rep < 3 * scale; ++rep)
    for (const auto& v : values) {
      Region r("bigval");
      update_metadata_string(r.data, "big", v.c_str());
      update_metadata_int(r.data, "len", static_cast<int>(v.size()));
    }
  // many
  std::vector<std::string> keys;
  for (int k = 0; k < 40; ++k) keys.push_back("k" + std::to_string(k));
  for (int e = 0; e < 50 * scale; ++e) {
    Region r("many");
    for (int k = 0; k < 40; ++k)
      update_metadata_int(r.data, keys[k].c_str(), k);
  }
  // fresh
  for (int e = 0; e < 50 * scale; ++e) {
    Region r("fresh");
    for (int k = 0; k < 40; ++k) {
      const std::string key = "f" + std::to_string(t) + "_" +
                              std::to_string(e) + "_" + std::to_string(k);
      update_metadata_int(r.data, key.c_str(), k);
    }
  }
  // unique
  for (int e = 0; e < 5000 * scale; ++e) {
    const std::string name = "u" + std::to_string(t) + "_" + std::to_string(e);
    Region r(name.c_str());
    for (int k = 0; k < 8; ++k) {
      const std::string key = name + "_" + std::to_string(k);
      update_metadata_int(r.data, key.c_str(), k);
    }
  }
  // addr
  char buf[32];
  for (int e = 0; e < 1000 * scale; ++e) {
    std::snprintf(buf, sizeof(buf), "addr%d", e % 7);
    Region r(buf);
  }
  // posix
  char block[256];
  for (size_t i = 0; i < sizeof(block); ++i) block[i] = static_cast<char>(i);
  for (int f = 0; f < 20; ++f) {
    const std::string path =
        root + "/t" + std::to_string(t) + "_" + std::to_string(f) + ".dat";
    const int fd = open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
      std::perror(path.c_str());
      std::exit(1);
    }
    for (int i = 0; i < 5; ++i) {
      if (pwrite(fd, block, sizeof(block), i * 256) != sizeof(block) ||
          pread(fd, block, sizeof(block), i * 256) != sizeof(block)) {
        std::perror("pwrite/pread");
        std::exit(1);
      }
    }
    close(fd);
    unlink(path.c_str());
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <data dir> [scale]\n", argv[0]);
    return 2;
  }
  if (argc > 2) scale = std::atoi(argv[2]);
  // A long nested directory: paths well past std::string's inline buffer.
  root = argv[1];
  for (int d = 0; d < 3; ++d) {
    root += "/pc_stress_" + std::string(60, static_cast<char>('p' + d));
    mkdir(root.c_str(), 0755);
  }
  DFTRACER_C_INIT(nullptr, nullptr, nullptr);
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) threads.emplace_back(worker, t);
  for (auto& th : threads) th.join();
  // Finalized twice: the second call must be ignored.
  DFTracerData* twice =
      initialize_region("twice", C_LOG_CATEGORY, DF_DATA_EVENT);
  finalize_region(twice);
  finalize_region(twice);
  // Left open: finalize closes and logs them.
  for (int i = 0; i < 3; ++i)
    initialize_region("leftover", C_LOG_CATEGORY, DF_DATA_EVENT);
  DFTRACER_C_FINI();
  return 0;
}
