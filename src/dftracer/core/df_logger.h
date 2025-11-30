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
#include <dftracer/core/utils/posix_internal.h>
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

class DFTLogger {
 private:
  std::shared_ptr<dftracer::ConfigurationManager> config;
  std::shared_mutex level_mtx;
  std::shared_mutex map_mtx;
  bool throw_error;
  bool is_init, dftracer_tid;
  ProcessID process_id;
  uint32_t level;
  std::vector<int> index_stack;
  std::unordered_map<std::string, HashType> computed_hash;
  std::atomic_int index;
  bool is_aggregated;
  bool has_entry;
#ifdef DFTRACER_MPI_ENABLE
  bool mpi_event;
#endif
#ifdef DFTRACER_HWLOC_ENABLE
  hwloc_topology_t topology;
#endif
  bool enable_core_affinity;
  std::shared_ptr<dftracer::BufferManager> buffer_manager;
  std::vector<unsigned> core_affinity() {
    DFTRACER_LOG_DEBUG("DFTLogger.core_affinity", "");
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
        level(0),
        index_stack(),
        computed_hash(),
        index(0),
        is_aggregated(false),
        has_entry(false),
#ifdef DFTRACER_MPI_ENABLE
        mpi_event(false),
#endif
        enable_core_affinity(false),
        include_metadata(false) {
    DFTRACER_LOG_DEBUG("DFTLogger.DFTLogger", "");
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
        dftracer::Singleton<dftracer::BufferManager>::get_new_instance();
    this->is_init = true;
  }
  ~DFTLogger() {
    for (auto &hash : computed_hash) {
      if (hash.second) free(hash.second);
    }
  }

  inline HashType get_hash(char *name) {
    uint8_t result[HASH_OUTPUT];
    md5String(name, result);
    char *hash_str = (char *)malloc(HASH_OUTPUT * 2 + 1);
    for (int i = 0; i < HASH_OUTPUT; i += 2) {
      sprintf(hash_str + i, "%02x", result[i]);
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
    if (this->buffer_manager != nullptr) {
      this->buffer_manager->initialize(log_file.c_str(), hostname_hash);
      hostname_hash = hash_and_store(hostname, METADATA_NAME_HOSTNAME_HASH);
      char thread_name[128];
      auto size = sprintf(thread_name, "%d", this->process_id);
      thread_name[size] = '\0';
      this->buffer_manager->log_metadata_event(
          thread_name, METADATA_NAME_THREAD_NAME, METADATA_NAME_THREAD_NAME,
          this->process_id, tid);
      dftracer::Metadata *meta = nullptr;
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
        auto size = sprintf(timestamp, "%s", asctime(localtime(&ltime)));
        timestamp[size - 1] = '\0';
        meta->insert_or_assign("date", std::string(timestamp));
        meta->insert_or_assign("ppid", getppid());
        this->buffer_manager->set_app_name(exec_name.c_str());
      }
      this->enter_event();
      this->log("start", "dftracer", this->get_time(), 0, meta);
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
              this->process_id, tid, false);
        }
#endif
      }
    }
    this->is_init = true;
    DFTRACER_LOG_INFO("Writing trace to %s", log_file.c_str());
  }

  inline void clean_stack() {
    std::unique_lock<std::shared_mutex> lock(level_mtx);
    index_stack.clear();
  }
  inline int enter_event() {
    std::unique_lock<std::shared_mutex> lock(level_mtx);
    index++;
    level++;
    int current_index = index.load();
    index_stack.push_back(current_index);
    return current_index;
  }

  inline void exit_event() {
    std::unique_lock<std::shared_mutex> lock(level_mtx);
    level--;
    index_stack.pop_back();
  }

  inline int get_parent() {
    std::shared_lock<std::shared_mutex> lock(level_mtx);
    size_t stack_size = index_stack.size();
    if (level > 1 && stack_size > 1 && level <= stack_size) {
      return index_stack[level - 2];
    }
    return -1;
  }

  inline int get_current() {
    std::shared_lock<std::shared_mutex> lock(level_mtx);
    size_t stack_size = index_stack.size();
    if (level > 0 && stack_size > 0 && level <= stack_size) {
      return index_stack[level - 1];
    }
    return -1;
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
    DFTRACER_LOG_DEBUG("DFTLogger.get_time", "");
    struct timeval tv{};
    gettimeofday(&tv, NULL);
    TimeResolution t = 1000000 * tv.tv_sec + tv.tv_usec;
    return t;
  }

  inline void handle_mpi(ThreadID tid) {
#ifdef DFTRACER_MPI_ENABLE
    if (!mpi_event) {
      int initialized;
      int status = MPI_Initialized(&initialized);
      if (status == MPI_SUCCESS && initialized == true) {
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (this->buffer_manager != nullptr) {
          this->buffer_manager->set_rank(rank);
        }
        this->buffer_manager->log_metadata_event(
            "rank", std::to_string(rank).c_str(), METADATA_NAME_PROCESS,
            this->process_id, tid);
        char process_name[1024];
        auto size = sprintf(process_name, "Rank %d", rank);
        process_name[size] = '\0';
        this->buffer_manager->log_metadata_event(
            process_name, METADATA_NAME_PROCESS_NAME,
            METADATA_NAME_PROCESS_NAME, this->process_id, tid);
        mpi_event = true;
      }
    }
#endif
  }

  inline void log(ConstEventNameType event_name, ConstEventNameType category,
                  TimeResolution start_time, TimeResolution duration,
                  dftracer::Metadata *metadata) {
    DFTRACER_LOG_DEBUG("DFTLogger.log", "");

    // Get thread id and process id from metadata if it exists
    ThreadID tid = 0;
    if (dftracer_tid) {
      tid = df_gettid();
#ifndef DFTRACER_MPI_ENABLE
      // WARN: Not tested with MPI enabled
      if (metadata != nullptr) {
        auto iter = metadata->find("tid");
        if (iter != metadata->end()) {
          tid = std::any_cast<ThreadID>(iter->second.second);
          metadata->erase("tid");
        }
      }
#endif
    }
    int local_index = 0;
    if (!include_metadata) {
      local_index = index.load();
    }
    if (metadata != nullptr && !is_aggregated) {
      metadata->insert_or_assign("level", level);
      int parent_index_value = get_parent();
      metadata->insert_or_assign("p_idx", parent_index_value);
    }
    handle_mpi(tid);
    if (include_metadata) {
      int current_index = get_current();
      this->buffer_manager->log_data_event(current_index, event_name, category,
                                           start_time, duration, metadata,
                                           this->process_id, tid);
    } else {
      this->buffer_manager->log_data_event(local_index, event_name, category,
                                           start_time, duration, metadata,
                                           this->process_id, tid);
    }
    has_entry = true;
  }

  inline void log_metadata(ConstEventNameType key, ConstEventNameType value) {
    DFTRACER_LOG_DEBUG("DFTLogger.log_metadata", "");
    ThreadID tid = 0;
    if (dftracer_tid) {
      tid = df_gettid();
    }
    handle_mpi(tid);
    this->buffer_manager->log_metadata_event(key, value, CUSTOM_METADATA,
                                             this->process_id, tid);
  }

  inline HashType hash_and_store(char *filename, ConstEventNameType name) {
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

  void fix_str(char *str, size_t len) {
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
      this->buffer_manager->log_metadata_event(file, hash, name,
                                               this->process_id, tid, true);
    }
    return hash;
  }

  inline HashType hash_and_store(const char *filename,
                                 ConstEventNameType name) {
    if (filename == NULL) return NO_HASH_DEFAULT;
    char file[PATH_MAX];
    strcpy(file, filename);
    file[PATH_MAX - 1] = '\0';
    return hash_and_store_str(file, name);
  }

  inline void finalize() {
    DFTRACER_LOG_DEBUG("DFTLogger.finalize", "");
    if (this->buffer_manager != nullptr) {
      auto meta = new dftracer::Metadata();
      meta->insert_or_assign("num_events", index.load());
      int current_index = this->enter_event();
      auto tid = df_gettid();
      this->buffer_manager->log_data_event(current_index, "end", "dftracer",
                                           this->get_time(), 0, meta,
                                           this->process_id, tid);
      this->exit_event();
      this->buffer_manager->finalize(index.load(), this->process_id, true);
      DFTRACER_LOG_INFO("Released Logger", "");
      this->buffer_manager.reset();
      clean_stack();
    } else {
      DFTRACER_LOG_WARN("DFTLogger.finalize buffer manager not initialized",
                        "");
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

#define DFT_LOGGER_START(entity)                           \
  DFTRACER_LOG_DEBUG("Calling function %s", __FUNCTION__); \
  HashType fhash = is_traced(entity, __FUNCTION__);        \
  bool trace = fhash != NO_HASH_DEFAULT;                   \
  TimeResolution start_time = 0;                           \
  dftracer::Metadata *metadata = nullptr;                  \
  if (trace) {                                             \
    if (this->logger->include_metadata) {                  \
      metadata = new dftracer::Metadata();                 \
      DFT_LOGGER_UPDATE(fhash);                            \
    }                                                      \
    this->logger->enter_event();                           \
    start_time = this->logger->get_time();                 \
  }
#define DFT_LOGGER_START_ALWAYS()                          \
  DFTRACER_LOG_DEBUG("Calling function %s", __FUNCTION__); \
  bool trace = true;                                       \
  TimeResolution start_time = 0;                           \
  dftracer::Metadata *metadata = nullptr;                  \
  if (trace) {                                             \
    if (this->logger->include_metadata) {                  \
      metadata = new dftracer::Metadata();                 \
    }                                                      \
    this->logger->enter_event();                           \
    start_time = this->logger->get_time();                 \
  }
#define DFT_LOGGER_END()                                          \
  if (trace) {                                                    \
    TimeResolution end_time = this->logger->get_time();           \
    this->logger->log((char *)__FUNCTION__, CATEGORY, start_time, \
                      end_time - start_time, metadata);           \
    this->logger->exit_event();                                   \
  }

#endif  // DFTRACER_GENERIC_LOGGER_H
