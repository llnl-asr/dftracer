//
// Created by haridev on 3/28/23.
//

#ifndef DFTRACER_GENERIC_LOGGER_H
#define DFTRACER_GENERIC_LOGGER_H

#include <dftracer/core/buffer/buffer.h>
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/utils/md5.h>
#include <dftracer/core/utils/posix_bypass.h>
#include <dftracer/core/utils/utils.h>
#include <libgen.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <any>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dftracer/core/dftracer_config.hpp>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#ifdef DFTRACER_HWLOC_ENABLE
#include <hwloc.h>
#endif
#ifdef DFTRACER_MPI_ENABLE
#include <mpi.h>
#endif

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

class DFTLogger {
 private:
  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::shared_mutex map_mtx;

  inline void clear_hash_cache() {
    std::unique_lock<std::shared_mutex> lock(map_mtx);
    for (auto& hash : computed_hash) {
      if (hash.second) free(hash.second);
    }
    computed_hash.clear();
  }
  bool throw_error;
  bool is_init, dftracer_tid;
  ProcessID process_id;
  std::unordered_map<std::string, HashType> computed_hash;
  std::atomic_int index;
  [[maybe_unused]] bool is_aggregated;
  bool has_entry;
#ifdef DFTRACER_MPI_ENABLE
  bool mpi_event;
#endif
#ifdef DFTRACER_HWLOC_ENABLE
  hwloc_topology_t topology;
#endif
  bool enable_core_affinity;
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

  std::vector<unsigned> core_affinity() {
    DFTRACER_LOG_DEBUG("DFTLogger.core_affinity");
    auto cores = std::vector<unsigned>();
#ifdef DFTRACER_HWLOC_ENABLE
    if (enable_core_affinity) {
      hwloc_cpuset_t set = hwloc_bitmap_alloc();
      hwloc_get_cpubind(topology, set, HWLOC_CPUBIND_PROCESS);
      for (int id = hwloc_bitmap_first(set); id != -1;
           id = hwloc_bitmap_next(set, id)) {
        cores.push_back(id);
      }
      hwloc_bitmap_free(set);
    }
#endif
    return cores;
  }

 public:
  bool include_metadata;
  DFTLogger(bool init_log = false)
      : is_init(false),
        dftracer_tid(false),
        computed_hash(),
        index(0),
        is_aggregated(false),
        has_entry(false),
#ifdef DFTRACER_MPI_ENABLE
        mpi_event(false),
#endif
        enable_core_affinity(false),
        include_metadata(false) {
    DFTRACER_LOG_DEBUG("DFTLogger.DFTLogger");
    config =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    enable_core_affinity = config->core_affinity;
    include_metadata = config->metadata;
    dftracer_tid = config->tids;
    throw_error = config->throw_error;
    if (enable_core_affinity) {
#ifdef DFTRACER_HWLOC_ENABLE
      hwloc_topology_init(&topology);  // initialization
      hwloc_topology_load(topology);   // actual detection
#endif
    }
    this->buffer_manager =
        dftracer::Singleton<dftracer::BufferManager>::get_instance();
    this->is_init = true;
  }
  ~DFTLogger() { clear_hash_cache(); }

