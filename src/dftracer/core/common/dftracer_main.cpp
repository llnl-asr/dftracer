//
// Created by haridev on 10/8/23.
//
#include <dftracer/core/common/dftracer_main.h>
#include <dftracer/core/finstrument/functions.h>
#include <dftracer/core/function/cuda/intercept.h>
#include <dftracer/core/function/hip/intercept.h>
#include <dftracer/core/function/papi/counters.h>
#include <dftracer/core/utils/posix_bypass.h>
#include <dftracer/core/utils/stdio_bypass.h>
#include <pthread.h>

template <>
std::shared_ptr<dftracer::DFTracerCore>
    dftracer::Singleton<dftracer::DFTracerCore>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::DFTracerCore>::stop_creating_instances =
    false;
void dft_finalize(bool force) {
  DFTRACER_LOG_DEBUG("DFTracerCore.dft_finalize");
  auto conf =
      dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
  if (force || conf->init_type == ProfileInitType::PROFILER_INIT_FUNCTION) {
    auto dftracer = DFTRACER_MAIN_SINGLETON(ProfilerStage::PROFILER_FINI,
                                            ProfileType::PROFILER_ANY);
    if (dftracer != nullptr) {
      dftracer->finalize();
      dftracer::Singleton<dftracer::DFTracerCore>::finalize();
    }
  }
}

dftracer::DFTracerCore::DFTracerCore(ProfilerStage stage, ProfileType type,
                                     const char* log_file,
                                     const char* data_dirs,
                                     const int* process_id)
    : process_id(-1),
      is_initialized(false),
      bind(false),
      log_file_suffix(),
      include_metadata(false) {
  int requested_process_id = (process_id != nullptr) ? *process_id : -1;
  this->process_id = requested_process_id;
  conf = dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
  DFTRACER_LOG_INFO(
      "Loading DFTracer with ProfilerStage %d ProfileType %d and process "
      "%d",
      stage, type, requested_process_id);
  switch (type) {
    case ProfileType::PROFILER_ANY:
    case ProfileType::PROFILER_PRELOAD: {
      if (stage == ProfilerStage::PROFILER_INIT) {
        log_file_suffix = "preload";
        if (conf->init_type == ProfileInitType::PROFILER_INIT_LD_PRELOAD) {
          initialize(true, log_file, data_dirs, process_id);
        }
        DFTRACER_LOG_INFO("Preloading DFTracer with log_file %s data_dir %s",
                          this->log_file.c_str(), this->data_dirs.c_str());
      }
      break;
    }
    case ProfileType::PROFILER_PY_APP:
    case ProfileType::PROFILER_C_APP:
    case ProfileType::PROFILER_CPP_APP: {
      log_file_suffix = "app";
      bool bind = false;
      if (stage == ProfilerStage::PROFILER_INIT &&
          conf->init_type == ProfileInitType::PROFILER_INIT_FUNCTION) {
        bind = true;
      }
      initialize(bind, log_file, data_dirs, process_id);
      DFTRACER_LOG_INFO(
          "App Initializing DFTracer with log_file %s data_dir %s",
          this->log_file.c_str(), this->data_dirs.c_str());
      break;
    }
    default: {  // GCOVR_EXCL_START
      DFTRACER_LOG_ERROR(DFTRACER_UNKNOWN_PROFILER_TYPE_MSG, type);
      throw std::runtime_error(DFTRACER_UNKNOWN_PROFILER_TYPE_CODE);
    }  // GCOVR_EXCL_STOP
  }
  DFTRACER_LOG_DEBUG("DFTracerCore::DFTracerCore type %d", type);
}

bool dftracer::DFTracerCore::log(ConstEventNameType event_name,
                                 ConstEventNameType category,
                                 TraceEventType type, TimeResolution start_time,
                                 TimeResolution duration,
                                 dftracer::Metadata* metadata) {
  DFTRACER_LOG_DEBUG("DFTracerCore::log");
  if (this->is_initialized && conf->enable) {
    if (logger != nullptr) {
      // PAPI counters are deliberately not sampled here: they are read on the
      // sampler's own libuv timer thread, so this path stays free of PAPI work.
      logger->log(event_name, category, type, start_time, duration, metadata);
      return true;
    } else {
      DFTRACER_LOG_ERROR("DFTracerCore::log logger not initialized");
    }
  }
  return false;
}

