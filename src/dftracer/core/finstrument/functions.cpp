#include <dftracer/core/finstrument/functions.h>
#ifdef DFTRACER_FTRACING_ENABLE
#include <link.h>
std::shared_ptr<dftracer::Function> dftracer::Function::instance = nullptr;

thread_local std::unordered_map<std::string, TimeResolution>
    dftracer::Function::map = std::unordered_map<std::string, TimeResolution>();
bool dftracer::Function::stop_trace = false;
int dftracer::Function::enter_event(std::string& name) {
  if (stop_trace) return -1;
  auto start = this->logger->get_time();
  map.insert_or_assign(name, start);
  return 0;
}

int dftracer::Function::exit_event(std::string& name, TimeResolution& start) {
  if (stop_trace) return -1;
  auto tmap = map.find(name);
  if (tmap != map.end()) {
    start = tmap->second;
    map.erase(name);
    return 0;
  }
  return -1;
}

void __cyg_profile_func_enter(void* func, void* caller) {
  auto function = dftracer::Function::get_instance();
  if (!function || !function->is_active() || !function->logger) return;
  Dl_info info;
  if (!dladdr(func, &info)) return;
  if (!info.dli_fname) return;
  std::string event_name;
  if (!info.dli_sname) {
    char name[256];
    dftracer_logging_real_sprintf()(name, "%p", func);
    event_name = name;
  } else {
    event_name = info.dli_sname;
  }

  DFTRACER_LOG_DEBUG("Calling function %s", event_name);
  function->enter_event(event_name);
}

void __cyg_profile_func_exit(void* func, void* caller) {
  auto function = dftracer::Function::get_instance();
  if (!function || !function->is_active() || !function->logger) return;
  auto end_time = function->logger->get_time();
  Dl_info info;
  if (!dladdr(func, &info)) return;
  if (!info.dli_fname) return;
  std::string event_name;
  if (!info.dli_sname) {
    char name[256];
    dftracer_logging_real_sprintf()(name, "%p", func);
    event_name = name;
  } else {
    event_name = info.dli_sname;
  }
  TimeResolution start_time;
  int status = function->exit_event(event_name, start_time);
  if (status == 0) {
    dftracer::DraftScope draft(function->logger->include_metadata);
    if (draft.get() != nullptr) draft.get()->set("so", info.dli_fname);
    function->logger->enter_event();
    function->logger->log(event_name, CATEGORY, TRACE_TYPE, start_time,
                          end_time - start_time, draft.get());
    function->logger->exit_event();
  }
}
#endif
