#ifndef DFTRACER_BUFFER_COMPACT_EVENT_H
#define DFTRACER_BUFFER_COMPACT_EVENT_H

#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/event_draft.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/ipc/event_codec.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace dftracer {
namespace compact {

/**
 * The form an interceptor's event takes in a thread buffer of this process
 * (LocalBufferManager), about a quarter of the ring's EventHead encoding, so
 * blocks that wait to be compressed take less memory and memory traffic.
 *
 *   EVENT   u8 type | u8 flags | index | start | duration | pid | tid |
 *           name | category | count | count x ENTRY
 *   ENTRY   u8 (ShmValueTag | MetadataType << 3) | key | value
 *   value   INT, UINT: the change from this key's previous value
 *           DOUBLE: 8 bytes; STRING, RAW_JSON: a string
 *   string  (id << 1) for a string seen before in the block, else
 *           (length << 1 | 1) + bytes, which takes the next id
 *
 * Numbers are LEB128 varints; index, start, pid, tid and values are zigzag
 * changes from the previous event. All state starts over with each block, so
 * a block decodes alone.
 */
inline uint8_t* put_varint(uint8_t* p, uint64_t v) {
  while (v >= 0x80) {
    *p++ = static_cast<uint8_t>(v | 0x80);
    v >>= 7;
  }
  *p++ = static_cast<uint8_t>(v);
  return p;
}
inline uint64_t zigzag(uint64_t change) {
  return (change << 1) ^ (0 - (change >> 63));
}
inline uint64_t unzigzag(uint64_t v) { return (v >> 1) ^ (0 - (v & 1)); }

// Values of keys with an id below this are sent as changes.
constexpr uint32_t kTrackedValues = 256;
// Largest varint plus the head bytes of an entry or event field.
constexpr size_t kVarintMax = 10;

class Encoder {
 public:
  Encoder() { reset(); }
  void reset() {
    std::memset(slots_, 0, sizeof(slots_));
    std::memset(last_, 0, sizeof(last_));
    std::memset(values_, 0, sizeof(values_));
    next_id_ = 0;
    index_ = start_ = pid_ = tid_ = 0;
  }

  /** An upper bound of what event() writes for these fields. */
  static size_t bound(std::string_view name, std::string_view category,
                      const EventDraft* args) {
    size_t n = 2 + 8 * kVarintMax + name.size() + category.size();
    if (args != nullptr) n += args->size() + args->count() * (2 * kVarintMax);
    return n;
  }

  /**
   * Writes an event with the args of `args` (null for none) at `out`, which
   * has room for bound(); returns its size.
   */
  size_t event(uint8_t* out, int index, std::string_view name,
               std::string_view category, TraceEventType type,
               TimeResolution start, TimeResolution duration,
               const EventDraft* args, ProcessID pid, ThreadID tid,
               uint8_t flags) {
    uint8_t* p = out;
    *p++ = static_cast<uint8_t>(type);
    *p++ = flags;
    p = put_varint(p, zigzag(static_cast<uint64_t>(index) - index_));
    p = put_varint(p, zigzag(start - start_));
    p = put_varint(p, duration);
    p = put_varint(p, zigzag(static_cast<uint64_t>(pid) - pid_));
    p = put_varint(p, zigzag(tid - tid_));
    index_ = static_cast<uint64_t>(index);
    start_ = start;
    pid_ = static_cast<uint64_t>(pid);
    tid_ = tid;
    uint32_t id;
    p = string(p, name.data(), ipc::short_len(name), id, 0);
    p = string(p, category.data(), ipc::short_len(category), id, 1);
    const uint16_t count = args != nullptr ? args->count() : 0;
    p = put_varint(p, count);
    const uint8_t* at = args != nullptr ? args->data() : nullptr;
    for (uint16_t i = 0; i < count; ++i) {
      const EventDraft::Entry e = EventDraft::entry_at(at);
      at += e.size;
      *p++ = static_cast<uint8_t>(e.tag | (static_cast<uint8_t>(e.mtype) << 3));
      p = string(p, e.key.data(), e.key.size(), id, 2 + 2 * i);
      if (e.tag == SHM_VALUE_STRING || e.tag == SHM_VALUE_RAW_JSON) {
        uint32_t value_id;
        p = string(p, reinterpret_cast<const char*>(e.value), e.value_len,
                   value_id, 3 + 2 * i);
      } else if (e.tag == SHM_VALUE_DOUBLE) {
        std::memcpy(p, e.value, 8);
        p += 8;
      } else {
        uint64_t v;
        std::memcpy(&v, e.value, sizeof(v));
        uint64_t last = 0;
        if (id < kTrackedValues) {
          last = values_[id];
          values_[id] = v;
        }
        p = put_varint(p, zigzag(v - last));
      }
    }
    return static_cast<size_t>(p - out);
  }

