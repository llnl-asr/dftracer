#ifndef DFTRACER_IPC_SHM_LAYOUT_H
#define DFTRACER_IPC_SHM_LAYOUT_H

#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <time.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dftracer {
namespace ipc {

/**
 * Memory layout of the segment shared between traced processes (producers)
 * and dftracer_service (the consumer). See docs/design_pc_service.rst.
 *
 *   SegmentHeader | BlockControl[block_count] | (page aligned)
 * data[block_count][block_size]
 *
 * Everything in here is POD plus std::atomic of 32/64-bit integers. Those are
 * lock-free and address-free on x86-64 and aarch64 (asserted below), which is
 * what makes the layout valid when every process maps it at a different
 * address. Nothing in the segment is a pointer. The structures themselves
 * (SegmentHeader, BlockControl, RecordHeader) are in
 * core/common/datastructure.h.
 *
 * The data area is one ring of fixed-size blocks, and producers allocate whole
 * blocks, not records. A producer claims the block that `head` names with one
 * CAS, fills it with records using plain stores into memory no other producer
 * touches, and publishes it with one store of `committed` when it is full (or
 * flushed). The consumer takes published blocks in order. Per record there is
 * no shared write at all, which is what lets the aggregate rate grow with the
 * number of producers: a shared per-record fetch_add caps the whole node at
 * one cache-line transfer per record.
 */

inline uint64_t pack_record_word(uint32_t epoch, uint32_t length,
                                 uint8_t state) {
  return (static_cast<uint64_t>(epoch) << 32) |
         (static_cast<uint64_t>(length) << 8) | state;
}
inline uint32_t record_epoch(uint64_t word) {
  return static_cast<uint32_t>(word >> 32);
}
inline uint32_t record_length(uint64_t word) {
  return static_cast<uint32_t>(word >> 8) & 0xffffffu;
}
inline uint8_t record_state(uint64_t word) {
  return static_cast<uint8_t>(word & 0xffu);
}

inline uint32_t align_record(size_t bytes) {
  return static_cast<uint32_t>((bytes + DFT_SHM_RECORD_ALIGN - 1) &
                               ~static_cast<size_t>(DFT_SHM_RECORD_ALIGN - 1));
}

/** BlockControl::committed of a block published in lap `epoch`. */
inline uint64_t published_word(uint32_t epoch, uint32_t block_size) {
  return (static_cast<uint64_t>(epoch) << 32) | block_size;
}
/** The same once a drain thread has claimed it. */
inline uint64_t claimed_word(uint32_t epoch, uint32_t block_size) {
  return published_word(epoch, block_size) | DFT_SHM_BLOCK_CLAIMED;
}
/** BlockControl::owner. */
inline uint64_t owner_word(uint32_t epoch, ShmBlockOwner who) {
  return (static_cast<uint64_t>(epoch) << 8) | who;
}

inline uint64_t monotonic_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

/**
 * Typed view over a mapped segment. Cheap to copy; holds no ownership. Both
 * sides build one from the base address of their own mapping.
 */
class RingView {
 public:
  RingView() = default;

  static bool valid_geometry(uint32_t block_count, uint32_t block_size) {
    auto pow2 = [](uint32_t v) { return v != 0 && (v & (v - 1)) == 0; };
    return pow2(block_count) && block_count >= 2 && pow2(block_size) &&
           block_size >= 4096 && block_size <= (1u << 24);
  }

  static size_t control_offset() {
    return (sizeof(SegmentHeader) + DFT_SHM_CACHE_LINE - 1) &
           ~(DFT_SHM_CACHE_LINE - 1);
  }

  static size_t data_offset(uint32_t block_count) {
    size_t end = control_offset() + sizeof(BlockControl) * block_count;
    return (end + DFT_SHM_DATA_ALIGN - 1) & ~(DFT_SHM_DATA_ALIGN - 1);
  }

  static size_t bytes_for(uint32_t block_count, uint32_t block_size) {
    return data_offset(block_count) +
           static_cast<size_t>(block_count) * block_size;
  }

