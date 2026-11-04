// The lock-free FILE* table of the STDIO interceptors (issue #396): lookup,
// overwrite, removal and reuse of a slot, a full probe window, and many
// threads that add, look up and remove streams whose addresses malloc hands
// from one thread to the next.

#include <dftracer/core/utils/file_handle_table.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include "check.h"

using dftracer::FileHandleTable;

namespace {

HashType hash_of(uintptr_t v) { return reinterpret_cast<HashType>(v); }
const void* key_of(uintptr_t v) { return reinterpret_cast<const void*>(v); }

void test_basic() {
  auto table = std::make_unique<FileHandleTable>();
  DFT_CHECK(table->empty());
  DFT_CHECK(table->find(nullptr) == NO_HASH_DEFAULT);
  DFT_CHECK(!table->insert(nullptr, hash_of(1)));

  const void* a = key_of(0x1000);
  const void* b = key_of(0x2000);
  DFT_CHECK(table->find(a) == NO_HASH_DEFAULT);
  DFT_CHECK(table->insert(a, hash_of(10)));
  DFT_CHECK(table->insert(b, hash_of(20)));
  DFT_CHECK(table->find(a) == hash_of(10));
  DFT_CHECK(table->find(b) == hash_of(20));
  DFT_CHECK(!table->empty());

  // freopen traces the same FILE* again with a new hash.
  DFT_CHECK(table->insert(a, hash_of(11)));
  DFT_CHECK(table->find(a) == hash_of(11));

  table->erase(a);
  DFT_CHECK(table->find(a) == NO_HASH_DEFAULT);
  DFT_CHECK(table->find(b) == hash_of(20));
  table->erase(a);  // a second erase is a no-op
  table->erase(b);
  DFT_CHECK(table->empty());

  // The address comes back from malloc for another file.
  DFT_CHECK(table->insert(a, hash_of(12)));
  DFT_CHECK(table->find(a) == hash_of(12));
  table->erase(a);
  DFT_CHECK(table->empty());
}

// Keys that share one home slot fill the probe window; one more is refused
// and stays untraced, and an erase frees a slot for it.
void test_full_window() {
  auto table = std::make_unique<FileHandleTable>();
  std::vector<uintptr_t> keys;
  const size_t home = FileHandleTable::home_slot(0x10);
  for (uintptr_t k = 0x10; keys.size() <= FileHandleTable::kProbe; k += 0x10)
    if (FileHandleTable::home_slot(k) == home) keys.push_back(k);

  for (size_t i = 0; i < FileHandleTable::kProbe; ++i)
    DFT_CHECK(table->insert(key_of(keys[i]), hash_of(100 + i)));
  const void* extra = key_of(keys.back());
  DFT_CHECK(!table->insert(extra, hash_of(999)));
  DFT_CHECK(table->find(extra) == NO_HASH_DEFAULT);
  for (size_t i = 0; i < FileHandleTable::kProbe; ++i)
    DFT_CHECK(table->find(key_of(keys[i])) == hash_of(100 + i));

  // Erase a key in the middle of the window: the keys after it must still be
  // found past the tombstone, and the extra key can now claim it.
  table->erase(key_of(keys[3]));
  DFT_CHECK(table->find(key_of(keys[3])) == NO_HASH_DEFAULT);
  DFT_CHECK(table->find(key_of(keys[10])) == hash_of(110));
  DFT_CHECK(table->insert(extra, hash_of(999)));
  DFT_CHECK(table->find(extra) == hash_of(999));
}

// Each thread allocates a block, traces it, looks it up, untraces it and
// frees it. malloc gives a freed block to the next caller, often another
// thread, so slots are claimed, cleared and claimed again concurrently, as
// with fopen and fclose. A std::unordered_map here corrupts or crashes.
void test_threads() {
  constexpr int kThreads = 8;
  constexpr int kRounds = 200000;
  auto table = std::make_unique<FileHandleTable>();
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int r = 0; r < kRounds; ++r) {
        void* fp = malloc(216);  // sizeof(FILE) on glibc
        HashType mine = hash_of((static_cast<uintptr_t>(t) << 32) | (r + 1));
        if (!table->insert(fp, mine) || table->find(fp) != mine)
          failures.fetch_add(1);
        table->erase(fp);
        if (table->find(fp) != NO_HASH_DEFAULT) failures.fetch_add(1);
        free(fp);
      }
    });
  }
  for (auto& th : threads) th.join();
  DFT_CHECK(failures.load() == 0);
  DFT_CHECK(table->empty());
}

}  // namespace

int main() {
  test_basic();
  test_full_window();
  test_threads();
  return 0;
}
