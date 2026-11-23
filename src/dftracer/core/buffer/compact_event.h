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
 * (LocalBufferManager), smaller than the ring's EventHead encoding, so
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
 * changes from the previous event. With kSameSource in the flags, index is
 * one past the previous event's and pid and tid are the same; the three are
 * left out. All state starts
 * over with each block, so a block decodes alone.
 *
 * An event from an interceptor's call site whose args all have StaticKeys
 * (EventDraft::shaped()) is sent by its shape: the name, category and the
 * tag, type and key of each entry.
 * The first event of a shape in a block is a full EVENT with kShapeNew in
 * its flags, and takes the next shape id. A later event of the shape is
 *
 *   SHAPED  u8 type | u8 flags | shape id | index | start | duration | pid |
 *           tid | one value for each entry of the shape
 *
 * with kShaped in its flags. Its INT and UINT values are changes from the
 * value at the same entry of the previous event of the shape.
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
// Flag bits of this encoding only; the decoder clears them.
constexpr uint8_t kSameSource = 0x20;
constexpr uint8_t kShapeNew = 0x40;
constexpr uint8_t kShaped = 0x80;
static_assert(((kSameSource | kShapeNew | kShaped) &
               (SHM_EVENT_INC_METADATA | SHM_EVENT_HAS_METADATA |
                SHM_EVENT_NO_AGGREGATE | SHM_EVENT_INTERNED)) == 0,
              "shape flags clash with ShmEventFlags");

class Encoder {
  static constexpr uint32_t kShapes = 64;
  static constexpr uint32_t kSiteIds = 256;
  struct Shape {
    uint32_t block = 0;  // the block_ it was made in; another is stale
    uint32_t id;
    uint64_t used;
    const void* site;
    uint64_t sig;
    uint16_t count;
    // Bit i: arg i is a string (or raw JSON), or a double.
    uint32_t strings;
    uint32_t doubles;
    // Arg 0 is a string and the rest are integers, as at every DFT_SITE_END
    // site.
    bool site_shape;
    uint64_t last[EventDraft::kShapeKeys];
  };

 public:
  Encoder() { reset(); }
  void reset() {
    std::memset(slots_, 0, sizeof(slots_));
    std::memset(last_, 0, sizeof(last_));
    std::memset(values_, 0, sizeof(values_));
    std::memset(by_site_, 0, sizeof(by_site_));
    next_id_ = 0;
    index_ = start_ = pid_ = tid_ = 0;
    ++block_;
    next_shape_ = 0;
    tick_ = 0;
  }

  /** An upper bound of what event() writes for these fields. */
  static size_t bound(std::string_view name, std::string_view category,
                      const EventDraft* args) {
    return args == nullptr ? bound(name, category, 0, 0)
                           : bound(name, category, args->size(), args->count());
  }
  /** bound() of a draft whose size() is `args_size`, with `count` entries. */
  static size_t bound(std::string_view name, std::string_view category,
                      size_t args_size, uint16_t count) {
    return compact_bound(name.size(), category.size(), args_size, count);
  }

  /**
   * Writes an event with the args of `args` (null for none) at `out`, which
   * has room for bound(); returns its size.
   */
  __attribute__((always_inline)) size_t event(
      uint8_t* out, int index, std::string_view name, std::string_view category,
      TraceEventType type, TimeResolution start, TimeResolution duration,
      const EventDraft* args, ProcessID pid, ThreadID tid, uint8_t flags) {
    Shape* fresh = nullptr;
    if (args != nullptr && args->shaped()) {
      Shape* a;
      Shape* b;
      Shape* s = lookup(args->site(), args->sig(), args->count(), a, b);
      if (s != nullptr)
        return shaped(out, *s, index, type, start, duration, args->entries(),
                      pid, tid, flags);
      fresh = a->block != block_   ? a
              : b->block != block_ ? b
              : a->used <= b->used ? a
                                   : b;
    }
    return full(out, index, name, category, type, start, duration, args, pid,
                tid, flags, fresh);
  }

