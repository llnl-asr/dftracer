#include <dftracer/core/aggregator/aggregator.h>

template <>
std::shared_ptr<dftracer::Aggregator>
    dftracer::Singleton<dftracer::Aggregator>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::Aggregator>::stop_creating_instances = false;

namespace dftracer {
bool Aggregator::aggregate(AggregatedKey& aggregated_key) {
  const bool first = is_first.exchange(false, std::memory_order_relaxed);
  // Use cached_interval (in the configured time_metric unit) to avoid
  // division per event
  aggregated_key.time_interval =
      (aggregated_key.time_interval / cached_interval) * cached_interval;
  TimeResolution& interval = aggregated_key.time_interval;
  TimeResolution previous = last_interval.load(std::memory_order_relaxed);
  bool advanced = false;
  while (interval > previous) {
    if (last_interval.compare_exchange_weak(previous, interval,
                                            std::memory_order_relaxed)) {
      advanced = true;
      break;
    }
  }

  Shard& shard = shard_for(aggregated_key.thread_id);
  std::lock_guard<std::mutex> lock(shard.mtx);
  AggregatedDataType& data = shard.data;
  insert_number_value(data, interval, aggregated_key, "dur",
                      aggregated_key.duration);

  // Fast path: skip metadata iteration if empty
  if (aggregated_key.additional_keys &&
      !aggregated_key.additional_keys->empty()) {
    for (const auto& entry : *aggregated_key.additional_keys) {
      const std::string& key = entry.first;
      const Metadata::Value& value = entry.second;
      // Only process MT_VALUE metadata (skip MT_KEY)
      if (std::get<0>(value) == MetadataType::MT_VALUE) {
        std::visit(
            [&](const auto& v) {
              using T = std::decay_t<decltype(v)>;
              if constexpr (std::is_arithmetic_v<T>) {
                insert_number_value(data, interval, aggregated_key, key, v);
              } else if constexpr (std::is_same_v<T, const char*> ||
                                   std::is_same_v<T, std::string>) {
                insert_general_value(data, interval, aggregated_key, key, v);
              }
            },
            std::get<1>(value));
      }
    }
  }

  return !first && advanced;
}
int Aggregator::get_previous_aggregations(AggregatedDataType& data, bool all) {
  const TimeResolution last = last_interval.load(std::memory_order_relaxed);
  int moved = 0;
  for (auto& shard : shards_) {
    std::lock_guard<std::mutex> lock(shard.mtx);
    for (auto it = shard.data.begin(); it != shard.data.end();) {
      if (all || it->first < last) {
        auto& events = data[it->first];
        if (events.empty()) ++moved;
        // Keys differ across shards, so merge moves every entry.
        events.merge(it->second);
        it = shard.data.erase(it);
      } else {
        ++it;
      }
    }
  }
  return moved;
}
}  // namespace dftracer
