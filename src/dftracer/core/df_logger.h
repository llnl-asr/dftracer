//
// Created by haridev on 3/28/23.
//

#ifndef DFTRACER_GENERIC_LOGGER_H
#define DFTRACER_GENERIC_LOGGER_H

#include <dftracer/core/buffer/local_buffer_manager.h>
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/event_draft.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/module_hooks.h>
#include <dftracer/core/common/profiler.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/common/tracer_scope.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/utils/fs_info.h>
#include <dftracer/core/utils/md5.h>
#include <dftracer/core/utils/posix_bypass.h>
#include <dftracer/core/utils/tsc_clock.h>
#include <dftracer/core/utils/utils.h>
#include <libgen.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dftracer/core/dftracer_config.hpp>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

typedef std::chrono::high_resolution_clock chrono;

// A process that has just been fork()'d (e.g. MPI runtimes spawning a
// helper daemon for singleton MPI_Init, before it execs a new image) must
// not touch MPI: the child shares MPI's memory-mapped/shared-memory
// transport state with the parent, and calling into MPI there (without this
// process ever having called MPI_Init itself) corrupts that shared state and
// can crash the parent later. Set by the post-fork child handlers and
// cleared once this process legitimately calls MPI_Init/MPI_Init_thread.
inline std::atomic<bool>& dftracer_mpi_fork_guard() {
  static std::atomic<bool> in_forked_child_without_mpi_init{false};
  return in_forked_child_without_mpi_init;
}

namespace dftracer {
class ServiceBufferManager;
}

class DFTLogger {
 private:
  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::shared_mutex map_mtx;

  inline void clear_hash_cache() {
    std::unique_lock<std::shared_mutex> lock(map_mtx);
    for (auto& hash : computed_hash) {
      if (hash.second.hash) free(hash.second.hash);
      free(const_cast<char*>(hash.first.data()));
    }
    computed_hash.clear();
    // Its values point into computed_hash, freed above.
    std::unique_lock<std::shared_mutex> file_lock(mpi_file_mtx);
    mpi_file_hash.clear();
  }
  bool throw_error;
  bool is_init, dftracer_tid;
  ProcessID process_id;
  // Every string hashed this run, keyed by the string as traced. `file` marks
  // the FH (file) ones, which get "FS" records at finalize (emit_fs_records);
  // that walk is the only cost, so tracing pays nothing per operation.
  // Keyed by string_view over a copy of the string the table owns (freed in
  // clear_hash_cache), so a lookup, once per path-based call, takes the
  // caller's pointer as it is and allocates nothing.
  struct HashEntry {
    HashType hash;
    bool file;
  };
  std::unordered_map<std::string_view, HashEntry> computed_hash;
  // Ids are handed to threads kIdBlock at a time, and each thread counts its
  // own events, so no counter is written by every thread on every event. Ids
  // stay unique in the process but are not dense; num_events() is the count.
  std::atomic_int index;
  static constexpr int kIdBlock = 1024;
  struct ThreadEvents {
    explicit ThreadEvents(uint64_t e) : epoch(e) {}
    const uint64_t epoch;
    std::atomic<uint64_t> count{0};
  };
  // reinitialize() starts a new epoch. A thread's old ThreadEvents stays
  // allocated, since the thread may still be counting into it.
  std::atomic<uint64_t> events_epoch_{1};
  std::mutex events_mtx_;
  std::vector<std::unique_ptr<ThreadEvents>> thread_events_;
  [[maybe_unused]] bool is_aggregated;
  // Every logging thread sets it.
  std::atomic<bool> has_entry;
  // Set once the MPI rank has been recorded.
  bool mpi_event;
  // The fhash of the path each open MPI_File was opened with, keyed by the
  // handle's bits. An MPI-IO call records this fhash, never the handle: a
  // handle is an opaque pointer in OpenMPI and ROMIO and means nothing in a
  // trace. Values are owned by computed_hash.
  //
  // The handle type is a template parameter so this header needs no mpi.h:
  // only the MPI module, which has MPI_File, instantiates these.
  std::shared_mutex mpi_file_mtx;
  std::unordered_map<uint64_t, HashType> mpi_file_hash;
  template <typename FileHandle>
  static uint64_t mpi_file_key(FileHandle fh) {
    static_assert(sizeof(FileHandle) <= sizeof(uint64_t),
                  "an MPI_File handle must fit the map key");
    uint64_t key = 0;
    std::memcpy(&key, &fh, sizeof(fh));
    return key;
  }
  bool enable_core_affinity;
  // DFTRACER_MIN_DURATION_NS, and the ns in one unit of config->time_metric:
  // log() skips an event whose duration * ns_per_unit_ < min_duration_ns_.
  uint64_t min_duration_ns_ = 0;
  uint64_t ns_per_unit_ = 1000;
  std::shared_ptr<dftracer::BufferManager> buffer_manager;

  // Bitmask of TraceEventType values seen by log(), i.e. which instrumentation
  // layers actually fired at least one event this run. Read out in finalize()
  // to record e.g. "was MPI/HDF5/HIP/Python actually exercised" alongside the
  // compile-time availability of those layers.
  std::atomic<uint32_t> used_layers_{0};

  // Process-global, app-supplied metadata (set via the public C/C++/Python
  // set_app_metadata* API) that gets folded into the "end" event at
  // finalize(), separate from the per-region metadata the update_* APIs emit.
  std::mutex app_metadata_mtx_;
  dftracer::Metadata app_metadata_;

