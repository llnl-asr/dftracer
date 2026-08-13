#include <dftracer/core/common/event_loop.h>
#include <dftracer/core/common/logging.h>

#include <utility>

namespace dftracer {

EventLoop::EventLoop() {}

EventLoop::~EventLoop() {
  stop();
  join();

  std::lock_guard<std::mutex> guard(mutex);
  if (loop_initialized) {
    // Handles are closed by the stop path; this only reclaims the loop itself.
    uv_loop_close(&loop);
    loop_initialized = false;
  }
}

bool EventLoop::ensure_loop() {
  if (loop_initialized) return true;
  if (uv_loop_init(&loop) != 0) {
    DFTRACER_LOG_WARN("EventLoop could not initialize its libuv loop", "");
    return false;
  }
  loop_initialized = true;
  return true;
}

bool EventLoop::add_timer(uint64_t interval_ms, uint64_t first_delay_ms,
                          Callback on_tick) {
  if (!on_tick) return false;

  std::lock_guard<std::mutex> guard(mutex);
  // Adding a handle to a loop that is already spinning would mean creating it
  // from a thread that does not own the loop, which libuv does not allow.
  if (running.load(std::memory_order_acquire)) {
    DFTRACER_LOG_WARN(
        "EventLoop timers must be registered before the loop runs", "");
    return false;
  }
  if (!ensure_loop()) return false;

  auto task = std::make_unique<TimerTask>();
  task->loop = this;
  task->interval_ms = interval_ms;
  task->first_delay_ms = first_delay_ms;
  task->on_tick = std::move(on_tick);
  if (uv_timer_init(&loop, &task->handle) != 0) return false;
  task->handle.data = task.get();
  timers.push_back(std::move(task));
  return true;
}

bool EventLoop::add_signal(int signum, Callback on_signal) {
  if (!on_signal) return false;

  std::lock_guard<std::mutex> guard(mutex);
  if (running.load(std::memory_order_acquire)) {
    DFTRACER_LOG_WARN(
        "EventLoop signals must be registered before the loop runs", "");
    return false;
  }
  if (!ensure_loop()) return false;

  auto task = std::make_unique<SignalTask>();
  task->loop = this;
  task->signum = signum;
  task->on_signal = std::move(on_signal);
  if (uv_signal_init(&loop, &task->handle) != 0) return false;
  task->handle.data = task.get();
  signals.push_back(std::move(task));
  return true;
}

void EventLoop::add_thread_hooks(Callback on_start, Callback on_stop) {
  std::lock_guard<std::mutex> guard(mutex);
  thread_start = std::move(on_start);
  thread_stop = std::move(on_stop);
}

void EventLoop::arm_handles() {
  // Called on the loop thread with the loop not yet spinning.
  stop_signal.data = this;
  uv_async_init(&loop, &stop_signal, EventLoop::on_stop_requested);

  for (auto &timer : timers) {
    uv_timer_start(&timer->handle, EventLoop::on_timer, timer->first_delay_ms,
                   timer->interval_ms);
  }
  for (auto &signal : signals) {
    uv_signal_start(&signal->handle, EventLoop::on_signal_raised,
                    signal->signum);
  }
  handles_armed = true;
}

void EventLoop::close_handles_locked() {
  // On the loop thread. Closing every handle is what ends the loop: uv_run
  // returns once nothing is left to wait on, which lets work already queued on
  // the threadpool finish rather than being abandoned by a uv_stop().
  if (!handles_armed) return;
  handles_armed = false;

  for (auto &timer : timers) {
    uv_timer_stop(&timer->handle);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&timer->handle))) {
      uv_close(reinterpret_cast<uv_handle_t *>(&timer->handle), nullptr);
    }
  }
  for (auto &signal : signals) {
    uv_signal_stop(&signal->handle);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&signal->handle))) {
      uv_close(reinterpret_cast<uv_handle_t *>(&signal->handle), nullptr);
    }
  }
  if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&stop_signal))) {
    uv_close(reinterpret_cast<uv_handle_t *>(&stop_signal), nullptr);
  }
}

