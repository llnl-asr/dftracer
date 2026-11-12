#ifndef DFTRACER_COMMON_EVENT_DRAFT_H
#define DFTRACER_COMMON_EVENT_DRAFT_H
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/serialization/line_format.h>
#include <dftracer/core/serialization/metadata_json.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace dftracer {

/**
 * The args of one intercepted call, encoded while the interceptor runs as the
 * ring's metadata entries (ipc/event_codec.h: u8 ShmValueTag | u8
 * MetadataType | u16 key_len | key | 8-byte number or u32 len + bytes), so an
 * event reaches its buffer without a Metadata.
 *
 * set() follows Metadata::insert_or_assign: a key set again is replaced, and
 * a value of a type Metadata does not store removes the key. Args past
 * DFTRACER_SERIALIZATION_EVENT_MAX bytes are dropped.
 */
class EventDraft {
 public:
  EventDraft() { bytes_.resize(kInitialBytes); }

  void reset() {
    size_ = 0;
    count_ = 0;
    seen_ = 0;
  }
  bool empty() const { return count_ == 0; }
  const uint8_t* data() const { return bytes_.data(); }
  size_t size() const { return size_; }
  uint16_t count() const { return count_; }

  /** One entry of data(), as set() wrote it. */
  struct Entry {
    ShmValueTag tag;
    MetadataType mtype;
    std::string_view key;
    const uint8_t* value;  // 8 bytes, or the bytes of a string
    uint32_t value_len;    // a string's length
    size_t size;           // the whole entry
  };
  __attribute__((always_inline)) static Entry entry_at(const uint8_t* p) {
    Entry e;
    e.tag = static_cast<ShmValueTag>(p[0]);
    e.mtype = static_cast<MetadataType>(p[1]);
    uint16_t key_len;
    std::memcpy(&key_len, p + 2, sizeof(key_len));
    e.key = std::string_view(reinterpret_cast<const char*>(p + 4), key_len);
    const uint8_t* v = p + 4 + key_len;
    if (e.tag == SHM_VALUE_STRING || e.tag == SHM_VALUE_RAW_JSON) {
      std::memcpy(&e.value_len, v, sizeof(e.value_len));
      v += sizeof(e.value_len);
      e.size = static_cast<size_t>(v - p) + e.value_len;
    } else {
      e.value_len = 0;
      e.size = static_cast<size_t>(v - p) + 8;
    }
    e.value = v;
    return e;
  }

  template <typename V>
  __attribute__((always_inline)) void set(
      std::string_view key, V&& value,
      MetadataType type = MetadataType::MT_KEY) {
    using D = std::decay_t<V>;
    const uint64_t bit = key_bit(key);
    size_t slot = kNoSlot;
    if (__builtin_expect((seen_ & bit) != 0, 0)) slot = remove(key);
    seen_ |= bit;
    const size_t last = size_;
    if constexpr (detail::is_metadata_value_v<D>) {
      if constexpr (std::is_same_v<D, MetadataValue>) {
        std::visit([&](const auto& v) { put(key, v, type); }, value);
      } else if constexpr (std::is_enum_v<D>) {
        put(key, static_cast<std::underlying_type_t<D>>(value), type);
      } else {
        put<D>(key, value, type);
      }
      if (slot != kNoSlot && size_ != last) move_last_to(slot, last);
    }
  }
  template <typename V>
  __attribute__((always_inline)) void set(
      StaticKey key, V&& value, MetadataType type = MetadataType::MT_KEY) {
    set(key.name, std::forward<V>(value), type);
  }
  /** Every entry of `metadata`, in its order. */
  void set_all(const Metadata& metadata) {
    for (const auto& entry : metadata)
      set(entry.first.view(), std::get<1>(entry.second),
          std::get<0>(entry.second));
  }

 private:
  static constexpr size_t kInitialBytes = 512;
  static constexpr size_t kMaxBytes = DFTRACER_SERIALIZATION_EVENT_MAX;
  static constexpr size_t kNoSlot = ~size_t{0};
  struct StringWriter {
    std::string& out;
    void raw(std::string_view s) { out.append(s.data(), s.size()); }
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

  // Drops the entry of `key`; returns its byte offset, so a replacement keeps
  // the position Metadata::insert_or_assign keeps.
  __attribute__((noinline)) size_t remove(std::string_view key) {
    uint8_t* base = bytes_.data();
    for (size_t at = 0; at < size_;) {
      const Entry e = entry_at(base + at);
      if (e.key == key) {
        std::memmove(base + at, base + at + e.size, size_ - at - e.size);
        size_ -= e.size;
        --count_;
        return at;
      }
      at += e.size;
    }
    return kNoSlot;
  }

  // Moves the entry that starts at byte `last` (the newest) to byte `at`.
  __attribute__((noinline)) void move_last_to(size_t at, size_t last) {
    std::rotate(bytes_.data() + at, bytes_.data() + last,
                bytes_.data() + size_);
  }

  __attribute__((always_inline)) uint8_t* grow(size_t n) {
    if (__builtin_expect(size_ + n > bytes_.size(), 0) && !grow_slow(n))
      return nullptr;
    uint8_t* at = bytes_.data() + size_;
    size_ += n;
    return at;
  }