  // Named "sub-layers" reported used via the public mark_used API, folded
  // into the same "used" object as the TraceEventType-derived layers (see
  // used_layers_). This is how integrations that all log through a single
  // TraceEventType (e.g. every Python-side integration logs as PYTHON) can
  // still distinguish themselves in the end event, e.g. "used.torch_profiler"
  // vs "used.dynamo" vs "used.ai" vs "used.python_function", the same way
  // "used.C_APP" vs "used.CPP_APP" fall out of the C and C++ APIs using
  // different TraceEventTypes.
  std::shared_mutex used_extra_mtx_;
  std::unordered_set<std::string> used_extra_;

  // Runtime facts resolved by DFTracerCore at init (see set_runtime_info),
  // folded into the "cfg" object at finalize() alongside the static
  // ConfigurationManager settings: whether GOTCHA interception was actually
  // bound this run, and the fully-resolved trace log file path.
  bool runtime_bind_ = false;
  std::string runtime_log_file_;

  std::vector<unsigned> core_affinity() {
    DFTRACER_LOG_DEBUG("DFTLogger.core_affinity");
    auto cores = std::vector<unsigned>();
    if (enable_core_affinity && dftracer_module_hwloc_core_affinity) {
      dftracer_module_hwloc_core_affinity(&cores);
    }
    return cores;
  }

 public:
  bool include_metadata;
  // The buffer manager in pc mode with metadata, else null (log_site).
  dftracer::ServiceBufferManager* site_sink_ = nullptr;
  DFTLogger(bool init_log = false)
      : is_init(false),
        dftracer_tid(false),
        process_id(-1),
        computed_hash(),
        index(0),
        is_aggregated(false),
        has_entry(false),
        mpi_event(false),
        enable_core_affinity(false),
        include_metadata(false) {
    DFTRACER_LOG_DEBUG("DFTLogger.DFTLogger");
    config =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    enable_core_affinity = config->core_affinity;
    include_metadata = config->metadata;
    dftracer_tid = config->tids;
    throw_error = config->throw_error;
    min_duration_ns_ = config->min_duration_ns;
    dftracer::TscClock::start(config->tsc_clock);
    DFTRACER_LOG_INFO(
        "timestamps from %s (DFTRACER_TSC_CLOCK=%s)",
        dftracer::TscClock::enabled() ? "the TSC" : "clock_gettime",
        to_string(config->tsc_clock).c_str());
    switch (config->time_metric) {
      case TimeMetricType::TIME_METRIC_NS:
        ns_per_unit_ = 1;
        break;
      case TimeMetricType::TIME_METRIC_MS:
        ns_per_unit_ = 1000000ULL;
        break;
      case TimeMetricType::TIME_METRIC_SEC:
        ns_per_unit_ = 1000000000ULL;
        break;
      case TimeMetricType::TIME_METRIC_US:
      default:
        ns_per_unit_ = 1000ULL;
        break;
    }
    // The hwloc module loads the topology on first use.
    this->buffer_manager = dftracer::BufferManagerFactory::get_instance();
    site_sink_ = include_metadata && this->buffer_manager != nullptr
                     ? this->buffer_manager->as_service()
                     : nullptr;
    this->is_init = true;
    fork_owner_.store(this, std::memory_order_release);
  }
  ~DFTLogger() {
    DFTLogger* self = this;
    fork_owner_.compare_exchange_strong(self, nullptr);
    clear_hash_cache();
  }

  /** pthread_atfork handlers: no lock may be held across fork. */
  void lock_for_fork();
  void unlock_in_parent();
  void reset_in_child();
  static std::atomic<DFTLogger*> fork_owner_;

  void reinitialize() {
    DFTRACER_LOG_DEBUG("DFTLogger.reinitialize");
    index.store(0);
    events_epoch_.fetch_add(1, std::memory_order_relaxed);
  }

  /** Events logged in this epoch, by every thread. */
  uint64_t num_events() {
    const uint64_t epoch = events_epoch_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(events_mtx_);
    uint64_t total = 0;
    for (const auto& e : thread_events_)
      if (e->epoch == epoch) total += e->count.load(std::memory_order_relaxed);
    return total;
  }

  // App-supplied metadata, folded into the "end" event's args at finalize().
  // Safe to call from any traced API (C, C++, Python) at any point before
  // finalize(); last write for a given key wins.
  inline void add_app_metadata(const std::string& key, int64_t value) {
    std::lock_guard<std::mutex> lock(app_metadata_mtx_);
    app_metadata_.insert_or_assign(key, value);
  }
  inline void add_app_metadata(const std::string& key,
                               const std::string& value) {
    std::lock_guard<std::mutex> lock(app_metadata_mtx_);
    app_metadata_.insert_or_assign(key, value);
  }

  // Reports that a named sub-layer/integration was exercised this run (see
  // used_extra_). Meant to be cheap to call on every invocation of the
  // integration when there is no natural one-time init hook: the common case
  // (name already recorded) only takes a shared/read lock to check
  // membership, so concurrent repeat callers don't serialize on each other;
  // only the first caller for a given name pays the exclusive lock to insert.
  inline void mark_used(const std::string& name) {
    {
      std::shared_lock<std::shared_mutex> lock(used_extra_mtx_);
      if (used_extra_.find(name) != used_extra_.end()) return;
    }
    std::unique_lock<std::shared_mutex> lock(used_extra_mtx_);
    used_extra_.insert(name);
  }