  /**
   * event() of `e` with these flags, if its shape is already in the block;
   * else 0, with nothing written.
   */
  __attribute__((always_inline)) size_t site_event(uint8_t* out,
                                                   const SiteEvent& e,
                                                   uint8_t flags) {
    const SiteInfo& info = *e.info;
    Shape* s = info.id < kSiteIds ? by_site_[info.id] : nullptr;
    // A site's sig and count never change, so a shape of this block made for
    // this site is its shape.
    if (__builtin_expect(
            s == nullptr || s->block != block_ || s->site != info.site, 0)) {
      s = find(info);
      if (s == nullptr) return 0;
      if (info.id < kSiteIds) by_site_[info.id] = s;
    }
    return shaped(out, *s, e.index, info.type, e.start_time, e.duration, e.args,
                  e.process_id, e.tid, flags);
  }

 private:
  // The EVENT form; with `fresh`, it also defines that shape.
  __attribute__((noinline)) size_t
  full(uint8_t* out, int index, std::string_view name,
       std::string_view category, TraceEventType type, TimeResolution start,
       TimeResolution duration, const EventDraft* args, ProcessID pid,
       ThreadID tid, uint8_t flags, Shape* fresh) {
    if (fresh != nullptr) flags |= kShapeNew;
    uint8_t* p = out;
    *p++ = static_cast<uint8_t>(type);
    *p++ = flags;
    p = head(p, out + 1, index, start, duration, pid, tid);
    uint32_t id;
    p = string(p, name.data(), ipc::short_len(name), id, 0);
    p = string(p, category.data(), ipc::short_len(category), id, 1);
    const uint16_t count = args != nullptr ? args->count() : 0;
    p = put_varint(p, count);
    const EventDraft::Entry* es = args != nullptr ? args->entries() : nullptr;
    for (uint16_t i = 0; i < count; ++i) {
      const EventDraft::Entry& e = es[i];
      *p++ = static_cast<uint8_t>(e.tag() |
                                  (static_cast<uint8_t>(e.mtype()) << 3));
      p = string(p, e.key, e.key_len(), id, 2 + 2 * i);
      if (e.is_string()) {
        uint32_t value_id;
        p = string(p, e.str, e.value_len, value_id, 3 + 2 * i);
      } else if (e.tag() == SHM_VALUE_DOUBLE) {
        p = put_double(p, e.bits);
      } else {
        uint64_t untracked = 0;
        p = put_delta(p, e.bits, id < kTrackedValues ? values_[id] : untracked);
        if (fresh != nullptr) fresh->last[i] = e.bits;
      }
    }
    if (fresh != nullptr) define(*fresh, *args);
    return static_cast<size_t>(p - out);
  }

  __attribute__((always_inline)) uint8_t* head(uint8_t* p, uint8_t* flags,
                                               int index, TimeResolution start,
                                               TimeResolution duration,
                                               ProcessID pid, ThreadID tid) {
    const uint64_t i = static_cast<uint64_t>(index);
    const uint64_t pr = static_cast<uint64_t>(pid);
    if (((i ^ (index_ + 1)) | (pr ^ pid_) | (tid ^ tid_)) == 0) {
      *flags |= kSameSource;
      p = put_varint(p, zigzag(start - start_));
      p = put_varint(p, duration);
      index_ = i;
    } else {
      p = put_varint(p, zigzag(i - index_));
      p = put_varint(p, zigzag(start - start_));
      p = put_varint(p, duration);
      p = put_varint(p, zigzag(pr - pid_));
      p = put_varint(p, zigzag(tid - tid_));
      index_ = i;
      pid_ = pr;
      tid_ = tid;
    }
    start_ = start;
    return p;
  }

  __attribute__((always_inline)) static uint8_t* put_double(uint8_t* p,
                                                            uint64_t bits) {
    std::memcpy(p, &bits, 8);
    return p + 8;
  }

  __attribute__((always_inline)) static uint8_t* put_delta(uint8_t* p,
                                                           uint64_t v,
                                                           uint64_t& last) {
    p = put_varint(p, zigzag(v - last));
    last = v;
    return p;
  }

  __attribute__((noinline)) Shape* find(const SiteInfo& info) {
    Shape* a;
    Shape* b;
    return lookup(info.site, info.sig, info.count, a, b);
  }

  __attribute__((always_inline)) Shape* lookup(const void* site, uint64_t sig,
                                               uint16_t count, Shape*& a,
                                               Shape*& b) {
    const uint32_t h = shape_slot(site);
    a = &shapes_[h];
    b = &shapes_[h ^ 1];
    return matches(*a, site, sig, count)   ? a
           : matches(*b, site, sig, count) ? b
                                           : nullptr;
  }

