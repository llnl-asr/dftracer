#ifndef DFTRACER_CORE_DATASTRUCTURE_H
#define DFTRACER_CORE_DATASTRUCTURE_H

// Internal
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/macros.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/utils/utils.h>

// standard headers
#include <stddef.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <variant>
#include <vector>

namespace dftracer {

// A metadata value that is already a serialized JSON fragment (an object or
// array literal) and must be embedded into "args" verbatim, unquoted, rather
// than escaped as a string. Lets a single args key nest a whole object (e.g.
// "cfg": {"enable":1,...}) instead of flattening it into many dotted scalar
// keys, which is both more compact and easier to query with jq.
struct RawJson {
  std::string value;
  explicit RawJson(std::string v) : value(std::move(v)) {}
};

// A list arg built by DFTracer::append(): "key":[...], or "key":{"sub":[...],
// ...} when nested. Values are kept as given -- strings and sub names by
// pointer, so they must outlive the event, like the strings of update() --
// and written as JSON with the event (serialization/metadata_json.h).
struct ArgList {
  struct Value {
    enum Kind : uint8_t { STRING, INT, ID } kind;  // ID: 16 hex digits
    union {
      const char* s;
      int64_t i;
      uint64_t u;
    };
  };
  struct Sub {
    const char* name;  // nullptr in a plain list
    std::vector<Value> values;
  };
  bool nested;
  std::vector<Sub> subs;
};

/**
 * A metadata value. Integers are held as int64_t or uint64_t and floats as
 * double, so one number has one representation however the caller typed it;
 * strings are held by pointer (const char*, which must outlive the event) or
 * by value (std::string). Use make_metadata_value() to convert.
 */
using MetadataValue = std::variant<int64_t, uint64_t, double, const char*,
                                   std::string, RawJson, ArgList>;

namespace detail {
template <typename T>
inline constexpr bool is_char_like_v =
    std::is_same_v<T, char> || std::is_same_v<T, signed char> ||
    std::is_same_v<T, unsigned char> || std::is_same_v<T, wchar_t> ||
    std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;
// Whether make_metadata_value() takes T. insert() skips any other type,
// such as an MPI handle under Open MPI, an HDF5 void* or an H5O_token_t, as
// the std::any writer did.
template <typename T>
inline constexpr bool is_metadata_value_v =
    std::is_same_v<T, MetadataValue> ||
    (std::is_arithmetic_v<T> && !is_char_like_v<T>) || std::is_enum_v<T> ||
    std::is_same_v<T, char*> || std::is_same_v<T, const char*> ||
    std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> ||
    std::is_same_v<T, RawJson> || std::is_same_v<T, ArgList>;
}  // namespace detail

/** The MetadataValue alternative that holds `v`; an enum is stored as its
 * underlying integer and a bool as 0 or 1 (hbool_t is unsigned before HDF5
 * 1.14 and bool since). A type with none does not compile. */
template <typename T>
inline MetadataValue make_metadata_value(T&& v) {
  using D = std::decay_t<T>;
  if constexpr (std::is_same_v<D, MetadataValue>) {
    return std::forward<T>(v);
  } else if constexpr (std::is_same_v<D, bool>) {
    return MetadataValue(std::in_place_type<uint64_t>, v ? 1u : 0u);
  } else if constexpr (detail::is_char_like_v<D>) {
    static_assert(!sizeof(D), "char is not a metadata value");
  } else if constexpr (std::is_enum_v<D>) {
    return make_metadata_value(static_cast<std::underlying_type_t<D>>(v));
  } else if constexpr (std::is_integral_v<D> && std::is_signed_v<D>) {
    return MetadataValue(std::in_place_type<int64_t>, static_cast<int64_t>(v));
  } else if constexpr (std::is_integral_v<D>) {
    return MetadataValue(std::in_place_type<uint64_t>,
                         static_cast<uint64_t>(v));
  } else if constexpr (std::is_floating_point_v<D>) {
    return MetadataValue(std::in_place_type<double>, static_cast<double>(v));
  } else if constexpr (std::is_same_v<D, char*> ||
                       std::is_same_v<D, const char*>) {
    return MetadataValue(std::in_place_type<const char*>,
                         static_cast<const char*>(v));
  } else if constexpr (std::is_same_v<D, std::string> ||
                       std::is_same_v<D, std::string_view>) {
    return MetadataValue(std::in_place_type<std::string>,
                         std::string(std::forward<T>(v)));
  } else if constexpr (std::is_same_v<D, RawJson> ||
                       std::is_same_v<D, ArgList>) {
    return MetadataValue(std::in_place_type<D>, std::forward<T>(v));
  } else {
    static_assert(!sizeof(D), "unsupported metadata value type");
  }
}

class Metadata {
 public:
  using Value = std::tuple<MetadataType, MetadataValue, std::size_t>;
  using Entry = std::pair<std::string, Value>;

