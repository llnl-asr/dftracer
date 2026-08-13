#ifndef DFTRACER_EVENT_LOOP_H
#define DFTRACER_EVENT_LOOP_H

#include <uv.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dftracer {

/**
 * @brief The one libuv loop in a process.
 *
 * Everything in DFTracer that needs to be woken on a timer shares this: the
 * PAPI sampler in a traced process, and every telemetry collector in the
 * service. Reach it through Singleton<EventLoop>, never by constructing one --
 * a second loop would mean a second thread and a second set of handles doing
 * the same job, and the handles of one loop cannot be touched from the thread
 * of another.
 *
 * Two ways to drive it, because the two callers need different things:
 *
 *  * run() turns the calling thread into the loop thread and blocks until the
 *    loop stops. The service does this: its main thread has nothing else to do.
 *  * start_thread() runs the loop on a thread of its own. A traced process does
 *    this, since the application owns every thread it already has.
 *
 * Timers and signals must be registered before the loop starts. libuv handles
 * belong to the thread running the loop and may not be created from another
 * one, and both callers know what they need up front, so this is a constraint
 * rather than a limitation.
 *
 * Callbacks -- timers, signal handlers, work completions and the thread hooks
 * -- all run on the loop thread. Only stop() may be called from elsewhere.
 */
class EventLoop {
 public:
  using Callback = std::function<void()>;

  EventLoop();
  ~EventLoop();

  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  /**
   * @brief Call `on_tick` every `interval_ms`, on the loop thread.
   *
   * `first_delay_ms` is how long to wait for the first tick. The two callers
   * want different things and both are deliberate: the service passes 0 so a
   * short run still gets a sample of the node at startup, while the PAPI
   * sampler waits a full interval, since a counter that has just been started
   * has nothing to report yet.
   *
   * @return false if the loop is already running or could not be set up.
   */
  bool add_timer(uint64_t interval_ms, uint64_t first_delay_ms,
                 Callback on_tick);

  /**
   * @brief Call `on_signal` when `signum` arrives, on the loop thread.
   *
   * Used for the graceful-shutdown signals. A handler that calls stop() is the
   * expected shape.
   */
  bool add_signal(int signum, Callback on_signal);

  /**
   * @brief Run `on_start` and `on_stop` on the loop thread, around the loop.
   *
   * For work that has to happen on the same thread that will service the
   * timers. PAPI needs this: an event set can only be read and torn down by
   * the thread that created it, so its setup cannot run on whichever thread
   * happened to call start_thread().
   *
   * `on_start` runs before the loop is entered, `on_stop` after it returns.
   */
  void add_thread_hooks(Callback on_start, Callback on_stop);

  /**
   * @brief Run `work` on the libuv threadpool, then `after` on the loop thread.
   *
   * For a tick that may block -- reading /proc, scraping an exporter, talking
   * to a power register -- so a slow collector cannot hold up the others. The
   * loop stays alive until every queued piece of work has finished, so a stop()
   * mid-flight still lets it complete rather than abandoning it.
   *
   * Call from the loop thread. `after` may be empty.
   */
  bool queue_work(Callback work, Callback after);

  /** @brief Run the loop on the calling thread. Returns when it stops. */
  void run();

  /**
   * @brief Run the loop on a thread of its own.
   *
   * @return false if it is already running, or the thread could not start.
   */
  bool start_thread();

  /**
   * @brief Ask the loop to stop. Safe to call from any thread, and repeatedly.
   *
   * Stops and closes the timers and signal handlers, so the loop falls out on
   * its own once the work already queued has drained. It does not wait; pair it
   * with join(), or with run() returning.
   */
  void stop();

  /** @brief Wait for a loop started by start_thread() to have finished. */
  void join();

  bool is_running() const { return running.load(std::memory_order_acquire); }

 private:
  struct TimerTask {
    EventLoop* loop = nullptr;
    uv_timer_t handle{};
    uint64_t interval_ms = 0;
    uint64_t first_delay_ms = 0;
    Callback on_tick;
  };

  struct SignalTask {
    EventLoop* loop = nullptr;
    uv_signal_t handle{};
    int signum = 0;
    Callback on_signal;
  };

  struct WorkTask {
    EventLoop* loop = nullptr;
    uv_work_t request{};
    Callback work;
    Callback after;
  };

  uv_loop_t loop{};
  // Woken by stop() from any thread; its callback does the closing, on the loop
  // thread, which is the only thread allowed to touch the handles.
  uv_async_t stop_signal{};
  bool loop_initialized = false;
  // Guarded by mutex. stop() only wakes the async while this is true, and the
  // loop thread only closes the handles while holding the same lock, so a
  // uv_async_send can never land on a handle that is being closed.
  bool handles_armed = false;
  // A loop is single-use: its handles are closed on the way out and cannot be
  // restarted, so a second run() would arm freed handles.
  bool finished = false;

  std::atomic<bool> running{false};
  std::atomic<bool> stop_requested{false};
  std::thread loop_thread;

  // Held while registering and while starting or stopping, so a stop() racing
  // a start cannot see a half-built loop.
  mutable std::mutex mutex;

  // unique_ptr because libuv keeps the address of every handle: a vector that
  // reallocated would leave the loop pointing at freed memory.
  std::vector<std::unique_ptr<TimerTask>> timers;
  std::vector<std::unique_ptr<SignalTask>> signals;

  bool ensure_loop();
  // All three assume `mutex` is held: libuv handles are shared between the
  // loop thread and whoever calls stop().
  void arm_handles();
  void close_handles_locked();
  void drive();

  Callback thread_start;
  Callback thread_stop;

  static void on_timer(uv_timer_t* handle);
  static void on_signal_raised(uv_signal_t* handle, int signum);
  static void on_stop_requested(uv_async_t* handle);
  static void on_work(uv_work_t* request);
  static void on_work_done(uv_work_t* request, int status);
};

}  // namespace dftracer

#endif  // DFTRACER_EVENT_LOOP_H
