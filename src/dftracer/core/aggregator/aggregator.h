#ifndef DFTRACER_AGGREGATOR_H
#define DFTRACER_AGGREGATOR_H
#include <dftracer/core/common/logging.h>
//
#include <dftracer/core/aggregator/rules.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/utils/utils.h>

#include <any>
#include <atomic>
#include <map>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <unordered_map>

namespace dftracer {
typedef std::unordered_map<AggregatedKey, AggregatedValues*> AggregatedDataPair;
typedef std::map<TimeResolution, AggregatedDataPair> AggregatedDataType;
class Aggregator {
 private:
  static void cleanup_aggregated_values(AggregatedDataPair& event_map) {
    for (auto& pair : event_map) {
      for (auto& val_pair : pair.second->values) {
        delete val_pair.second;
      }
      delete pair.second;
      // A stored key owns its MT_KEY fields (see copy_mt_keys).
      delete pair.first.additional_keys;
    }
  }

  // A bucket key holds the thread id, so one thread's buckets all live in one
  // shard and threads in different shards never wait for each other.
  static constexpr size_t SHARDS = 64;
  struct alignas(64) Shard {
    std::mutex mtx;
    AggregatedDataType data;
  };
  Shard shards_[SHARDS];
  Shard& shard_for(ThreadID tid) { return shards_[tid % SHARDS]; }

  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::atomic<TimeResolution> last_interval;
  TimeResolution cached_interval;  // trace_interval_ms, cached in the
                                   // configured time_metric unit
  std::atomic<bool> is_first;
  Rules inclusion_rules;
  Rules exclusion_rules;
  bool always_aggregate;
  // The MT_KEY fields of `metadata`, which a stored key owns: the event's own
  // metadata is freed once it has been aggregated. nullptr when there are
  // none. The serializer writes them with the bucket's aggregated values and
  // frees them (JsonLines::aggregated).
  static Metadata* copy_mt_keys(const Metadata* metadata) {
    if (metadata == nullptr) return nullptr;
    Metadata* keys = nullptr;
    for (const auto& pair : *metadata) {
      if (std::get<0>(pair.second) != MetadataType::MT_KEY) continue;
      if (keys == nullptr) keys = new Metadata();
      (*keys)[pair.first] = pair.second;
    }
    return keys;
  }

  // Folds one value into the bucket of `aggregated_key`: its category, name,
  // type, interval and thread, and the values of its MT_KEY fields, so events
  // that differ in an MT_KEY field are aggregated apart.
  inline void insert_value(AggregatedDataType& data,
                           TimeResolution& time_interval,
                           AggregatedKey& aggregated_key,
                           const std::string& key, BaseAggregatedValue* value) {
    auto& events = data[time_interval];
    auto iter = events.find(aggregated_key);
    if (iter != events.end()) {
      iter->second->update(key, typeid(TimeResolution), value);
      return;
    }
    AggregatedKey stored(aggregated_key);
    stored.additional_keys = copy_mt_keys(aggregated_key.additional_keys);
    auto values = new AggregatedValues();
    values->update(key, typeid(TimeResolution), value);
    events.emplace(stored, values);
    DFTRACER_LOG_INFO("Events in %llu are %d", time_interval, events.size());
  }

  template <typename T>
  inline void insert_number_value(AggregatedDataType& data,
                                  TimeResolution& time_interval,
                                  AggregatedKey& aggregated_key,
                                  const std::string& key, T value) {
    insert_value(data, time_interval, aggregated_key, key,
                 new NumberAggregationValue<T>(value));
  }

  template <typename T>
  inline void insert_general_value(AggregatedDataType& data,
                                   TimeResolution& time_interval,
                                   AggregatedKey& aggregated_key,
                                   const std::string& key, T value) {
    insert_value(data, time_interval, aggregated_key, key,
                 new AggregatedValue<T>(value));
  }

 public:
  static void release_aggregated_data(AggregatedDataType& data) {
    for (auto& interval_map : data) {
      cleanup_aggregated_values(interval_map.second);
    }
    data.clear();
  }

  Aggregator() {
    config =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    for (const auto& rule : config->aggregation_inclusion_rules) {
      inclusion_rules.addRule(rule);
    }
    for (const auto& rule : config->aggregation_exclusion_rules) {
      exclusion_rules.addRule(rule);
    }
    always_aggregate = true;
    if (config->aggregation_inclusion_rules.size() > 0 ||
        config->aggregation_exclusion_rules.size() > 0) {
      always_aggregate = false;
    }
    last_interval.store(0, std::memory_order_relaxed);
    is_first.store(true, std::memory_order_relaxed);
    // Cache trace_interval_ms (a wall-clock millisecond value) scaled into
    // whatever unit get_time()/config->time_metric is currently using, so
    // interval bucketing below stays correct regardless of TIME_METRIC.
    double units_per_ms =
        time_metric_units_per_second(config->time_metric) / 1000.0;
    cached_interval =
        (TimeResolution)(config->trace_interval_ms * units_per_ms);
    if (cached_interval == 0) cached_interval = 1;  // avoid divide-by-zero
  }
  bool should_aggregate(const AggregatedKey* key) {
    if (always_aggregate) return true;
    if (inclusion_rules.satisfies(key) && !exclusion_rules.satisfies(key))
      return true;
    return false;
  }
  ~Aggregator() {}
  void finalize() {
    for (auto& shard : shards_) {
      std::lock_guard<std::mutex> lock(shard.mtx);
      release_aggregated_data(shard.data);
    }
  }
  /** pthread_atfork handlers: no shard lock may be held across fork. */
  void lock_for_fork() {
    for (auto& shard : shards_) shard.mtx.lock();
  }
  void unlock_in_parent() {
    for (auto& shard : shards_) shard.mtx.unlock();
  }
  void reset_in_child() {
    for (auto& shard : shards_) new (&shard.mtx) std::mutex();
  }
  bool aggregate(AggregatedKey& aggregated_key);
  int get_previous_aggregations(AggregatedDataType& data, bool all = false);
};
}  // namespace dftracer
#endif  // DFTRACER_AGGREGATOR_H