 private:
  /**
   * Entries in insertion order, in one vector. An event has a handful of
   * keys, so a linear scan finds one faster than hashing it would, and the
   * vector holds them without a heap node each: with the per-thread map of
   * acquire()/release(), whose clear() keeps the vector's capacity, an event
   * whose keys and values fit std::string's inline buffer allocates
   * nothing.
   */
  using DataMap = std::vector<Entry>;
  DataMap data;
  // Whether the MT_KEY hashes of this map's entries are computed (see
  // defer_key_hashes). Fixed when the map is created.
  bool hashed_ = !defer_.load(std::memory_order_relaxed);
  inline static std::atomic<bool> defer_{true};
  // The thread's cached map (acquire/release). Plain pointer and flag so they
  // stay readable after the guard has run at thread exit: a thread's exit
  // handlers may still log.
  inline static thread_local Metadata* tl_cached_ = nullptr;
  inline static thread_local bool tl_exited_ = false;
  struct CacheGuard {
    ~CacheGuard() {
      tl_exited_ = true;
      delete tl_cached_;
      tl_cached_ = nullptr;
    }
  };
  inline static thread_local CacheGuard tl_guard_;
  std::size_t key_hash(const MetadataValue& value,
                       const MetadataType& type) const {
    return hashed_ ? get_hash(value, type) : 0;
  }
  std::size_t get_hash(const MetadataValue& value,
                       const MetadataType& type) const {
    if (type != MetadataType::MT_KEY) {
      return 0;
    }
    return std::visit(
        [](const auto& v) -> std::size_t {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_arithmetic_v<T>) {
            return std::hash<std::string>()(std::to_string(v));
          } else if constexpr (std::is_same_v<T, const char*>) {
            return v == nullptr ? 0 : std::hash<std::string_view>()(v);
          } else if constexpr (std::is_same_v<T, std::string>) {
            return std::hash<std::string>()(v);
          } else {
            return 0;
          }
        },
        value);
  }

 public:
  using iterator = DataMap::iterator;
  using const_iterator = DataMap::const_iterator;

  Metadata() { data.reserve(8); }
  ~Metadata() {}
  /**
   * Every MT_KEY entry carries a hash of its value (the third element), for
   * the aggregation key -- a std::to_string and std::hash each. Most events
   * are never aggregated, so by default inserts store 0 and the AggregatedKey
   * built from the map computes them (ensure_key_hashes()). false makes the
   * maps created afterwards hash on insert.
   */
  static void defer_key_hashes(bool defer) {
    defer_.store(defer, std::memory_order_relaxed);
  }
  /**
   * An event's Metadata from the calling thread's cache, or a new one. An
   * event's map lives from its interceptor (or API region) to the buffer
   * manager that writes it, on one thread, so each thread keeps the last one
   * it released and reuses it: clear() keeps the entry vector's capacity, so
   * its allocation is paid once per thread instead of once per event. A map
   * from acquire() is an ordinary heap object: deleting it instead of
   * releasing it is fine.
   */
  static Metadata* acquire() {
    Metadata* m = tl_cached_;
    if (m == nullptr) return new Metadata();
    tl_cached_ = nullptr;
    m->hashed_ = !defer_.load(std::memory_order_relaxed);
    return m;
  }
  /** Gives an event's map back (see acquire()); deletes it when the thread
   * already holds one or is exiting. Null is ignored. */
  static void release(Metadata* m) {
    if (m == nullptr) return;
    if (tl_cached_ != nullptr || tl_exited_) {
      delete m;
      return;
    }
    (void)&tl_guard_;  // frees the cached map at thread exit
    m->data.clear();
    tl_cached_ = m;
  }

  // Computes the MT_KEY hashes of a map created with them deferred.
  void ensure_key_hashes() {
    if (hashed_) return;
    for (auto& entry : data)
      std::get<2>(entry.second) =
          get_hash(std::get<1>(entry.second), std::get<0>(entry.second));
    hashed_ = true;
  }
  iterator find(std::string_view key) {
    for (auto it = data.begin(); it != data.end(); ++it)
      if (it->first == key) return it;
    return data.end();
  }
  const_iterator find(std::string_view key) const {
    for (auto it = data.begin(); it != data.end(); ++it)
      if (it->first == key) return it;
    return data.end();
  }

