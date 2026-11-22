#ifndef DFTRACER_COMMON_EVENT_DRAFT_H
#define DFTRACER_COMMON_EVENT_DRAFT_H
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/serialization/line_format.h>
#include <dftracer/core/serialization/metadata_json.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace dftracer {

/**
 * The args of one intercepted call, kept as typed entries while the
 * interceptor runs, so an event reaches its buffer without a Metadata. A
 * StaticKey's key is kept by pointer; other keys and every string value are
 * copied into the draft.
 *
 * set() follows Metadata::insert_or_assign: a key set again is replaced, and
 * a value of a type Metadata does not store removes the key. Args whose ring
 * form (write_wire()) would pass DFTRACER_SERIALIZATION_EVENT_MAX bytes are
 * dropped.
 */
class EventDraft {
 public:
  struct Entry {
    const char* key;
    // tag | MetadataType << 8 | key length << 16
    uint32_t head;
    uint32_t value_len;  // a string's length
    union {
      uint64_t bits;  // INT and UINT as 64 bits, DOUBLE as its bit pattern
      const char* str;
    };
    ShmValueTag tag() const { return static_cast<ShmValueTag>(head & 0xff); }
    MetadataType mtype() const {
      return static_cast<MetadataType>((head >> 8) & 0xff);
    }
    uint16_t key_len() const { return static_cast<uint16_t>(head >> 16); }
    std::string_view key_view() const { return {key, key_len()}; }
    bool is_string() const {
      return tag() == SHM_VALUE_STRING || tag() == SHM_VALUE_RAW_JSON;
    }
  };

  EventDraft() = default;
  EventDraft(const EventDraft& other) { *this = other; }
  EventDraft& operator=(const EventDraft& other) {
    if (this == &other) return *this;
    reset();
    for (uint16_t i = 0; i < other.count_; ++i) {
      const Entry& e = other.entries_[i];
      const bool copy_key =
          !(e.key < other.arena_ || e.key >= other.arena_ + other.arena_cap_);
      Entry* n = add(e.key_view(), copy_key, e.tag(), e.mtype(),
                     e.is_string() ? 4 + e.value_len : 8,
                     e.is_string() ? e.value_len : 0);
      if (n == nullptr) continue;
      if (e.is_string()) {
        char* at = arena_ + used_;
        if (e.value_len != 0) std::memcpy(at, e.str, e.value_len);
        used_ += e.value_len;
        n->str = at;
        n->value_len = e.value_len;
      } else {
        n->bits = e.bits;
      }
    }
    seen_ = other.seen_;
    sig_ = other.sig_;
    shaped_ = other.shaped_;
    site_ = other.site_;
    return *this;
  }

  void reset() {
    size_ = 0;
    used_ = 0;
    count_ = 0;
    seen_ = 0;
    sig_ = 0;
    shaped_ = true;
    site_ = nullptr;
  }
  uint16_t count() const { return count_; }
  const Entry* entries() const { return entries_; }
  /** Bytes write_wire() writes. */
  size_t size() const { return size_; }

  /**
   * Writes the entries in the ring's metadata form (ipc/event_codec.h: u8
   * ShmValueTag | u8 MetadataType | u16 key_len | key | 8-byte number or u32
   * len + bytes) through `w.put_bytes()`.
   */
  template <typename W>
  void write_wire(W& w) const {
    for (uint16_t i = 0; i < count_; ++i) {
      const Entry& e = entries_[i];
      uint8_t head[4];
      head[0] = static_cast<uint8_t>(e.tag());
      head[1] = static_cast<uint8_t>(e.mtype());
      const uint16_t key_len = e.key_len();
      std::memcpy(head + 2, &key_len, sizeof(key_len));
      w.put_bytes(head, sizeof(head));
      w.put_bytes(e.key, key_len);
      if (e.is_string()) {
        w.put_bytes(&e.value_len, sizeof(e.value_len));
        w.put_bytes(e.str, e.value_len);
      } else {
        w.put_bytes(&e.bits, sizeof(e.bits));
      }
    }
  }