  // Called once by DFTracerCore right after it resolves the trace log file
  // and binds (or doesn't bind) interceptors, so both facts can be folded
  // into the "cfg" object at finalize(). Single-threaded at init time (no
  // lock needed).
  inline void set_runtime_info(bool bind, const std::string& log_file) {
    runtime_bind_ = bind;
    runtime_log_file_ = log_file;
  }

  // Returns false once finalize() has been called (is_init set to false).
  // Interceptors should treat a false return as a signal to skip tracing
  // and pass through to the real function.
  inline bool is_active() const { return is_init; }
  inline ProcessID get_process_id() const { return process_id; }

  // Every other byte of the MD5, as 16 hex digits.
  inline HashType get_hash(char* name) {
    static constexpr char digits[] = "0123456789abcdef";
    uint8_t result[HASH_OUTPUT];
    md5String(name, result);
    char* hash_str = (char*)malloc(HASH_OUTPUT * 2 + 1);
    for (int i = 0; i < HASH_OUTPUT; i += 2) {
      hash_str[i] = digits[result[i] >> 4];
      hash_str[i + 1] = digits[result[i] & 0xf];
    }
    hash_str[HASH_OUTPUT] = '\0';
    return hash_str;
  }

  inline void update_log_file(std::string log_file, std::string exec_name,
                              std::string cmd, ProcessID process_id = -1) {
    DFTRACER_LOG_DEBUG("DFTLogger.update_log_file %s", log_file.c_str());
    this->process_id = df_getpid();
    ThreadID tid = 0;
    if (dftracer_tid) {
      tid = df_gettid();
    }

    HashType hostname_hash;
    HashType cmd_hash;
    HashType exec_hash;
    char hostname[256];
    gethostname(hostname, 256);
    hostname[255] = '\0';
    hostname_hash = has_hash(hostname);
    if (hostname_hash == NO_HASH_DEFAULT) {
      hostname_hash = get_hash(hostname);
      insert_hash(hostname, hostname_hash);
    }
    if (this->buffer_manager != nullptr) {
      this->buffer_manager->initialize(log_file.c_str(), hostname_hash);
      this->buffer_manager->log_metadata_event(
          hostname, hostname_hash, METADATA_NAME_HOSTNAME_HASH,
          TraceEventType::TRACE_TYPE_DFTRACER, this->process_id, tid, true);
      char thread_name[128];
      auto size =
          dftracer_logging_real_sprintf()(thread_name, "%d", this->process_id);
      thread_name[size] = '\0';
      this->buffer_manager->log_metadata_event(
          thread_name, METADATA_NAME_THREAD_NAME, METADATA_NAME_THREAD_NAME,
          TraceEventType::TRACE_TYPE_DFTRACER, this->process_id, tid);
      std::string time_metric_value = to_string(config->time_metric);
      this->buffer_manager->log_metadata_event(
          "time_metric", time_metric_value.c_str(), CUSTOM_METADATA,
          TraceEventType::TRACE_TYPE_DFTRACER, this->process_id, tid);
      dftracer::DraftScope draft(include_metadata);
      dftracer::EventDraft* meta = draft.get();
      if (meta != nullptr) {
        char cwd[PATH_MAX];
        if (dftracer::POSIXBypass::get_instance().getcwd(cwd, sizeof(cwd)) !=
            NULL) {
          auto cwd_hash = hash_and_store(cwd, METADATA_NAME_FILE_HASH);
          meta->set("cwd", cwd_hash);
        }
        cmd_hash = hash_and_store(cmd.data(), METADATA_NAME_STRING_HASH);
        exec_hash = hash_and_store(exec_name.data(), METADATA_NAME_STRING_HASH);
#ifdef DFTRACER_GIT_VERSION
        meta->set("version", DFTRACER_GIT_VERSION);
#else
#ifdef DFTRACER_VERSION
        meta->set("version", DFTRACER_VERSION);
#endif
#endif
        meta->set("exec_hash", exec_hash);
        meta->set("cmd_hash", cmd_hash);
        time_t ltime;       /* calendar time */
        ltime = time(NULL); /* get current cal time */
        char timestamp[1024];
        auto size = dftracer_logging_real_sprintf()(timestamp, "%s",
                                                    asctime(localtime(&ltime)));
        timestamp[size - 1] = '\0';
        meta->set("date", std::string(timestamp));
        meta->set("ppid", getppid());
        this->buffer_manager->set_app_name(exec_name.c_str());
      }
      this->enter_event();
      this->log("start", "dftracer", TraceEventType::TRACE_TYPE_DFTRACER,
                this->get_time(), 0, meta);
      this->exit_event();
      if (enable_core_affinity) {
        auto cores = core_affinity();
        auto cores_size = cores.size();
        if (cores_size > 0) {
          std::stringstream all_stream;
          all_stream << "[";
          for (long unsigned int i = 0; i < cores_size; ++i) {
            all_stream << cores[i];
            if (i < cores_size - 1) all_stream << ",";
          }
          all_stream << "]";
          ThreadID tid = 0;
          if (dftracer_tid) {
            tid = df_gettid() + this->process_id;
          }
          this->buffer_manager->log_metadata_event(
              "core_affinity", all_stream.str().c_str(), METADATA_NAME_PROCESS,
              TraceEventType::TRACE_TYPE_DFTRACER, this->process_id, tid,
              false);
        }
      }
    }
    this->is_init = true;
    DFTRACER_LOG_INFO("Writing trace to %s", log_file.c_str());
  }