void dftracer::DFTracerCore::log_metadata(ConstEventNameType key,
                                          ConstEventNameType value,
                                          TraceEventType type) {
  DFTRACER_LOG_DEBUG("DFTracerCore::log");
  if (this->is_initialized && conf->enable) {
    if (logger != nullptr) {
      logger->log_metadata(key, value, type);
    } else {
      DFTRACER_LOG_ERROR("DFTracerCore::log logger not initialized");
    }
  }
}
bool dftracer::DFTracerCore::finalize() {
  DFTRACER_LOG_DEBUG("DFTracerCore::finalize");
  if (this->is_initialized && conf->enable) {
    DFTRACER_LOG_INFO("Calling finalize on pid %d", this->process_id);
    auto trie = dftracer::Singleton<Trie>::get_instance();
    if (trie != nullptr) {
      DFTRACER_LOG_INFO("Release Prefix Tree");
      trie->finalize();
      dftracer::Singleton<Trie>::finalize();
    }
#ifdef DFTRACER_PAPI_TRACING_ENABLE
    // MUST come before the I/O interception is unbound below.
    //
    // The PAPI sampler runs on its own libuv timer thread and calls PAPI_read()
    // on every tick. PAPI reads its perf_event counters with a plain read(2),
    // which dftracer's OWN brahma/GOTCHA POSIX interception wraps. So while the
    // sampler thread is alive, every sample re-enters this library's read
    // wrapper.
    //
    // Finalising the sampler after `posix_instance->unbind()/finalize()` leaves
    // a window where the sampler is still ticking but GOTCHA's wrappee table
    // has already been freed, and the next sample dies in gotcha_get_wrappee():
    //
    //   #0 gotcha_get_wrappee()
    //   #1 brahma::POSIXDFTracer::read(int, void*, unsigned long)
    //   #2 read_wrapper(int, void*, unsigned long)
    //   #3 _pe_read() / PAPI_read()
    //   #4 dftracer::PAPICounterFunction::emit_sample()
    //   #5 uv_run() / PAPICounterFunction::run_sampler()
    //
    // It is a race, so it looks size-dependent rather than deterministic: short
    // runs usually exit between ticks and survive, long runs almost always land
    // inside PAPI_read and SIGSEGV at exit. Measured on Laghos/MFEM at 4 nodes
    // x 16 ranks: clean at ~335 steps, reliable SIGSEGV past ~600 steps --
    // which also truncated the gzip trace stream on 11 of 16 ranks, so this
    // silently corrupts output rather than merely being an ugly exit.
    //
    // Stopping the sampler first closes the window: no sampler thread means no
    // re-entry into the interception being dismantled.
    {
      auto papi_instance =
          dftracer::Singleton<dftracer::PAPICounterFunction>::get_instance();
      if (papi_instance != nullptr) {
        DFTRACER_LOG_INFO("Stop PAPI sampler before releasing I/O bindings");
        papi_instance->finalize();
      }
    }
#endif
    // GPU profiler teardown must happen BEFORE the logger is destroyed, and
    // must NOT be conditional on `bind`.
    //
    // `bind` gates the brahma/GOTCHA I/O *interception*. The HIP/CUDA backends
    // are not interception -- they are profiler contexts owned by
    // rocprofiler/CUPTI, whose callbacks fire from THEIR OWN threads and keep
    // pointing at our DFTLogger until we stop them. Leaving their teardown
    // inside `if (bind)` means an unbound session (initialize_no_bind, i.e.
    // pure FUNCTION-mode annotation) never stops them at all: rocprofiler then
    // flushes from its atexit tool_fini() into a logger this function has
    // already freed, and the process SIGSEGVs at exit inside
    // BufferManager::log_data_event() -- after the science is done but while
    // the gzip trace stream is still being written, so it silently truncates
    // output rather than merely being an ugly exit.
    //
    // Hoisting it out also puts it in the same position as the PAPI sampler
    // above: stop every asynchronous producer of trace events first, then
    // dismantle what they were writing into.
#ifdef DFTRACER_HIP_TRACING_ENABLE
    {
      auto hip_instance =
          dftracer::Singleton<dftracer::HIPFunction>::get_instance();
      if (hip_instance != nullptr) {
        DFTRACER_LOG_INFO("Stop HIP tracing before releasing the logger");
        hip_instance->finalize();
      }
    }
#endif
#ifdef DFTRACER_CUDA_TRACING_ENABLE
    {
      auto cuda_instance =
          dftracer::Singleton<dftracer::CUDAFunction>::get_instance();
      if (cuda_instance != nullptr) {
        DFTRACER_LOG_INFO("Stop CUDA tracing before releasing the logger");
        cuda_instance->finalize();
      }
    }
#endif
    if (bind) {
#ifdef DFTRACER_FTRACING_ENABLE
      auto function_instance = dftracer::Function::get_instance();
      if (function_instance != nullptr) {
        function_instance->finalize();
      }
#endif
      if (conf->io) {
        DFTRACER_LOG_INFO("Release I/O bindings");
        auto posix_instance = brahma::POSIXDFTracer::get_instance();
        if (posix_instance != nullptr) {
          posix_instance->unbind();
          posix_instance->finalize();
        }
        auto stdio_instance = brahma::STDIODFTracer::get_instance();
        if (stdio_instance != nullptr) {
          stdio_instance->unbind();
          stdio_instance->finalize();
        }
#if defined(DFTRACER_MPI_ENABLE) && defined(BRAHMA_ENABLE_MPI)
        auto mpi_instance = brahma::MPIDFTracer::get_instance();
        if (mpi_instance != nullptr) {
          mpi_instance->unbind();
          mpi_instance->finalize();
        }
        auto mpiio_instance = brahma::MPIIODFTracer::get_instance();
        if (mpiio_instance != nullptr) {
          mpiio_instance->unbind();
          mpiio_instance->finalize();
        }
#endif
#if defined(DFTRACER_HDF5_ENABLE) && defined(BRAHMA_ENABLE_HDF5)
        auto hdf5_instance = brahma::HDF5DFTracer::get_instance();
        if (hdf5_instance != nullptr) {
          hdf5_instance->unbind();
          hdf5_instance->finalize();
        }
#endif
      }
    }
    // NOTE: the PAPI sampler is deliberately finalised EARLIER in this
    // function, before the I/O interception is unbound -- see the long comment
    // there. Do not move it back here: doing so reintroduces the teardown
    // SIGSEGV where the still-running sampler re-enters a half-freed GOTCHA
    // read wrapper.
    if (logger != nullptr) {
      logger->finalize();
      dftracer::Singleton<DFTLogger>::finalize();
    }
    this->is_initialized = false;
    return true;
  } else {
    DFTRACER_LOG_INFO("Already finalized on pid %d", this->process_id);
  }
  return false;
}

