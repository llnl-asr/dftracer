#include <dftracer/core/df_logger.h>
#include <pthread.h>

#include <new>
template <>
std::shared_ptr<DFTLogger> dftracer::Singleton<DFTLogger>::instance = nullptr;
template <>
bool dftracer::Singleton<DFTLogger>::stop_creating_instances = false;

std::atomic<DFTLogger*> DFTLogger::fork_owner_{nullptr};

namespace {
__attribute__((constructor)) void register_logger_fork_handlers() {
  pthread_atfork(
      [] {
        auto* l = DFTLogger::fork_owner_.load(std::memory_order_acquire);
        if (l != nullptr) l->lock_for_fork();
      },
      [] {
        auto* l = DFTLogger::fork_owner_.load(std::memory_order_acquire);
        if (l != nullptr) l->unlock_in_parent();
      },
      [] {
        auto* l = DFTLogger::fork_owner_.load(std::memory_order_acquire);
        if (l != nullptr) l->reset_in_child();
      });
}
}  // namespace

// The order clear_hash_cache takes them in.
void DFTLogger::lock_for_fork() {
  map_mtx.lock();
  mpi_file_mtx.lock();
  app_metadata_mtx_.lock();
  used_extra_mtx_.lock();
}

void DFTLogger::unlock_in_parent() {
  used_extra_mtx_.unlock();
  app_metadata_mtx_.unlock();
  mpi_file_mtx.unlock();
  map_mtx.unlock();
}

// pthread_rwlock_unlock matches the writer by tid, which is new in the child.
void DFTLogger::reset_in_child() {
  new (&map_mtx) std::shared_mutex();
  new (&mpi_file_mtx) std::shared_mutex();
  new (&app_metadata_mtx_) std::mutex();
  new (&used_extra_mtx_) std::shared_mutex();
}
