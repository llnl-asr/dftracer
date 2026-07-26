#ifndef DFTRACER_CORE_DATASTRUCTURE_H
#define DFTRACER_CORE_DATASTRUCTURE_H

// Internal
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/macros.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/utils/utils.h>

// standard headers
#include <stddef.h>

#include <any>
#include <memory>
#include <optional>
#include <set>
#include <string>
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

class Metadata {
 private:
  typedef std::unordered_map<std::string,
                             std::tuple<MetadataType, std::any, std::size_t>>
      DataMap;
  DataMap data;
  std::size_t get_hash(const std::any& value, const MetadataType& type) const {
    if (type != MetadataType::MT_KEY) {
      return 0;
    }
    DFTRACER_FOR_EACH_NUMERIC_TYPE(DFTRACER_ANY_CAST_MACRO, value, {
      auto val = std::to_string(res.value());
      return std::hash<std::string>()(val);
    });
    DFTRACER_FOR_EACH_STRING_TYPE(DFTRACER_ANY_CAST_MACRO, value, {
      return std::hash<std::string>()(res.value());
    });
    return 0;
  }

 public:
  Metadata() {}
  ~Metadata() {}
  std::pair<DataMap::iterator, bool> insert_or_assign(const std::string& key,
                                                      const std::any& value) {
    auto ret = data.insert_or_assign(
        key, std::make_tuple(MetadataType::MT_KEY, value,
                             get_hash(value, MetadataType::MT_KEY)));
    return ret;
  }
  std::pair<DataMap::iterator, bool> insert_or_assign(
      const std::string& key, const std::any& value, const MetadataType& type) {
    auto ret = data.insert_or_assign(
        key, std::make_tuple(type, value, get_hash(value, type)));
    return ret;
  }
  bool contains(const std::string& key) const {
    return data.find(key) != data.end();
  }

  size_t erase(const std::string& key) { return data.erase(key); }

  size_t size() const { return data.size(); }

  bool empty() const { return data.empty(); }

  void clear() { data.clear(); }

  std::pair<DataMap::iterator, bool> insert(const std::string& key,
                                            const std::any& value) {
    return data.insert(
        {key, std::make_tuple(MetadataType::MT_KEY, value,
                              get_hash(value, MetadataType::MT_KEY))});
  }
  std::pair<DataMap::iterator, bool> insert(const std::string& key,
                                            const std::any& value,
                                            const MetadataType& type) {
    return data.insert(
        {key, std::make_tuple(type, value, get_hash(value, type))});
  }

  DataMap::iterator find(const std::string& key) { return data.find(key); }

  DataMap::const_iterator find(const std::string& key) const {
    return data.find(key);
  }

  std::tuple<MetadataType, std::any, std::size_t>& operator[](
      const std::string& key) {
    return data[key];
  }

  const std::tuple<MetadataType, std::any, std::size_t>& at(
      const std::string& key) const {
    return data.at(key);
  }

  DataMap::iterator begin() { return data.begin(); }

  DataMap::const_iterator begin() const { return data.begin(); }

  DataMap::iterator end() { return data.end(); }

  DataMap::const_iterator end() const { return data.end(); }

  std::string getTagValue(const std::string& tagKey) const;
};

inline bool compare_any(const std::any& a, const std::any& b) {
  if (a.type() != b.type()) return false;
  DFTRACER_FOR_EACH_NUMERIC_TYPE(DFTRACER_COMPARE_TYPE, NULL,
                                 { return result; })
  DFTRACER_FOR_EACH_STRING_TYPE(DFTRACER_COMPARE_TYPE, NULL, { return result; })
  return false;
}

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
  mutable size_t _cached_hash;  // Cached hash value

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
        _cached_hash(0) {}
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
        _cached_hash(other._cached_hash) {}
  bool operator==(const AggregatedKey& other) const {
    if (category != other.category || event_name != other.event_name ||
        type != other.type || time_interval != other.time_interval ||
        thread_id != other.thread_id) {
      return false;
    }

    // Compare additional_keys for MetadataType::MT_KEY
    if (additional_keys && other.additional_keys) {
      for (const auto& pair : *additional_keys) {
        if (std::get<0>(pair.second) == MetadataType::MT_KEY) {
          auto it = other.additional_keys->find(pair.first);
          if (it == other.additional_keys->end() ||
              std::get<0>(it->second) != MetadataType::MT_KEY ||
              std::get<2>(it->second) != std::get<2>(pair.second)) {
            return false;
          }
        }
      }
      for (const auto& pair : *other.additional_keys) {
        if (std::get<0>(pair.second) == MetadataType::MT_KEY) {
          auto it = additional_keys->find(pair.first);
          if (it == additional_keys->end() ||
              std::get<0>(it->second) != MetadataType::MT_KEY ||
              std::get<2>(it->second) != std::get<2>(pair.second)) {
            return false;
          }
        }
      }
    } else if (additional_keys || other.additional_keys) {
      // One is nullptr, the other is not
      return false;
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
          // For std::any, we can only hash the type info
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
  } else if (fieldName.rfind("tags.", 0) == 0 && key->additional_keys) {
    // tags.<tagname>
    std::string tagKey = fieldName.substr(5);
    std::string tagValue = key->additional_keys->getTagValue(tagKey);
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

#endif  // DFTRACER_CORE_DATASTRUCTURE_H
