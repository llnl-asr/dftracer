#ifndef DFTRACER_SERVICE_PIPELINE_CPU_PLACER_H
#define DFTRACER_SERVICE_PIPELINE_CPU_PLACER_H

#include <dirent.h>
#include <sched.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace dftracer {

/**
 * Keeps the service's threads off the CPUs that its clients' threads may run
 * on, and off the SMT siblings of those CPUs: a drain thread there takes
 * cycles and cache from the application.
 *
 * DFTRACER_PC_SERVICE_CPUS selects the policy: "auto" (the default) follows
 * the clients, "off" leaves the service where it started, and a CPU list
 * such as "8-11,20-23" fixes it there. Not thread-safe: one thread calls
 * update().
 */
class CpuPlacer {
 public:
  enum class Mode { kAuto, kOff, kFixed };

  /**
   * Parses `spec` and, for a CPU list, moves every service thread there.
   * A list with no CPU the service may use is reported and treated as auto.
   */
  void configure(const std::string& spec) {
    CPU_ZERO(&base_);
    if (sched_getaffinity(0, sizeof(base_), &base_) != 0) {
      mode_ = Mode::kOff;
      return;
    }
    current_ = base_;
    mode_ = Mode::kAuto;
    if (spec.empty() || spec == "auto") {
      read_siblings();
      return;
    }
    if (spec == "off") {
      mode_ = Mode::kOff;
      return;
    }
    cpu_set_t fixed;
    if (parse_cpu_list(spec.c_str(), &fixed)) {
      CPU_AND(&fixed, &fixed, &base_);
      if (CPU_COUNT(&fixed) != 0) {
        mode_ = Mode::kFixed;
        move_to(fixed);
        return;
      }
    }
    std::fprintf(stderr,
                 "[DFTRACER] WARNING: DFTRACER_PC_SERVICE_CPUS=%s names no CPU "
                 "the service may use; placing it automatically\n",
                 spec.c_str());
    read_siblings();
  }

  Mode mode() const { return mode_; }

  /**
   * In auto mode, moves the service to fit the client processes `pids`.
   * Processes set their affinity after they start (taskset, MPI, OpenMP), so
   * it looks again when the set changes, and after a wait that starts at
   * kFirstLookNs on any change and doubles up to kLastLookNs.
   */
  void update(const std::vector<uint32_t>& pids, uint64_t now_ns) {
    if (mode_ != Mode::kAuto) return;
    const bool same = pids == pids_;
    if (same && now_ns < next_look_ns_) return;
    pids_ = pids;
    cpu_set_t used;
    CPU_ZERO(&used);
    for (uint32_t pid : pids) add_threads_of(pid, &used);
    const bool moved = !same || !CPU_EQUAL(&used, &used_);
    used_ = used;
    look_ns_ = moved ? kFirstLookNs : std::min(2 * look_ns_, kLastLookNs);
    next_look_ns_ = now_ns + look_ns_;
    cpu_set_t want;
    choose(base_, used, siblings_, &want);
    if (!CPU_EQUAL(&want, &current_)) move_to(want);
  }

  /**
   * The CPUs for the service: `base` without `used` and without the SMT
   * siblings of `used`; else `base` without `used`; else all of `base`.
   * `siblings[c]` holds CPU c and its siblings (empty for an unknown c).
   */
  static void choose(const cpu_set_t& base, const cpu_set_t& used,
                     const std::vector<cpu_set_t>& siblings, cpu_set_t* out) {
    cpu_set_t near = used;
    for (size_t c = 0; c < siblings.size(); ++c)
      if (CPU_ISSET(c, &used)) CPU_OR(&near, &near, &siblings[c]);
    cpu_set_t free_core;
    cpu_set_t free_cpu;
    CPU_ZERO(&free_core);
    CPU_ZERO(&free_cpu);
    for (int c = 0; c < CPU_SETSIZE; ++c) {
      if (!CPU_ISSET(c, &base)) continue;
      if (!CPU_ISSET(c, &near)) CPU_SET(c, &free_core);
      if (!CPU_ISSET(c, &used)) CPU_SET(c, &free_cpu);
    }
    if (CPU_COUNT(&free_core) != 0)
      *out = free_core;
    else if (CPU_COUNT(&free_cpu) != 0)
      *out = free_cpu;
    else
      *out = base;
  }

