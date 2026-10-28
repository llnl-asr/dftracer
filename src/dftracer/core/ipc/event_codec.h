#ifndef DFTRACER_IPC_EVENT_CODEC_H
#define DFTRACER_IPC_EVENT_CODEC_H

#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/typedef.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace dftracer {
namespace ipc {

/**
 * Binary payloads of the records a traced process pushes into the ring in
 * producer/consumer mode (docs/design_pc_service.rst, "Records"). The service
 * decodes them and does the JSON, aggregation and compression; the producer
 * only copies fields and strings, so an event costs a few hundred bytes of
 * stores instead of a stringstream.
 *
 * All integers are little-endian (the producer and the service run on the same
 * node) and unaligned; strings are length-prefixed and not NUL-terminated.
 * Every record starts with a fixed head; the variable part follows it:
 *
 *   DATA / COUNTER  EventHead | name | category | meta_count x MetaEntry
 *   MetaEntry       u8 ShmValueTag | u8 MetadataType | u16 key_len | key |
 *                   value: 8 bytes (INT, UINT, DOUBLE) or u32 len + bytes
 *   METADATA        MetadataHead | name | value | record_name
 *   RECORD          RecordHead | record_name | fields
 *   HELLO           HelloHead | hostname_hash | log_file
 *   SET_APP_NAME    name
 *   SET_RANK        i32 rank
 *   GOODBYE         i32 index
 *
 * The fixed heads (EventHead, ...) and the decoded forms are in
 * core/common/datastructure.h.
 */

/** Appends to a reusable byte vector; one per producer thread. */
class ByteWriter {
 public:
  explicit ByteWriter(std::vector<uint8_t>& out) : out_(out) { out_.clear(); }

  uint8_t* grow(size_t n) {
    const size_t at = out_.size();
    out_.resize(at + n);
    return out_.data() + at;
  }
  template <typename T>
  void put(const T& value) {
    static_assert(std::is_trivially_copyable<T>::value, "POD only");
    std::memcpy(grow(sizeof(T)), &value, sizeof(T));
  }
  void put_bytes(const void* data, size_t n) {
    if (n != 0) std::memcpy(grow(n), data, n);
  }
  size_t size() const { return out_.size(); }
  uint8_t* data() { return out_.data(); }

