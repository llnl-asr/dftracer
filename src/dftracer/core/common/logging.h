#ifndef DFTRACER_COMMON_DFTRACER_LOGGING_H
#define DFTRACER_COMMON_DFTRACER_LOGGING_H

#ifdef DFTRACER_DEBUG
#include <dftracer/core/dftracer_config_dbg.hpp>
#else
#include <dftracer/core/dftracer_config.hpp>
#endif

/* External Headers */
#include <dlfcn.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

// dftracer's own internal logging (this file) calls sprintf/snprintf/fprintf
// to format its debug/error messages. Those same symbol names are
// GOTCHA-intercepted by STDIODFTracer (brahma/interface/stdio.h's expanded
// printf family). Calling the plain global names here would recurse into
// STDIODFTracer's own trace-writing path -- which itself logs -- causing a
// self-recursive deadlock/stack overflow (observed as `std::system_error:
// Resource deadlock avoided` and SIGSEGV in CI once fprintf/snprintf were
// added to the interception surface). Resolving the real libc symbols
// directly via dlsym sidesteps GOTCHA's PLT/GOT rewriting entirely, mirroring
// dftracer::STDIOBypass -- but implemented locally (not via STDIOBypass)
// since logging can fire before DFTracerCore::initialize() has run.
using dftracer_logging_snprintf_fn = int (*)(char*, size_t, const char*, ...);
using dftracer_logging_fprintf_fn = int (*)(FILE*, const char*, ...);
using dftracer_logging_sprintf_fn = int (*)(char*, const char*, ...);

#ifndef DFTRACER_LIBC_SO_PATH
#define DFTRACER_LIBC_SO_PATH "libc.so.6"
#endif

inline void* dftracer_logging_libc_handle() {
  static void* handle = dlopen(DFTRACER_LIBC_SO_PATH, RTLD_LAZY | RTLD_NOLOAD);
  return handle;
}
inline dftracer_logging_snprintf_fn dftracer_logging_real_snprintf() {
  static auto fn = reinterpret_cast<dftracer_logging_snprintf_fn>(
      dlsym(dftracer_logging_libc_handle(), "snprintf"));
  return fn;
}
inline dftracer_logging_fprintf_fn dftracer_logging_real_fprintf() {
  static auto fn = reinterpret_cast<dftracer_logging_fprintf_fn>(
      dlsym(dftracer_logging_libc_handle(), "fprintf"));
  return fn;
}
inline dftracer_logging_sprintf_fn dftracer_logging_real_sprintf() {
  static auto fn = reinterpret_cast<dftracer_logging_sprintf_fn>(
      dlsym(dftracer_logging_libc_handle(), "sprintf"));
  return fn;
}

// localtime() takes a glibc lock that a forked child can inherit held, so the
// UTC offset is read once and the date is computed by hand.
inline long dftracer_macro_utc_offset() {
  static const long offset = [] {
    time_t t = std::time(nullptr);
    struct tm local{};
    localtime_r(&t, &local);
    return local.tm_gmtoff;
  }();
  return offset;
}

inline std::string dftracer_macro_get_time() {
  struct timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  long long seconds = ts.tv_sec + dftracer_macro_utc_offset();
  long long days = seconds / 86400;
  long long of_day = seconds % 86400;
  if (of_day < 0) {
    of_day += 86400;
    --days;
  }
  // civil_from_days from Howard Hinnant, "chrono-Compatible Low-Level Date
  // Algorithms", public domain:
  // https://howardhinnant.github.io/date_algorithms.html
  days += 719468;
  const long long era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned day = doy - (153 * mp + 2) / 5 + 1;
  const unsigned month = mp < 10 ? mp + 3 : mp - 9;
  const long long year = yoe + era * 400 + (month <= 2 ? 1 : 0);
  char dftracer_ts_time_str[256];
  dftracer_logging_real_sprintf()(
      dftracer_ts_time_str, "%04lld-%02u-%02u %02lld:%02lld:%02lld.%ld", year,
      month, day, of_day / 3600, of_day / 60 % 60, of_day % 60,
      ts.tv_nsec / 1000000);
  return dftracer_ts_time_str;
}

#define DFTRACER_NOOP_MACRO \
  do {                      \
  } while (0)

