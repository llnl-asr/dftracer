#ifndef DFTRACER_IPC_RING_PRODUCER_H
#define DFTRACER_IPC_RING_PRODUCER_H

#include <dftracer/core/ipc/shm_layout.h>
#include <sched.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace dftracer {
namespace ipc {

inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
  _mm_pause();
#elif defined(__aarch64__)
  asm volatile("yield" ::: "memory");
#endif
}

/**
 * Lock-free producer side of the ring. Any number of threads in any number of
 * processes may use their own RingProducer on the same segment concurrently;
 * one RingProducer belongs to one thread.
 *
 * Space is allocated a block at a time. The producer claims the block that
 * `head` names with one CAS, then places records in it with plain stores to
 * memory that only it writes, and publishes the whole block with one release
 * store of its `committed` count when the next record does not fit or on
 * flush(). Per record that is a header store and the payload copy -- nothing
 * any other core reads or writes -- so producers do not slow each other down.
 * The shared `head` line is touched once per block.
 *
 * Records become visible to the consumer only when their block is published,
 * so a producer that goes quiet must call flush() (or flush_if_older() from a
 * timer); until it does, its block also holds back the release of every later
 * block. The destructor flushes.
 *
 * The service may take over a block that its producer keeps without
 * publishing (an idle thread, a dead process): it drains the committed records
 * itself. So that it never reads a record being written, the producer marks
 * the block WRITING from reserve to commit (one uncontended CAS and one store
 * on a line nothing else touches); the service can only take an IDLE block.
 * A producer that finds its block taken drops it and claims another; nothing
 * is lost.
 *
 * At most one reservation may be outstanding: commit it before the next
 * try_reserve() or flush(). A reservation that is never committed is left as
 * padding, and the block cannot be taken over while the producer lives. Not
 * async-signal-safe with respect to the same RingProducer.
 */
class RingProducer {
 public:
  using Result = ShmReserveResult;

  RingProducer() = default;
  RingProducer(RingView ring, uint32_t pid, uint32_t tid = 0)
      : ring_(ring), pid_(pid), tid_(tid) {}
  ~RingProducer() { flush(); }

  RingProducer(const RingProducer&) = delete;
  RingProducer& operator=(const RingProducer&) = delete;

  /**
   * Reserve room for `payload_bytes`. On OK, `out.payload` points at that many
   * writable bytes, which stay the caller's until commit(). FULL means no free
   * block could be claimed (the consumer has not released the next one yet);
   * this producer then holds no block and the call may simply be retried.
   */
  Result try_reserve(size_t payload_bytes, ShmRecordKind kind,
                     Reservation& out) {
    const uint32_t block_size = ring_.block_size();
    if (payload_bytes > DFT_SHM_MAX_RECORD_LENGTH - sizeof(RecordHeader))
      return Result::TOO_LARGE;
    const uint32_t length = align_record(sizeof(RecordHeader) + payload_bytes);
    if (length > block_size) return Result::TOO_LARGE;

    for (;;) {
      if (data_ == nullptr || used_ + length > block_size) {
        publish();
        if (!claim()) return Result::FULL;
      }
      if (begin_write()) break;
      drop();  // taken over by the service, which drains what is in it
    }
    auto* hdr = reinterpret_cast<RecordHeader*>(data_ + used_);
    hdr->pid = pid_;
    hdr->kind = kind;
    hdr->flags = 0;
    hdr->word.store(pack_record_word(epoch_, length, SHM_RECORD_PAD),
                    std::memory_order_relaxed);
    out.header = hdr;
    out.payload = data_ + used_ + sizeof(RecordHeader);
    out.epoch = epoch_;
    out.length = length;
    used_ += length;
    return Result::OK;
  }

  /** Mark a reservation complete. The payload must be fully written. */
  void commit(const Reservation& r) {
    r.header->word.store(
        pack_record_word(r.epoch, r.length, SHM_RECORD_COMMITTED),
        std::memory_order_relaxed);
    end_write();
  }

  /**
   * try_reserve, retrying while the ring is FULL for at most `wait_ns`: spin
   * briefly, then yield. Returns FULL if it is still full afterwards.
   */
  Result reserve(size_t payload_bytes, ShmRecordKind kind, Reservation& out,
                 uint64_t wait_ns) {
    Result r = try_reserve(payload_bytes, kind, out);
    if (r != Result::FULL || wait_ns == 0) return r;
    const uint64_t deadline = monotonic_ns() + wait_ns;
    for (uint32_t attempt = 0;; ++attempt) {
      if (attempt < 64) {
        cpu_relax();
      } else {
        sched_yield();
      }
      r = try_reserve(payload_bytes, kind, out);
      if (r != Result::FULL) return r;
      if ((attempt & 15) == 15 && monotonic_ns() >= deadline) return r;
    }
  }