  template <typename V>
  std::pair<iterator, bool> insert_or_assign(
      std::string_view key, V&& value,
      const MetadataType& type = MetadataType::MT_KEY) {
    if constexpr (!detail::is_metadata_value_v<std::decay_t<V>>) {
      erase(key);
      return {data.end(), false};
    } else {
      MetadataValue converted = make_metadata_value(std::forward<V>(value));
      const std::size_t hash = key_hash(converted, type);
      auto it = find(key);
      if (it != data.end()) {
        it->second = Value(type, std::move(converted), hash);
        return {it, false};
      }
      data.emplace_back(
          std::piecewise_construct, std::forward_as_tuple(key),
          std::forward_as_tuple(type, std::move(converted), hash));
      return {data.end() - 1, true};
    }
  }

  template <typename V>
  std::pair<iterator, bool> insert(
      std::string_view key, V&& value,
      const MetadataType& type = MetadataType::MT_KEY) {
    auto it = find(key);
    if (it != data.end()) return {it, false};
    if constexpr (!detail::is_metadata_value_v<std::decay_t<V>>) {
      return {data.end(), false};
    } else {
      MetadataValue converted = make_metadata_value(std::forward<V>(value));
      const std::size_t hash = key_hash(converted, type);
      data.emplace_back(
          std::piecewise_construct, std::forward_as_tuple(key),
          std::forward_as_tuple(type, std::move(converted), hash));
      return {data.end() - 1, true};
    }
  }

  bool contains(std::string_view key) const { return find(key) != data.end(); }

  size_t erase(std::string_view key) {
    auto it = find(key);
    if (it == data.end()) return 0;
    data.erase(it);
    return 1;
  }

  iterator erase(const_iterator it) { return data.erase(it); }

  size_t size() const { return data.size(); }

  bool empty() const { return data.empty(); }

  void clear() { data.clear(); }

  Value& operator[](std::string_view key) {
    auto it = find(key);
    if (it != data.end()) return it->second;
    data.emplace_back(std::piecewise_construct, std::forward_as_tuple(key),
                      std::forward_as_tuple());
    return data.back().second;
  }

  const Value& at(std::string_view key) const {
    auto it = find(key);
    if (it == data.end()) throw std::out_of_range("Metadata::at");
    return it->second;
  }

  iterator begin() { return data.begin(); }

  const_iterator begin() const { return data.begin(); }

  iterator end() { return data.end(); }

  const_iterator end() const { return data.end(); }

  std::string getTagValue(const std::string& tagKey) const;
};

/** Whether two values are the same alternative with equal contents. Strings
 * compare by content; RawJson and ArgList never match. */
inline bool metadata_value_equal(const MetadataValue& a,
                                 const MetadataValue& b) {
  if (a.index() != b.index()) return false;
  return std::visit(
      [&b](const auto& x) -> bool {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_arithmetic_v<T> ||
                      std::is_same_v<T, std::string>) {
          return x == *std::get_if<T>(&b);
        } else if constexpr (std::is_same_v<T, const char*>) {
          const char* y = *std::get_if<T>(&b);
          if (x == nullptr || y == nullptr) return x == y;
          return std::strcmp(x, y) == 0;
        } else {
          return false;
        }
      },
      a);
}

/** Where rules read tags.<key> of an event that has no Metadata: the
 * service's packed events (PackedAggregator). Same strings as getTagValue. */
struct TagSource {
  virtual ~TagSource() = default;
  virtual std::string tag(const std::string& key) const = 0;
};

struct AggregatedKey {
  std::string category;
  std::string event_name;
  TraceEventType type;
  TimeResolution time_interval;
  ThreadID thread_id;
  Metadata* additional_keys;
  /* These attributes are just holder for rules not used in aggregation-key */
  TimeResolution duration;
  const char* app_name;
  const int* rank;
  const TagSource* tag_source = nullptr;  // rules only, when no Metadata
  mutable size_t _cached_hash;            // Cached hash value

  AggregatedKey()
      : category(nullptr),
        event_name(nullptr),
        type(TraceEventType::TRACE_TYPE_UNKNOWN),
        time_interval(0),
        thread_id(0),
        additional_keys(nullptr),
        duration(0),
        app_name(nullptr),
        rank(nullptr),
        _cached_hash(0) {}