 private:
  static constexpr uint32_t kSlots = 64;
  // Fields (name, category, then key and value of each arg) whose string
  // from the previous event is checked first.
  static constexpr uint32_t kPositions = 34;
  struct Slot {
    const uint8_t* bytes;
    uint32_t len;
    uint32_t id;
  };
  // Length and three bytes: a clash only costs the string inline again.
  static uint32_t slot_of(const char* s, size_t n) {
    if (n == 0) return 0;
    const uint32_t h = static_cast<uint32_t>(n) * 31 +
                       static_cast<uint8_t>(s[0]) +
                       static_cast<uint8_t>(s[n - 1]) * 7 +
                       static_cast<uint8_t>(s[n / 2]) * 131;
    return (h ^ (h >> 6)) & (kSlots - 1);
  }
  static bool same(const uint8_t* a, const char* b, size_t n) {
    if (n >= 8 && n <= 16) {
      uint64_t a0, a1, b0, b1;
      std::memcpy(&a0, a, 8);
      std::memcpy(&b0, b, 8);
      std::memcpy(&a1, a + n - 8, 8);
      std::memcpy(&b1, b + n - 8, 8);
      return ((a0 ^ b0) | (a1 ^ b1)) == 0;
    }
    if (n >= 4 && n < 8) {
      uint32_t a0, a1, b0, b1;
      std::memcpy(&a0, a, 4);
      std::memcpy(&b0, b, 4);
      std::memcpy(&a1, a + n - 4, 4);
      std::memcpy(&b1, b + n - 4, 4);
      return ((a0 ^ b0) | (a1 ^ b1)) == 0;
    }
    if (n < 4) {
      for (size_t i = 0; i < n; ++i)
        if (a[i] != static_cast<uint8_t>(b[i])) return false;
      return true;
    }
    return std::memcmp(a, b, n) == 0;
  }
  __attribute__((always_inline)) uint8_t* string(uint8_t* p, const char* s,
                                                 size_t n, uint32_t& id,
                                                 uint32_t position) {
    if (position < kPositions) {
      const Slot& last = last_[position];
      if (last.len == n && last.bytes != nullptr && same(last.bytes, s, n)) {
        id = last.id;
        return put_varint(p, static_cast<uint64_t>(id) << 1);
      }
    }
    return string_slow(p, s, n, id, position);
  }
  __attribute__((noinline)) uint8_t* string_slow(uint8_t* p, const char* s,
                                                 size_t n, uint32_t& id,
                                                 uint32_t position) {
    Slot* last = position < kPositions ? &last_[position] : nullptr;
    Slot& slot = slots_[slot_of(s, n)];
    if (slot.bytes != nullptr && slot.len == n && same(slot.bytes, s, n)) {
      id = slot.id;
      if (last != nullptr) *last = slot;
      return put_varint(p, static_cast<uint64_t>(id) << 1);
    }
    id = next_id_++;
    p = put_varint(p, (static_cast<uint64_t>(n) << 1) | 1);
    if (n != 0) std::memcpy(p, s, n);
    slot = Slot{p, static_cast<uint32_t>(n), id};
    if (last != nullptr) *last = slot;
    return p + n;
  }

