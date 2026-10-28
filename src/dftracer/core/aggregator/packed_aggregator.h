#ifndef DFTRACER_PACKED_AGGREGATOR_H
#define DFTRACER_PACKED_AGGREGATOR_H
#include <dftracer/core/aggregator/rules.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dftracer {

/** Strings by id, for the lifetime of the table; ids are dense. */
class StringTable {
 public:
  uint32_t intern(std::string_view s);
  std::string_view at(uint32_t id) const { return strings_[id]; }
  size_t size() const { return strings_.size(); }

 private:
  void grow();
  std::deque<std::string> strings_;  // stable: views into it stay valid
  std::vector<uint64_t> hashes_;     // per id
  std::vector<uint32_t> slots_;      // open addressing: id + 1, 0 empty
};

/**
 * Aggregation of the service's packed events (docs/design_pc_service.rst,
 * "Metadata in the service"). The same rows Aggregator gives a process's
 * Metadata events, from the decoded record and without the heap Metadata,
 * std::any and per-value allocations that Aggregator needs for its general
 * API.
 *
 * An event's metadata is two sets. Its keys (MT_KEY fields) are part of the
 * bucket, with category, name, type, thread and interval: events that differ
 * in one are aggregated apart, and each row carries its bucket's keys. Its
 * values (MT_VALUE fields) are folded into the bucket: count, sum, min and
 * max of a number, the count of a string. Other fields are not aggregated.
 *
 * Intervals, rules and the row layout are Aggregator's; one per lane (one
 * producer thread), used under the lane's lock.
 */
class PackedAggregator {
 public:
  explicit PackedAggregator(std::shared_ptr<ConfigurationManager> config);

  /** SELECTIVE aggregation: whether the rules aggregate `event`. */
  bool should_aggregate(const ipc::DecodedEvent& event, const char* app_name,
                        const int* rank) const;
  /** Fold `event` in. True when it opened a later interval than any so far
   * (not the first), so the earlier intervals are complete. */
  bool aggregate(const ipc::DecodedEvent& event);

  /** Receives the rows of drain(), one line at a time. */
  struct RowSink {
    virtual ~RowSink() = default;
    /** Room for one line (DFTRACER_SERIALIZATION_EVENT_MAX bytes). */
    virtual char* row() = 0;
    /** The line written into row() is `size` bytes. */
    virtual void wrote(size_t size) = 0;
  };
  /** Write and drop the complete intervals, or every one with `all`. */
  void drain(bool all, ProcessID process_id, HashType hostname_hash,
             RowSink& sink);

 private:
  union Number {
    int64_t i;
    uint64_t u;
    double d;
  };
  /** A key field of a bucket; `v.u` is a string id for STRING / RAW_JSON. */
  struct KeyField {
    uint32_t key;
    uint8_t tag;
    Number v;
  };
  /** A value field of a bucket. */
  struct Accumulator {
    uint32_t key;
    uint8_t tag;  // ShmValueTag
    uint64_t count;
    Number sum, min, max;
  };
  struct Bucket {
    uint32_t category, name;
    uint8_t type;
    uint64_t thread_id;
    uint64_t signature;
    uint32_t keys_at, keys_count;  // in Interval::keys
    uint32_t next;                 // same signature chain, UINT32_MAX ends
    uint64_t count;
    std::vector<Accumulator> values;  // values[0] is dur
  };
  struct Interval {
    std::vector<Bucket> buckets;
    std::vector<KeyField> keys;
    std::unordered_map<uint64_t, uint32_t> by_signature;  // first bucket
    StringTable key_strings;  // string keys' values: freed with the interval
  };

  Bucket& bucket(Interval& interval, const ipc::DecodedEvent& event,
                 uint32_t category, uint32_t name);
  static void fold(std::vector<Accumulator>& values, uint32_t key, uint8_t tag,
                   Number v);
  void write_row(char* out, size_t& size, const Interval& interval,
                 const Bucket& bucket, TimeResolution ts, ProcessID process_id,
                 HashType hostname_hash) const;

  std::shared_ptr<ConfigurationManager> config_;
  Rules inclusion_rules_, exclusion_rules_;
  bool always_aggregate_;
  TimeResolution cached_interval_;
  TimeResolution last_interval_ = 0;
  bool is_first_ = true;
  uint32_t dur_key_;
  StringTable names_;  // categories, event names and keys: bounded by code
  std::map<TimeResolution, Interval> intervals_;
  std::vector<KeyField> scratch_;
};

}  // namespace dftracer
#endif  // DFTRACER_PACKED_AGGREGATOR_H