void EventLoop::drive() {
  // The whole of the loop thread's life: hooks, then the loop, then hooks. The
  // hooks run outside the lock; PAPI's take milliseconds.
  if (thread_start) thread_start();

  uv_run(&loop, UV_RUN_DEFAULT);

  {
    std::lock_guard<std::mutex> guard(mutex);
    // A stop() that arrived before the loop was entered leaves the handles
    // armed and uv_run returning at once; close them here so the loop is left
    // clean either way. Taking the lock is what makes it safe against a stop()
    // running right now on another thread.
    close_handles_locked();
    running.store(false, std::memory_order_release);
    finished = true;
  }
  // Lets the close callbacks run; nothing is left to wait on afterwards.
  uv_run(&loop, UV_RUN_DEFAULT);

  if (thread_stop) thread_stop();
}

void EventLoop::run() {
  {
    std::lock_guard<std::mutex> guard(mutex);
    if (running.load(std::memory_order_acquire) || finished) return;
    if (!ensure_loop()) return;
    arm_handles();
    running.store(true, std::memory_order_release);
  }
  drive();
}

bool EventLoop::start_thread() {
  {
    std::lock_guard<std::mutex> guard(mutex);
    if (running.load(std::memory_order_acquire) || finished) return false;
    if (!ensure_loop()) return false;
    arm_handles();
    running.store(true, std::memory_order_release);
  }

  try {
    loop_thread = std::thread([this]() { drive(); });
  } catch (const std::exception &error) {
    std::lock_guard<std::mutex> guard(mutex);
    running.store(false, std::memory_order_release);
    close_handles_locked();
    DFTRACER_LOG_WARN("EventLoop could not start its thread: %s", error.what());
    return false;
  }
  return true;
}

void EventLoop::stop() {
  stop_requested.store(true, std::memory_order_release);

  std::lock_guard<std::mutex> guard(mutex);
  if (!loop_initialized || !handles_armed) return;
  if (!running.load(std::memory_order_acquire)) {
    // Never started, or already finished: nothing is going to service the
    // async, so undo the arming here instead of waking a loop that is not
    // going to run.
    close_handles_locked();
    return;
  }
  // uv_async_send is the one libuv call that may cross threads, and the lock
  // guarantees the handle is still open: only close_handles_locked() closes it,
  // and it clears handles_armed under this same lock first.
  uv_async_send(&stop_signal);
}

void EventLoop::join() {
  if (loop_thread.joinable()) {
    loop_thread.join();
  }
}

bool EventLoop::queue_work(Callback work, Callback after) {
  if (!work) return false;
  if (!loop_initialized) return false;

  auto *task = new WorkTask();
  task->loop = this;
  task->work = std::move(work);
  task->after = std::move(after);
  task->request.data = task;

  int result = uv_queue_work(&loop, &task->request, EventLoop::on_work,
                             EventLoop::on_work_done);
  if (result != 0) {
    delete task;
    return false;
  }
  return true;
}

void EventLoop::on_timer(uv_timer_t *handle) {
  if (handle == nullptr || handle->data == nullptr) return;
  auto *task = static_cast<TimerTask *>(handle->data);
  if (task->on_tick) task->on_tick();
}

void EventLoop::on_signal_raised(uv_signal_t *handle, int /*signum*/) {
  if (handle == nullptr || handle->data == nullptr) return;
  auto *task = static_cast<SignalTask *>(handle->data);
  if (task->on_signal) task->on_signal();
}

void EventLoop::on_stop_requested(uv_async_t *handle) {
  if (handle == nullptr || handle->data == nullptr) return;
  auto *self = static_cast<EventLoop *>(handle->data);
  std::lock_guard<std::mutex> guard(self->mutex);
  self->close_handles_locked();
}

void EventLoop::on_work(uv_work_t *request) {
  if (request == nullptr || request->data == nullptr) return;
  auto *task = static_cast<WorkTask *>(request->data);
  if (task->work) task->work();
}

void EventLoop::on_work_done(uv_work_t *request, int /*status*/) {
  if (request == nullptr || request->data == nullptr) return;
  auto *task = static_cast<WorkTask *>(request->data);
  if (task->after) task->after();
  delete task;
}

}  // namespace dftracer