  AggregatedKey(ConstEventNameType category_, ConstEventNameType event_name_,
                TraceEventType type_, TimeResolution time_interval_,
                TimeResolution duration_, ThreadID thread_id_,
                Metadata* metadata_, const char* app_name_, const int* rank_)
      : category(category_),
        event_name(event_name_),
        type(type_),
        time_interval(time_interval_),
        thread_id(thread_id_),
        additional_keys(metadata_),
        duration(duration_),
        app_name(app_name_),
        rank(rank_),
        _cached_hash(0) {
    if (metadata_ != nullptr) metadata_->ensure_key_hashes();
  }
  AggregatedKey(const AggregatedKey& other)
      : category(other.category),
        event_name(other.event_name),
        type(other.type),
        time_interval(other.time_interval),
        thread_id(other.thread_id),
        additional_keys(other.additional_keys),
        duration(other.duration),
        app_name(other.app_name),
        rank(other.rank),
        tag_source(other.tag_source),
        _cached_hash(other._cached_hash) {}
  bool operator==(const AggregatedKey& other) const {
    if (category != other.category || event_name != other.event_name ||
        type != other.type || time_interval != other.time_interval ||
        thread_id != other.thread_id) {
      return false;
    }

    // The MT_KEY fields are part of the key; every other field is not. A
    // stored key holds only its MT_KEY fields (or nullptr for none) while a
    // live event's key holds all of its metadata, so compare the MT_KEY sets
    // alone, with nullptr as the empty set.
    return same_mt_keys(additional_keys, other.additional_keys) &&
           same_mt_keys(other.additional_keys, additional_keys);
  }

 private:
  // Every MT_KEY field of `a` is an MT_KEY field of `b` with an equal value.
  static bool same_mt_keys(const Metadata* a, const Metadata* b) {
    if (a == nullptr) return true;
    for (const auto& pair : *a) {
      if (std::get<0>(pair.second) != MetadataType::MT_KEY) continue;
      if (b == nullptr) return false;
      auto it = b->find(pair.first);
      if (it == b->end() || std::get<0>(it->second) != MetadataType::MT_KEY ||
          std::get<2>(it->second) != std::get<2>(pair.second) ||
          !metadata_value_equal(std::get<1>(it->second),
                                std::get<1>(pair.second))) {
        return false;
      }
    }
    return true;
  }
};

// Specialize std::hash for AggregatedKey
}  // namespace dftracer

namespace std {
template <>
struct hash<dftracer::AggregatedKey> {
  std::size_t operator()(const dftracer::AggregatedKey& key) const {
    // Use cached hash if available
    if (key._cached_hash != 0) {
      return key._cached_hash;
    }

    // Hash string members
    std::size_t h1 = std::hash<std::string>()(key.category);
    std::size_t h2 = std::hash<std::string>()(key.event_name);
    std::size_t h3 = std::hash<TimeResolution>()(key.time_interval);
    std::size_t h4 = std::hash<ThreadID>()(key.thread_id);
    std::size_t h6 = std::hash<uint8_t>()(static_cast<uint8_t>(key.type));

    std::size_t h5 = 0;
    if (key.additional_keys && !key.additional_keys->empty()) {
      for (const auto& pair : *key.additional_keys) {
        if (std::get<0>(pair.second) == MetadataType::MT_KEY) {
          h5 ^= std::hash<std::string>()(pair.first);
          h5 ^= std::get<2>(pair.second);
        }
      }
    }

    // Combine hashes
    std::size_t result = h1;
    result ^= h2 + 0x9e3779b9 + (result << 6) + (result >> 2);
    result ^= h3 + 0x9e3779b9 + (result << 6) + (result >> 2);
    result ^= h4 + 0x9e3779b9 + (result << 6) + (result >> 2);
    result ^= h5 + 0x9e3779b9 + (result << 6) + (result >> 2);
    result ^= h6 + 0x9e3779b9 + (result << 6) + (result >> 2);

    // Cache for next time
    const_cast<dftracer::AggregatedKey&>(key)._cached_hash = result;
    return result;
  }
};
}  // namespace std

namespace dftracer {

struct BaseAggregatedValue {
 public:
  BaseAggregatedValue* _child;
  ValueType _type;
  std::type_index _id;

 protected:
  BaseAggregatedValue(BaseAggregatedValue* child, ValueType type,
                      std::type_index id)
      : _child(child), _type(type), _id(id) {}