  inline int enter_event() {
    // @Note @ray:
    // intentionally a no-op: kept for API compatibility;
    // level/stack tracking previously handled here has been removed.
    return 0;
  }

  inline void exit_event() {
    // @Note @ray:
    // intentionally a no-op: kept for API compatibility;
    // level/stack tracking previously handled here has been removed.
  }

  /** A new event's id, unique in the process; also counts the event. */
  inline int increment_index() {
    struct Slot {
      const DFTLogger* owner = nullptr;
      uint64_t epoch = 0;
      int next = 0;
      int end = 0;
      ThreadEvents* events = nullptr;
    };
    thread_local Slot slot;
    const uint64_t epoch = events_epoch_.load(std::memory_order_relaxed);
    if (__builtin_expect(slot.owner != this || slot.epoch != epoch, 0)) {
      auto events = std::make_unique<ThreadEvents>(epoch);
      ThreadEvents* raw = events.get();
      {
        std::lock_guard<std::mutex> lock(events_mtx_);
        thread_events_.push_back(std::move(events));
      }
      slot = Slot{this, epoch, 0, 0, raw};
    }
    if (slot.next == slot.end) {
      slot.next = index.fetch_add(kIdBlock, std::memory_order_relaxed) + 1;
      slot.end = slot.next + kIdBlock;
    }
    // Only this thread writes its count.
    slot.events->count.store(
        slot.events->count.load(std::memory_order_relaxed) + 1,
        std::memory_order_relaxed);
    return slot.next++;
  }

  inline HashType has_hash(std::string_view key) {
    std::shared_lock<std::shared_mutex> lock(map_mtx);
    auto iter = computed_hash.find(key);
    if (iter != computed_hash.end()) return iter->second.hash;
    return NO_HASH_DEFAULT;
  }

  inline void insert_hash(std::string_view key, HashType hash,
                          bool file = false) {
    std::unique_lock<std::shared_mutex> lock(map_mtx);
    auto iter = computed_hash.find(key);
    if (iter != computed_hash.end()) {
      iter->second = HashEntry{hash, file};
      return;
    }
    char* owned = static_cast<char*>(malloc(key.size() + 1));
    if (owned == nullptr) return;
    std::memcpy(owned, key.data(), key.size());
    owned[key.size()] = '\0';
    computed_hash.emplace(std::string_view(owned, key.size()),
                          HashEntry{hash, file});
  }

  // MPI_File_open: `fh` now refers to the file whose path hashed to `fhash`.
  template <typename FileHandle>
  inline void mpi_file_opened(FileHandle fh, HashType fhash) {
    if (fhash == NO_HASH_DEFAULT) return;
    std::unique_lock<std::shared_mutex> lock(mpi_file_mtx);
    mpi_file_hash.insert_or_assign(mpi_file_key(fh), fhash);
  }
  // The fhash of the file `fh` was opened on, or NO_HASH_DEFAULT if it was
  // not opened through a traced MPI_File_open.
  template <typename FileHandle>
  inline HashType mpi_file_fhash(FileHandle fh) {
    std::shared_lock<std::shared_mutex> lock(mpi_file_mtx);
    auto iter = mpi_file_hash.find(mpi_file_key(fh));
    return iter != mpi_file_hash.end() ? iter->second : NO_HASH_DEFAULT;
  }
  // MPI_File_close: the handle no longer refers to the file (MPI may reuse it).
  template <typename FileHandle>
  inline void mpi_file_closed(FileHandle fh) {
    std::unique_lock<std::shared_mutex> lock(mpi_file_mtx);
    mpi_file_hash.erase(mpi_file_key(fh));
  }

  inline TimeResolution get_time() {
    DFTRACER_LOG_DEBUG("DFTLogger.get_time");
    const TimeResolution us = dftracer::TscClock::now_us();
    if (__builtin_expect(ns_per_unit_ == 1000ULL, 1)) return us;
    if (ns_per_unit_ == 1) return us * 1000ULL;
    return us / (ns_per_unit_ / 1000ULL);
  }

  // Records this process's MPI rank once MPI is up. The MPI module asks MPI
  // for it (dftracer_module_mpi_rank in core/brahma/mpi_module.cpp); without
  // the module there is no rank to record.
  inline void handle_mpi(ThreadID tid) {
    if (!mpi_event && dftracer_module_mpi_rank != nullptr &&
        !dftracer_mpi_fork_guard().load()) {
      int rank = 0;
      if (dftracer_module_mpi_rank(&rank) == 1) {
        if (this->buffer_manager != nullptr) {
          this->buffer_manager->set_rank(rank);
        }
        this->buffer_manager->log_metadata_event(
            "rank", std::to_string(rank).c_str(), METADATA_NAME_PROCESS,
            TraceEventType::TRACE_TYPE_DFTRACER, this->process_id, tid);
        char process_name[1024];
        auto size =
            dftracer_logging_real_sprintf()(process_name, "Rank %d", rank);
        process_name[size] = '\0';
        this->buffer_manager->log_metadata_event(
            process_name, METADATA_NAME_PROCESS_NAME,
            METADATA_NAME_PROCESS_NAME, TraceEventType::TRACE_TYPE_DFTRACER,
            this->process_id, tid);
        mpi_event = true;
      }
    }
  }