void dftracer::DFTracerCore::reinitialize() {
  DFTRACER_LOG_DEBUG("DFTracerCore::reinitialize");
  // Guard against double-reinit: the pthread_atfork child handler and brahma's
  // fork() hook both call reinitialize(). The first call updates process_id to
  // the child's pid; the second call sees it already matches and returns early.
  if (df_getpid() == this->process_id) {
    DFTRACER_LOG_DEBUG("DFTracerCore::reinitialize already done for pid %d",
                       this->process_id);
    return;
  }
  // Both callers (brahma's fork() hook and the pthread_atfork child handler
  // below) fire only in a just-forked child — including children spawned via
  // MPI_Comm_spawn or Python multiprocessing's "fork" start method, since the
  // OS-level fork() they perform under the hood still runs any registered
  // pthread_atfork child handlers. Mark the resulting trace file as such: if
  // this is the very first thing to construct DFTracerCore (the parent never
  // made a traced call before forking), the constructor never set
  // log_file_suffix (it only does so for ProfilerStage::PROFILER_INIT, and
  // this path always runs as PROFILER_OTHER), leaving it empty and producing
  // a file name ending "-<hash>-.pfw.gz". Setting it unconditionally here
  // also means every forked child's trace is labeled "fork" regardless of
  // what the parent's file was named, making forked traces easy to find.
  this->log_file_suffix = "fork";
  is_initialized = false;
  if (this->log_file_prefix.empty()) {
    const char* log_file_env = getenv("DFTRACER_LOG_FILE");
    if (log_file_env != nullptr && log_file_env[0] != '\0') {
      this->log_file_prefix = std::string(log_file_env);
    } else if (!conf->log_file.empty()) {
      this->log_file_prefix = conf->log_file;
    } else {
      DFTRACER_LOG_ERROR(DFTRACER_UNDEFINED_LOG_FILE_MSG);
      throw std::runtime_error(DFTRACER_UNDEFINED_LOG_FILE_CODE);
    }
  }
  conf->log_file = this->log_file_prefix;
  if (!this->log_file_prefix.empty())
    setenv("DFTRACER_LOG_FILE", this->log_file_prefix.c_str(), 1);
  if (this->data_dirs.empty()) {
    const char* data_dirs_env = getenv("DFTRACER_DATA_DIR");
    if (data_dirs_env != nullptr && data_dirs_env[0] != '\0') {
      this->data_dirs = std::string(data_dirs_env);
    } else if (!conf->data_dirs.empty()) {
      this->data_dirs = conf->data_dirs;
    } else {
      DFTRACER_LOG_ERROR(DFTRACER_UNDEFINED_DATA_DIR_MSG);
      throw std::runtime_error(DFTRACER_UNDEFINED_DATA_DIR_CODE);
    }
  }
  conf->data_dirs = this->data_dirs;
  if (!this->data_dirs.empty())
    setenv("DFTRACER_DATA_DIR", this->data_dirs.c_str(), 1);

  this->process_id = df_getpid();
  DFTRACER_LOG_INFO(
      "Reinitializing DFTracer with log_file %s data_dirs %s and process %d",
      this->log_file_prefix.c_str(), this->data_dirs.c_str(), this->process_id);
  logger = dftracer::Singleton<DFTLogger>::get_instance();
  logger->reinitialize();
  initialize(false, this->log_file_prefix.c_str(), this->data_dirs.c_str(),
             nullptr);
}