 private:
  std::vector<uint8_t>& out_;
};

inline uint16_t short_len(const char* s, size_t* n) {
  size_t len = s == nullptr ? 0 : std::strlen(s);
  if (len > 0xffff) len = 0xffff;
  *n = len;
  return static_cast<uint16_t>(len);
}

namespace detail {
template <typename T>
inline void put_number_value(ByteWriter& w, T value) {
  if constexpr (std::is_floating_point<T>::value) {
    w.put(static_cast<double>(value));
  } else if constexpr (std::is_signed<T>::value) {
    w.put(static_cast<int64_t>(value));
  } else {
    w.put(static_cast<uint64_t>(value));
  }
}
inline void put_key(ByteWriter& w, uint8_t tag, uint8_t mtype,
                    const std::string& key) {
  w.put(tag);
  w.put(mtype);
  const uint16_t len =
      static_cast<uint16_t>(key.size() > 0xffff ? 0xffff : key.size());
  w.put(len);
  w.put_bytes(key.data(), len);
}
inline void put_long_string(ByteWriter& w, const char* s, size_t n) {
  w.put(static_cast<uint32_t>(n));
  w.put_bytes(s, n);
}
}  // namespace detail

/**
 * Append the metadata entries; returns how many were written. Mirrors
 * JsonLines::convert_metadata: RawJson, then the numeric types, then the
 * string types. A value of any other type is written as an empty string so the
 * key is not lost silently.
 */
inline uint16_t encode_metadata(ByteWriter& w, const Metadata* metadata) {
  if (metadata == nullptr) return 0;
  uint16_t count = 0;
  for (const auto& item : *metadata) {
    if (count == 0xffff) break;
    const std::string& key = item.first;
    const uint8_t mtype = static_cast<uint8_t>(std::get<0>(item.second));
    const std::any& value = std::get<1>(item.second);
    ++count;
    if (value.type() == typeid(RawJson)) {
      const auto& raw = std::any_cast<const RawJson&>(value);
      detail::put_key(w, SHM_VALUE_RAW_JSON, mtype, key);
      detail::put_long_string(w, raw.value.data(), raw.value.size());
      continue;
    }
    DFTRACER_FOR_EACH_NUMERIC_TYPE(DFTRACER_ANY_CAST_MACRO, value, {
      using V = std::decay_t<decltype(res.value())>;
      uint8_t tag = std::is_floating_point<V>::value ? SHM_VALUE_DOUBLE
                    : std::is_signed<V>::value       ? SHM_VALUE_INT
                                                     : SHM_VALUE_UINT;
      detail::put_key(w, tag, mtype, key);
      detail::put_number_value(w, res.value());
      continue;
    });
    if (value.type() == typeid(std::string)) {
      const auto& str = std::any_cast<const std::string&>(value);
      detail::put_key(w, SHM_VALUE_STRING, mtype, key);
      detail::put_long_string(w, str.data(), str.size());
      continue;
    }
    const char* cstr = nullptr;
    if (value.type() == typeid(const char*)) {
      cstr = std::any_cast<const char*>(value);
    } else if (value.type() == typeid(HashType)) {
      cstr = std::any_cast<HashType>(value);
    }
    detail::put_key(w, SHM_VALUE_STRING, mtype, key);
    detail::put_long_string(w, cstr == nullptr ? "" : cstr,
                            cstr == nullptr ? 0 : std::strlen(cstr));
  }
  return count;
}

/** DATA or COUNTER payload. Does not take ownership of `metadata`. */
inline void encode_event(ByteWriter& w, int index, ConstEventNameType name,
                         ConstEventNameType category, TraceEventType type,
                         TimeResolution start_time, TimeResolution duration,
                         const Metadata* metadata, ProcessID process_id,
                         ThreadID thread_id, uint8_t flags = 0) {
  EventHead head{};
  size_t name_n, category_n;
  head.start_time = start_time;
  head.duration = duration;
  head.thread_id = thread_id;
  head.index = index;
  head.process_id = process_id;
  head.type = static_cast<uint8_t>(type);
  head.flags = static_cast<uint8_t>(
      flags | (metadata != nullptr ? SHM_EVENT_HAS_METADATA : 0));
  head.name_len = short_len(name, &name_n);
  head.category_len = short_len(category, &category_n);
  w.put(head);
  w.put_bytes(name, name_n);
  w.put_bytes(category, category_n);
  const uint16_t count = encode_metadata(w, metadata);
  std::memcpy(w.data() + offsetof(EventHead, meta_count), &count,
              sizeof(count));
}

inline void encode_metadata_event(ByteWriter& w, ConstEventNameType name,
                                  ConstEventNameType value,
                                  ConstEventNameType record_name,
                                  TraceEventType type, ProcessID process_id,
                                  ThreadID thread_id, bool is_string) {
  MetadataHead head{};
  size_t name_n, record_n;
  const size_t value_n = value == nullptr ? 0 : std::strlen(value);
  head.thread_id = thread_id;
  head.process_id = process_id;
  head.type = static_cast<uint8_t>(type);
  head.is_string = is_string ? 1 : 0;
  head.name_len = short_len(name, &name_n);
  head.value_len = static_cast<uint32_t>(value_n);
  head.record_name_len = short_len(record_name, &record_n);
  w.put(head);
  w.put_bytes(name, name_n);
  w.put_bytes(value, value_n);
  w.put_bytes(record_name, record_n);
}

inline void encode_record(ByteWriter& w, ConstEventNameType record_name,
                          const char* fields, TraceEventType type,
                          ProcessID process_id, ThreadID thread_id) {
  RecordHead head{};
  size_t record_n;
  const size_t fields_n = fields == nullptr ? 0 : std::strlen(fields);
  head.thread_id = thread_id;
  head.process_id = process_id;
  head.type = static_cast<uint8_t>(type);
  head.record_name_len = short_len(record_name, &record_n);
  head.fields_len = static_cast<uint32_t>(fields_n);
  w.put(head);
  w.put_bytes(record_name, record_n);
  w.put_bytes(fields, fields_n);
}

inline void encode_hello(ByteWriter& w, ProcessID process_id,
                         const char* hostname_hash, const char* log_file) {
  HelloHead head{};
  size_t hash_n, file_n;
  head.process_id = process_id;
  head.hostname_hash_len = short_len(hostname_hash, &hash_n);
  head.log_file_len = short_len(log_file, &file_n);
  w.put(head);
  w.put_bytes(hostname_hash, hash_n);
  w.put_bytes(log_file, file_n);
}

/** Bounds-checked reader over one record payload. */
class ByteReader {
 public:
  ByteReader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}
  template <typename T>
  bool get(T& out) {
    if (static_cast<size_t>(end_ - p_) < sizeof(T)) return false;
    std::memcpy(&out, p_, sizeof(T));
    p_ += sizeof(T);
    return true;
  }
  bool view(size_t n, std::string_view& out) {
    if (static_cast<size_t>(end_ - p_) < n) return false;
    out = std::string_view(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    return true;
  }

 private:
  const uint8_t* p_;
  const uint8_t* end_;
};

/**
 * Decode a DATA or COUNTER payload. `size` may include the record's alignment
 * slack after the last entry. Returns false on a truncated or malformed
 * record.
 */
inline bool decode_event(const uint8_t* data, size_t size, DecodedEvent& out) {
  ByteReader r(data, size);
  if (!r.get(out.head)) return false;
  if (!r.view(out.head.name_len, out.name)) return false;
  if (!r.view(out.head.category_len, out.category)) return false;
  out.values.clear();
  out.values.reserve(out.head.meta_count);
  for (uint16_t i = 0; i < out.head.meta_count; ++i) {
    DecodedValue v{};
    uint8_t tag, mtype;
    uint16_t key_len;
    if (!r.get(tag) || !r.get(mtype) || !r.get(key_len)) return false;
    if (!r.view(key_len, v.key)) return false;
    v.tag = static_cast<ShmValueTag>(tag);
    v.mtype = static_cast<MetadataType>(mtype);
    switch (v.tag) {
      case SHM_VALUE_INT:
        if (!r.get(v.i)) return false;
        break;
      case SHM_VALUE_UINT:
        if (!r.get(v.u)) return false;
        break;
      case SHM_VALUE_DOUBLE:
        if (!r.get(v.d)) return false;
        break;
      case SHM_VALUE_STRING:
      case SHM_VALUE_RAW_JSON: {
        uint32_t len;
        if (!r.get(len) || !r.view(len, v.s)) return false;
        break;
      }
      default:
        return false;
    }
    out.values.push_back(v);
  }
  return true;
}

inline bool decode_metadata_event(const uint8_t* data, size_t size,
                                  DecodedMetadata& out) {
  ByteReader r(data, size);
  return r.get(out.head) && r.view(out.head.name_len, out.name) &&
         r.view(out.head.value_len, out.value) &&
         r.view(out.head.record_name_len, out.record_name);
}

inline bool decode_record(const uint8_t* data, size_t size,
                          DecodedRecord& out) {
  ByteReader r(data, size);
  return r.get(out.head) && r.view(out.head.record_name_len, out.record_name) &&
         r.view(out.head.fields_len, out.fields);
}

inline bool decode_hello(const uint8_t* data, size_t size, DecodedHello& out) {
  ByteReader r(data, size);
  return r.get(out.head) &&
         r.view(out.head.hostname_hash_len, out.hostname_hash) &&
         r.view(out.head.log_file_len, out.log_file);
}

}  // namespace ipc
}  // namespace dftracer

#endif  // DFTRACER_IPC_EVENT_CODEC_H
