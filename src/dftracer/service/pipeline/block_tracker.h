#ifndef DFTRACER_SERVICE_PIPELINE_BLOCK_TRACKER_H
#define DFTRACER_SERVICE_PIPELINE_BLOCK_TRACKER_H

#include <dftracer/core/ipc/shm_layout.h>
#include <signal.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <memory>

namespace dftracer {
namespace ipc {

/**
 * The consumer's half of the ring protocol: find out when the oldest block is
 * complete (published by the producer that claimed it), and hand it back to
 * producers once it has been taken. Flushing a quiet producer's partly filled
 * block is that producer's job (RingProducer::flush_if_older): the block is
 * private to it until published.
 *
 * Single-threaded: exactly one BlockTracker per segment, used from one thread
 * at a time. Blocks are completed and released strictly in sequence order.
 *
 * It does not take over blocks that are held without being published: a
 * producer that keeps its block stalls the tail. The service uses
 * ParallelBlockClaimer, which does.
 */
class BlockTracker {
 public:
  /**
   * With `verify`, poll() also walks every record header of a block it reports
   * complete and checks epoch, state and length (counted in corrupt()). That
   * walk is slow -- a chain of dependent loads -- so it is for tests only.
   */
  explicit BlockTracker(RingView ring, bool verify = false)
      : ring_(ring),
        tail_(ring.header()->tail.load(std::memory_order_acquire)),
        verify_(verify) {}

  /**
   * True if the oldest unreleased block has every byte committed; its sequence
   * number is returned in `seq`. One acquire load of the block's `committed`
   * count, which also makes every record in it visible to this thread.
   */
  bool poll(uint64_t& seq) {
    const uint64_t head = ring_.header()->head.load(std::memory_order_acquire);
    // Every block before `head` has been claimed, though not necessarily
    // published yet.
    if (tail_ >= head) return false;
    if (ring_.block(tail_).committed.load(std::memory_order_acquire) !=
        published_word(ring_.epoch(tail_), ring_.block_size()))
      return false;
    if (verify_ && !verify_block(tail_)) return false;
    seq = tail_;
    return true;
  }

  /**
   * Re-arm block `seq` (which must be the one poll() just returned) for its
   * next lap, letting producers claim it again. Call only after the
   * block's contents have been copied out or are no longer needed.
   */
  void release(uint64_t seq) {
    BlockControl& block = ring_.block(seq);
    block.claim_ns.store(0, std::memory_order_relaxed);
    block.committed.store(0, std::memory_order_relaxed);
    block.owner.store(0, std::memory_order_relaxed);
    // Release: the reset above, and every read of this block's contents, happen
    // before any producer can see the block armed for its next lap.
    block.armed.store(ring_.epoch(seq) + 1, std::memory_order_release);
    tail_ = seq + 1;
    ring_.header()->tail.store(tail_, std::memory_order_release);
  }

  uint64_t tail() const { return tail_; }
  uint64_t backlog_blocks() const {
    return ring_.header()->head.load(std::memory_order_acquire) - tail_;
  }
  /** verify mode: complete blocks whose headers did not check out. */
  uint64_t corrupt() const { return corrupt_; }
  /** The most recent such header word, and where it was found. */
  uint64_t bad_word() const { return bad_word_; }
  uint64_t bad_seq() const { return bad_seq_; }
  uint32_t bad_offset() const { return bad_offset_; }

 private:
  // Walk the headers of a block whose `committed` count is complete. Every
  // header must carry this lap's epoch, a final state and a sane length, and
  // the lengths must tile the block exactly.
  bool verify_block(uint64_t seq) {
    const uint32_t block_size = ring_.block_size();
    const uint32_t epoch = ring_.epoch(seq);
    const uint8_t* base = ring_.block_data(seq);
    uint32_t offset = 0;
    while (offset < block_size) {
      const auto* hdr = reinterpret_cast<const RecordHeader*>(base + offset);
      const uint64_t word = hdr->word.load(std::memory_order_acquire);
      const uint8_t state = record_state(word);
      const uint32_t length = record_length(word);
      if (record_epoch(word) != epoch ||
          (state != SHM_RECORD_COMMITTED && state != SHM_RECORD_PAD) ||
          length < sizeof(RecordHeader) || length % DFT_SHM_RECORD_ALIGN != 0 ||
          offset + length > block_size) {
        ++corrupt_;
        bad_word_ = word;
        bad_seq_ = seq;
        bad_offset_ = offset;
        return false;
      }
      offset += length;
    }
    return true;
  }