  void reinitialize() {
    DFTRACER_LOG_DEBUG("DFTLogger.reinitialize");
    index.store(0);
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

  // Returns false once finalize() has been called (is_init set to false).
  // Interceptors should treat a false return as a signal to skip tracing
  // and pass through to the real function.
  inline bool is_active() const { return is_init; }

  inline HashType get_hash(char* name) {
    uint8_t result[HASH_OUTPUT];
    md5String(name, result);
    char* hash_str = (char*)malloc(HASH_OUTPUT * 2 + 1);
    for (int i = 0; i < HASH_OUTPUT; i += 2) {
      dftracer_logging_real_sprintf()(hash_str + i, "%02x", result[i]);
    }
    hash_str[HASH_OUTPUT * 2] = '\0';
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
    hostname_hash = get_hash(hostname);
    insert_hash(hostname, hostname_hash);
    if (this->buffer_manager != nullptr) {
      this->buffer_manager->initialize(log_file.c_str(), hostname_hash);
      char hostname_event[256];
      strcpy(hostname_event, hostname);
      hostname_event[255] = '\0';
      fix_str(hostname_event, sizeof(hostname_event));
      this->buffer_manager->log_metadata_event(
          hostname_event, hostname_hash, METADATA_NAME_HOSTNAME_HASH,
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
      dftracer::Metadata* meta = nullptr;
      if (include_metadata) {
        meta = new dftracer::Metadata();
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
          auto cwd_hash = hash_and_store(cwd, METADATA_NAME_FILE_HASH);
          meta->insert_or_assign("cwd", cwd_hash);
        }
        cmd_hash = hash_and_store(cmd.data(), METADATA_NAME_STRING_HASH);
        exec_hash = hash_and_store(exec_name.data(), METADATA_NAME_STRING_HASH);
#ifdef DFTRACER_GIT_VERSION
        meta->insert_or_assign("version", DFTRACER_GIT_VERSION);
#else
#ifdef DFTRACER_VERSION
        meta->insert_or_assign("version", DFTRACER_VERSION);
#endif
#endif
        meta->insert_or_assign("exec_hash", exec_hash);
        meta->insert_or_assign("cmd_hash", cmd_hash);
        time_t ltime;       /* calendar time */
        ltime = time(NULL); /* get current cal time */
        char timestamp[1024];
        auto size = dftracer_logging_real_sprintf()(timestamp, "%s",
                                                    asctime(localtime(&ltime)));
        timestamp[size - 1] = '\0';
        meta->insert_or_assign("date", std::string(timestamp));
        meta->insert_or_assign("ppid", getppid());
        this->buffer_manager->set_app_name(exec_name.c_str());
      }
      this->enter_event();
      this->log("start", "dftracer", TraceEventType::TRACE_TYPE_DFTRACER,
                this->get_time(), 0, meta);
      this->exit_event();
      if (enable_core_affinity) {
#ifdef DFTRACER_HWLOC_ENABLE
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
#endif
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

  inline int increment_index() {
    // @Note @ray:
    // This method is atomic and thread-safe
    // ensuring that each event gets a unique index even in multi-threaded
    // scenarios.
    return ++index;
  }

  inline HashType has_hash(ConstEventNameType key) {
    std::shared_lock<std::shared_mutex> lock(map_mtx);
    auto iter = computed_hash.find(key);
    if (iter != computed_hash.end()) return iter->second;
    return NO_HASH_DEFAULT;
  }

  inline void insert_hash(ConstEventNameType key, HashType hash) {
    std::unique_lock<std::shared_mutex> lock(map_mtx);
    computed_hash.insert_or_assign(key, hash);
  }

  inline TimeResolution get_time() {
    DFTRACER_LOG_DEBUG("DFTLogger.get_time");
    struct timeval tv{};
    gettimeofday(&tv, NULL);
    TimeResolution t;
    switch (config->time_metric) {
      case TimeMetricType::TIME_METRIC_NS:
        t = (TimeResolution)tv.tv_sec * 1000000000ULL +
            (TimeResolution)tv.tv_usec * 1000ULL;
        break;
      case TimeMetricType::TIME_METRIC_MS:
        t = (TimeResolution)tv.tv_sec * 1000ULL +
            (TimeResolution)tv.tv_usec / 1000ULL;
        break;
      case TimeMetricType::TIME_METRIC_SEC:
        t = (TimeResolution)tv.tv_sec;
        break;
      case TimeMetricType::TIME_METRIC_US:
      default:
        t = (TimeResolution)tv.tv_sec * 1000000ULL + (TimeResolution)tv.tv_usec;
        break;
    }
    return t;
  }

  inline void handle_mpi(ThreadID tid) {
#if defined(DFTRACER_MPI_ENABLE) && defined(BRAHMA_ENABLE_MPI)
    if (!mpi_event && !dftracer_mpi_fork_guard().load()) {
      int initialized;
      int finalized;
      int status = MPI_SUCCESS;
      int finalized_status = MPI_SUCCESS;
#if defined(BRAHMA_MPI_IMPL_CRAYMPICH) || defined(BRAHMA_MPI_IMPL_MPICH) || \
    defined(BRAHMA_MPI_IMPL_OPENMPI)
      status = PMPI_Initialized(&initialized);
      finalized_status = PMPI_Finalized(&finalized);
#else
      status = MPI_Initialized(&initialized);
      finalized_status = MPI_Finalized(&finalized);
#endif
      // MPI_Initialized stays true forever after MPI_Init, even once
      // MPI_Finalize has run, so it alone cannot tell us whether it is
      // still safe to call MPI_Comm_rank.
      if (status == MPI_SUCCESS && initialized == true &&
          finalized_status == MPI_SUCCESS && finalized == false) {
        int rank = 0;
#if defined(BRAHMA_MPI_IMPL_CRAYMPICH) || defined(BRAHMA_MPI_IMPL_MPICH) || \
    defined(BRAHMA_MPI_IMPL_OPENMPI)
        PMPI_Comm_rank(MPI_COMM_WORLD, &rank);
#else
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif
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
#endif
  }

  inline void log(ConstEventNameType event_name, ConstEventNameType category,
                  TraceEventType type, TimeResolution start_time,
                  TimeResolution duration, dftracer::Metadata* metadata) {
    DFTRACER_LOG_DEBUG("DFTLogger.log");

    // Get thread id and process id from metadata if it exists
    ThreadID tid = 0;
    if (dftracer_tid) {
      tid = df_gettid();
#ifndef DFTRACER_MPI_ENABLE
      // WARN: Not tested with MPI enabled
      if (metadata != nullptr) {
        auto iter = metadata->find("tid");
        if (iter != metadata->end()) {
          tid = std::any_cast<ThreadID>(std::get<1>(iter->second));
          metadata->erase("tid");
        }
      }
#endif
    }

    int current_index = this->increment_index();
    handle_mpi(tid);
    // Defensive null check: buffer_manager is reset during finalize(); any
    // interceptor still firing after finalization (e.g. via GOTCHA hooks
    // during Py_FinalizeEx) must not dereference a destroyed manager.
    if (this->buffer_manager == nullptr) return;
    this->buffer_manager->log_data_event(current_index, event_name, category,
                                         type, start_time, duration, metadata,
                                         this->process_id, tid);
    has_entry = true;
    if (type != TraceEventType::TRACE_TYPE_DFTRACER) {
      used_layers_.fetch_or(1u << static_cast<uint32_t>(type),
                            std::memory_order_relaxed);
    }
  }

  inline void log_metadata(ConstEventNameType key, ConstEventNameType value,
                           TraceEventType type) {
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
    if (filename == NULL) return NO_HASH_DEFAULT;
    char file[PATH_MAX];
    strcpy(file, filename);
    file[PATH_MAX - 1] = '\0';
    return hash_and_store_str(file, name);
  }

  bool ignore_chars(char c) {
    switch (c) {
      case '(':
      case ')':
      case '\\':
      case '"':
      case '\'':
      case '|':
        return true;
      default:
        return false;
    }
  }

  void fix_str(char* str, size_t len) {
    for (size_t i = 0; i < len && str[i] != '\0'; ++i) {
      if (ignore_chars(str[i])) str[i] = ' ';
    }
  }

  inline HashType hash_and_store_str(char file[PATH_MAX],
                                     ConstEventNameType name) {
    HashType hash = has_hash(file);
    if (hash == NO_HASH_DEFAULT) {
      hash = get_hash(file);
      insert_hash(file, hash);
      ThreadID tid = 0;
      if (dftracer_tid) {
        tid = df_gettid();
      }
      fix_str(file, PATH_MAX);
      this->buffer_manager->log_metadata_event(
          file, hash, name, TraceEventType::TRACE_TYPE_DFTRACER,
          this->process_id, tid, true);
    }
    return hash;
  }

  inline HashType hash_and_store(const char* filename,
                                 ConstEventNameType name) {
    if (filename == NULL) return NO_HASH_DEFAULT;
    char file[PATH_MAX];
    strcpy(file, filename);
    file[PATH_MAX - 1] = '\0';
    return hash_and_store_str(file, name);
  }

  // Folds the effective ConfigurationManager settings, which instrumentation
  // layers actually produced events this run, and any app-supplied metadata
  // into a single "end" event, so a trace is self-describing without
  // cross-referencing how it was launched and without paying for one
  // metadata event per setting.
  inline void add_end_event_metadata(dftracer::Metadata* meta) {
    config->populate_metadata(meta);

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
        app_json << "\"" << entry.first << "\":";
        const std::any& value = std::get<1>(entry.second);
        if (value.type() == typeid(int64_t)) {
          app_json << std::any_cast<int64_t>(value);
        } else if (value.type() == typeid(std::string)) {
          app_json << "\"" << std::any_cast<std::string>(value) << "\"";
        }
        first_app = false;
      }
    }
    app_json << "}";
    meta->insert_or_assign("app", dftracer::RawJson(app_json.str()));
  }

  inline void finalize() {
    DFTRACER_LOG_DEBUG("DFTLogger.finalize");
    if (this->buffer_manager != nullptr) {
      auto meta = new dftracer::Metadata();
      meta->insert_or_assign("num_events", index.load());
      add_end_event_metadata(meta);
      int current_index = this->increment_index();
      auto tid = df_gettid();
      this->buffer_manager->log_data_event(
          current_index, "end", "dftracer", TraceEventType::TRACE_TYPE_DFTRACER,
          this->get_time(), 0, meta, this->process_id, tid);
      this->exit_event();
      this->buffer_manager->finalize(index.load(), this->process_id);
      DFTRACER_LOG_INFO("Released Logger");
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
#define DFT_LOGGER_UPDATE(value)               \
  if (trace && this->logger->include_metadata) \
    metadata->insert_or_assign(#value, value);

#define DFT_LOGGER_UPDATE_TYPE(value, type)    \
  if (trace && this->logger->include_metadata) \
    metadata->insert_or_assign(#value, value, type);

#define DFT_LOGGER_UPDATE_HASH(value)                                 \
  if (trace && this->logger->include_metadata) {                      \
    HashType value##_hash =                                           \
        this->logger->hash_and_store(value, METADATA_NAME_FILE_HASH); \
    DFT_LOGGER_UPDATE(value##_hash);                                  \
  }

#define DFT_LOGGER_START(entity)                             \
  HashType fhash = is_traced(entity, __FUNCTION__);          \
  bool trace = fhash != NO_HASH_DEFAULT;                     \
  if (trace) {                                               \
    DFTRACER_LOG_DEBUG("Calling function %s", __FUNCTION__); \
  }                                                          \
  TimeResolution start_time = 0;                             \
  dftracer::Metadata* metadata = nullptr;                    \
  if (trace) {                                               \
    if (this->logger->include_metadata) {                    \
      metadata = new dftracer::Metadata();                   \
      DFT_LOGGER_UPDATE(fhash);                              \
    }                                                        \
    this->logger->enter_event();                             \
    start_time = this->logger->get_time();                   \
  }
#define DFT_LOGGER_START_ALWAYS()                                 \
  DFTRACER_LOG_DEBUG("Calling function %s", __FUNCTION__);        \
  bool trace = this->logger->is_active(); /* skip if finalized */ \
  TimeResolution start_time = 0;                                  \
  dftracer::Metadata* metadata = nullptr;                         \
  if (trace) {                                                    \
    if (this->logger->include_metadata) {                         \
      metadata = new dftracer::Metadata();                        \
    }                                                             \
    this->logger->enter_event();                                  \
    start_time = this->logger->get_time();                        \
  }
#define DFT_LOGGER_END()                                                     \
  if (trace) {                                                               \
    TimeResolution end_time = this->logger->get_time();                      \
    this->logger->log((char*)__FUNCTION__, CATEGORY, TRACE_TYPE, start_time, \
                      end_time - start_time, metadata);                      \
    this->logger->exit_event();                                              \
  }

#endif  // DFTRACER_GENERIC_LOGGER_H