  static uint32_t shape_slot(const void* site) {
    const uint64_t h =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(site)) *
        0x9e3779b97f4a7c15ull;
    return static_cast<uint32_t>(h >> 58) & (kShapes - 1);
  }

  bool matches(const Shape& s, const void* site, uint64_t sig,
               uint16_t count) const {
    return s.block == block_ && s.site == site && s.count == count &&
           s.sig == sig;
  }

  void define(Shape& s, const EventDraft& args) {
    s.block = block_;
    s.id = next_shape_++;
    s.used = ++tick_;
    s.site = args.site();
    s.sig = args.sig();
    s.count = args.count();
    const EventDraft::Entry* es = args.entries();
    s.strings = 0;
    s.doubles = 0;
    for (uint16_t i = 0; i < s.count; ++i) {
      if (es[i].is_string()) s.strings |= 1u << i;
      if (es[i].tag() == SHM_VALUE_DOUBLE) s.doubles |= 1u << i;
    }
    s.site_shape = s.strings == 1 && s.doubles == 0;
  }

  __attribute__((always_inline)) size_t shaped(
      uint8_t* out, Shape& s, int index, TraceEventType type,
      TimeResolution start, TimeResolution duration,
      const EventDraft::Entry* es, ProcessID pid, ThreadID tid, uint8_t flags) {
    s.used = ++tick_;
    uint8_t* p = out;
    *p++ = static_cast<uint8_t>(type);
    *p++ = static_cast<uint8_t>(flags | kShaped);
    p = put_varint(p, s.id);
    p = head(p, out + 1, index, start, duration, pid, tid);
    // Copied out: a store through `p` may alias `s`, so the compiler would
    // load them again after each byte.
    const uint32_t count = s.count;
    const uint32_t strings = s.strings;
    const uint32_t doubles = s.doubles;
    uint64_t* last = s.last;
    if (s.site_shape) {
      uint32_t value_id;
      p = string(p, es[0].str, es[0].value_len, value_id, 3);
      for (uint32_t i = 1; i < count; ++i)
        p = put_delta(p, es[i].bits, last[i]);
      return static_cast<size_t>(p - out);
    }
    for (uint32_t i = 0; i < count; ++i) {
      const EventDraft::Entry& e = es[i];
      if (((strings >> i) & 1) != 0) {
        uint32_t value_id;
        p = string(p, e.str, e.value_len, value_id, 3 + 2 * i);
      } else if (((doubles >> i) & 1) != 0) {
        p = put_double(p, e.bits);
      } else {
        p = put_delta(p, e.bits, last[i]);
      }
    }
    return static_cast<size_t>(p - out);
  }

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
  __attribute__((always_inline)) static bool same(const uint8_t* a,
                                                  const char* b, size_t n) {
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
      for (const Slot& last : last_[position]) {
        if (last.len == n && last.bytes != nullptr && same(last.bytes, s, n)) {
          id = last.id;
          return put_varint(p, static_cast<uint64_t>(id) << 1);
        }
      }
    }
    return string_slow(p, s, n, id, position);
  }
  __attribute__((noinline)) uint8_t* string_slow(uint8_t* p, const char* s,
                                                 size_t n, uint32_t& id,
                                                 uint32_t position) {
    Slot* last = position < kPositions ? last_[position] : nullptr;
    Slot& slot = slots_[slot_of(s, n)];
    if (slot.bytes != nullptr && slot.len == n && same(slot.bytes, s, n)) {
      id = slot.id;
      remember(last, slot);
      return put_varint(p, static_cast<uint64_t>(id) << 1);
    }
    id = next_id_++;
    p = put_varint(p, (static_cast<uint64_t>(n) << 1) | 1);
    if (n != 0) std::memcpy(p, s, n);
    slot = Slot{p, static_cast<uint32_t>(n), id};
    remember(last, slot);
    return p + n;
  }
  static void remember(Slot* last, const Slot& slot) {
    if (last == nullptr) return;
    last[1] = last[0];
    last[0] = slot;
  }

  Slot slots_[kSlots];
  // Two strings per position, so events that alternate (lseek and read in
  // IOR) still take the fast path.
  Slot last_[kPositions][2];
  uint64_t values_[kTrackedValues];
  uint32_t next_id_ = 0;
  uint64_t index_ = 0, start_ = 0, pid_ = 0, tid_ = 0;
  // Two ways per hash: shapes_[h] and shapes_[h ^ 1].
  Shape shapes_[kShapes];
  // A site's shape from its last event, checked by matches() before use.
  Shape* by_site_[kSiteIds];
  uint32_t block_ = 0;
  uint32_t next_shape_ = 0;
  uint64_t tick_ = 0;
};

