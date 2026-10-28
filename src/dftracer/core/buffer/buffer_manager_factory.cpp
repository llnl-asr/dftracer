#include <dftracer/core/buffer/buffer_manager.h>
#include <dftracer/core/buffer/local_buffer_manager.h>
#include <dftracer/core/buffer/service_buffer_manager.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <atomic>

namespace dftracer {

namespace {
std::atomic<bool>& local_forced() {
  static std::atomic<bool> forced{false};
  return forced;
}
}  // namespace

void BufferManagerFactory::force_local() {
  local_forced().store(true, std::memory_order_release);
}

std::shared_ptr<BufferManager> BufferManagerFactory::get_instance() {
  // Decided once: the first caller's view of the configuration picks the
  // implementation for the life of the process.
  static const bool pc_mode = []() {
    if (local_forced().load(std::memory_order_acquire)) return false;
    auto config = Singleton<ConfigurationManager>::get_instance();
    return config != nullptr && config->pc_mode;
  }();
  if (pc_mode) return Singleton<ServiceBufferManager>::get_instance();
  return Singleton<LocalBufferManager>::get_instance();
}

}  // namespace dftracer