// Guards against DFTracer's own logging recursing back into itself. cpp-logger
// writes each line with stdio (fprintf, then fflush), and those calls are
// intercepted by STDIODFTracer, whose interceptor bodies log at DEBUG -- which
// calls cpp-logger again, and so on until the process wedges. Any binary run at
// DEBUG level hung on this. The guard makes a nested log from inside a log a
// no-op, which breaks every such cycle at the root rather than one intercepted
// stdio symbol at a time.
class DFTracerLogReentryGuard {
 public:
  DFTracerLogReentryGuard() : entered_(!active()) {
    if (entered_) active() = true;
  }
  ~DFTracerLogReentryGuard() {
    if (entered_) active() = false;
  }
  // True when this is the outermost log call on this thread, i.e. it is safe
  // to actually emit.
  bool should_log() const { return entered_; }

 private:
  static bool& active() {
    static thread_local bool in_logging = false;
    return in_logging;
  }
  bool entered_;
};
//=============================================================================

#if defined(DFTRACER_LOGGER_CPP_LOGGER)  // CPP_LOGGER
// ---------------------------
#include <cpp-logger/clogger.h>

#define DFTRACER_LOG_STDOUT_REDIRECT(fpath) freopen((fpath), "a+", stdout);
#define DFTRACER_LOG_STDERR_REDIRECT(fpath) freopen((fpath), "a+", stderr);
#define DFTRACER_LOGGER_NAME "DFTRACER"

inline void dftracer_internal_trace(const char* file, int line,
                                    const char* function, const char* name,
                                    int logger_level) {
  DFTracerLogReentryGuard guard;
  if (!guard.should_log()) return;
  cpp_logger_clog(logger_level, name, "[%s] %s [%s:%d]",
                  dftracer_macro_get_time().c_str(), function, file, line);
}

#define DFTRACER_INTERNAL_TRACE(file, line, function, name, logger_level) \
  dftracer_internal_trace(file, line, function, name, logger_level);

template <typename... Args>
inline void dftracer_internal_trace_format(const char* file, int line,
                                           const char* function,
                                           const char* name, int logger_level,
                                           const char* format, Args... args) {
  DFTracerLogReentryGuard guard;
  if (!guard.should_log()) return;
  char user_message[4096];
  dftracer_logging_real_snprintf()(user_message, sizeof(user_message), format,
                                   args...);
  cpp_logger_clog(logger_level, name, "[%s] %s %s [%s:%d]",
                  dftracer_macro_get_time().c_str(), function, user_message,
                  file, line);
}

#define DFTRACER_LOG_PRINT(...)                                            \
  dftracer_internal_trace_format(__FILE__, __LINE__, __FUNCTION__,         \
                                 DFTRACER_LOGGER_NAME, CPP_LOGGER_C_PRINT, \
                                 __VA_ARGS__);
#ifdef DFTRACER_LOGGER_LEVEL_TRACE
#define DFTRACER_LOGGER_INIT() \
  cpp_logger_clog_level(CPP_LOGGER_C_TRACE, DFTRACER_LOGGER_NAME);
#elif defined(DFTRACER_LOGGER_LEVEL_DEBUG)
#define DFTRACER_LOGGER_INIT() \
  cpp_logger_clog_level(CPP_LOGGER_C_DEBUG, DFTRACER_LOGGER_NAME);
#elif defined(DFTRACER_LOGGER_LEVEL_INFO)
#define DFTRACER_LOGGER_INIT() \
  cpp_logger_clog_level(CPP_LOGGER_C_INFO, DFTRACER_LOGGER_NAME);
#elif defined(DFTRACER_LOGGER_LEVEL_WARN)
#define DFTRACER_LOGGER_INIT() \
  cpp_logger_clog_level(CPP_LOGGER_C_WARN, DFTRACER_LOGGER_NAME);
#else
#define DFTRACER_LOGGER_INIT() \
  cpp_logger_clog_level(CPP_LOGGER_C_ERROR, DFTRACER_LOGGER_NAME);
#endif

#define DFTRACER_LOGGER_LEVEL(level) \
  cpp_logger_clog_level(level, DFTRACER_LOGGER_NAME);