  /** At most this many keys keep an event's shape (shaped()). */
  static constexpr uint16_t kShapeKeys = 16;
  /**
   * Whether the event's args have a shape: the draft has a call site, each
   * key came from a StaticKey and was set once and there are at most
   * kShapeKeys. The site and sig() then describe the args, and the site
   * fixes the event's name and category (compact::Encoder sends an event by
   * this shape).
   */
  bool shaped() const {
    return site_ != nullptr && shaped_ && count_ <= kShapeKeys;
  }
  const void* site() const { return site_; }
  /** `site` is one interceptor's own static object, or null. */
  void set_site(const void* site) { site_ = site; }
  /**
   * A hash of the key pointers and heads of the entries, in order: one
   * site's events with the same sig() have the same keys and types.
   */
  uint64_t sig() const { return sig_; }

  template <typename V>
  __attribute__((always_inline)) void set(
      std::string_view key, V&& value,
      MetadataType type = MetadataType::MT_KEY) {
    shaped_ = false;
    write(key, true, std::forward<V>(value), type);
  }
  template <typename V>
  __attribute__((always_inline)) void set(
      StaticKey key, V&& value, MetadataType type = MetadataType::MT_KEY) {
    write(key.name, false, std::forward<V>(value), type);
  }
  /**
   * The entry set(key, value, type) would add, for an arg of a site
   * interceptor (DFT_SITE_END). A string value is borrowed, not copied.
   */
  template <typename V>
  __attribute__((always_inline)) static Entry entry(StaticKey key,
                                                    const V& value,
                                                    MetadataType type) {
    using D = std::decay_t<V>;
    Entry e;
    e.key = key.name.data();
    e.value_len = 0;
    ShmValueTag tag;
    if constexpr (std::is_same_v<D, std::string_view>) {
      tag = SHM_VALUE_STRING;
      e.str = value.data();
      e.value_len = static_cast<uint32_t>(value.size());
    } else if constexpr (std::is_enum_v<D>) {
      return entry(key, static_cast<std::underlying_type_t<D>>(value), type);
    } else {
      static_assert(std::is_arithmetic_v<D>,
                    "a site arg is a number or a "
                    "string_view");
      tag = tag_of<D>();
      e.bits = bits_of(value);
    }
    e.head = head_of(tag, type, static_cast<uint16_t>(key.name.size()));
    return e;
  }
  /** entry() of a number, the only kind of arg DFT_ARG takes. */
  template <typename V>
  __attribute__((always_inline)) static Entry number(StaticKey key,
                                                     const V& value,
                                                     MetadataType type) {
    static_assert(std::is_arithmetic_v<std::decay_t<V>> ||
                      std::is_enum_v<std::decay_t<V>>,
                  "DFT_ARG takes a number");
    return entry(key, value, type);
  }
  /** Adds `e` from entry() as set() of its StaticKey would. */
  void append(const Entry& e) {
    seen_ |= key_bit(e.key_view());
    if (e.is_string()) {
      put_string(e.key_view(), false, e.tag(), e.mtype(), e.str, e.value_len);
    } else {
      Entry* n = add(e.key_view(), false, e.tag(), e.mtype(), 8, 0);
      if (n != nullptr) n->bits = e.bits;
    }
  }
  /** sig() after an entry `e` is added to a draft whose sig() is `sig`. */
  __attribute__((always_inline)) static uint64_t mix(uint64_t sig,
                                                     const Entry& e) {
    return ((sig + reinterpret_cast<uintptr_t>(e.key)) *
            0x9e3779b97f4a7c15ull) ^
           e.head;
  }
  /** Bytes write_wire() writes for `e`. */
  static size_t wire_size(const Entry& e) {
    return 4 + e.key_len() + (e.is_string() ? 4 + e.value_len : 8);
  }
  /** Every entry of `metadata`, in its order. */
  void set_all(const Metadata& metadata) {
    for (const auto& entry : metadata)
      set(entry.first.view(), std::get<1>(entry.second),
          std::get<0>(entry.second));
  }

