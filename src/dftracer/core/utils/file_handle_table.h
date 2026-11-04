#ifndef DFTRACER_UTILS_FILE_HANDLE_TABLE_H
#define DFTRACER_UTILS_FILE_HANDLE_TABLE_H

#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/typedef.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace dftracer {

// The file hash of each traced FILE*, shared by every thread without a lock.
// A fixed array of slots, allocated once with calloc, each holding an atomic
// key (the FILE* address) and an atomic hash. A key goes from empty to a
// FILE*, then to a tombstone when the stream is closed, and a tombstone can
// take a new FILE*. A slot is never empty again, so a lookup stops at the
// first empty slot. A stream whose probe window is full stays untraced.
class FileHandleTable {
 public:
  static constexpr size_t kSlotBits = 16;
  static constexpr size_t kSlots = size_t{1} << kSlotBits;
  static constexpr size_t kProbe = 16;

  FileHandleTable()
      : slots_(static_cast<Slot*>(calloc(kSlots, sizeof(Slot)))), live_(0) {}
  ~FileHandleTable() { free(slots_); }
  FileHandleTable(const FileHandleTable&) = delete;
  FileHandleTable& operator=(const FileHandleTable&) = delete;

  // Returns false when the table could not be allocated or the window is
  // full. A FILE* already in the table gets the new hash.
  bool insert(const void* fh, HashType hash) {
    if (slots_ == nullptr || fh == nullptr) return false;
    const uintptr_t key = reinterpret_cast<uintptr_t>(fh);
    if (Slot* slot = find_slot(key)) {
      slot->hash.store(hash, std::memory_order_release);
      return true;
    }
    const size_t first = home_slot(key);
    for (size_t i = 0; i < kProbe; ++i) {
      Slot& slot = slots_[(first + i) & (kSlots - 1)];
      uintptr_t current = slot.key.load(std::memory_order_relaxed);
      while (current == kEmpty || current == kTombstone) {
        if (slot.key.compare_exchange_weak(current, key,
                                           std::memory_order_acq_rel,
                                           std::memory_order_relaxed)) {
          slot.hash.store(hash, std::memory_order_release);
          live_.fetch_add(1, std::memory_order_relaxed);
          return true;
        }
      }
    }
    return false;
  }

  HashType find(const void* fh) const {
    if (slots_ == nullptr || fh == nullptr) return NO_HASH_DEFAULT;
    Slot* slot = find_slot(reinterpret_cast<uintptr_t>(fh));
    return slot == nullptr ? NO_HASH_DEFAULT
                           : slot->hash.load(std::memory_order_acquire);
  }

  void erase(const void* fh) {
    if (slots_ == nullptr || fh == nullptr) return;
    Slot* slot = find_slot(reinterpret_cast<uintptr_t>(fh));
    if (slot == nullptr) return;
    slot->hash.store(NO_HASH_DEFAULT, std::memory_order_relaxed);
    slot->key.store(kTombstone, std::memory_order_release);
    live_.fetch_sub(1, std::memory_order_relaxed);
  }

  // The first slot probed for a key. malloc aligns a FILE to 16 bytes and
  // std::hash on a pointer is the pointer, so the slot comes from the top
  // bits of a multiplicative hash instead.
  static size_t home_slot(uintptr_t key) {
    return static_cast<size_t>(
        (static_cast<uint64_t>(key) * 0x9E3779B97F4A7C15ull) >>
        (64 - kSlotBits));
  }

  bool empty() const { return live_.load(std::memory_order_relaxed) == 0; }

 private:
  struct Slot {
    std::atomic<uintptr_t> key;
    std::atomic<HashType> hash;
  };
  static_assert(std::atomic<uintptr_t>::is_always_lock_free &&
                    std::atomic<HashType>::is_always_lock_free,
                "calloc'd slots need lock-free atomics");
  static constexpr uintptr_t kEmpty = 0;
  static constexpr uintptr_t kTombstone = 1;

  Slot* find_slot(uintptr_t key) const {
    const size_t first = home_slot(key);
    for (size_t i = 0; i < kProbe; ++i) {
      Slot& slot = slots_[(first + i) & (kSlots - 1)];
      uintptr_t current = slot.key.load(std::memory_order_acquire);
      if (current == key) return &slot;
      if (current == kEmpty) return nullptr;
    }
    return nullptr;
  }

  Slot* slots_;
  std::atomic<long> live_;
};

}  // namespace dftracer

#endif  // DFTRACER_UTILS_FILE_HANDLE_TABLE_H