  // An event shorter than DFTRACER_MIN_DURATION_NS is dropped before it takes
  // an index. dftracer's own events always stay, and the rank is still noted.
  inline bool too_short(TraceEventType type, TimeResolution duration) {
    if (min_duration_ns_ == 0 || type == TraceEventType::TRACE_TYPE_DFTRACER ||
        static_cast<uint64_t>(duration) * ns_per_unit_ >= min_duration_ns_)
      return false;
    handle_mpi(dftracer_tid ? df_gettid() : 0);
    return true;
  }

  template <class Log>
  inline void emit(ThreadID tid, TraceEventType type, Log log_event) {
    const int index = this->increment_index();
    handle_mpi(tid);
    // finalize() resets buffer_manager, and a hook can still fire after it,
    // such as a GOTCHA hook in Py_FinalizeEx.
    if (this->buffer_manager == nullptr) return;
    log_event(index);
    note_logged(type);
  }

  // An interceptor's event, with the args it encoded (null without
  // DFTRACER_INC_METADATA).
  inline void log(std::string_view event_name, std::string_view category,
                  TraceEventType type, TimeResolution start_time,
                  TimeResolution duration, const dftracer::EventDraft* args) {
    dftracer::TracerScope scope;
    if (too_short(type, duration)) return;
    log_draft(dftracer_tid ? df_gettid() : 0, event_name, category, type,
              start_time, duration, args);
  }

  // log() of a site interceptor (DFT_SITE_END) with the `info->count`
  // entries at `args`. The buffer takes them as they are once the site's
  // shape is in its block; only otherwise is an EventDraft built.
  inline void log_site(const dftracer::SiteInfo& info,
                       TimeResolution start_time, TimeResolution duration,
                       const dftracer::EventDraft::Entry* args) {
    if (too_short(info.type, duration)) return;
    if (__builtin_expect(site_sink_ != nullptr, 1)) {
      log_site_service(info, start_time, duration, args);
      return;
    }
    const ThreadID tid = dftracer_tid ? df_gettid() : 0;
    emit(tid, info.type, [&](int index) {
      const dftracer::SiteEvent e{
          &info, index, start_time, duration, args, this->process_id, tid};
      if (include_metadata && this->buffer_manager->log_site_event(e)) return;
      log_site_draft(e);
    });
  }

  // log_site() in pc mode with metadata: emit() and the service manager's
  // log_site_event() in one frame. In service_buffer_manager.cpp, where that
  // inlines.
  __attribute__((noinline)) void log_site_service(
      const dftracer::SiteInfo& info, TimeResolution start_time,
      TimeResolution duration, const dftracer::EventDraft::Entry* args);

  __attribute__((noinline)) void log_site_draft(const dftracer::SiteEvent& e) {
    const dftracer::SiteInfo& info = *e.info;
    if (!include_metadata) {
      this->buffer_manager->log_data_event(
          e.index, info.event_name, info.category, info.type, e.start_time,
          e.duration, nullptr, e.process_id, e.tid);
      return;
    }
    dftracer::EventDraft draft;
    draft.set_site(info.site);
    for (uint16_t i = 0; i < info.count; ++i) draft.append(e.args[i]);
    this->buffer_manager->log_data_event(
        e.index, info.event_name, info.category, info.type, e.start_time,
        e.duration, &draft, e.process_id, e.tid);
  }

  // An event that ran on `thread`, such as a GPU API call reported later from
  // a profiler thread. Without thread ids, `thread` goes in the args.
  inline void log_on(ThreadID thread, std::string_view event_name,
                     std::string_view category, TraceEventType type,
                     TimeResolution start_time, TimeResolution duration,
                     dftracer::EventDraft* args) {
    dftracer::TracerScope scope;
    if (too_short(type, duration)) return;
    if (!dftracer_tid && args != nullptr) args->set("tid", thread);
    log_draft(dftracer_tid ? thread : 0, event_name, category, type, start_time,
              duration, args);
  }

  inline void log_draft(ThreadID tid, std::string_view event_name,
                        std::string_view category, TraceEventType type,
                        TimeResolution start_time, TimeResolution duration,
                        const dftracer::EventDraft* args) {
    emit(tid, type, [&](int index) {
      this->buffer_manager->log_data_event(index, event_name, category, type,
                                           start_time, duration, args,
                                           this->process_id, tid);
    });
  }

  // Read before writing: a store, even of the same value, takes the cache
  // line from every other logging thread.
  inline void note_logged(TraceEventType type) {
    if (!has_entry.load(std::memory_order_relaxed))
      has_entry.store(true, std::memory_order_relaxed);
    // Other projects' layers (DFTRACER_TRACE_TYPE_EXTERNAL up) are not
    // tracked here.
    if (type != TraceEventType::TRACE_TYPE_DFTRACER &&
        type < TraceEventType::TRACE_TYPE_MAX) {
      const uint32_t bit = 1u << static_cast<uint32_t>(type);
      if ((used_layers_.load(std::memory_order_relaxed) & bit) == 0)
        used_layers_.fetch_or(bit, std::memory_order_relaxed);
    }
  }

  inline void log_metadata(ConstEventNameType key, ConstEventNameType value,
                           TraceEventType type) {
    dftracer::TracerScope scope;
    DFTRACER_LOG_DEBUG("DFTLogger.log_metadata");
    ThreadID tid = 0;
    if (dftracer_tid) {
      tid = df_gettid();
    }
    handle_mpi(tid);
    this->buffer_manager->log_metadata_event(key, value, CUSTOM_METADATA, type,
                                             this->process_id, tid);
  }

  inline HashType hash_and_store(char* filename, ConstEventNameType name) {
    return hash_and_store(static_cast<const char*>(filename), name);
  }