  Slot slots_[kSlots];
  Slot last_[kPositions];
  uint64_t values_[kTrackedValues];
  uint32_t next_id_ = 0;
  uint64_t index_ = 0, start_ = 0, pid_ = 0, tid_ = 0;
};

class Decoder {
 public:
  Decoder() { reset(); }
  void reset() {
    strings_.clear();
    std::memset(values_, 0, sizeof(values_));
    index_ = start_ = pid_ = tid_ = 0;
  }

  /** Decodes the event at `data`; views point into it. */
  bool event(const uint8_t* data, size_t size, ipc::DecodedEvent& out) {
    p_ = data;
    end_ = data + size;
    if (size < 2) return false;
    ipc::EventHead& h = out.head;
    h = ipc::EventHead{};
    h.type = *p_++;
    h.flags = *p_++;
    uint64_t v;
    if (!varint(v)) return false;
    index_ += unzigzag(v);
    if (!varint(v)) return false;
    start_ += unzigzag(v);
    if (!varint(h.duration)) return false;
    if (!varint(v)) return false;
    pid_ += unzigzag(v);
    if (!varint(v)) return false;
    tid_ += unzigzag(v);
    h.index = static_cast<int32_t>(index_);
    h.start_time = start_;
    h.process_id = static_cast<int32_t>(pid_);
    h.thread_id = tid_;
    uint32_t id;
    if (!string(out.name, id) || !string(out.category, id)) return false;
    h.name_len = static_cast<uint16_t>(out.name.size());
    h.category_len = static_cast<uint16_t>(out.category.size());
    uint64_t count;
    if (!varint(count) || count > 0xffff) return false;
    h.meta_count = static_cast<uint16_t>(count);
    out.values.clear();
    out.values.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
      if (p_ >= end_) return false;
      ipc::DecodedValue d{};
      const uint8_t head = *p_++;
      d.tag = static_cast<ShmValueTag>(head & 7);
      d.mtype = static_cast<MetadataType>(head >> 3);
      if (!string(d.key, id)) return false;
      switch (d.tag) {
        case SHM_VALUE_INT:
        case SHM_VALUE_UINT: {
          if (!varint(v)) return false;
          uint64_t last = 0;
          uint64_t* slot = id < kTrackedValues ? &values_[id] : &last;
          *slot += unzigzag(v);
          if (d.tag == SHM_VALUE_INT)
            d.i = static_cast<int64_t>(*slot);
          else
            d.u = *slot;
          break;
        }
        case SHM_VALUE_DOUBLE:
          if (end_ - p_ < 8) return false;
          std::memcpy(&d.d, p_, 8);
          p_ += 8;
          break;
        case SHM_VALUE_STRING:
        case SHM_VALUE_RAW_JSON: {
          uint32_t value_id;
          if (!string(d.s, value_id)) return false;
          break;
        }
        default:
          return false;
      }
      out.values.push_back(d);
    }
    return true;
  }

 private:
  bool varint(uint64_t& out) {
    uint64_t v = 0;
    for (int shift = 0; shift < 64 && p_ < end_; shift += 7) {
      const uint8_t b = *p_++;
      v |= static_cast<uint64_t>(b & 0x7f) << shift;
      if ((b & 0x80) == 0) {
        out = v;
        return true;
      }
    }
    return false;
  }
  bool string(std::string_view& out, uint32_t& id) {
    uint64_t v;
    if (!varint(v)) return false;
    if ((v & 1) == 0) {
      if ((v >> 1) >= strings_.size()) return false;
      id = static_cast<uint32_t>(v >> 1);
      out = strings_[id];
      return true;
    }
    const uint64_t n = v >> 1;
    if (static_cast<uint64_t>(end_ - p_) < n) return false;
    out = std::string_view(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    id = static_cast<uint32_t>(strings_.size());
    strings_.push_back(out);
    return true;
  }

  const uint8_t* p_ = nullptr;
  const uint8_t* end_ = nullptr;
  std::vector<std::string_view> strings_;
  uint64_t values_[kTrackedValues];
  uint64_t index_ = 0, start_ = 0, pid_ = 0, tid_ = 0;
};

}  // namespace compact
}  // namespace dftracer
#endif  // DFTRACER_BUFFER_COMPACT_EVENT_H