  /** Parses a CPU list such as "0-3,8,10-11"; false if malformed. */
  static bool parse_cpu_list(const char* s, cpu_set_t* out) {
    CPU_ZERO(out);
    while (*s != '\0' && *s != '\n') {
      char* end;
      const long lo = std::strtol(s, &end, 10);
      if (end == s || lo < 0 || lo >= CPU_SETSIZE) return false;
      long hi = lo;
      s = end;
      if (*s == '-') {
        hi = std::strtol(s + 1, &end, 10);
        if (end == s + 1 || hi < lo || hi >= CPU_SETSIZE) return false;
        s = end;
      }
      for (long c = lo; c <= hi; ++c) CPU_SET(static_cast<int>(c), out);
      if (*s == ',') ++s;
    }
    return CPU_COUNT(out) != 0;
  }

  /** `set` as a CPU list, such as "8-11,20-23". */
  static std::string format_cpu_list(const cpu_set_t& set) {
    std::string text;
    for (int c = 0; c < CPU_SETSIZE; ++c) {
      if (!CPU_ISSET(c, &set)) continue;
      int last = c;
      while (last + 1 < CPU_SETSIZE && CPU_ISSET(last + 1, &set)) ++last;
      if (!text.empty()) text += ',';
      text += std::to_string(c);
      if (last != c) text += '-' + std::to_string(last);
      c = last;
    }
    return text;
  }

 private:
  void read_siblings() {
    siblings_.assign(CPU_SETSIZE, cpu_set_t{});
    for (int c = 0; c < CPU_SETSIZE; ++c) {
      CPU_ZERO(&siblings_[c]);
      if (!CPU_ISSET(c, &base_)) continue;
      char path[96];
      std::snprintf(
          path, sizeof(path),
          "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", c);
      char text[256];
      if (read_line(path, text, sizeof(text)))
        parse_cpu_list(text, &siblings_[c]);
    }
  }

  static bool read_line(const char* path, char* text, size_t size) {
    FILE* f = std::fopen(path, "re");
    if (f == nullptr) return false;
    const bool ok = std::fgets(text, static_cast<int>(size), f) != nullptr;
    std::fclose(f);
    return ok;
  }

  // Every thread of `pid`, not only its first: threads bind themselves
  // (OpenMP, a runtime's helpers) after the process starts.
  static void add_threads_of(uint32_t pid, cpu_set_t* used) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/task", pid);
    DIR* dir = opendir(path);
    if (dir == nullptr) return;
    while (dirent* entry = readdir(dir)) {
      const long tid = std::strtol(entry->d_name, nullptr, 10);
      if (tid <= 0) continue;
      cpu_set_t mask;
      if (sched_getaffinity(static_cast<pid_t>(tid), sizeof(mask), &mask) == 0)
        CPU_OR(used, used, &mask);
    }
    closedir(dir);
  }

  // Moves every thread of the service; threads started later inherit the
  // mask of the thread that starts them.
  void move_to(const cpu_set_t& want) {
    DIR* dir = opendir("/proc/self/task");
    if (dir == nullptr) return;
    bool moved = false;
    while (dirent* entry = readdir(dir)) {
      const long tid = std::strtol(entry->d_name, nullptr, 10);
      if (tid > 0 &&
          sched_setaffinity(static_cast<pid_t>(tid), sizeof(want), &want) == 0)
        moved = true;
    }
    closedir(dir);
    if (!moved) {
      std::fprintf(stderr,
                   "[DFTRACER] WARNING: cannot move service threads to CPUs "
                   "%s\n",
                   format_cpu_list(want).c_str());
      return;
    }
    current_ = want;
    std::fprintf(stderr, "[DFTRACER] service threads on CPUs %s\n",
                 format_cpu_list(want).c_str());
  }

  static constexpr uint64_t kFirstLookNs = 10000000ULL;
  static constexpr uint64_t kLastLookNs = 1000000000ULL;

  Mode mode_ = Mode::kOff;
  cpu_set_t base_{};
  cpu_set_t current_{};
  std::vector<cpu_set_t> siblings_;
  std::vector<uint32_t> pids_;
  cpu_set_t used_{};
  uint64_t look_ns_ = kFirstLookNs;
  uint64_t next_look_ns_ = 0;
};

}  // namespace dftracer

#endif  // DFTRACER_SERVICE_PIPELINE_CPU_PLACER_H