  RingView ring_;
  uint64_t tail_;
  bool verify_;
  uint64_t corrupt_ = 0;
  uint64_t bad_word_ = 0;
  uint64_t bad_seq_ = 0;
  uint32_t bad_offset_ = 0;
};

/**
 * Multi-threaded counterpart of BlockTracker, for a consumer that processes
 * several blocks at once. Any number of threads call claim(), take_over() and
 * release() concurrently.
 *
 * claim() hands out complete blocks oldest first, passing over blocks that
 * their producers still hold: one CAS on the block's own `committed` word, so
 * claims do not contend on a shared cursor, and a quiet producer does not hold
 * back the blocks after it. take_over() drains such a held block once it is
 * old enough, so it does not hold back producers either (they claim strictly
 * in order, and would otherwise wait for it on the next lap).
 *
 * Releasing out of order is safe because producers claim blocks strictly in
 * order: a block released early just waits, armed, until `head` reaches it.
 */
class ParallelBlockClaimer {
 public:
  explicit ParallelBlockClaimer(RingView ring)
      : ring_(ring),
        mask_(ring.block_count() - 1),
        next_(ring.header()->tail.load(std::memory_order_acquire)),
        tail_(next_.load(std::memory_order_relaxed)),
        released_(new std::atomic<uint64_t>[ring.block_count()]) {
    for (uint32_t i = 0; i < ring.block_count(); ++i)
      released_[i].store(0, std::memory_order_relaxed);
  }

  /** Claim the oldest complete block that no other thread has claimed. */
  bool claim(uint64_t& seq) {
    const uint64_t head = ring_.header()->head.load(std::memory_order_acquire);
    for (uint64_t s = advance(head); s < head; ++s) {
      std::atomic<uint64_t>& committed = ring_.block(s).committed;
      const uint64_t complete =
          published_word(ring_.epoch(s), ring_.block_size());
      uint64_t seen = committed.load(std::memory_order_relaxed);
      // Acquire: makes the block's records visible to this thread.
      if (seen == complete &&
          committed.compare_exchange_strong(
              seen, claimed_word(ring_.epoch(s), ring_.block_size()),
              std::memory_order_acquire, std::memory_order_relaxed)) {
        seq = s;
        return true;
      }
    }
    return false;
  }

  /**
   * Take over the oldest block that its producer has held unpublished since
   * `older_than_ns` or earlier: IDLE -> TAKEN, or WRITING -> TAKEN when the
   * producer is dead. The caller then drains it with for_each_record(...,
   * salvage = true) and releases it like a claimed block. `pid` is the
   * producer that held it.
   */
  bool take_over(uint64_t& seq, uint64_t older_than_ns, uint32_t& pid) {
    const uint64_t head = ring_.header()->head.load(std::memory_order_acquire);
    for (uint64_t s = advance(head); s < head; ++s) {
      BlockControl& block = ring_.block(s);
      const uint32_t epoch = ring_.epoch(s);
      // Acquire: claim_ns and the owner ids of this lap, if it is this lap.
      uint64_t owner = block.owner.load(std::memory_order_acquire);
      if (owner != owner_word(epoch, SHM_OWNER_IDLE) &&
          owner != owner_word(epoch, SHM_OWNER_WRITING))
        continue;
      const uint64_t claimed_at =
          block.claim_ns.load(std::memory_order_relaxed);
      if (claimed_at == 0 || claimed_at > older_than_ns) continue;
      const uint32_t owner_pid =
          block.owner_pid.load(std::memory_order_relaxed);
      // A record is being written: wait for it, unless nobody is left to
      // finish it.
      if (owner == owner_word(epoch, SHM_OWNER_WRITING) && alive(owner_pid))
        continue;
      // Acquire: pairs with the producer's release of IDLE, so every record it
      // committed is visible.
      if (block.owner.compare_exchange_strong(
              owner, owner_word(epoch, SHM_OWNER_TAKEN),
              std::memory_order_acquire, std::memory_order_relaxed)) {
        seq = s;
        pid = owner_pid;
        return true;
      }
    }
    return false;
  }

  /** Re-arm block `seq` for its next lap once its contents are no longer
   * needed. */
  void release(uint64_t seq) {
    BlockControl& block = ring_.block(seq);
    block.claim_ns.store(0, std::memory_order_relaxed);
    block.committed.store(0, std::memory_order_relaxed);
    block.owner.store(0, std::memory_order_relaxed);
    block.armed.store(ring_.epoch(seq) + 1, std::memory_order_release);
    // Advance the published tail over every block released so far in order.
    // Diagnostics only; producers never read it.
    //
    // Sequentially consistent on purpose: a thread releasing t+1 stores its
    // mark and then reads `tail_`, while one releasing t moves `tail_` and then
    // reads that mark. With acquire/release alone each can miss the other's
    // write, and the tail stops one block short until the next release.
    released_[seq & mask_].store(seq + 1, std::memory_order_seq_cst);
    uint64_t t = tail_.load(std::memory_order_seq_cst);
    //
    // `> t`, not `== t + 1`: blocks are released out of order, so the slot of
    // block t may already hold the mark of t + block_count -- which producers
    // could only have claimed after t itself was released.
    while (released_[t & mask_].load(std::memory_order_seq_cst) > t) {
      if (tail_.compare_exchange_weak(t, t + 1, std::memory_order_seq_cst)) {
        ++t;
        ring_.header()->tail.store(t, std::memory_order_release);
      }
    }
  }