class Decoder {
 public:
  Decoder() { reset(); }
  void reset() {
    strings_.clear();
    shapes_.clear();
    shape_values_.clear();
    std::memset(values_, 0, sizeof(values_));
    index_ = start_ = pid_ = tid_ = 0;
    shape_ = kNoShape;
  }

  static constexpr size_t kNoShape = ~size_t{0};
  /**
   * The shape of the last decoded event, an index below the shape count of
   * this block, or kNoShape for an event that has none. Events of one shape
   * share their name, category and keys until reset().
   */
  size_t shape() const { return shape_; }

  /** Decodes the event at `data`; views point into it. */
  bool event(const uint8_t* data, size_t size, ipc::DecodedEvent& out) {
    Cursor c{data, data + size};
    shape_ = kNoShape;
    if (size < 2) return false;
    ipc::EventHead& h = out.head;
    h = ipc::EventHead{};
    h.type = *c.p++;
    const uint8_t flags = *c.p++;
    h.flags =
        static_cast<uint8_t>(flags & ~(kSameSource | kShapeNew | kShaped));
    uint64_t v;
    uint64_t shape_id = 0;
    if ((flags & kShaped) != 0 &&
        (!varint(c, shape_id) || shape_id >= shapes_.size()))
      return false;
    if ((flags & kSameSource) != 0) {
      ++index_;
      if (!varint(c, v)) return false;
      start_ += unzigzag(v);
      if (!varint(c, h.duration)) return false;
    } else {
      if (!varint(c, v)) return false;
      index_ += unzigzag(v);
      if (!varint(c, v)) return false;
      start_ += unzigzag(v);
      if (!varint(c, h.duration)) return false;
      if (!varint(c, v)) return false;
      pid_ += unzigzag(v);
      if (!varint(c, v)) return false;
      tid_ += unzigzag(v);
    }
    h.index = static_cast<int32_t>(index_);
    h.start_time = start_;
    h.process_id = static_cast<int32_t>(pid_);
    h.thread_id = tid_;
    if ((flags & kShaped) != 0) {
      if (!shaped(c, shapes_[shape_id], out)) return false;
      shape_ = static_cast<size_t>(shape_id);
      return true;
    }
    uint32_t id;
    if (!string(c, out.name, id) || !string(c, out.category, id)) return false;
    h.name_len = static_cast<uint16_t>(out.name.size());
    h.category_len = static_cast<uint16_t>(out.category.size());
    uint64_t count;
    if (!varint(c, count) || count > 0xffff) return false;
    h.meta_count = static_cast<uint16_t>(count);
    out.values.clear();
    out.values.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
      if (c.p >= c.end) return false;
      ipc::DecodedValue d{};
      const uint8_t head = *c.p++;
      d.tag = static_cast<ShmValueTag>(head & 7);
      d.mtype = static_cast<MetadataType>(head >> 3);
      if (!string(c, d.key, id)) return false;
      switch (d.tag) {
        case SHM_VALUE_INT:
        case SHM_VALUE_UINT:
        case SHM_VALUE_DOUBLE: {
          uint64_t last = 0;
          uint64_t* slot = id < kTrackedValues ? &values_[id] : &last;
          if (!number(c, d, *slot)) return false;
          break;
        }
        case SHM_VALUE_STRING:
        case SHM_VALUE_RAW_JSON: {
          uint32_t value_id;
          if (!string(c, d.s, value_id)) return false;
          break;
        }
        default:
          return false;
      }
      out.values.push_back(d);
    }
    if ((flags & kShapeNew) != 0) {
      shapes_.push_back(Shape{out.name, out.category, shape_values_.size(),
                              out.values.size()});
      for (ipc::DecodedValue d : out.values) {
        if (d.tag == SHM_VALUE_INT) d.u = static_cast<uint64_t>(d.i);
        shape_values_.push_back(d);
      }
      shape_ = shapes_.size() - 1;
    }
    return true;
  }

 private:
  // A shape's entries are shape_values_[first, first + count), with the last
  // INT or UINT value of each in its u.
  struct Shape {
    std::string_view name;
    std::string_view category;
    size_t first;
    size_t count;
  };

  // The read position: a local, so that it stays in a register through the
  // byte stores into the decoded event.
  struct Cursor {
    const uint8_t* p;
    const uint8_t* end;
  };

  __attribute__((always_inline)) bool shaped(Cursor& c, const Shape& shape,
                                             ipc::DecodedEvent& out) {
    out.name = shape.name;
    out.category = shape.category;
    out.head.name_len = static_cast<uint16_t>(shape.name.size());
    out.head.category_len = static_cast<uint16_t>(shape.category.size());
    out.head.meta_count = static_cast<uint16_t>(shape.count);
    // Filled in place: the vector keeps its size from one event of a shape
    // to the next, so this costs no allocation or element copy.
    out.values.resize(shape.count);
    for (size_t i = 0; i < shape.count; ++i) {
      ipc::DecodedValue& last = shape_values_[shape.first + i];
      ipc::DecodedValue& d = out.values[i];
      d.tag = last.tag;
      d.mtype = last.mtype;
      d.key = last.key;
      d.i = 0;
      d.u = 0;
      d.d = 0;
      d.s = std::string_view();
      switch (d.tag) {
        case SHM_VALUE_INT:
        case SHM_VALUE_UINT:
        case SHM_VALUE_DOUBLE:
          if (!number(c, d, last.u)) return false;
          break;
        default: {
          uint32_t value_id;
          if (!string(c, d.s, value_id)) return false;
          break;
        }
      }
    }
    return true;
  }

  // An INT or UINT is a change to `slot`; a DOUBLE is its 8 bytes.
  __attribute__((always_inline)) bool number(Cursor& c, ipc::DecodedValue& d,
                                             uint64_t& slot) {
    if (d.tag == SHM_VALUE_DOUBLE) {
      if (c.end - c.p < 8) return false;
      std::memcpy(&d.d, c.p, 8);
      c.p += 8;
      return true;
    }
    uint64_t v;
    if (!varint(c, v)) return false;
    slot += unzigzag(v);
    if (d.tag == SHM_VALUE_INT)
      d.i = static_cast<int64_t>(slot);
    else
      d.u = slot;
    return true;
  }

  __attribute__((always_inline)) bool varint(Cursor& c, uint64_t& out) {
    // Most values (deltas, ids, counts) take one byte.
    if (c.p < c.end && *c.p < 0x80) {
      out = *c.p++;
      return true;
    }
    uint64_t v = 0;
    for (int shift = 0; shift < 64 && c.p < c.end; shift += 7) {
      const uint8_t b = *c.p++;
      v |= static_cast<uint64_t>(b & 0x7f) << shift;
      if ((b & 0x80) == 0) {
        out = v;
        return true;
      }
    }
    return false;
  }
  __attribute__((always_inline)) bool string(Cursor& c, std::string_view& out,
                                             uint32_t& id) {
    uint64_t v;
    if (!varint(c, v)) return false;
    if ((v & 1) == 0) {
      if ((v >> 1) >= strings_.size()) return false;
      id = static_cast<uint32_t>(v >> 1);
      out = strings_[id];
      return true;
    }
    const uint64_t n = v >> 1;
    if (static_cast<uint64_t>(c.end - c.p) < n) return false;
    out = std::string_view(reinterpret_cast<const char*>(c.p), n);
    c.p += n;
    id = static_cast<uint32_t>(strings_.size());
    strings_.push_back(out);
    return true;
  }

  std::vector<std::string_view> strings_;
  std::vector<Shape> shapes_;
  std::vector<ipc::DecodedValue> shape_values_;
  uint64_t values_[kTrackedValues];
  uint64_t index_ = 0, start_ = 0, pid_ = 0, tid_ = 0;
  size_t shape_ = kNoShape;
};

}  // namespace compact
}  // namespace dftracer
#endif  // DFTRACER_BUFFER_COMPACT_EVENT_H
