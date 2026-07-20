#include <dftracer/service/service.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
// Basename helper (avoids depending on <libgen.h> POSIX basename, which can
// mutate its argument).
std::string basename_of(const std::string& path) {
  auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Returns the short hostname, used to namespace per-node state files so that
// concurrent daemons sharing a log_dir on a network filesystem (e.g. one
// state_dir passed to every node of a multi-node job) never clobber each
// other's PID file.
std::string short_hostname() {
  char hostname[256] = {0};
  if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
    return "unknown";
  }
  std::string h(hostname);
  auto dot = h.find('.');
  if (dot != std::string::npos) h = h.substr(0, dot);
  return h;
}

// Finds PIDs of other "<binary_name> start <log_dir>" processes on this
// node, excluding `self_pid`. Used to catch daemons that leaked because a
// previous `stop` was never called (or used a mismatched/legacy pid file
// path) — they are otherwise invisible since they hold no valid PID-file
// reference once a later start overwrites it.
//
// Scans /proc directly rather than shelling out to `pgrep -f`. The
// popen()-based version ran `sh -c "pgrep -f '<binary_name> start
// <log_dir>' "`, and that wrapper shell's own argv literally contains the
// search pattern as a substring — `pgrep -f` matched the wrapper itself,
// treating it as a "rogue" process distinct from `self_pid`. Killing that
// transient, about-to-exit PID (SIGINT, escalating to SIGKILL) risked
// hitting an unrelated process if the PID was recycled before the signal
// was delivered — this manifested as node/job-shell processes elsewhere in
// the job getting killed and taking down the whole allocation. Reading
// /proc/<pid>/cmdline for real dftracer_service processes avoids ever
// spawning a matching shell process in the first place.
std::vector<pid_t> find_other_start_pids(const std::string& binary_name,
                                          const std::string& log_dir,
                                          pid_t self_pid) {
  std::vector<pid_t> pids;
  DIR* proc = opendir("/proc");
  if (!proc) return pids;

  struct dirent* entry;
  while ((entry = readdir(proc)) != nullptr) {
    // /proc entries for processes are all-digit directory names.
    const char* name = entry->d_name;
    if (name[0] < '0' || name[0] > '9') continue;
    pid_t pid = static_cast<pid_t>(std::strtol(name, nullptr, 10));
    if (pid <= 0 || pid == self_pid) continue;

    std::ifstream cmdline_file("/proc/" + std::string(name) + "/cmdline",
                                std::ios::binary);
    if (!cmdline_file) continue;
    std::string cmdline((std::istreambuf_iterator<char>(cmdline_file)),
                         std::istreambuf_iterator<char>());
    if (cmdline.empty()) continue;

    // cmdline is NUL-separated argv; split it out instead of doing a raw
    // substring match so a log_dir path that happens to be a substring of
    // some unrelated process's argv can never match.
    std::vector<std::string> args;
    size_t start = 0;
    for (size_t i = 0; i <= cmdline.size(); ++i) {
      if (i == cmdline.size() || cmdline[i] == '\0') {
        if (i > start) args.push_back(cmdline.substr(start, i - start));
        start = i + 1;
      }
    }
    if (args.size() < 3) continue;
    if (basename_of(args[0]) != binary_name) continue;
    if (args[1] != "start") continue;
    if (args[2] != log_dir) continue;

    pids.push_back(pid);
  }
  closedir(proc);
  return pids;
}
}  // namespace

// Daemonize the process: detach from terminal and run in background
void daemonize() {
  pid_t pid = fork();
  if (pid < 0) exit(EXIT_FAILURE);  // Fork failed
  if (pid > 0) exit(EXIT_SUCCESS);  // Parent exits

  // Child continues as session leader
  if (setsid() < 0) exit(EXIT_FAILURE);

  pid = fork();
  if (pid < 0) exit(EXIT_FAILURE);  // Second fork failed
  if (pid > 0) exit(EXIT_SUCCESS);  // First child exits

  // Close standard file descriptors
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
}