  inline HashType hash_and_store_str(char file[PATH_MAX],
                                     ConstEventNameType name) {
    HashType hash = has_hash(file);
    if (hash == NO_HASH_DEFAULT) {
      hash = get_hash(file);
      insert_hash(file, hash, strcmp(name, METADATA_NAME_FILE_HASH) == 0);
      ThreadID tid = 0;
      if (dftracer_tid) {
        tid = df_gettid();
      }
      this->buffer_manager->log_metadata_event(
          file, hash, name, TraceEventType::TRACE_TYPE_DFTRACER,
          this->process_id, tid, true);
    }
    return hash;
  }

  // A metadata record whose args are the given fields, written from them
  // straight into the trace (or the ring). Takes ownership of fields.
  inline void log_record(ConstEventNameType record, dftracer::Metadata* fields,
                         TraceEventType type) {
    dftracer::TracerScope scope;
    ThreadID tid = 0;
    if (dftracer_tid) tid = df_gettid();
    if (this->buffer_manager != nullptr)
      this->buffer_manager->log_record(record, fields, type, this->process_id,
                                       tid);
    delete fields;
  }

  // The common case, a string hashed already, is one lookup on the caller's
  // string: no copy, no allocation. Only a new one is copied, bounded to
  // PATH_MAX as it is keyed.
  inline HashType hash_and_store(const char* filename,
                                 ConstEventNameType name) {
    if (filename == NULL) return NO_HASH_DEFAULT;
    const size_t len = strnlen(filename, PATH_MAX);
    if (len < PATH_MAX) {
      HashType hash = has_hash(std::string_view(filename, len));
      if (hash != NO_HASH_DEFAULT) return hash;
    }
    char file[PATH_MAX];
    const size_t n = len < PATH_MAX ? len : PATH_MAX - 1;
    std::memcpy(file, filename, n);
    file[n] = '\0';
    return hash_and_store_str(file, name);
  }

  // Folds the effective ConfigurationManager settings, which instrumentation
  // layers actually produced events this run, and any app-supplied metadata
  // into a single "end" event, so a trace is self-describing without
  // cross-referencing how it was launched and without paying for one
  // metadata event per setting.
  inline void add_end_event_metadata(dftracer::Metadata* meta) {
    config->populate_metadata(meta, runtime_bind_, runtime_log_file_);

    // Instrumentation layers that produced at least one event this run (see
    // used_layers_ in log()), e.g. {"MPI":1,"PYTHON":1}.
    uint32_t used = used_layers_.load(std::memory_order_relaxed);
    std::ostringstream used_json;
    used_json << "{";
    bool first = true;
    for (uint32_t t = 0;
         t < static_cast<uint32_t>(TraceEventType::TRACE_TYPE_MAX); t++) {
      if (used & (1u << t)) {
        if (!first) used_json << ",";
        used_json << "\"" << to_string(static_cast<TraceEventType>(t))
                  << "\":1";
        first = false;
      }
    }
    // Named sub-layers/integrations reported via mark_used(), e.g. a Python
    // integration distinguishing itself within TRACE_TYPE_PYTHON.
    {
      std::shared_lock<std::shared_mutex> lock(used_extra_mtx_);
      for (const auto& name : used_extra_) {
        if (!first) used_json << ",";
        used_json << dftracer::line_format::quoted(name) << ":1";
        first = false;
      }
    }
    used_json << "}";
    meta->insert_or_assign("used", dftracer::RawJson(used_json.str()));

    // App-supplied metadata, set via the public set_app_metadata* API from
    // any of the C/C++/Python bindings.
    std::ostringstream app_json;
    app_json << "{";
    {
      std::lock_guard<std::mutex> lock(app_metadata_mtx_);
      bool first_app = true;
      for (auto& entry : app_metadata_) {
        if (!first_app) app_json << ",";
        app_json << dftracer::line_format::quoted(entry.first.view()) << ":";
        const dftracer::MetadataValue& value = std::get<1>(entry.second);
        if (const auto* i = std::get_if<int64_t>(&value)) {
          app_json << *i;
        } else if (const auto* s = std::get_if<std::string>(&value)) {
          app_json << dftracer::line_format::quoted(*s);
        }
        first_app = false;
      }
    }
    app_json << "}";
    meta->insert_or_assign("app", dftracer::RawJson(app_json.str()));
  }

  inline void flush() {
    if (this->buffer_manager != nullptr) this->buffer_manager->flush();
  }

  // The "FS" records (utils/fs_info.h): for every traced file one at file
  // level, plus one per directory and per mount it is in, the first time
  // either is seen, so a fact is stored once at the level where it changes:
  //   {"level":"mount","mhash":..,"fs":"lustre","bsize":..,"capacity":{..},
  //    "mount":{"type":..,"source":..,"options":..},"lustre":{"fsname":..}}
  //   {"level":"dir","dhash":..,"mhash":..,"lustre":{"mdt":0,..}}
  //   {"level":"file","fhash":..,"dhash":..,"size":..,"blksize":..,
  //    "lustre":{"stripe_count":4,..,"osts":[3,9,12,40]}}
  // Done at finalize from the hash table, once the run has created and laid
  // out its files, and after the interceptors are unbound, so the lookups are
  // not traced. A file already removed by then has no record.
  inline void emit_fs_records() {
    std::vector<std::pair<std::string, std::string>> files;
    {
      std::shared_lock<std::shared_mutex> lock(map_mtx);
      for (const auto& entry : computed_hash)
        if (entry.second.file && entry.second.hash != NO_HASH_DEFAULT)
          files.emplace_back(std::string(entry.first), entry.second.hash);
    }
    if (files.empty()) return;
    dftracer::FsRecordWriter writer(
        [this](const std::string& path) {
          // Directories and mount points get FH records like files, so
          // dhash/mhash resolve to paths the same way fhash does.
          HashType h = hash_and_store(path.c_str(), METADATA_NAME_FILE_HASH);
          return h == NO_HASH_DEFAULT ? std::string() : std::string(h);
        },
        [this](dftracer::Metadata* fields) {
          log_record(METADATA_NAME_FILE_SYSTEM, fields,
                     TraceEventType::TRACE_TYPE_DFTRACER);
        });
    size_t found = 0;
    for (const auto& file : files)
      if (writer.add(file.first, file.second)) ++found;
    DFTRACER_LOG_INFO("Wrote %zu FS records for %zu of %zu traced files",
                      writer.records(), found, files.size());
  }