 private:
  template <typename V>
  __attribute__((always_inline)) void write(std::string_view key, bool copy_key,
                                            V&& value, MetadataType type) {
    using D = std::decay_t<V>;
    const uint64_t bit = key_bit(key);
    uint16_t slot = kNoSlot;
    if (__builtin_expect((seen_ & bit) != 0, 0)) {
      slot = remove(key);
      if (slot != kNoSlot) shaped_ = false;
    }
    seen_ |= bit;
    const uint16_t last = count_;
    if constexpr (detail::is_metadata_value_v<D>) {
      if constexpr (std::is_same_v<D, MetadataValue>) {
        std::visit([&](const auto& v) { put(key, copy_key, v, type); }, value);
      } else if constexpr (std::is_enum_v<D>) {
        put(key, copy_key, static_cast<std::underlying_type_t<D>>(value), type);
      } else {
        put<D>(key, copy_key, value, type);
      }
      if (slot != kNoSlot && count_ != last) move_last_to(slot);
    }
  }

  // On the stack of each interceptor; enough for the args of almost every
  // call, so the heap is used only past it.
  static constexpr size_t kInlineBytes = 256;
  static constexpr uint16_t kInlineEntries = kShapeKeys;
  static constexpr size_t kMaxBytes = DFTRACER_SERIALIZATION_EVENT_MAX;
  static constexpr uint16_t kNoSlot = 0xffff;
  struct StringWriter {
    std::string& out;
    void raw(std::string_view s) { out.append(s.data(), s.size()); }
    void escaped(std::string_view s) { line_format::escaped(*this, s); }
    void i64(int64_t v) { out += std::to_string(v); }
    void u64(uint64_t v) { out += std::to_string(v); }
    void f64(double v) {
      char tmp[32];
      const int n = std::snprintf(tmp, sizeof(tmp), "%g", v);
      if (n > 0) out.append(tmp, static_cast<size_t>(n));
    }
  };

  __attribute__((always_inline)) static uint64_t key_bit(std::string_view key) {
    const size_t h = key.size() * 31 + (key.empty() ? 0 : key[0]) +
                     (key.size() > 1 ? key[key.size() - 1] * 7 : 0);
    return uint64_t{1} << (h & 63);
  }

  static uint32_t head_of(ShmValueTag tag, MetadataType type,
                          uint16_t key_len) {
    return static_cast<uint32_t>(tag) |
           (static_cast<uint32_t>(static_cast<uint8_t>(type)) << 8) |
           (static_cast<uint32_t>(key_len) << 16);
  }

  // Drops the entry of `key`; returns its position, so a replacement keeps
  // the position Metadata::insert_or_assign keeps.
  __attribute__((noinline)) uint16_t remove(std::string_view key) {
    for (uint16_t i = 0; i < count_; ++i) {
      if (entries_[i].key_view() == key) {
        size_ -= wire_size(entries_[i]);
        std::memmove(entries_ + i, entries_ + i + 1,
                     (count_ - i - 1) * sizeof(Entry));
        --count_;
        return i;
      }
    }
    return kNoSlot;
  }

  // Moves the newest entry to position `at`.
  __attribute__((noinline)) void move_last_to(uint16_t at) {
    std::rotate(entries_ + at, entries_ + count_ - 1, entries_ + count_);
  }

  // A new entry whose ring form takes `wire` bytes and that copies
  // `copy_bytes` into the arena; null when it is dropped.
  __attribute__((always_inline)) Entry* add(std::string_view key, bool copy_key,
                                            ShmValueTag tag, MetadataType type,
                                            size_t wire, size_t copy_bytes) {
    const uint16_t key_len =
        static_cast<uint16_t>(key.size() > 0xffff ? 0xffff : key.size());
    wire += 4 + key_len;
    if (copy_key) copy_bytes += key_len;
    if (__builtin_expect(size_ + wire > kMaxBytes ||
                             used_ + copy_bytes > arena_cap_ ||
                             count_ == entries_cap_,
                         0) &&
        !make_room(wire, copy_bytes))
      return nullptr;
    Entry& e = entries_[count_++];
    e.head = head_of(tag, type, key_len);
    e.value_len = 0;
    if (copy_key) {
      char* at = arena_ + used_;
      std::memcpy(at, key.data(), key_len);
      used_ += key_len;
      e.key = at;
    } else {
      e.key = key.data();
    }
    sig_ = mix(sig_, e);
    size_ += wire;
    return &e;
  }