 public:
  virtual ~BaseAggregatedValue() = default;
  void update(BaseAggregatedValue* value);
  BaseAggregatedValue* get_value();
};

template <typename T>
struct AggregatedValue : public BaseAggregatedValue {
 protected:
  AggregatedValue(const AggregatedValue<T>& value, BaseAggregatedValue* child)
      : BaseAggregatedValue(child, ValueType::VALUE_TYPE_STRING, typeid(T)),
        count(value.count) {}
  AggregatedValue(const AggregatedValue<T>& value, BaseAggregatedValue* child,
                  ValueType id, std::type_index tid)
      : BaseAggregatedValue(child, id, tid), count(value.count) {}
  AggregatedValue(BaseAggregatedValue* child, ValueType id, std::type_index tid)
      : BaseAggregatedValue(child, id, tid), count(1) {}

 public:
  size_t count;
  void update(AggregatedValue<T>* value) { count += value->count; }
  AggregatedValue(T value)
      : BaseAggregatedValue(nullptr, ValueType::VALUE_TYPE_STRING, typeid(T)),
        count(1) {}
};

template <typename T>
struct NumberAggregationValue : public AggregatedValue<T> {
 public:
  T min, max, sum;
  NumberAggregationValue(NumberAggregationValue<T>& value)
      : AggregatedValue<T>(value, nullptr, ValueType::VALUE_TYPE_NUMBER,
                           typeid(T)),
        min(value.min),
        max(value.max),
        sum(value.sum) {}
  NumberAggregationValue(T value)
      : AggregatedValue<T>(nullptr, ValueType::VALUE_TYPE_NUMBER, typeid(T)),
        min(value),
        max(value),
        sum(value) {}
  void update(NumberAggregationValue<T>* value) {
    if (value->min < min) min = value->min;
    if (value->max > max) max = value->max;
    sum += value->sum;
    AggregatedValue<T>::update(value);
  }
};

class AggregatedValues {
 public:
  AggregatedValues() {}
  ~AggregatedValues() {}
  std::unordered_map<std::string, BaseAggregatedValue*> values;
  int update(const std::string& key, const std::type_info& id,
             BaseAggregatedValue* value) {
    auto it = values.find(key);
    if (it != values.end()) {
      it->second->update(value);
      delete value;
    } else {
      values.insert_or_assign(key, value);
    }
    return 0;
  }
};

struct Value {
  std::variant<std::string, TimeResolution, double, std::set<std::string>> data;
};

struct Field {
  std::vector<std::string>
      path;  // e.g., tags.performance -> ["tags", "performance"]
};

struct RuleAST {
  // AST Node
  struct Node {
    virtual ~Node() = default;
  };

  struct BinaryOp : Node {
    RuleOp op;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
    BinaryOp(RuleOp op, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
        : op(op), left(std::move(l)), right(std::move(r)) {}
  };

  struct UnaryOp : Node {
    RuleOp op;
    std::unique_ptr<Node> operand;
    UnaryOp(RuleOp op, std::unique_ptr<Node> operand)
        : op(op), operand(std::move(operand)) {}
  };

  struct Comparison : Node {
    RuleOp op;
    Field field;
    Value value;
    Comparison(RuleOp op, Field field, Value value)
        : op(op), field(std::move(field)), value(std::move(value)) {}
  };

  struct InOp : Node {
    Field field;
    std::set<std::string> values;
    InOp(Field field, std::set<std::string> values)
        : field(std::move(field)), values(std::move(values)) {}
  };

  struct LikeOp : Node {
    Field field;
    std::string pattern;
    LikeOp(Field field, std::string pattern)
        : field(std::move(field)), pattern(std::move(pattern)) {}
  };