  /** Complete blocks no thread has claimed yet. */
  uint64_t ready_blocks() {
    const uint64_t head = ring_.header()->head.load(std::memory_order_acquire);
    uint64_t ready = 0;
    for (uint64_t s = advance(head); s < head; ++s) {
      if (ring_.block(s).committed.load(std::memory_order_relaxed) ==
          published_word(ring_.epoch(s), ring_.block_size()))
        ++ready;
    }
    return ready;
  }

  uint64_t backlog_blocks() const {
    return ring_.header()->head.load(std::memory_order_acquire) -
           tail_.load(std::memory_order_acquire);
  }
  /** Oldest block not yet released, and oldest not yet claimed or taken. */
  uint64_t tail() const { return tail_.load(std::memory_order_acquire); }
  uint64_t next() const { return next_.load(std::memory_order_acquire); }

 private:
  static bool alive(uint32_t pid) {
    return pid == 0 || kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH;
  }

  /**
   * True once block `seq` is no longer waiting for a drain thread in its lap:
   * claimed, taken over, or already released. A stale `false` only makes a
   * scan start earlier than it needs to.
   */
  bool done(uint64_t seq) const {
    const BlockControl& block = ring_.block(seq);
    const uint32_t epoch = ring_.epoch(seq);
    if (block.armed.load(std::memory_order_acquire) != epoch) return true;
    if (block.committed.load(std::memory_order_acquire) ==
        claimed_word(epoch, ring_.block_size()))
      return true;
    return block.owner.load(std::memory_order_acquire) ==
           owner_word(epoch, SHM_OWNER_TAKEN);
  }

  /** Move the scan start past blocks that are done; returns it. */
  uint64_t advance(uint64_t head) {
    uint64_t s = next_.load(std::memory_order_acquire);
    while (s < head && done(s)) {
      if (next_.compare_exchange_weak(s, s + 1, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
        ++s;
    }
    return s;
  }

  RingView ring_;
  uint64_t mask_;
  alignas(64) std::atomic<uint64_t> next_;
  alignas(64) std::atomic<uint64_t> tail_;
  // Per block slot: seq + 1 of the latest release of any lap, so the tail can
  // advance over blocks that finished out of order.
  std::unique_ptr<std::atomic<uint64_t>[]> released_;
};

/**
 * Call fn(header, payload, payload_bytes) for every committed record of block
 * `seq`, in ring order. Payload length is the record length minus the header,
 * so it includes up to 15 bytes of alignment slack.
 *
 * A claimed block is complete: its records and padding tile it. A block taken
 * over from its producer (`salvage`) ends wherever the producer stopped: the
 * walk stops at the first header that is not of this lap. A reservation that
 * was never committed (its producer died writing it) is skipped by its length.
 * Returns the number of those.
 */
template <typename Fn>
inline uint32_t for_each_record(const RingView& ring, uint64_t seq, Fn&& fn,
                                bool salvage = false) {
  const uint32_t block_size = ring.block_size();
  const uint32_t epoch = ring.epoch(seq);
  const uint8_t* base = ring.block_data(seq);
  uint32_t offset = 0;
  uint32_t uncommitted = 0;
  while (offset + sizeof(RecordHeader) <= block_size) {
    const auto* hdr = reinterpret_cast<const RecordHeader*>(base + offset);
    const uint64_t word = hdr->word.load(std::memory_order_relaxed);
    const uint32_t length = record_length(word);
    const uint8_t state = record_state(word);
    if (salvage &&
        (record_epoch(word) != epoch ||
         (state != SHM_RECORD_COMMITTED && state != SHM_RECORD_PAD) ||
         length < sizeof(RecordHeader) || length % DFT_SHM_RECORD_ALIGN != 0 ||
         offset + length > block_size))
      break;
    if (state == SHM_RECORD_COMMITTED) {
      fn(*hdr, base + offset + sizeof(RecordHeader),
         length - static_cast<uint32_t>(sizeof(RecordHeader)));
    } else if (salvage && hdr->kind != SHM_KIND_PAD) {
      ++uncommitted;
    }
    offset += length;
  }
  return uncommitted;
}

}  // namespace ipc
}  // namespace dftracer

#endif  // DFTRACER_SERVICE_PIPELINE_BLOCK_TRACKER_H