  __attribute__((noinline)) bool make_room(size_t wire, size_t copy_bytes) {
    if (size_ + wire > kMaxBytes || used_ + copy_bytes > kMaxBytes) {
      DFTRACER_LOG_WARN("EventDraft: args over %zu bytes, arg dropped",
                        kMaxBytes);
      return false;
    }
    if (count_ == entries_cap_) {
      const uint16_t cap = static_cast<uint16_t>(entries_cap_ * 2);
      std::unique_ptr<Entry[]> heap(new Entry[cap]);
      std::memcpy(heap.get(), entries_, count_ * sizeof(Entry));
      heap_entries_ = std::move(heap);
      entries_ = heap_entries_.get();
      entries_cap_ = cap;
    }
    if (used_ + copy_bytes > arena_cap_) {
      const size_t cap =
          std::min(kMaxBytes, std::max(used_ + copy_bytes, arena_cap_ * 2));
      std::unique_ptr<char[]> heap(new char[cap]);
      if (used_ != 0) std::memcpy(heap.get(), arena_, used_);
      const char* old = arena_;
      const size_t old_cap = arena_cap_;
      heap_arena_ = std::move(heap);
      arena_ = heap_arena_.get();
      arena_cap_ = cap;
      // Copied keys and strings point into the arena; move them with it.
      for (uint16_t i = 0; i < count_; ++i) {
        Entry& e = entries_[i];
        if (e.key >= old && e.key < old + old_cap)
          e.key = arena_ + (e.key - old);
        if (e.is_string() && e.str >= old && e.str < old + old_cap)
          e.str = arena_ + (e.str - old);
      }
    }
    return true;
  }

  __attribute__((always_inline)) void put_string(std::string_view key,
                                                 bool copy_key, ShmValueTag tag,
                                                 MetadataType type,
                                                 const char* s, size_t n) {
    Entry* e = add(key, copy_key, tag, type, sizeof(uint32_t) + n, n);
    if (e == nullptr) return;
    char* at = arena_ + used_;
    if (n != 0) std::memcpy(at, s, n);
    used_ += n;
    e->str = at;
    e->value_len = static_cast<uint32_t>(n);
  }

  template <typename T>
  __attribute__((always_inline)) void put(std::string_view key, bool copy_key,
                                          const T& v, MetadataType type) {
    if constexpr (std::is_same_v<T, RawJson>) {
      put_string(key, copy_key, SHM_VALUE_RAW_JSON, type, v.value.data(),
                 v.value.size());
    } else if constexpr (std::is_same_v<T, ArgList>) {
      std::string json;
      StringWriter w{json};
      metadata_json::list(w, v);
      put_string(key, copy_key, SHM_VALUE_RAW_JSON, type, json.data(),
                 json.size());
    } else if constexpr (std::is_same_v<T, std::string> ||
                         std::is_same_v<T, std::string_view>) {
      put_string(key, copy_key, SHM_VALUE_STRING, type, v.data(), v.size());
    } else if constexpr (std::is_same_v<T, const char*> ||
                         std::is_same_v<T, char*>) {
      put_string(key, copy_key, SHM_VALUE_STRING, type, v == nullptr ? "" : v,
                 v == nullptr ? 0 : std::strlen(v));
    } else {
      Entry* e = add(key, copy_key, tag_of<T>(), type, 8, 0);
      if (e == nullptr) return;
      e->bits = bits_of(v);
    }
  }