#ifdef DFTRACER_LOGGER_LEVEL_TRACE
#define DFTRACER_LOG_TRACE()                                \
  DFTRACER_INTERNAL_TRACE(__FILE__, __LINE__, __FUNCTION__, \
                          DFTRACER_LOGGER_NAME, CPP_LOGGER_C_TRACE);
#define DFTRACER_LOG_TRACE_FORMAT(...)                                     \
  dftracer_internal_trace_format(__FILE__, __LINE__, __FUNCTION__,         \
                                 DFTRACER_LOGGER_NAME, CPP_LOGGER_C_TRACE, \
                                 __VA_ARGS__);
#else
#define DFTRACER_LOG_TRACE(...) DFTRACER_NOOP_MACRO
#define DFTRACER_LOG_TRACE_FORMAT(...) DFTRACER_NOOP_MACRO
#endif

#ifdef DFTRACER_LOGGER_LEVEL_DEBUG
#define DFTRACER_LOG_DEBUG(...)                                            \
  dftracer_internal_trace_format(__FILE__, __LINE__, __FUNCTION__,         \
                                 DFTRACER_LOGGER_NAME, CPP_LOGGER_C_DEBUG, \
                                 __VA_ARGS__);
#else
#define DFTRACER_LOG_DEBUG(...) DFTRACER_NOOP_MACRO
#endif

#ifdef DFTRACER_LOGGER_LEVEL_INFO
#define DFTRACER_LOG_INFO(...)                                            \
  dftracer_internal_trace_format(__FILE__, __LINE__, __FUNCTION__,        \
                                 DFTRACER_LOGGER_NAME, CPP_LOGGER_C_INFO, \
                                 __VA_ARGS__);
#else
#define DFTRACER_LOG_INFO(...) DFTRACER_NOOP_MACRO
#endif

#ifdef DFTRACER_LOGGER_LEVEL_WARN
#define DFTRACER_LOG_WARN(...)                                            \
  dftracer_internal_trace_format(__FILE__, __LINE__, __FUNCTION__,        \
                                 DFTRACER_LOGGER_NAME, CPP_LOGGER_C_WARN, \
                                 __VA_ARGS__);
#else
#define DFTRACER_LOG_WARN(...) DFTRACER_NOOP_MACRO
#endif

#ifdef DFTRACER_LOGGER_LEVEL_ERROR
#define DFTRACER_LOG_ERROR(...)                                            \
  dftracer_internal_trace_format(__FILE__, __LINE__, __FUNCTION__,         \
                                 DFTRACER_LOGGER_NAME, CPP_LOGGER_C_ERROR, \
                                 __VA_ARGS__);
#else
#define DFTRACER_LOG_ERROR(...) DFTRACER_NOOP_MACRO
#endif
#else
#define DFTRACER_LOGGER_INIT() DFTRACER_NOOP_MACRO
#define DFTRACER_LOGGER_LEVEL(level) DFTRACER_NOOP_MACRO
template <typename... Args>
inline void dftracer_log_printf(FILE* stream, const char* format,
                                Args... args) {
  dftracer_logging_real_fprintf()(stream, format, args...);
}
// Without cpp-logger (the release libraries and dftracer_service) only PRINT
// is compiled in; every other level comes with cpp-logger, in the _dbg
// builds.
#define DFTRACER_LOG_PRINT(...) dftracer_log_printf(stdout, __VA_ARGS__);
#define DFTRACER_LOG_ERROR(...) DFTRACER_NOOP_MACRO
#define DFTRACER_LOG_WARN(...) DFTRACER_NOOP_MACRO
#define DFTRACER_LOG_INFO(...) DFTRACER_NOOP_MACRO
#define DFTRACER_LOG_DEBUG(...) DFTRACER_NOOP_MACRO
#define DFTRACER_LOG_TRACE() DFTRACER_NOOP_MACRO
#define DFTRACER_LOG_TRACE_FORMAT(...) DFTRACER_NOOP_MACRO
#define DFTRACER_LOG_STDOUT_REDIRECT(fpath) DFTRACER_NOOP_MACRO
#define DFTRACER_LOG_STDERR_REDIRECT(fpath) DFTRACER_NOOP_MACRO
#endif  // DFTRACER_LOGGER_CPP_LOGGER
        // -----------------------------------------------
//=============================================================================

#endif /* DFTRACER_COMMON_DFTRACER_LOGGING_H */
