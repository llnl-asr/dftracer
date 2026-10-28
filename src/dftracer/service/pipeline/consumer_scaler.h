#ifndef DFTRACER_SERVICE_PIPELINE_CONSUMER_SCALER_H
#define DFTRACER_SERVICE_PIPELINE_CONSUMER_SCALER_H

#include <dftracer/service/common/datastructure.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dftracer {
namespace ipc {

/**
 * Decides how many drain threads the ring needs, once per tick, from what is
 * outstanding and how fast it arrives:
 *
 *   need = (arrival rate + ready backlog / catch-up time) / per-thread rate
 *
 * in blocks, where the per-thread rate is measured from the drain threads' own
 * busy time, so it follows the event mix (bigger metadata, slower decode). The
 * target is `need` at `utilization` (so a thread has slack before it falls
 * behind), clamped to [min, max].
 *
 * Up at once, down one thread at a time after `down_ticks` ticks in a row
 * that wanted fewer: a burst gets threads within one tick, and a dip between
 * bursts does not shed them.
 *
 * Pure: no clocks, no threads; RingSink feeds it samples, tests feed it
 * numbers.
 */
class ConsumerScaler {
 public:
  using Sample = ScalerSample;

  ConsumerScaler(uint32_t min_threads, uint32_t max_threads,
                 uint64_t catchup_ns, uint32_t down_ticks,
                 double utilization = 0.8)
      : min_(std::max<uint32_t>(1, min_threads)),
        max_(std::max(std::max<uint32_t>(1, min_threads), max_threads)),
        catchup_ns_(catchup_ns == 0 ? 1 : catchup_ns),
        down_ticks_(down_ticks),
        utilization_(utilization) {}

  uint32_t min_threads() const { return min_; }
  uint32_t max_threads() const { return max_; }
  /** Blocks one thread drains per second, as last measured (0: unknown). */
  double per_thread_rate() const { return rate_ * 1e9; }
  /** Threads the last sample asked for, before the step-down delay. */
  uint32_t last_target() const { return target_; }

  /** First sample: only sets the baseline. */
  void reset(const Sample& s) {
    last_ = s;
    primed_ = true;
    below_ = 0;
  }

  /** Feed one sample; returns the number of threads to run from now on. */
  uint32_t update(const Sample& s, uint32_t active) {
    active = std::min(std::max(active, min_), max_);
    if (!primed_ || s.now_ns <= last_.now_ns) {
      reset(s);
      return active;
    }
    const double dt = static_cast<double>(s.now_ns - last_.now_ns);
    const double arrived = static_cast<double>(s.head - last_.head);
    const uint64_t drained = s.drained - last_.drained;
    const uint64_t busy = s.busy_ns - last_.busy_ns;
    last_ = s;

    // Blocks per busy nanosecond of one thread. Smoothed: a tick holds only
    // a handful of blocks per thread, and a preempted thread looks slow.
    if (drained != 0 && busy != 0) {
      const double r = static_cast<double>(drained) / static_cast<double>(busy);
      rate_ = rate_ == 0 ? r : rate_ + kSmoothing * (r - rate_);
    }

    if (rate_ == 0) {
      // Nothing drained yet to measure: add a thread while work waits.
      target_ = s.ready > active ? active + 1 : active;
    } else {
      const double demand =
          arrived / dt + static_cast<double>(s.ready) / catchup_ns_;
      target_ = static_cast<uint32_t>(
          std::ceil(demand / (rate_ * utilization_) - 1e-9));
    }
    target_ = std::min(std::max(target_, min_), max_);

    if (target_ > active) {
      below_ = 0;
      return target_;
    }
    if (target_ < active) {
      if (++below_ >= down_ticks_) {
        below_ = 0;
        return active - 1;
      }
      return active;
    }
    below_ = 0;
    return active;
  }

 private:
  static constexpr double kSmoothing = 0.25;

  uint32_t min_, max_;
  double catchup_ns_;
  uint32_t down_ticks_;
  double utilization_;
  Sample last_{};
  bool primed_ = false;
  double rate_ = 0;  // blocks per busy ns, per thread
  uint32_t target_ = 0;
  uint32_t below_ = 0;
};

}  // namespace ipc
}  // namespace dftracer

#endif  // DFTRACER_SERVICE_PIPELINE_CONSUMER_SCALER_H