void dftracer::DFTracerCore::initialize(bool _bind, const char* _log_file,
                                        const char* _data_dirs,
                                        const int* _process_id) {
  DFTRACER_LOG_INFO(
      "DFTracerCore::initialize _bind:%d _log_file:%s _data_dirs:%s "
      "_process_id:%p\n",
      _bind, _log_file, _data_dirs, _process_id);
  // Resolve dftracer's own internal-I/O bypass singletons eagerly, before
  // anything else: dlopen/dlsym (used by STDIOBypass) are not
  // async-signal-safe, and STDIOWriter::write()/finalize() can run from a
  // signal handler (SIGTERM-driven forced finalize). Resolving here,
  // unconditionally and independent of conf->enable/this->bind, guarantees
  // the actual bypass_* calls at signal-handler time are just cached
  // function-pointer invocations. See stdio_bypass.h for why these must
  // never be resolved via GOTCHA/gotcha_get_wrappee or a plain `&function`.
  dftracer::STDIOBypass::get_instance().initialize();
  dftracer::POSIXBypass::get_instance().initialize();
  if (conf->bind_signals) set_signal();
  if (!is_initialized) {
    this->bind = _bind;
    include_metadata = conf->metadata;
    logger = dftracer::Singleton<DFTLogger>::get_instance();
    this->process_id = df_getpid();
    if (conf->enable) {
      DFTRACER_LOG_DEBUG("DFTracer enabled");
      DFTRACER_LOG_DEBUG("Setting process_id to %d", this->process_id);
      char exec_name[128] = "DEFAULT";
      char exec_cmd[DFT_PATH_MAX] = "DEFAULT";
      char cmd[128];
      dftracer_logging_real_sprintf()(cmd, "/proc/%d/cmdline", df_getpid());
      auto& posix_bypass = dftracer::POSIXBypass::get_instance();
      int fd = posix_bypass.open(cmd, O_RDONLY);
      if (fd != -1) {
        ssize_t read_bytes = posix_bypass.read(fd, exec_cmd, DFT_PATH_MAX);
        posix_bypass.close(fd);
        ssize_t index = 0;
        size_t last_index = 0;
        bool has_extracted = false;
        while (index < read_bytes - 1 && index < DFT_PATH_MAX - 2) {
          if (exec_cmd[index] == '\0') {
            if (!has_extracted) {
              strcpy(exec_name, basename(exec_cmd + last_index));
              if (exec_name[0] != '-' && strstr(exec_name, "python") == NULL &&
                  strstr(exec_name, "env") == NULL &&
                  strstr(exec_name, "multiprocessing") == NULL) {
                has_extracted = true;
                DFTRACER_LOG_INFO("Extracted process_name %s", exec_name);
              }
            }
            exec_cmd[index] = SEPARATOR;
            last_index = index + 1;
          }
          index++;
        }
        if (!has_extracted) {
          if (strstr(exec_name, "multiprocessing") != NULL) {
            dftracer_logging_real_sprintf()(exec_name, "DEFAULT-spawn");
          } else {
            dftracer_logging_real_sprintf()(exec_name, "DEFAULT");
          }
        }
        exec_cmd[DFT_PATH_MAX - 1] = '\0';
        DFTRACER_LOG_DEBUG("Exec command line %s", exec_cmd);
      }
      DFTRACER_LOG_INFO("Extracted process_name %s", exec_name);
      char log_filename_str[DFT_PATH_MAX];
      char hostname[256] = "unknown";
      gethostname(hostname, sizeof(hostname));
      hostname[sizeof(hostname) - 1] = '\0';
      dftracer_logging_real_snprintf()(log_filename_str,
                                       sizeof(log_filename_str), "%s-%s-%d",
                                       exec_name, hostname, this->process_id);
      char* log_file_hash = logger->get_hash(log_filename_str);
      if (_log_file == nullptr) {
        if (!conf->log_file.empty()) {
          DFTRACER_LOG_DEBUG("Conf has log file %s", conf->log_file.c_str());
          std::string extension = ".pfw";
          if (conf->compression) {
            extension += ".gz";
          }
          this->log_file_prefix = std::string(conf->log_file);
          this->log_file = std::string(conf->log_file) + "-" +
                           std::string(log_file_hash) + "-" + log_file_suffix +
                           extension;
        } else {  // GCOV_EXCL_START
          DFTRACER_LOG_ERROR(DFTRACER_UNDEFINED_LOG_FILE_MSG);
          throw std::runtime_error(DFTRACER_UNDEFINED_LOG_FILE_CODE);
        }  // GCOV_EXCL_STOP
      } else {
        this->log_file = _log_file;
        // Ensure log file extension matches compression setting
        std::string extension = ".pfw";
        if (conf->compression) {
          extension += ".gz";
        }
        // Strip the extension from the basename only. Handles compound
        // .pfw[.gz] and skips a parent-dir '.' or a dotfile, which would empty
        // the base.
        size_t sep_pos = this->log_file.find_last_of("/\\");
        size_t base_start = (sep_pos == std::string::npos) ? 0 : sep_pos + 1;
        std::string basename = this->log_file.substr(base_start);
        size_t strip = std::string::npos;
        if (basename.size() > 7 &&
            basename.compare(basename.size() - 7, 7, ".pfw.gz") == 0) {
          strip = basename.size() - 7;
        } else if (basename.size() > 4 &&
                   basename.compare(basename.size() - 4, 4, ".pfw") == 0) {
          strip = basename.size() - 4;
        } else {
          size_t dot = basename.find_last_of(".");
          if (dot != std::string::npos && dot != 0) strip = dot;
        }
        if (strip != std::string::npos) {
          this->log_file = this->log_file.substr(0, base_start + strip);
        }
        this->log_file_prefix = this->log_file;
        this->log_file += "-" + std::string(log_file_hash) + "-" +
                          log_file_suffix + extension;
      }
      free(log_file_hash);
      DFTRACER_LOG_DEBUG("Setting log file to %s", this->log_file.c_str());
      logger->update_log_file(this->log_file, exec_name, exec_cmd,
                              this->process_id);
      logger->set_runtime_info(this->bind, this->log_file);
      if (bind) {
        if (conf->io) {
          auto trie = dftracer::Singleton<Trie>::get_instance();
          const char* ignore_extensions[3] = {".pfw", ".py", ".pfw.gz"};
          const char* ignore_prefix[8] = {"/pipe",  "/socket", "/proc",
                                          "/sys",   "/collab", "anon_inode",
                                          "socket", "/var/tmp"};
          for (const char* folder : ignore_prefix) {
            trie->exclude(folder, strlen(folder));
          }
          for (const char* ext : ignore_extensions) {
            trie->exclude_reverse(ext, strlen(ext));
          }
          if (!conf->trace_all_files) {
            if (_data_dirs == nullptr) {
              if (!conf->data_dirs.empty()) {
                this->data_dirs = conf->data_dirs;
              } else {  // GCOV_EXCL_START
                DFTRACER_LOG_ERROR("%s", DFTRACER_UNDEFINED_DATA_DIR_MSG);
                throw std::runtime_error(DFTRACER_UNDEFINED_DATA_DIR_CODE);
              }  // GCOV_EXCL_STOP
            } else {
              this->data_dirs = _data_dirs;
              if (!conf->data_dirs.empty()) {
                this->data_dirs += ":" + conf->data_dirs;
              }
            }
            DFTRACER_LOG_DEBUG("Setting data_dirs to %s",
                               this->data_dirs.c_str());
          } else {
            DFTRACER_LOG_DEBUG("Ignoring data_dirs as tracing all files");
          }

          if (!conf->trace_all_files) {
            auto paths = split(this->data_dirs, DFTRACER_DATA_DIR_DELIMITER);
            for (const auto& path : paths) {
              DFTRACER_LOG_DEBUG("Profiler will trace %s\n", path.c_str());
              trie->include(path.c_str(), path.size());
            }
          }
          if (conf->posix) {
            auto posix =
                brahma::POSIXDFTracer::get_instance(conf->trace_all_files);
            posix->bind<brahma::POSIXDFTracer>("dftracer",
                                               conf->gotcha_priority);
          }
          if (conf->stdio) {
            auto stdio =
                brahma::STDIODFTracer::get_instance(conf->trace_all_files);
            stdio->bind<brahma::STDIODFTracer>("dftracer",
                                               conf->gotcha_priority);
          }
#if defined(DFTRACER_MPI_ENABLE) && defined(BRAHMA_ENABLE_MPI)
          auto mpi = brahma::MPIDFTracer::get_instance();
          mpi->bind<brahma::MPIDFTracer>("dftracer", conf->gotcha_priority);
          auto mpiio = brahma::MPIIODFTracer::get_instance();
          mpiio->bind<brahma::MPIIODFTracer>("dftracer", conf->gotcha_priority);
#endif
#if defined(DFTRACER_HDF5_ENABLE) && defined(BRAHMA_ENABLE_HDF5)
          auto hdf5 = brahma::HDF5DFTracer::get_instance();
          hdf5->bind<brahma::HDF5DFTracer>("dftracer", conf->gotcha_priority);
#endif
        }
        DFTRACER_LOG_DEBUG("Checking if FTRACING and HIP_TRACING are enabled",
                           "");
#ifdef DFTRACER_FTRACING_ENABLE
        dftracer::Function::get_instance();
#endif
#ifdef DFTRACER_HIP_TRACING_ENABLE
        DFTRACER_LOG_DEBUG("HIP tracing is enabled");
        auto hip_instance =
            dftracer::Singleton<dftracer::HIPFunction>::get_instance();
        hip_instance->initialize();
#else
        DFTRACER_LOG_DEBUG("HIP tracing is not enabled");
#endif
#ifdef DFTRACER_CUDA_TRACING_ENABLE
        DFTRACER_LOG_DEBUG("CUDA tracing is enabled");
        auto cuda_instance =
            dftracer::Singleton<dftracer::CUDAFunction>::get_instance();
        cuda_instance->initialize();
#else
        DFTRACER_LOG_DEBUG("CUDA tracing is not enabled");
#endif
      }
    } else {
#ifdef DFTRACER_FTRACING_ENABLE
      dftracer::Function::get_instance()->finalize();
#endif
#ifdef DFTRACER_HIP_TRACING_ENABLE
      auto hip_instance =
          dftracer::Singleton<dftracer::HIPFunction>::get_instance();
      if (hip_instance != nullptr) {
        hip_instance->finalize();
      }
#endif
#ifdef DFTRACER_CUDA_TRACING_ENABLE
      auto cuda_instance =
          dftracer::Singleton<dftracer::CUDAFunction>::get_instance();
      if (cuda_instance != nullptr) {
        cuda_instance->finalize();
      }
#endif
    }
#ifdef DFTRACER_PAPI_TRACING_ENABLE
    // Hardware counters are independent of whether I/O interception is bound,
    // so the sampler starts for API-only apps too, not just LD_PRELOAD runs.
    {
      auto papi_instance =
          dftracer::Singleton<dftracer::PAPICounterFunction>::get_instance();
      if (papi_instance != nullptr) {
        if (conf->papi_tracing) {
          papi_instance->initialize();
        } else {
          papi_instance->finalize();
        }
      }
    }
#endif
    if (!this->log_file_prefix.empty())
      setenv("DFTRACER_LOG_FILE", this->log_file_prefix.c_str(), 1);
    if (!this->data_dirs.empty())
      setenv("DFTRACER_DATA_DIR", this->data_dirs.c_str(), 1);
    DFTRACER_LOG_INFO(
        "DFTracerCore::initialize _bind:%d _log_file:%s _data_dirs:%s "
        "_process_id:%d\n",
        this->bind, this->log_file.c_str(), this->data_dirs.c_str(),
        this->process_id);
    // Register a child-after-fork handler so that forked workers (e.g. Python
    // multiprocessing with start method "fork") re-open their own log file
    // instead of using the inherited FILE* whose internal mutex state is
    // inconsistent in the child and causes flockfile/fflush to crash on exit.
    static std::once_flag atfork_once;
    std::call_once(atfork_once, []() {
      pthread_atfork(nullptr, nullptr, []() {
        // This child has not called MPI_Init itself: forbid MPI calls from
        // tracing code until (if ever) it does, since MPI runtimes may fork
        // helper processes (e.g. singleton MPI_Init) that share MPI's
        // shared-memory transport state with the parent, and touching MPI
        // here would corrupt that shared state. See dftracer_mpi_fork_guard.
        dftracer_mpi_fork_guard().store(true);
        auto core = dftracer::Singleton<dftracer::DFTracerCore>::get_instance(
            ProfilerStage::PROFILER_OTHER, ProfileType::PROFILER_ANY);
        if (core != nullptr) core->reinitialize();
      });
    });
    is_initialized = true;
  }
}

TimeResolution dftracer::DFTracerCore::get_time() {
  DFTRACER_LOG_DEBUG("DFTracerCore::get_time");
  if (this->is_initialized && conf->enable && logger != nullptr) {
    return logger->get_time();
  } else {
    DFTRACER_LOG_DEBUG("DFTracerCore::get_time logger not initialized");
  }
  return -1;
}