  /** Publish the block being filled, if any, padding out its unused tail. */
  void flush() { publish(); }

  /**
   * flush() if the block being filled was claimed at least `max_age_ns` before
   * `now_ns`. For a periodic timer that bounds how long records can sit in a
   * quiet producer's block. Returns true if it published a block.
   */
  bool flush_if_older(uint64_t now_ns, uint64_t max_age_ns) {
    if (data_ == nullptr || now_ns - claim_ns_ < max_age_ns) return false;
    publish();
    return true;
  }

  /** True while this producer holds a claimed, unpublished block. */
  bool holding() const { return data_ != nullptr; }

  /** Blocks the service took over from this producer. */
  uint64_t taken_blocks() const { return taken_; }

 private:
  /**
   * Claim the block `head` names. Returns false if it is not free for this lap
   * yet and `head` has not moved meanwhile, i.e. the ring is full.
   */
  bool claim() {
    SegmentHeader* header = ring_.header();
    uint64_t seq = header->head.load(std::memory_order_acquire);
    for (;;) {
      BlockControl& block = ring_.block(seq);
      if (block.armed.load(std::memory_order_acquire) != ring_.epoch(seq)) {
        // Not released for this lap yet -- or `seq` is stale and the block is
        // already armed for a later one. Only a moved `head` tells them apart.
        const uint64_t now = header->head.load(std::memory_order_acquire);
        if (now == seq) return false;
        seq = now;
        continue;
      }
      // `head` only grows, so a successful CAS from `seq` means this producer
      // alone owns block `seq`, and it was armed for exactly this lap.
      if (header->head.compare_exchange_weak(seq, seq + 1,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
        claim_ns_ = monotonic_ns();
        epoch_ = ring_.epoch(seq);
        block.claim_ns.store(claim_ns_, std::memory_order_relaxed);
        block.owner_pid.store(pid_, std::memory_order_relaxed);
        block.owner_tid.store(tid_, std::memory_order_relaxed);
        // Release: claim_ns and the owner ids are visible to a service thread
        // that sees this lap's owner word.
        block.owner.store(owner_word(epoch_, SHM_OWNER_IDLE),
                          std::memory_order_release);
        block_ = &block;
        data_ = ring_.block_data(seq);
        used_ = 0;
        return true;
      }
    }
  }

  /**
   * Own the block for writing: IDLE -> WRITING. False if the service has
   * taken it over. Acquire: pairs with a service thread's release of the
   * block's previous lap, through the owner word.
   */
  bool begin_write() {
    if (writing_) return true;  // a reservation was left uncommitted
    uint64_t idle = owner_word(epoch_, SHM_OWNER_IDLE);
    if (block_->owner.compare_exchange_strong(
            idle, owner_word(epoch_, SHM_OWNER_WRITING),
            std::memory_order_acquire, std::memory_order_relaxed)) {
      writing_ = true;
      return true;
    }
    ++taken_;
    return false;
  }

  /**
   * WRITING -> IDLE. Release: the records committed so far are visible to a
   * service thread that takes the block over from IDLE.
   */
  void end_write() {
    block_->owner.store(owner_word(epoch_, SHM_OWNER_IDLE),
                        std::memory_order_release);
    writing_ = false;
  }

  /** Forget the block without publishing it (the service has it). */
  void drop() {
    block_ = nullptr;
    data_ = nullptr;
    writing_ = false;
  }

  void publish() {
    if (data_ == nullptr) return;
    if (!begin_write()) {
      drop();
      return;
    }
    const uint32_t block_size = ring_.block_size();
    if (used_ < block_size)
      write_pad(data_ + used_, epoch_, block_size - used_, pid_);
    // Release: every record and pad header written above is visible to the
    // consumer once it sees the block complete.
    block_->committed.store(published_word(epoch_, block_size),
                            std::memory_order_release);
    // Never back to IDLE: a published block is drained by claim, not taken.
    block_->owner.store(owner_word(epoch_, SHM_OWNER_PUBLISHED),
                        std::memory_order_relaxed);
    drop();
  }

  RingView ring_;
  uint32_t pid_ = 0;
  uint32_t tid_ = 0;
  // The block being filled; data_ == nullptr when none is held.
  BlockControl* block_ = nullptr;
  uint8_t* data_ = nullptr;
  uint32_t epoch_ = 0;
  uint32_t used_ = 0;
  uint64_t claim_ns_ = 0;
  bool writing_ = false;  // the block is WRITING: a reservation is open
  uint64_t taken_ = 0;
};

}  // namespace ipc
}  // namespace dftracer

#endif  // DFTRACER_IPC_RING_PRODUCER_H