  inline void finalize() {
    DFTRACER_LOG_DEBUG("DFTLogger.finalize");
    if (this->buffer_manager != nullptr) {
      if (config->fs_metadata) emit_fs_records();
      dftracer::Metadata meta;
      meta.insert_or_assign("num_events", num_events());
      add_end_event_metadata(&meta);
      dftracer::DraftScope draft(true);
      draft.get()->set_all(meta);
      int current_index = this->increment_index();
      auto tid = df_gettid();
      this->buffer_manager->log_data_event(
          current_index, "end", "dftracer", TraceEventType::TRACE_TYPE_DFTRACER,
          this->get_time(), 0, draft.get(), this->process_id, tid);
      this->exit_event();
      this->buffer_manager->finalize(static_cast<int>(num_events()),
                                     this->process_id);
      // After the last compress and write, so they are in the totals.
      DFTRACER_PROFILE_REPORT(config->pc_mode ? "pc" : "local");
      DFTRACER_LOG_INFO("Released Logger");
      site_sink_ = nullptr;
      this->buffer_manager.reset();
      this->is_init = false;
      clear_hash_cache();
    } else {
      DFTRACER_LOG_WARN("DFTLogger.finalize buffer manager not initialized",
                        "");
      clear_hash_cache();
    }
  }
};

#define DFT_LOGGER_INIT() dftracer::Singleton<DFTLogger>::get_instance()
#define DFT_LOGGER_FINI() \
  dftracer::Singleton<DFTLogger>::get_instance()->finalize()