  std::unique_ptr<Node> root;
};

// Helper functions for field extraction and comparison
inline std::optional<Value> getFieldValue(const AggregatedKey* key,
                                          const Field& field) {
  // Extract field value from AggregatedKey based on field.path
  if (!key || field.path.empty()) return std::nullopt;
  const std::string& fieldName = field.path[0];

  if (fieldName == "cat") {
    return Value{key->category};
  } else if (fieldName == "name") {
    return Value{key->event_name};
  } else if (fieldName == "ts") {
    return Value{key->time_interval};
  } else if (fieldName == "dur") {
    return Value{key->duration};
  } else if (fieldName == "app" && key->app_name != nullptr) {
    return Value{key->app_name};
  } else if (fieldName == "rank" && *key->rank != -1) {
    return Value{(TimeResolution)*key->rank};
  } else if (fieldName.rfind("tags.", 0) == 0 &&
             (key->additional_keys || key->tag_source)) {
    // tags.<tagname>
    std::string tagKey = fieldName.substr(5);
    std::string tagValue = key->additional_keys
                               ? key->additional_keys->getTagValue(tagKey)
                               : key->tag_source->tag(tagKey);
    return Value{tagValue};
  }
  return std::nullopt;
}

inline bool compareValues(const Value& lhs, const Value& rhs, RuleOp op) {
  if (lhs.data.index() != rhs.data.index()) return false;
  if (std::holds_alternative<TimeResolution>(lhs.data)) {
    int l = std::get<TimeResolution>(lhs.data);
    int r = std::get<TimeResolution>(rhs.data);
    switch (op) {
      case RuleOp::EQ:
        return l == r;
      case RuleOp::NEQ:
        return l != r;
      case RuleOp::GT:
        return l > r;
      case RuleOp::LT:
        return l < r;
      case RuleOp::GTE:
        return l >= r;
      case RuleOp::LTE:
        return l <= r;
      default:
        return false;
    }
  }
  if (std::holds_alternative<double>(lhs.data)) {
    double l = std::get<double>(lhs.data);
    double r = std::get<double>(rhs.data);
    switch (op) {
      case RuleOp::EQ:
        return l == r;
      case RuleOp::NEQ:
        return l != r;
      case RuleOp::GT:
        return l > r;
      case RuleOp::LT:
        return l < r;
      case RuleOp::GTE:
        return l >= r;
      case RuleOp::LTE:
        return l <= r;
      default:
        return false;
    }
  }
  if (std::holds_alternative<std::string>(lhs.data)) {
    const std::string& l = std::get<std::string>(lhs.data);
    const std::string& r = std::get<std::string>(rhs.data);
    switch (op) {
      case RuleOp::EQ:
        return l == r;
      case RuleOp::NEQ:
        return l != r;
      default:
        return false;
    }
  }
  return false;
}

inline bool likeMatch(const std::string& value, const std::string& pattern) {
  // Handles patterns: "*stat", "*stat*", "stat*"
  if (pattern == "*") return true;  // matches anything

  size_t first_star = pattern.find('*');
  size_t last_star = pattern.rfind('*');

  if (first_star == std::string::npos) {
    // No wildcard, exact match
    return value == pattern;
  }

  if (first_star == 0 && last_star == pattern.size() - 1 &&
      pattern.size() > 1) {
    // Pattern: *stat*
    std::string inner = pattern.substr(1, pattern.size() - 2);
    return value.find(inner) != std::string::npos;
  }

  if (first_star == 0) {
    // Pattern: *stat
    std::string suffix = pattern.substr(1);
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
  }

  if (last_star == pattern.size() - 1) {
    // Pattern: stat*
    std::string prefix = pattern.substr(0, pattern.size() - 1);
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
  }

  // Fallback: only support single '*' at start or end or both
  return false;
}
}  // namespace dftracer

/*
 * Producer/consumer mode (docs/design_pc_service.rst): the shared-memory
 * segment's layout, the binary record payloads, and the producer's per-thread
 * state. The code that uses them is in core/ipc and core/buffer.
 */
namespace dftracer {
namespace ipc {

static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "shared-memory ring needs lock-free 64-bit atomics");
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "shared-memory ring needs lock-free 32-bit atomics");

/**
 * 16-byte header in front of every record. `word` packs
 *
 *   [63:32] epoch of the block when the space was allocated (RingView::epoch)
 *   [31:8]  length of the whole record (header + payload), multiple of 16
 *   [7:0]   RecordState
 *
 * A record is written as PAD when it is reserved and turned into COMMITTED by
 * commit(), so a reservation that is never committed reads as padding once its
 * block is published. Visibility to the consumer comes from the block's
 * `committed` store, not from this word.
 */
struct RecordHeader {
  std::atomic<uint64_t> word;
  uint32_t pid;
  uint16_t kind;
  uint16_t flags;
};
static_assert(sizeof(RecordHeader) == 16, "RecordHeader must be 16 bytes");

struct alignas(DFT_SHM_CACHE_LINE) SegmentHeader {
  uint64_t magic;
  uint32_t abi_version;
  uint32_t block_count;  // power of two
  uint32_t block_size;   // bytes, power of two, <= 16 MiB
  uint32_t block_shift;  // log2(block_count)
  std::atomic<uint32_t> state;
  int32_t service_pid;
  uint64_t service_start_time;
  std::atomic<uint64_t> heartbeat_ns;
  std::atomic<uint32_t> announced;
  // Producers only. Sequence number of the next block to be claimed; the block
  // index is head & (block_count - 1), its epoch RingView::epoch(head). Every
  // block before it has been claimed by some producer.
  alignas(DFT_SHM_CACHE_LINE) std::atomic<uint64_t> head;
  // Consumer only. Oldest block not yet released; published for diagnostics
  // and backlog accounting, never read on the producer fast path.
  alignas(DFT_SHM_CACHE_LINE) std::atomic<uint64_t> tail;
  // Events producers dropped because the ring stayed full: the service had
  // fallen behind. Only touched on that slow path; the service reports it.
  alignas(DFT_SHM_CACHE_LINE) std::atomic<uint64_t> dropped;
  // Events producers wrote to their own spill file instead (ring full), so
  // not lost: the service reports where they are.
  std::atomic<uint64_t> spilled;
};

/**
 * Per-block control, cache-line aligned so the consumer releasing block k does
 * not slow down producers claiming block k+1.
 *
 * `armed` is the epoch the block is free for: block sequence number s may be
 * claimed exactly when armed == epoch(s). The consumer re-arms a block for its
 * next lap by storing epoch + 1 once it has taken the contents.
 *
 * `committed` is written once per lap, by the producer that claimed the block,
 * when it publishes the block: every byte is by then either a record or
 * padding. The consumer's completeness check is one acquire load of it, which
 * also makes every record in the block visible. It sits on its own cache line
 * so the publishing producer does not contend with the next claim.
 *
 * `owner` lets the service take over a block that its producer holds but does
 * not publish -- a quiet thread, or a dead process -- see ShmBlockOwner.
 */
struct alignas(DFT_SHM_CACHE_LINE) BlockControl {
  std::atomic<uint64_t> armed;
  // When the block was claimed this lap; 0 while free. The service takes over
  // a block held idle for longer than DFTRACER_PC_FLUSH_MS after it.
  std::atomic<uint64_t> claim_ns;
  // Who claimed it this lap, for the service's dead-producer check and for
  // routing its records (one aggregation lane per producer thread).
  std::atomic<uint32_t> owner_pid;
  std::atomic<uint32_t> owner_tid;
  // epoch << 32 | block_size once published, plus DFT_SHM_BLOCK_CLAIMED once
  // a drain thread has taken it. Carrying the epoch means a value can never be
  // mistaken for the same slot's state in another lap.
  alignas(DFT_SHM_CACHE_LINE) std::atomic<uint64_t> committed;
  // epoch << 8 | ShmBlockOwner. On its own line: the owning producer writes it
  // around every record, and nothing else touches it until the block is old.
  alignas(DFT_SHM_CACHE_LINE) std::atomic<uint64_t> owner;
};

/** Space handed out by RingProducer::try_reserve, to be filled and committed.
 */
struct Reservation {
  RecordHeader* header = nullptr;
  uint8_t* payload = nullptr;
  uint32_t epoch = 0;
  uint32_t length = 0;  // whole record, header included
};

#pragma pack(push, 1)
struct EventHead {
  uint64_t start_time;
  uint64_t duration;
  uint64_t thread_id;
  int32_t index;
  int32_t process_id;
  uint8_t type;   // TraceEventType
  uint8_t flags;  // ShmEventFlags
  uint16_t name_len;
  uint16_t category_len;
  uint16_t meta_count;
};
struct MetadataHead {
  uint64_t thread_id;
  int32_t process_id;
  uint8_t type;  // TraceEventType
  uint8_t is_string;
  uint16_t name_len;
  uint32_t value_len;
  uint16_t record_name_len;
};
struct RecordHead {
  uint64_t thread_id;
  int32_t process_id;
  uint8_t type;  // TraceEventType
  uint8_t reserved;
  uint16_t record_name_len;
  uint32_t fields_len;
};
struct HelloHead {
  int32_t process_id;
  uint16_t hostname_hash_len;
  uint16_t log_file_len;
};
#pragma pack(pop)

/** One decoded metadata entry; views point into the record. */
struct DecodedValue {
  ShmValueTag tag;
  MetadataType mtype;
  std::string_view key;
  int64_t i = 0;
  uint64_t u = 0;
  double d = 0;
  std::string_view s;
};

/** A decoded DATA or COUNTER record; views point into the record. */
/** The service's side of SHM_EVENT_INTERNED: the strings of one block, by
 * id, as views into the block. Cleared before each block. */
struct InternTable {
  std::vector<std::string_view> strings;
  void clear() { strings.clear(); }
};

struct DecodedEvent {
  EventHead head;
  std::string_view name;
  std::string_view category;
  std::vector<DecodedValue> values;
};

/** A decoded METADATA record; views point into the record. */
struct DecodedMetadata {
  MetadataHead head;
  std::string_view name;
  std::string_view value;
  std::string_view record_name;
};

/** A decoded RECORD record; views point into the record. */
struct DecodedRecord {
  RecordHead head;
  std::string_view record_name;
  std::string_view fields;
};

/** A decoded HELLO record; views point into the record. */
struct DecodedHello {
  HelloHead head;
  std::string_view hostname_hash;
  std::string_view log_file;
};

class RingProducer;
}  // namespace ipc