int main(int argc, char* argv[]) {
  // Check for correct usage
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " <start|stop> [log_dir]" << std::endl;
    return 1;
  }

  std::string cmd = argv[1];
  std::string log_dir = (argc == 3) ? argv[2] : "/tmp";

  // Ensure log_dir ends without trailing slash
  if (!log_dir.empty() && log_dir.back() == '/') log_dir.pop_back();

  // Namespace state files by hostname so that multiple nodes pointed at the
  // same (e.g. shared-filesystem) log_dir don't overwrite each other's PID
  // file — without this, only the last node to start "wins" the PID file
  // and every other node's daemon becomes unreachable via `stop`.
  std::string host = short_hostname();
  std::string pid_file_path = log_dir + "/dftracer_server_" + host + ".pid";
  std::string out_log_path = log_dir + "/dftracer_server_" + host + ".out";
  std::string err_log_path = log_dir + "/dftracer_server_" + host + ".err";

  if (cmd == "start") {
    std::string binary_name = basename_of(argv[0]);

    // 1. Refuse to start a second tracked instance for this node: if the
    //    PID file exists and that PID is alive, this node already has a
    //    running daemon for this log_dir.
    std::ifstream existing_pid_file(pid_file_path);
    pid_t existing_pid;
    if (existing_pid_file >> existing_pid) {
      existing_pid_file.close();
      if (kill(existing_pid, 0) == 0) {
        std::cerr << "dftracer_service is already running on " << host
                   << " (PID " << existing_pid << ") for " << log_dir
                   << " — refusing to start a second instance. Run `stop` "
                      "first."
                   << std::endl;
        return 1;
      }
      // PID file is stale (process no longer alive) — safe to remove and
      // continue.
      std::remove(pid_file_path.c_str());
    } else {
      existing_pid_file.close();
    }

    // 2. Clean up any rogue/untracked processes for this log_dir on this
    //    node before starting a new one — e.g. daemons left behind by a
    //    prior `start` whose `stop` was skipped, or whose PID file got
    //    overwritten by a since-removed later start.
    pid_t self_pid = getpid();
    auto rogue = find_other_start_pids(binary_name, log_dir, self_pid);
    if (!rogue.empty()) {
      for (pid_t pid : rogue) {
        std::cerr << "Found rogue " << binary_name << " process (PID " << pid
                   << ") for " << log_dir << " on " << host
                   << " — sending SIGINT." << std::endl;
        kill(pid, SIGINT);
      }
      // Give them a moment to shut down gracefully (flush trace buffer)
      // before force-killing any stragglers.
      for (int i = 0; i < 50 && !rogue.empty(); ++i) {
        usleep(100000);
        rogue.erase(std::remove_if(rogue.begin(), rogue.end(),
                                    [](pid_t pid) { return kill(pid, 0) != 0; }),
                    rogue.end());
      }
      for (pid_t pid : rogue) {
        std::cerr << "Rogue process " << pid
                   << " did not exit within 5s; sending SIGKILL."
                   << std::endl;
        kill(pid, SIGKILL);
      }
    }

    auto conf =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    auto libuv_threads = std::to_string(conf->libuv_thread_count);
    setenv("UV_THREADPOOL_SIZE", libuv_threads.c_str(), 1);

    // Start the server as a daemon
    daemonize();

    // Redirect stdout and stderr to log files (truncate on running)
    freopen(out_log_path.c_str(), "w", stdout);
    freopen(err_log_path.c_str(), "w", stderr);

    // Write the server's PID to a file for later reference
    std::ofstream pid_file(pid_file_path);
    pid_file << getpid();
    pid_file.close();

    // Create and start the DFTracerService server. start() blocks until SIGINT.
    auto server = dftracer::DFTracerService();
    server.start();

    // Remove the PID file
    std::remove(pid_file_path.c_str());

    return 0;
  } else if (cmd == "stop") {
    // Stop the running server by sending SIGINT to its PID
    std::ifstream pid_file(pid_file_path);
    pid_t pid;
    if (!(pid_file >> pid)) {
      std::cerr << "No running server found." << std::endl;
      return 1;
    }
    pid_file.close();

    // Send SIGINT to the server process
    if (kill(pid, SIGINT) == 0) {
      std::cout << "Sent SIGINT to server (PID " << pid << ")." << std::endl;
      std::remove(pid_file_path.c_str());
    } else {
      std::cerr << "Failed to send SIGINT to server." << std::endl;
      return 1;
    }
    return 0;
  } else {
    // Unknown command
    std::cerr << "Unknown command: " << cmd << std::endl;
    return 1;
  }
}