#define DFT_LOGGER_UPDATE(value) \
  if (metadata != nullptr) metadata->set(dftracer::StaticKey{#value}, value);

#define DFT_LOGGER_UPDATE_TYPE(value, type) \
  if (metadata != nullptr)                  \
    metadata->set(dftracer::StaticKey{#value}, value, type);

#define DFT_LOGGER_UPDATE_HASH(value)                                 \
  if (trace && this->logger->include_metadata) {                      \
    dftracer::TracerScope dft_hash_scope;                             \
    HashType value##_hash =                                           \
        this->logger->hash_and_store(value, METADATA_NAME_FILE_HASH); \
    DFT_LOGGER_UPDATE(value##_hash);                                  \
  }

// MPI-IO calls on an open file record the fhash of the path it was opened
// with, as POSIX calls do, instead of the handle (see mpi_file_fhash).
#define DFT_LOGGER_UPDATE_MPI_FILE(fh)                      \
  if (trace && this->logger->include_metadata) {            \
    dftracer::TracerScope dft_hash_scope;                   \
    HashType fhash = this->logger->mpi_file_fhash(fh);      \
    if (fhash != NO_HASH_DEFAULT) DFT_LOGGER_UPDATE(fhash); \
  }
// MPI_File_open and MPI_File_delete: hash the path (writing its FH record
// the first time) into `fhash` and record it.
#define DFT_LOGGER_UPDATE_MPI_PATH(path)                                   \
  HashType fhash = NO_HASH_DEFAULT;                                        \
  if (trace && (path) != nullptr) {                                        \
    dftracer::TracerScope dft_hash_scope;                                  \
    fhash = this->logger->hash_and_store((path), METADATA_NAME_FILE_HASH); \
    DFT_LOGGER_UPDATE(fhash);                                              \
  }

// The interceptor's own static object, so the encoder can send its events
// by their shape (EventDraft::shaped()). Its END names the event
// __FUNCTION__, so the site also fixes the name.
#define DFT_LOGGER_SITE() \
  static char dft_site;   \
  if (metadata != nullptr) metadata->set_site(&dft_site)

// An is_traced() hash, from DFTLogger::get_hash(): always HASH_OUTPUT hex
// digits, so its length needs no strlen.
#define DFT_LOGGER_UPDATE_FHASH(value)         \
  if (metadata != nullptr)                     \
    metadata->set(dftracer::StaticKey{#value}, \
                  std::string_view((value), HASH_OUTPUT));

// Each half of an interceptor runs with the thread's tracer depth raised, so
// what dftracer calls while it records the event is not traced. The real call
// between them is outside it, so the calls it makes are traced as before.
// dft_depth is the thread's tl_tracer_depth, looked up once per interceptor.
#define DFT_TRACER_ENTER(entity)                \
  int& dft_depth = dftracer::tl_tracer_depth;   \
  HashType fhash = NO_HASH_DEFAULT;             \
  if (dft_depth == 0) {                         \
    dftracer::TracerScope dft_scope(dft_depth); \
    fhash = is_traced(entity, __FUNCTION__);    \
  }                                             \
  bool trace = fhash != NO_HASH_DEFAULT;

// With DFTRACER_ENABLE_PROFILING, the two halves of an interceptor are timed
// as its layer's capture phase (the real call between them is not), and the
// end half counts the event; an untraced call is timed as "filtered".
#define DFT_LOGGER_START(entity)                                          \
  DFTRACER_PROFILE_BEGIN(dft_profile_start);                              \
  DFT_TRACER_ENTER(entity);                                               \
  TimeResolution start_time = 0;                                          \
  if (trace) ++dft_depth;                                                 \
  dftracer::DraftScope dft_draft(trace&& this->logger->include_metadata); \
  dftracer::EventDraft* metadata = dft_draft.get();                       \
  DFT_LOGGER_SITE();                                                      \
  if (trace) {                                                            \
    DFTRACER_LOG_DEBUG("Calling function %s", __FUNCTION__);              \
    DFT_LOGGER_UPDATE_FHASH(fhash);                                       \
    this->logger->enter_event();                                          \
    start_time = this->logger->get_time();                                \
    --dft_depth;                                                          \
  }                                                                       \
  DFTRACER_PROFILE_END_TRACED(dft_profile_start, trace,                   \
                              DFTRACER_PROFILE_CAPTURE(TRACE_TYPE))
#define DFT_LOGGER_START_ALWAYS()                                         \
  DFTRACER_PROFILE_BEGIN(dft_profile_start);                              \
  int& dft_depth = dftracer::tl_tracer_depth;                             \
  /* skip if finalized */                                                 \
  bool trace = dft_depth == 0 && this->logger->is_active();               \
  TimeResolution start_time = 0;                                          \
  if (trace) ++dft_depth;                                                 \
  dftracer::DraftScope dft_draft(trace&& this->logger->include_metadata); \
  dftracer::EventDraft* metadata = dft_draft.get();                       \
  DFT_LOGGER_SITE();                                                      \
  if (trace) {                                                            \
    DFTRACER_LOG_DEBUG("Calling function %s", __FUNCTION__);              \
    this->logger->enter_event();                                          \
    start_time = this->logger->get_time();                                \
    --dft_depth;                                                          \
  }                                                                       \
  DFTRACER_PROFILE_END(dft_profile_start,                                 \
                       DFTRACER_PROFILE_CAPTURE(TRACE_TYPE), false);
#define DFT_LOGGER_LOG_END(name)                                          \
  if (trace) {                                                            \
    DFTRACER_PROFILE_BEGIN(dft_profile_end);                              \
    TimeResolution end_time = this->logger->get_time();                   \
    this->logger->log(std::string_view(name), std::string_view(CATEGORY), \
                      TRACE_TYPE, start_time, end_time - start_time,      \
                      metadata);                                          \
    this->logger->exit_event();                                           \
    DFTRACER_PROFILE_END(dft_profile_end,                                 \
                         DFTRACER_PROFILE_CAPTURE(TRACE_TYPE), true);     \
  }
// A site interceptor (DFT_SITE_*): no EventDraft while the real call runs. Its
// args, each DFT_ARG(name, MetadataType), go to DFT_SITE_END after the call, in
// the order DFT_LOGGER_UPDATE_TYPE would set them, after the fhash. The args
// are numbers and the fhash has a fixed length, so the site's SiteInfo is built
// once.
#define DFT_SITE_START(entity)                               \
  DFTRACER_PROFILE_BEGIN(dft_profile_start);                 \
  DFT_TRACER_ENTER(entity);                                  \
  if (trace) {                                               \
    DFTRACER_LOG_DEBUG("Calling function %s", __FUNCTION__); \
  }                                                          \
  TimeResolution start_time = 0;                             \
  static char dft_site;                                      \
  if (trace) start_time = this->logger->get_time();          \
  DFTRACER_PROFILE_END_TRACED(dft_profile_start, trace,      \
                              DFTRACER_PROFILE_CAPTURE(TRACE_TYPE))
#define DFT_ARG(value, type) \
  dftracer::EventDraft::number(dftracer::StaticKey{#value}, value, type)
#define DFT_SITE_END(...)                                                 \
  if (trace) {                                                            \
    DFTRACER_PROFILE_BEGIN(dft_profile_end);                              \
    TimeResolution end_time = this->logger->get_time();                   \
    const dftracer::EventDraft::Entry dft_args[] = {                      \
        dftracer::EventDraft::entry(dftracer::StaticKey{"fhash"},         \
                                    std::string_view(fhash, HASH_OUTPUT), \
                                    MetadataType::MT_KEY),                \
        __VA_ARGS__};                                                     \
    static const dftracer::SiteInfo dft_info = dftracer::SiteInfo::of(    \
        &dft_site, __FUNCTION__, CATEGORY, TRACE_TYPE, dft_args);         \
    {                                                                     \
      dftracer::TracerScope dft_end_scope(dft_depth);                     \
      this->logger->log_site(dft_info, start_time, end_time - start_time, \
                             dft_args);                                   \
    }                                                                     \
    DFTRACER_PROFILE_END(dft_profile_end,                                 \
                         DFTRACER_PROFILE_CAPTURE(TRACE_TYPE), true);     \
  }

#define DFT_LOGGER_END() DFT_LOGGER_LOG_END(__FUNCTION__)

#endif  // DFTRACER_GENERIC_LOGGER_H