/** One producer thread's state in ServiceBufferManager. */
namespace ipc {
/**
 * A producer thread's table of the strings already in its current ring
 * block (SHM_EVENT_INTERNED). Keyed by the string's address -- names and
 * categories are mostly literals, and the keys of the thread's reused
 * Metadata sit at the same address event after event -- and every hit is
 * checked against the copy in the block, so a reused address never yields
 * a wrong reference. The service rebuilds the same ids from the block
 * (InternTable): every inline string of an interned record is the next id.
 */
class Interner {
 public:
  /** Before encoding into `block` (RingProducer::block_serial()). */
  void begin(uint64_t block) {
    if (block != block_) {
      block_ = block;
      next_ = 0;
      ++epoch_;
    }
    pending_n_ = 0;
  }
  /** The reference to `s`, or 0 when it is not in the block yet. */
  uint16_t find(const char* s, size_t n) const {
    const Entry& e = slot(s, n);
    if (e.epoch == epoch_ && e.src == s && e.len == n &&
        std::memcmp(e.bytes, s, n) == 0)
      return static_cast<uint16_t>(DFT_SHM_INTERN_REF | e.id);
    return 0;
  }
  /** `s` was just written inline at `bytes` in the block: the next id. */
  void define(const char* s, size_t n, const uint8_t* bytes) {
    if (next_ >= DFT_SHM_INTERN_MAX) return;  // stays inline, no id
    Entry& e = slot(s, n);
    e = Entry{s, bytes, static_cast<uint16_t>(n), next_++, epoch_};
    if (pending_n_ < kPending) pending_[pending_n_] = &e;
    ++pending_n_;
  }
  /** The record encoded since begin() was not committed: forget its ids. */
  void rollback(uint16_t mark) {
    if (pending_n_ <= kPending) {
      for (uint32_t i = 0; i < pending_n_; ++i) pending_[i]->epoch = 0;
    } else {
      ++epoch_;  // too many to undo one by one: forget every entry
    }
    next_ = mark;
    pending_n_ = 0;
  }
  uint16_t mark() const { return next_; }

