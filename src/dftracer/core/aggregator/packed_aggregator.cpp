#include <dftracer/core/aggregator/packed_aggregator.h>
#include <dftracer/core/serialization/packed_json.h>

#include <algorithm>
#include <cstring>
#include <functional>

namespace dftracer {

uint32_t StringTable::intern(std::string_view s) {
  if (slots_.empty() || (strings_.size() + 1) * 2 > slots_.size()) grow();
  const uint64_t hash = std::hash<std::string_view>{}(s);
  const size_t mask = slots_.size() - 1;
  for (size_t i = hash & mask;; i = (i + 1) & mask) {
    const uint32_t slot = slots_[i];
    if (slot == 0) {
      const uint32_t id = static_cast<uint32_t>(strings_.size());
      strings_.emplace_back(s);
      hashes_.push_back(hash);
      slots_[i] = id + 1;
      return id;
    }
    if (hashes_[slot - 1] == hash && strings_[slot - 1] == s) return slot - 1;
  }
}

void StringTable::grow() {
  std::vector<uint32_t> slots(slots_.empty() ? 64 : slots_.size() * 2, 0);
  const size_t mask = slots.size() - 1;
  for (uint32_t id = 0; id < strings_.size(); ++id) {
    size_t i = hashes_[id] & mask;
    while (slots[i] != 0) i = (i + 1) & mask;
    slots[i] = id + 1;
  }
  slots_.swap(slots);
}

PackedAggregator::PackedAggregator(std::shared_ptr<ConfigurationManager> config)
    : config_(std::move(config)) {
  for (const auto& rule : config_->aggregation_inclusion_rules)
    inclusion_rules_.addRule(rule);
  for (const auto& rule : config_->aggregation_exclusion_rules)
    exclusion_rules_.addRule(rule);
  always_aggregate_ = config_->aggregation_inclusion_rules.empty() &&
                      config_->aggregation_exclusion_rules.empty();
  // As Aggregator: trace_interval_ms in the configured time unit.
  const double units_per_ms =
      time_metric_units_per_second(config_->time_metric) / 1000.0;
  cached_interval_ =
      static_cast<TimeResolution>(config_->trace_interval_ms * units_per_ms);
  if (cached_interval_ == 0) cached_interval_ = 1;
  dur_key_ = names_.intern("dur");
}

namespace {
/** tags.<key> for the rules, as Metadata::getTagValue gives it. */
struct EventTags : TagSource {
  explicit EventTags(const ipc::DecodedEvent& e) : event(e) {}
  std::string tag(const std::string& key) const override {
    for (const ipc::DecodedValue& v : event.values) {
      if (v.key != key) continue;
      switch (v.tag) {
        case SHM_VALUE_INT:
          return std::to_string(v.i);
        case SHM_VALUE_UINT:
          return std::to_string(v.u);
        case SHM_VALUE_DOUBLE:
          return std::to_string(v.d);
        case SHM_VALUE_STRING:
          return std::string(v.s);
        default:
          return "";
      }
    }
    return "";
  }
  const ipc::DecodedEvent& event;
};

inline uint64_t mix(uint64_t h, uint64_t v) {
  return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}
}  // namespace

bool PackedAggregator::should_aggregate(const ipc::DecodedEvent& event,
                                        const char* app_name,
                                        const int* rank) const {
  if (always_aggregate_) return true;
  const std::string category(event.category), name(event.name);
  AggregatedKey key(category.c_str(), name.c_str(),
                    static_cast<TraceEventType>(event.head.type),
                    event.head.start_time, event.head.duration,
                    static_cast<ThreadID>(event.head.thread_id), nullptr,
                    app_name, rank);
  const EventTags tags(event);
  key.tag_source = &tags;
  return inclusion_rules_.satisfies(&key) && !exclusion_rules_.satisfies(&key);
}

bool PackedAggregator::aggregate(const ipc::DecodedEvent& event) {
  const bool first = is_first_;
  is_first_ = false;
  const TimeResolution ts =
      event.head.start_time / cached_interval_ * cached_interval_;
  Interval& interval = intervals_[ts];
  const TimeResolution previous = last_interval_;
  if (ts > last_interval_) last_interval_ = ts;

  Bucket& b = bucket(interval, event, names_.intern(event.category),
                     names_.intern(event.name));
  ++b.count;
  Number dur;
  dur.u = event.head.duration;
  fold(b.values, dur_key_, SHM_VALUE_UINT, dur);
  for (const ipc::DecodedValue& v : event.values) {
    if (v.mtype != MetadataType::MT_VALUE) continue;
    Number n;
    switch (v.tag) {
      case SHM_VALUE_INT:
        n.i = v.i;
        break;
      case SHM_VALUE_UINT:
        n.u = v.u;
        break;
      case SHM_VALUE_DOUBLE:
        n.d = v.d;
        break;
      default:
        // A string value only counts towards the row's dft_cnt, as in
        // Aggregator; nothing else of it is written.
        continue;
    }
    fold(b.values, names_.intern(v.key), v.tag, n);
  }
  return !first && previous != last_interval_;
}

PackedAggregator::Bucket& PackedAggregator::bucket(
    Interval& interval, const ipc::DecodedEvent& event, uint32_t category,
    uint32_t name) {
  // The event's keys, in key order, so the same set compares equal whatever
  // order the producer's map listed it in.
  scratch_.clear();
  for (const ipc::DecodedValue& v : event.values) {
    if (v.mtype != MetadataType::MT_KEY) continue;
    KeyField f;
    f.key = names_.intern(v.key);
    f.tag = v.tag;
    f.v.u = 0;
    switch (v.tag) {
      case SHM_VALUE_INT:
        f.v.i = v.i;
        break;
      case SHM_VALUE_UINT:
        f.v.u = v.u;
        break;
      case SHM_VALUE_DOUBLE:
        f.v.d = v.d;
        break;
      default:
        f.v.u = interval.key_strings.intern(v.s);
        break;
    }
    scratch_.push_back(f);
  }
  std::sort(scratch_.begin(), scratch_.end(),
            [](const KeyField& a, const KeyField& b) { return a.key < b.key; });

  uint64_t sig =
      mix(mix(mix(category, name), event.head.type), event.head.thread_id);
  for (const KeyField& f : scratch_)
    sig = mix(mix(mix(sig, f.key), f.tag), f.v.u);

  const auto same_keys = [&](const Bucket& c) {
    if (c.keys_count != scratch_.size()) return false;
    for (uint32_t i = 0; i < c.keys_count; ++i) {
      const KeyField& a = interval.keys[c.keys_at + i];
      const KeyField& b = scratch_[i];
      if (a.key != b.key || a.tag != b.tag || a.v.u != b.v.u) return false;
    }
    return true;
  };
  auto head = interval.by_signature.find(sig);
  uint32_t at = head == interval.by_signature.end() ? UINT32_MAX : head->second;
  for (; at != UINT32_MAX; at = interval.buckets[at].next) {
    Bucket& c = interval.buckets[at];
    if (c.signature == sig && c.category == category && c.name == name &&
        c.type == event.head.type && c.thread_id == event.head.thread_id &&
        same_keys(c))
      return c;
  }

  Bucket b;
  b.category = category;
  b.name = name;
  b.type = event.head.type;
  b.thread_id = event.head.thread_id;
  b.signature = sig;
  b.keys_at = static_cast<uint32_t>(interval.keys.size());
  b.keys_count = static_cast<uint32_t>(scratch_.size());
  b.next = head == interval.by_signature.end() ? UINT32_MAX : head->second;
  b.count = 0;
  interval.keys.insert(interval.keys.end(), scratch_.begin(), scratch_.end());
  interval.by_signature[sig] = static_cast<uint32_t>(interval.buckets.size());
  interval.buckets.push_back(std::move(b));
  return interval.buckets.back();
}

void PackedAggregator::fold(std::vector<Accumulator>& values, uint32_t key,
                            uint8_t tag, Number v) {
  for (Accumulator& a : values) {
    if (a.key != key) continue;
    // A key logged as different number types keeps the first one's.
    if (a.tag != tag) {
      const double d = tag == SHM_VALUE_DOUBLE ? v.d
                       : tag == SHM_VALUE_INT  ? static_cast<double>(v.i)
                                               : static_cast<double>(v.u);
      if (a.tag == SHM_VALUE_DOUBLE)
        v.d = d;
      else if (a.tag == SHM_VALUE_INT)
        v.i = tag == SHM_VALUE_UINT ? static_cast<int64_t>(v.u)
                                    : static_cast<int64_t>(d);
      else
        v.u = tag == SHM_VALUE_INT ? static_cast<uint64_t>(v.i)
                                   : static_cast<uint64_t>(d);
    }
    ++a.count;
    switch (a.tag) {
      case SHM_VALUE_INT:
        a.sum.i += v.i;
        a.min.i = std::min(a.min.i, v.i);
        a.max.i = std::max(a.max.i, v.i);
        break;
      case SHM_VALUE_UINT:
        a.sum.u += v.u;
        a.min.u = std::min(a.min.u, v.u);
        a.max.u = std::max(a.max.u, v.u);
        break;
      default:
        a.sum.d += v.d;
        a.min.d = std::min(a.min.d, v.d);
        a.max.d = std::max(a.max.d, v.d);
        break;
    }
    return;
  }
  values.push_back(Accumulator{key, tag, 1, v, v, v});
}

void PackedAggregator::drain(bool all, ProcessID process_id,
                             HashType hostname_hash, RowSink& sink) {
  for (auto it = intervals_.begin(); it != intervals_.end();) {
    if (!all && it->first >= last_interval_) break;
    for (const Bucket& b : it->second.buckets) {
      size_t size = 0;
      write_row(sink.row(), size, it->second, b, it->first, process_id,
                hostname_hash);
      sink.wrote(size);
    }
    it = intervals_.erase(it);
  }
}

void PackedAggregator::write_row(char* out, size_t& size,
                                 const Interval& interval, const Bucket& b,
                                 TimeResolution ts, ProcessID process_id,
                                 HashType hostname_hash) const {
  // JsonLines::aggregated: the bucket's keys, its dft_cnt, and per value
  // either `key` (one event) or key_sum, key_min and key_max.
  const auto number = [](packed_json::Line& w, uint8_t tag, Number n) {
    if (tag == SHM_VALUE_INT)
      w.i64(n.i);
    else if (tag == SHM_VALUE_UINT)
      w.u64(n.u);
    else
      w.f64(n.d);
  };
  packed_json::Line w(out);
  packed_json::series_head(w, names_.at(b.name), names_.at(b.category), b.type,
                           TracePhaseType::TRACE_PHASE_AGGREGATED, ts,
                           process_id, b.thread_id);
  w.raw(",\"args\":{\"hhash\":\"");
  w.raw(hostname_hash);
  w.raw("\"");
  for (uint32_t i = 0; i < b.keys_count; ++i) {
    const KeyField& f = interval.keys[b.keys_at + i];
    w.raw(",\"");
    w.raw(names_.at(f.key));
    w.raw("\":");
    if (f.tag == SHM_VALUE_STRING) {
      w.raw("\"");
      w.raw(interval.key_strings.at(static_cast<uint32_t>(f.v.u)));
      w.raw("\"");
    } else if (f.tag == SHM_VALUE_RAW_JSON) {
      w.raw(interval.key_strings.at(static_cast<uint32_t>(f.v.u)));
    } else {
      number(w, f.tag, f.v);
    }
  }
  w.raw(",\"" DFTRACER_AGGREGATOR_COUNT "\":");
  w.u64(b.count);
  for (const Accumulator& a : b.values) {
    const std::string_view key = names_.at(a.key);
    if (a.count == 1) {
      w.raw(",\"");
      w.raw(key);
      w.raw("\":");
      number(w, a.tag, a.sum);
      continue;
    }
    const std::pair<const char*, Number> parts[] = {
        {"_sum", a.sum}, {"_min", a.min}, {"_max", a.max}};
    for (const auto& [suffix, n] : parts) {
      w.raw(",\"");
      w.raw(key);
      w.raw(suffix);
      w.raw("\":");
      number(w, a.tag, n);
    }
  }
  w.raw("}}");
  size = w.finish("aggregated", 0);
}

}  // namespace dftracer
