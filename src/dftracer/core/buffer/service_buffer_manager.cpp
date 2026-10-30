#include <dftracer/core/buffer/service_buffer_manager.h>
#include <dftracer/core/common/event_types.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/utils/posix_bypass.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>

template <>
std::shared_ptr<dftracer::ServiceBufferManager>
    dftracer::Singleton<dftracer::ServiceBufferManager>::instance = nullptr;
template <>
bool dftracer::Singleton<
    dftracer::ServiceBufferManager>::stop_creating_instances = false;

namespace dftracer {

/**
 * The calling thread's slot. Its destructor runs at thread exit and publishes
 * whatever block the thread still holds, so a worker that logs and exits never
 * strands its last events.
 */
struct ServiceBufferManager::ThreadRef {
  ServiceBufferManager* owner = nullptr;
  std::shared_ptr<Slot> slot;

  ~ThreadRef() {
    tl_exited = true;
    if (owner == nullptr || slot == nullptr) return;
    Slot* s = slot.get();
    uint32_t expected = SHM_SLOT_IDLE;
    while (!s->state.compare_exchange_weak(expected, SHM_SLOT_BUSY,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed)) {
      if (expected == SHM_SLOT_DEAD) return;  // abandoned across fork()
      expected = SHM_SLOT_IDLE;
      ipc::cpu_relax();
    }
    if (s->producer) s->producer->flush();
    owner->retired_pushed_.fetch_add(s->pushed.load(std::memory_order_relaxed),
                                     std::memory_order_relaxed);
    s->state.store(SHM_SLOT_DEAD, std::memory_order_release);
    std::lock_guard<std::mutex> lock(owner->slots_mtx_);
    auto& slots = owner->slots_;
    slots.erase(std::remove(slots.begin(), slots.end(), slot), slots.end());
  }
};

thread_local ServiceBufferManager::ThreadRef ServiceBufferManager::tl_ref;
thread_local bool ServiceBufferManager::tl_exited = false;

ServiceBufferManager::ServiceBufferManager() {}

ServiceBufferManager::~ServiceBufferManager() {}

std::shared_ptr<ServiceBufferManager::Slot>
ServiceBufferManager::create_slot() {
  auto slot = std::make_shared<Slot>();
  slot->generation = generation_.load(std::memory_order_acquire);
  // Once per thread: the service keeps one aggregation lane per producer
  // thread, keyed by this id.
  slot->producer = std::make_unique<ipc::RingProducer>(
      region_->ring(), static_cast<uint32_t>(owner_pid_),
      static_cast<uint32_t>(syscall(SYS_gettid)));
  slot->scratch.reserve(4096);
  std::lock_guard<std::mutex> lock(slots_mtx_);
  slots_.push_back(slot);
  return slot;
}

ServiceBufferManager::Slot* ServiceBufferManager::register_slot() {
  tl_ref.owner = this;
  tl_ref.slot = create_slot();
  return tl_ref.slot.get();
}

ServiceBufferManager::Slot* ServiceBufferManager::acquire_slot() {
  if (tl_exited) {
    // This thread's slot is gone; tl_ref must not be touched again.
    orphan_mtx_.lock();
    if (orphan_ == nullptr ||
        orphan_->generation != generation_.load(std::memory_order_relaxed))
      orphan_ = create_slot();
    Slot* s = orphan_.get();
    uint32_t expected = SHM_SLOT_IDLE;
    while (!s->state.compare_exchange_weak(expected, SHM_SLOT_BUSY,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed)) {
      expected = SHM_SLOT_IDLE;
      ipc::cpu_relax();
    }
    return s;
  }
  Slot* s = tl_ref.slot.get();
  if (s == nullptr ||
      s->generation != generation_.load(std::memory_order_relaxed)) {
    s = register_slot();
  }
  // Only this thread and an occasional flush_idle() ever touch the word, so
  // this is an uncontended CAS on a line this thread already owns.
  uint32_t expected = SHM_SLOT_IDLE;
  while (!s->state.compare_exchange_weak(expected, SHM_SLOT_BUSY,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
    expected = SHM_SLOT_IDLE;
    ipc::cpu_relax();
  }
  return s;
}

void ServiceBufferManager::release_slot(Slot* slot) {
  slot->state.store(SHM_SLOT_IDLE, std::memory_order_release);
  if (tl_exited) orphan_mtx_.unlock();
}

bool ServiceBufferManager::push(Slot* slot, ShmRecordKind kind,
                                uint64_t wait_ns) {
  ipc::RingProducer& producer = *slot->producer;
  const size_t bytes = slot->scratch.size();
  ipc::Reservation r;
  auto result = producer.try_reserve(bytes, kind, r);
  if (result == ipc::RingProducer::Result::FULL && wait_ns != 0) {
    result = producer.reserve(bytes, kind, r, wait_ns);
  }
  if (result != ipc::RingProducer::Result::OK) return false;
  std::memcpy(r.payload, slot->scratch.data(), bytes);
  producer.commit(r);
  slot->pushed.store(slot->pushed.load(std::memory_order_relaxed) + 1,
                     std::memory_order_relaxed);
  return true;
}

bool ServiceBufferManager::service_alive() {
  if (service_gone_.load(std::memory_order_relaxed)) return false;
  const uint64_t now = ipc::monotonic_ns();
  uint64_t last = liveness_checked_ns_.load(std::memory_order_relaxed);
  if (now - last < DFT_SHM_LIVENESS_CHECK_NS) return true;
  // One thread checks; the others carry on until it has.
  if (!liveness_checked_ns_.compare_exchange_strong(last, now,
                                                    std::memory_order_relaxed))
    return true;
  auto* header = region_->ring().header();
  const pid_t service = static_cast<pid_t>(header->service_pid);
  const bool accepting =
      header->state.load(std::memory_order_acquire) == SHM_SEGMENT_ACCEPTING;
  const bool running = service <= 0 || kill(service, 0) == 0 || errno == EPERM;
  if (accepting && running) return true;
  service_gone_.store(true, std::memory_order_relaxed);
  std::fprintf(stderr,
               "[DFTRACER] producer/consumer mode: pid %d: dftracer_service "
               "(pid %d) %s; events go to %s from now on\n",
               owner_pid_, static_cast<int>(service),
               running ? "is no longer accepting events" : "is gone",
               spill_file_.c_str());
  return false;
}

template <typename Encode>
bool ServiceBufferManager::emit(ShmRecordKind kind, Encode&& encode) {
  if (region_ == nullptr || !service_alive()) return false;
  // Once this thread found the ring full it spills, in-process, without
  // encoding for the ring, until it is time to try the ring again.
  thread_local uint64_t spill_until_ns = 0;
  if (spill_until_ns != 0) {
    if (ipc::monotonic_ns() < spill_until_ns) return false;
    spill_until_ns = 0;
  }
  Slot* slot = acquire_slot();
  ipc::ByteWriter writer(slot->scratch);
  encode(writer);
  const bool pushed = push(slot, kind, full_wait_ns_);
  release_slot(slot);
  if (!pushed) spill_until_ns = ipc::monotonic_ns() + DFT_SHM_SPILL_RETRY_NS;
  return pushed;
}

template <typename Encode>
void ServiceBufferManager::emit_control(ShmRecordKind kind, Encode&& encode) {
  // No service, or none left: nothing to tell it.
  if (region_ == nullptr || !service_alive()) return;
  Slot* slot = acquire_slot();
  ipc::ByteWriter writer(slot->scratch);
  encode(writer);
  const bool pushed =
      push(slot, kind, std::max(full_wait_ns_, DFT_SHM_CONTROL_WAIT_NS));
  release_slot(slot);
  if (!pushed) control_dropped_.fetch_add(1, std::memory_order_relaxed);
}

void ServiceBufferManager::count_drop() {
  retired_dropped_.fetch_add(1, std::memory_order_relaxed);
  // For the service's report: it cannot see this process's counters.
  if (region_ != nullptr)
    region_->ring().header()->dropped.fetch_add(1, std::memory_order_relaxed);
}

LocalBufferManager* ServiceBufferManager::spill_manager() {
  if (spill_ready_.load(std::memory_order_acquire)) return spill_.get();
  std::lock_guard<std::mutex> lock(spill_mtx_);
  if (spill_ready_.load(std::memory_order_relaxed)) return spill_.get();
  if (spill_failed_ || spill_file_.empty()) return nullptr;
  auto manager = std::make_unique<LocalBufferManager>();
  if (manager->initialize(spill_file_.c_str(),
                          const_cast<char*>(hostname_hash_.c_str())) != 0) {
    spill_failed_ = true;
    return nullptr;
  }
  // Held until finalize, on a descriptor of its own: the service appends a
  // spill file to the node trace only once it can take this lock, so never
  // one this process is still writing.
  auto& posix = POSIXBypass::get_instance();
  spill_lock_fd_ = posix.open(spill_file_.c_str(), O_RDONLY | O_CLOEXEC);
  if (spill_lock_fd_ >= 0) flock(spill_lock_fd_, LOCK_EX);
  if (!app_name.empty()) manager->set_app_name(app_name.c_str());
  if (rank >= 0) manager->set_rank(rank);
  spill_ = std::move(manager);
  spill_ready_.store(true, std::memory_order_release);
  std::fprintf(stderr,
               "[DFTRACER] producer/consumer mode: pid %d: events that do not "
               "get into the ring go to %s\n",
               owner_pid_, spill_file_.c_str());
  return spill_.get();
}

template <typename Log>
bool ServiceBufferManager::spill(Log&& log) {
  LocalBufferManager* manager = spill_manager();
  if (manager == nullptr) {
    count_drop();
    return false;
  }
  log(*manager);
  spilled_.fetch_add(1, std::memory_order_relaxed);
  if (region_ != nullptr)
    region_->ring().header()->spilled.fetch_add(1, std::memory_order_relaxed);
  return true;
}

std::string ServiceBufferManager::spill_file_for(
    const std::string& trace_file) {
  if (trace_file.empty()) return "";
  auto strip = [](std::string& name, const std::string& end) {
    if (name.size() < end.size() ||
        name.compare(name.size() - end.size(), end.size(), end) != 0)
      return false;
    name.resize(name.size() - end.size());
    return true;
  };
  std::string base = trace_file;
  strip(base, ".pfw.gz");
  for (const char* suffix : {"-preload", "-app", "-fork"})
    if (strip(base, suffix)) break;
  return base + "-spill.pfw.gz";
}

void ServiceBufferManager::abandon_slots() {
  std::lock_guard<std::mutex> lock(slots_mtx_);
  generation_.fetch_add(1, std::memory_order_acq_rel);
  for (auto& slot : slots_) {
    slot->state.store(SHM_SLOT_DEAD, std::memory_order_relaxed);
    // The block belongs to the parent; never publish it from here.
    (void)slot->producer.release();
  }
  slots_.clear();
}

int ServiceBufferManager::initialize(const char* filename,
                                     HashType hostname_hash) {
  auto config = Singleton<ConfigurationManager>::get_instance();
  const int pid = static_cast<int>(getpid());
  if (owner_pid_ != 0 && owner_pid_ != pid) {
    // A forked child re-initializing: start over with fresh slots, and leave
    // the parent's spill file (and what it still buffers) to the parent.
    abandon_slots();
    (void)spill_.release();
    // The lock is shared with the parent through this descriptor: closing it
    // here leaves the parent's lock in place but lets it go once the parent
    // closes its own.
    if (spill_lock_fd_ >= 0) POSIXBypass::get_instance().close(spill_lock_fd_);
    spill_lock_fd_ = -1;
    spill_ready_.store(false, std::memory_order_relaxed);
    spill_failed_ = false;
    spilled_.store(0, std::memory_order_relaxed);
    control_dropped_.store(0, std::memory_order_relaxed);
    service_gone_.store(false, std::memory_order_relaxed);
  }
  owner_pid_ = pid;
  log_file_ = filename == nullptr ? "" : filename;
  spill_file_ = spill_file_for(log_file_);
  hostname_hash_ = hostname_hash == nullptr ? "" : hostname_hash;
  full_wait_ns_ = config->pc_full_wait_us * 1000ULL;
  event_flags_ = config->metadata ? SHM_EVENT_INC_METADATA : 0;
  if (region_ == nullptr) {
    region_ = ipc::ShmRegion::open(config->service_shm);
    if (region_ == nullptr ||
        region_->ring().header()->state.load(std::memory_order_acquire) !=
            SHM_SEGMENT_ACCEPTING) {
      region_.reset();
      std::fprintf(stderr,
                   "[DFTRACER] producer/consumer mode: no dftracer_service is "
                   "accepting events on %s; events of pid %d go to %s. "
                   "Start dftracer_service with DFTRACER_ENABLE_PC_MODE=1 "
                   "before the application.\n",
                   config->service_shm.c_str(), pid, spill_file_.c_str());
    }
  }
  ready.store(true, std::memory_order_release);
  emit_control(SHM_KIND_HELLO, [&](ipc::ByteWriter& w) {
    ipc::encode_hello(w, pid, hostname_hash_.c_str(), log_file_.c_str());
  });
  if (!app_name.empty()) set_app_name(app_name.c_str());
  if (rank >= 0) set_rank(rank);
  return 0;
}

int ServiceBufferManager::finalize(int index, ProcessID process_id) {
  if (!is_ready()) return 0;
  emit_control(SHM_KIND_GOODBYE,
               [&](ipc::ByteWriter& w) { w.put(static_cast<int32_t>(index)); });
  ready.store(false, std::memory_order_release);
  flush_all(DFT_SHM_FINALIZE_WAIT_NS);
  if (spill_ready_.load(std::memory_order_acquire)) {
    // At least 5 so the writer keeps the file: it holds events, not just
    // the few a short process would otherwise be deleted for.
    spill_->finalize(std::max(index, 5), process_id);
    // Complete now: the service may append it to the node trace.
    if (spill_lock_fd_ >= 0) {
      flock(spill_lock_fd_, LOCK_UN);
      POSIXBypass::get_instance().close(spill_lock_fd_);
      spill_lock_fd_ = -1;
    }
  }
  const uint64_t spilled_events = spilled();
  const uint64_t lost = dropped();
  const uint64_t control = control_dropped_.load(std::memory_order_relaxed);
  if (spilled_events != 0 || lost != 0 || control != 0) {
    std::fprintf(stderr,
                 "[DFTRACER] producer/consumer mode: pid %d: %llu records in "
                 "the ring, %llu events spilled to %s, %llu events dropped, "
                 "%llu control records not delivered\n",
                 owner_pid_, static_cast<unsigned long long>(pushed()),
                 static_cast<unsigned long long>(spilled_events),
                 spill_file_.c_str(), static_cast<unsigned long long>(lost),
                 static_cast<unsigned long long>(control));
  }
  return 0;
}

void ServiceBufferManager::flush_idle(uint64_t max_age_ns) {
  const uint64_t now = ipc::monotonic_ns();
  std::lock_guard<std::mutex> lock(slots_mtx_);
  for (auto& slot : slots_) {
    uint32_t expected = SHM_SLOT_IDLE;
    if (!slot->state.compare_exchange_strong(expected, SHM_SLOT_BUSY,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
      continue;  // logging right now, or gone; it publishes on its own
    }
    if (slot->producer) slot->producer->flush_if_older(now, max_age_ns);
    slot->state.store(SHM_SLOT_IDLE, std::memory_order_release);
  }
}

void ServiceBufferManager::flush() {
  if (!is_ready()) return;
  flush_all(DFT_SHM_FINALIZE_WAIT_NS);
  if (spill_ready_.load(std::memory_order_acquire)) spill_->flush();
}

void ServiceBufferManager::flush_all(uint64_t wait_ns) {
  const uint64_t deadline = ipc::monotonic_ns() + wait_ns;
  std::lock_guard<std::mutex> lock(slots_mtx_);
  for (auto& slot : slots_) {
    // A thread inside a log call holds its slot for a few hundred ns: wait
    // for it rather than leave its block for the service to take over.
    uint32_t expected = SHM_SLOT_IDLE;
    while (!slot->state.compare_exchange_weak(expected, SHM_SLOT_BUSY,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
      if (expected == SHM_SLOT_DEAD || ipc::monotonic_ns() > deadline) break;
      expected = SHM_SLOT_IDLE;
      ipc::cpu_relax();
    }
    if (expected != SHM_SLOT_IDLE) continue;
    if (slot->producer) slot->producer->flush();
    slot->state.store(SHM_SLOT_IDLE, std::memory_order_release);
  }
}

uint64_t ServiceBufferManager::pushed() {
  uint64_t total = retired_pushed_.load(std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(slots_mtx_);
  for (auto& slot : slots_)
    total += slot->pushed.load(std::memory_order_relaxed);
  return total;
}

uint64_t ServiceBufferManager::dropped() {
  return retired_dropped_.load(std::memory_order_relaxed);
}

void ServiceBufferManager::set_app_name(const char* name) {
  BufferManager::set_app_name(name);
  if (!is_ready() || name == nullptr) return;
  if (spill_ready_.load(std::memory_order_acquire)) spill_->set_app_name(name);
  emit_control(SHM_KIND_SET_APP_NAME, [&](ipc::ByteWriter& w) {
    w.put_bytes(name, std::strlen(name));
  });
}

void ServiceBufferManager::set_rank(const int& r) {
  BufferManager::set_rank(r);
  if (!is_ready()) return;
  if (spill_ready_.load(std::memory_order_acquire)) spill_->set_rank(r);
  emit_control(SHM_KIND_SET_RANK,
               [&](ipc::ByteWriter& w) { w.put(static_cast<int32_t>(r)); });
}

void ServiceBufferManager::log_data_event(
    int index, ConstEventNameType event_name, ConstEventNameType category,
    TraceEventType type, TimeResolution start_time, TimeResolution duration,
    dftracer::Metadata* metadata, ProcessID process_id, ThreadID tid) {
  if (!is_ready()) return;
  // The service has no event-type registry: tell it per event.
  const uint8_t flags = static_cast<uint8_t>(
      event_flags_ |
      (dftracer::event_type_aggregatable(type) ? 0 : SHM_EVENT_NO_AGGREGATE));
  if (emit(SHM_KIND_DATA, [&](ipc::ByteWriter& w) {
        ipc::encode_event(w, index, event_name, category, type, start_time,
                          duration, metadata, process_id, tid, flags);
      })) {
    delete metadata;
    return;
  }
  // The spill file's manager takes the metadata, as it does in-process.
  if (!spill([&](LocalBufferManager& m) {
        m.log_data_event(index, event_name, category, type, start_time,
                         duration, metadata, process_id, tid);
      }))
    delete metadata;
}

void ServiceBufferManager::log_counter_event(
    int index, ConstEventNameType name, ConstEventNameType category,
    TraceEventType type, TimeResolution start_time, ProcessID process_id,
    ThreadID thread_id, dftracer::Metadata* metadata) {
  if (!is_ready()) return;
  if (emit(SHM_KIND_COUNTER, [&](ipc::ByteWriter& w) {
        ipc::encode_event(w, index, name, category, type, start_time, 0,
                          metadata, process_id, thread_id, event_flags_);
      })) {
    delete metadata;
    return;
  }
  if (!spill([&](LocalBufferManager& m) {
        m.log_counter_event(index, name, category, type, start_time, process_id,
                            thread_id, metadata);
      }))
    delete metadata;
}

void ServiceBufferManager::log_record(ConstEventNameType record_name,
                                      const dftracer::Metadata* fields,
                                      TraceEventType type, ProcessID process_id,
                                      ThreadID tid) {
  if (!is_ready()) return;
  if (emit(SHM_KIND_RECORD, [&](ipc::ByteWriter& w) {
        ipc::encode_record(w, record_name, fields, type, process_id, tid);
      }))
    return;
  spill([&](LocalBufferManager& m) {
    m.log_record(record_name, fields, type, process_id, tid);
  });
}

void ServiceBufferManager::log_metadata_event(ConstEventNameType name,
                                              ConstEventNameType value,
                                              ConstEventNameType record_name,
                                              TraceEventType type,
                                              ProcessID process_id,
                                              ThreadID tid, bool is_string) {
  if (!is_ready()) return;
  if (emit(SHM_KIND_METADATA, [&](ipc::ByteWriter& w) {
        ipc::encode_metadata_event(w, name, value, record_name, type,
                                   process_id, tid, is_string);
      }))
    return;
  spill([&](LocalBufferManager& m) {
    m.log_metadata_event(name, value, record_name, type, process_id, tid,
                         is_string);
  });
}

}  // namespace dftracer