  __attribute__((noinline)) bool grow_slow(size_t n) {
    if (size_ + n > kMaxBytes) {
      DFTRACER_LOG_WARN("EventDraft: args over %zu bytes, arg dropped",
                        kMaxBytes);
      return false;
    }
    bytes_.resize(std::min(kMaxBytes, std::max(size_ + n, bytes_.size() * 2)));
    return true;
  }

  // Writes the entry head and reserves `value_len` bytes for the value.
  __attribute__((always_inline)) uint8_t* begin_entry(std::string_view key,
                                                      ShmValueTag tag,
                                                      MetadataType type,
                                                      size_t value_len) {
    const uint16_t key_len =
        static_cast<uint16_t>(key.size() > 0xffff ? 0xffff : key.size());
    uint8_t* p = grow(4 + key_len + value_len);
    if (p == nullptr) return nullptr;
    p[0] = static_cast<uint8_t>(tag);
    p[1] = static_cast<uint8_t>(type);
    std::memcpy(p + 2, &key_len, sizeof(key_len));
    std::memcpy(p + 4, key.data(), key_len);
    ++count_;
    return p + 4 + key_len;
  }

  __attribute__((always_inline)) void put_string(std::string_view key,
                                                 ShmValueTag tag,
                                                 MetadataType type,
                                                 const char* s, size_t n) {
    uint8_t* p = begin_entry(key, tag, type, sizeof(uint32_t) + n);
    if (p == nullptr) return;
    const uint32_t len = static_cast<uint32_t>(n);
    std::memcpy(p, &len, sizeof(len));
    if (n != 0) std::memcpy(p + sizeof(len), s, n);
  }

  template <typename T>
  __attribute__((always_inline)) void put(std::string_view key, const T& v,
                                          MetadataType type) {
    if constexpr (std::is_same_v<T, RawJson>) {
      put_string(key, SHM_VALUE_RAW_JSON, type, v.value.data(), v.value.size());
    } else if constexpr (std::is_same_v<T, ArgList>) {
      std::string json;
      StringWriter w{json};
      metadata_json::list(w, v);
      put_string(key, SHM_VALUE_RAW_JSON, type, json.data(), json.size());
    } else if constexpr (std::is_same_v<T, std::string> ||
                         std::is_same_v<T, std::string_view>) {
      put_string(key, SHM_VALUE_STRING, type, v.data(), v.size());
    } else if constexpr (std::is_same_v<T, const char*> ||
                         std::is_same_v<T, char*>) {
      put_string(key, SHM_VALUE_STRING, type, v == nullptr ? "" : v,
                 v == nullptr ? 0 : std::strlen(v));
    } else {
      constexpr ShmValueTag tag = std::is_floating_point_v<T> ? SHM_VALUE_DOUBLE
                                  : std::is_signed_v<T>       ? SHM_VALUE_INT
                                                              : SHM_VALUE_UINT;
      uint8_t* p = begin_entry(key, tag, type, 8);
      if (p == nullptr) return;
      if constexpr (std::is_floating_point_v<T>) {
        const double d = static_cast<double>(v);
        std::memcpy(p, &d, 8);
      } else if constexpr (std::is_signed_v<T>) {
        const int64_t i = static_cast<int64_t>(v);
        std::memcpy(p, &i, 8);
      } else {
        const uint64_t u = static_cast<uint64_t>(v);
        std::memcpy(p, &u, 8);
      }
    }
  }

  std::vector<uint8_t> bytes_;
  size_t size_ = 0;
  uint16_t count_ = 0;
  // A bit per key hash; a key whose bit is clear was never set, so set()
  // skips the search for it.
  uint64_t seen_ = 0;
};

/**
 * The calling thread's draft for one interceptor, from construction to
 * destruction; null when `active` is false. Nested interceptors on the same
 * thread each get their own.
 */
class DraftScope {
 public:
  explicit DraftScope(bool active) {
    if (!active) return;
    Pool& pool = pool_();
    if (pool.drafts == nullptr && !pool.exited) {
      pool.drafts = new EventDraft[kPooled];
      arm_reaper();
    }
    if (pool.drafts != nullptr && pool.depth < kPooled) {
      draft_ = &pool.drafts[pool.depth++];
      pool_of_ = &pool;
    } else {
      draft_ = new EventDraft();
    }
    draft_->reset();
  }
  ~DraftScope() {
    if (pool_of_ != nullptr)
      --pool_of_->depth;
    else
      delete draft_;
  }
  DraftScope(const DraftScope&) = delete;
  DraftScope& operator=(const DraftScope&) = delete;
  EventDraft* get() const { return draft_; }

 private:
  static constexpr uint32_t kPooled = 4;
  // Trivially destructible, so one TLS lookup with no init guard reads it,
  // and it stays readable when the thread's exit handlers log after the
  // reaper freed the drafts.
  struct Pool {
    EventDraft* drafts;
    uint32_t depth;
    bool exited;
  };
  struct Reaper {
    ~Reaper() {
      Pool& pool = pool_();
      pool.exited = true;
      delete[] pool.drafts;
      pool.drafts = nullptr;
    }
  };
  static Pool& pool_() {
    thread_local Pool pool{};
    return pool;
  }
  static void arm_reaper() {
    thread_local Reaper reaper;
    (void)reaper;
  }
  EventDraft* draft_ = nullptr;
  Pool* pool_of_ = nullptr;
};

}  // namespace dftracer
#endif  // DFTRACER_COMMON_EVENT_DRAFT_H