  /**
   * Shrink a valid geometry until the segment takes at most `room` bytes:
   * fewer blocks first, then smaller ones, down to DFT_SHM_MIN_BLOCKS of
   * DFT_SHM_MIN_BLOCK_SIZE (never below what was asked). False if even that
   * does not fit; the geometry is then the smallest one tried.
   */
  static bool fit(uint32_t& block_count, uint32_t& block_size, uint64_t room) {
    while (bytes_for(block_count, block_size) > room &&
           block_count > DFT_SHM_MIN_BLOCKS)
      block_count /= 2;
    while (bytes_for(block_count, block_size) > room &&
           block_size > DFT_SHM_MIN_BLOCK_SIZE)
      block_size /= 2;
    return bytes_for(block_count, block_size) <= room;
  }

  /**
   * Initialize a freshly created segment (service side only). Every block is
   * armed for its first lap (epoch 1), `head` and `tail` start at 0, and the
   * state is left at INITIALIZING; the caller flips it to ACCEPTING once it
   * is ready to drain.
   */
  static RingView format(void* base, uint32_t block_count,
                         uint32_t block_size) {
    auto* header = static_cast<SegmentHeader*>(base);
    std::memset(base, 0, data_offset(block_count));
    header->magic = DFT_SHM_SEGMENT_MAGIC;
    header->abi_version = DFT_SHM_ABI_VERSION;
    header->block_count = block_count;
    header->block_size = block_size;
    header->block_shift = static_cast<uint32_t>(__builtin_ctz(block_count));
    header->state.store(SHM_SEGMENT_INITIALIZING, std::memory_order_relaxed);
    header->head.store(0, std::memory_order_relaxed);
    header->tail.store(0, std::memory_order_relaxed);
    header->dropped.store(0, std::memory_order_relaxed);
    header->spilled.store(0, std::memory_order_relaxed);
    RingView view(base);
    for (uint32_t i = 0; i < block_count; ++i) {
      view.blocks_[i].armed.store(view.epoch(i), std::memory_order_relaxed);
      view.blocks_[i].claim_ns.store(0, std::memory_order_relaxed);
      view.blocks_[i].owner_pid.store(0, std::memory_order_relaxed);
      view.blocks_[i].owner_tid.store(0, std::memory_order_relaxed);
      view.blocks_[i].committed.store(0, std::memory_order_relaxed);
      view.blocks_[i].owner.store(0, std::memory_order_relaxed);
    }
    std::atomic_thread_fence(std::memory_order_release);
    return view;
  }

  explicit RingView(void* base)
      : header_(static_cast<SegmentHeader*>(base)),
        blocks_(reinterpret_cast<BlockControl*>(static_cast<uint8_t*>(base) +
                                                control_offset())),
        data_(static_cast<uint8_t*>(base) + data_offset(header_->block_count)),
        block_count_(header_->block_count),
        block_size_(header_->block_size),
        block_shift_(header_->block_shift),
        mask_(header_->block_count - 1) {}

  SegmentHeader* header() const { return header_; }
  uint32_t block_count() const { return block_count_; }
  uint32_t block_size() const { return block_size_; }

  BlockControl& block(uint64_t seq) const { return blocks_[seq & mask_]; }
  uint8_t* block_data(uint64_t seq) const {
    return data_ + static_cast<size_t>(seq & mask_) * block_size_;
  }
  /**
   * Epoch of block sequence number `seq`: its lap around the ring, plus one.
   * Starting at 1 rather than 0 matters: a fresh segment is all zeros, and a
   * zero header word must not read as "written in this lap" during the first
   * lap. Wraps after 2^32 laps, far beyond any service lifetime.
   */
  uint32_t epoch(uint64_t seq) const {
    return static_cast<uint32_t>(seq >> block_shift_) + 1;
  }

 private:
  SegmentHeader* header_ = nullptr;
  BlockControl* blocks_ = nullptr;
  uint8_t* data_ = nullptr;
  uint32_t block_count_ = 0;
  uint32_t block_size_ = 0;
  uint32_t block_shift_ = 0;
  uint64_t mask_ = 0;
};

/**
 * Write a header marking [at, at + length) as padding. Only ever done inside a
 * block the caller owns, before it is published, so a relaxed store suffices.
 */
inline void write_pad(uint8_t* at, uint32_t epoch, uint32_t length,
                      uint32_t pid) {
  auto* hdr = reinterpret_cast<RecordHeader*>(at);
  hdr->pid = pid;
  hdr->kind = SHM_KIND_PAD;
  hdr->flags = 0;
  hdr->word.store(pack_record_word(epoch, length, SHM_RECORD_PAD),
                  std::memory_order_relaxed);
}

}  // namespace ipc
}  // namespace dftracer

#endif  // DFTRACER_IPC_SHM_LAYOUT_H