  template <typename T>
  static constexpr ShmValueTag tag_of() {
    return std::is_floating_point_v<T> ? SHM_VALUE_DOUBLE
           : std::is_signed_v<T>       ? SHM_VALUE_INT
                                       : SHM_VALUE_UINT;
  }
  template <typename T>
  __attribute__((always_inline)) static uint64_t bits_of(T v) {
    if constexpr (std::is_floating_point_v<T>) {
      const double d = static_cast<double>(v);
      uint64_t bits;
      std::memcpy(&bits, &d, 8);
      return bits;
    } else if constexpr (std::is_signed_v<T>) {
      return static_cast<uint64_t>(static_cast<int64_t>(v));
    } else {
      return static_cast<uint64_t>(v);
    }
  }

  Entry inline_entries_[kInlineEntries];
  char inline_arena_[kInlineBytes];
  Entry* entries_ = inline_entries_;
  char* arena_ = inline_arena_;
  uint16_t entries_cap_ = kInlineEntries;
  size_t arena_cap_ = kInlineBytes;
  std::unique_ptr<Entry[]> heap_entries_;
  std::unique_ptr<char[]> heap_arena_;
  size_t size_ = 0;
  size_t used_ = 0;
  uint16_t count_ = 0;
  // A bit per key hash; a key whose bit is clear was never set, so set()
  // skips the search for it.
  uint64_t seen_ = 0;
  uint64_t sig_ = 0;
  bool shaped_ = true;
  const void* site_ = nullptr;
};

/** Largest varint, and room for the head bytes of an event field. */
constexpr size_t kCompactVarintMax = 10;

/** An upper bound of a compact event of these sizes (Encoder::bound). */
constexpr size_t compact_bound(size_t name, size_t category, size_t args_size,
                               size_t count) {
  return 2 + 8 * kCompactVarintMax + name + category + args_size +
         count * (2 * kCompactVarintMax);
}

/**
 * What one call site of DFT_SITE_END logs every time: its event's name,
 * category and type, and the site, sig(), size() and count() of a draft
 * that holds its args. Built once per site.
 */
struct SiteInfo {
  const void* site;
  std::string_view event_name;
  std::string_view category;
  TraceEventType type;
  uint64_t sig;
  size_t args_size;
  uint16_t count;
  // Small and dense across the sites of a process; it only speeds up lookups.
  uint32_t id;
  size_t bound;  // compact_bound() of its events

  template <size_t N>
  static SiteInfo of(const void* site, std::string_view event_name,
                     std::string_view category, TraceEventType type,
                     const EventDraft::Entry (&args)[N]) {
    static_assert(N <= EventDraft::kShapeKeys);
    SiteInfo info{site,
                  event_name,
                  category,
                  type,
                  0,
                  0,
                  static_cast<uint16_t>(N),
                  next_id().fetch_add(1, std::memory_order_relaxed),
                  0};
    for (const EventDraft::Entry& e : args) {
      info.sig = EventDraft::mix(info.sig, e);
      info.args_size += EventDraft::wire_size(e);
    }
    info.bound =
        compact_bound(event_name.size(), category.size(), info.args_size, N);
    return info;
  }

 private:
  static std::atomic<uint32_t>& next_id() {
    static std::atomic<uint32_t> next{0};
    return next;
  }
};

/**
 * One event of a DFT_SITE_END call site: what BufferManager::log_data_event()
 * takes, with its args as the info->count entries at `args`.
 */
struct SiteEvent {
  const SiteInfo* info;
  int index;
  TimeResolution start_time;
  TimeResolution duration;
  const EventDraft::Entry* args;
  ProcessID process_id;
  ThreadID tid;
};

/**
 * The draft of one interceptor, on its stack; null when `active` is false.
 * Nested interceptors on the same thread each get their own.
 */
class DraftScope {
 public:
  explicit DraftScope(bool active) : active_(active) {}
  DraftScope(const DraftScope&) = delete;
  DraftScope& operator=(const DraftScope&) = delete;
  EventDraft* get() { return active_ ? &draft_ : nullptr; }

 private:
  EventDraft draft_;
  bool active_;
};

}  // namespace dftracer
#endif  // DFTRACER_COMMON_EVENT_DRAFT_H