 private:
  struct Entry {
    const char* src = nullptr;
    const uint8_t* bytes = nullptr;
    uint16_t len = 0;
    uint16_t id = 0;
    uint32_t epoch = 0;  // valid only in the table's current epoch
  };
  static constexpr uint32_t kSlots = 256;
  static constexpr uint32_t kPending = 32;
  static uint32_t index(const char* s, size_t n) {
    auto bits = reinterpret_cast<uintptr_t>(s);
    return static_cast<uint32_t>(((bits >> 3) ^ (bits >> 11) ^ n) &
                                 (kSlots - 1));
  }
  Entry& slot(const char* s, size_t n) { return entries_[index(s, n)]; }
  const Entry& slot(const char* s, size_t n) const {
    return entries_[index(s, n)];
  }

  Entry entries_[kSlots];
  Entry* pending_[kPending] = {};
  uint32_t pending_n_ = 0;
  uint64_t block_ = ~0ULL;
  uint32_t epoch_ = 1;
  uint16_t next_ = 0;
};
}  // namespace ipc

struct alignas(DFT_SHM_CACHE_LINE) ServiceSlot {
  std::atomic<uint32_t> state{SHM_SLOT_IDLE};  // ShmSlotState
  uint64_t generation = 0;
  std::unique_ptr<ipc::RingProducer> producer;
  std::vector<uint8_t> scratch;
  ipc::Interner intern;  // strings already in the producer's block
  // Written only by the slot's thread (load + store, no read-modify-write),
  // so counting costs nothing shared; read by pushed(). Drops are rare and
  // counted per process (ServiceBufferManager::count_drop).
  std::atomic<uint64_t> pushed{0};
};
}  // namespace dftracer

#endif  // DFTRACER_CORE_DATASTRUCTURE_H
