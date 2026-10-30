#include <dftracer/service/service.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
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

// The hostname the node trace is named with (DFTracerService), so a node's
// pid and log files sit next to its trace. It also keeps the daemons of a
// multi-node job, sharing one log_file prefix on a network filesystem, from
// overwriting each other's pid file.
std::string hostname() {
  char name[256] = {0};
  if (gethostname(name, sizeof(name) - 1) != 0) return "unknown";
  return name;
}

// The content of a /proc file, or "" when unreadable. A process can exit at any
// moment of a scan, and a read of its file then fails with ESRCH, which
// std::filebuf turns into an exception out of istreambuf_iterator: a process
// exiting anywhere on the node must not take the service down with it.
std::string read_proc_file(const std::string& path) {
  std::string content;
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return content;
  char block[4096];
  ssize_t n;
  while ((n = read(fd, block, sizeof(block))) > 0)
    content.append(block, static_cast<size_t>(n));
  close(fd);
  return content;
}

// The value of `var` in /proc/<pid>/environ, or "" when absent or unreadable.
std::string environ_of(const char* pid, const std::string& var) {
  const std::string environ_text =
      read_proc_file("/proc/" + std::string(pid) + "/environ");
  size_t start = 0;
  while (start < environ_text.size()) {
    size_t end = environ_text.find('\0', start);
    if (end == std::string::npos) end = environ_text.size();
    if (end - start > var.size() &&
        environ_text.compare(start, var.size(), var) == 0 &&
        environ_text[start + var.size()] == '=')
      return environ_text.substr(start + var.size() + 1,
                                 end - start - var.size() - 1);
    start = end + 1;
  }
  return "";
}

// Finds PIDs of other "<binary_name> start" processes on this node with the
// same DFTRACER_LOG_FILE, excluding `self_pid`. Used to catch daemons that
// leaked because a previous `stop` was never called (or used a
// mismatched/legacy pid file path) — they are otherwise invisible since they
// hold no valid PID-file reference once a later start overwrites it.
//
// Scans /proc directly rather than shelling out to `pgrep -f`. The
// popen()-based version ran `sh -c "pgrep -f '<binary_name> start
// <dir>' "`, and that wrapper shell's own argv literally contains the
// search pattern as a substring — `pgrep -f` matched the wrapper itself,
// treating it as a "rogue" process distinct from `self_pid`. Killing that
// transient, about-to-exit PID (SIGINT, escalating to SIGKILL) risked
// hitting an unrelated process if the PID was recycled before the signal
// was delivered — this manifested as node/job-shell processes elsewhere in
// the job getting killed and taking down the whole allocation. Reading
// /proc/<pid>/cmdline for real dftracer_service processes avoids ever
// spawning a matching shell process in the first place.
std::vector<pid_t> find_other_start_pids(const std::string& binary_name,
                                         const std::string& log_file,
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

    const std::string cmdline =
        read_proc_file("/proc/" + std::string(name) + "/cmdline");
    if (cmdline.empty()) continue;

    // cmdline is NUL-separated argv; split it out instead of doing a raw
    // substring match so that some unrelated process whose argv merely
    // contains these words can never match.
    std::vector<std::string> args;
    size_t start = 0;
    for (size_t i = 0; i <= cmdline.size(); ++i) {
      if (i == cmdline.size() || cmdline[i] == '\0') {
        if (i > start) args.push_back(cmdline.substr(start, i - start));
        start = i + 1;
      }
    }
    if (args.size() < 2) continue;
    if (basename_of(args[0]) != binary_name) continue;
    if (args[1] != "start") continue;
    if (environ_of(name, DFTRACER_LOG_FILE) != log_file) continue;

    pids.push_back(pid);
  }
  closedir(proc);
  return pids;
}
}  // namespace

// How long `start` waits for the daemon to be ready before giving up.
constexpr int kReadyTimeoutMs = 60000;

// Daemonize the process: detach from terminal and run in background.
//
// The invoking process does not return at once: it waits until the daemon
// reports that it is ready (see notify_ready), so `dftracer_service start`
// followed by the application never races the service's set-up -- in
// producer/consumer mode the ring is accepting events by then. It exits 0
// then, or 1 if the daemon died first or did not report in time. Returns the
// daemon's end of that pipe.
int daemonize(const std::string& log_path) {
  int ready[2];
  if (pipe(ready) != 0) exit(EXIT_FAILURE);
  pid_t pid = fork();
  if (pid < 0) exit(EXIT_FAILURE);  // Fork failed
  if (pid > 0) {
    close(ready[1]);
    struct pollfd wait_for = {ready[0], POLLIN, 0};
    char byte = 0;
    if (poll(&wait_for, 1, kReadyTimeoutMs) == 1 &&
        read(ready[0], &byte, 1) == 1 && byte == 1)
      exit(EXIT_SUCCESS);
    std::cerr << "dftracer_service did not start; see " << log_path
              << std::endl;
    exit(EXIT_FAILURE);
  }
  close(ready[0]);

  // Child continues as session leader
  if (setsid() < 0) exit(EXIT_FAILURE);

  pid = fork();
  if (pid < 0) exit(EXIT_FAILURE);  // Second fork failed
  if (pid > 0) exit(EXIT_SUCCESS);  // First child exits

  // Close standard file descriptors
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  return ready[1];
}

// Tell the waiting `start` the service is up, and let go of the pipe.
void notify_ready(int fd) {
  const char byte = 1;
  if (write(fd, &byte, 1) != 1) {
    // `start` gave up already; nothing else to do.
  }
  close(fd);
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <start|stop>\n"
              << "The service writes next to the application's traces: "
                 "<log_file>_<host>.pfw.gz (node trace), .log and .pid, with "
                 "log_file from DFTRACER_LOG_FILE or the YAML configuration."
              << std::endl;
    return 1;
  }

  std::string cmd = argv[1];
  // The same log_file as the application, so its files sit next to the
  // traces. Namespaced by hostname, as the node trace is, so the daemons of
  // a multi-node job sharing one prefix never overwrite each other's files.
  auto conf =
      dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
  // Tracing off (DFTRACER_ENABLE not set to 1, nor enabled in the YAML):
  // there is nothing to collect, so both commands do nothing.
  if (!conf->enable) return 0;
  const std::string log_file = conf->log_file;
  const std::string host = hostname();
  const std::string prefix = log_file + "_" + host;
  std::string pid_file_path = prefix + ".pid";
  std::string log_path = prefix + ".log";

  if (cmd == "start") {
    std::string binary_name = basename_of(argv[0]);

    // 1. Refuse to start a second tracked instance for this node: if the
    //    PID file exists and that PID is alive, this node already has a
    //    running daemon for this log_file.
    std::ifstream existing_pid_file(pid_file_path);
    pid_t existing_pid;
    if (existing_pid_file >> existing_pid) {
      existing_pid_file.close();
      if (kill(existing_pid, 0) == 0) {
        std::cerr << "dftracer_service is already running on " << host
                  << " (PID " << existing_pid << ") for " << log_file
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

    // 2. Clean up any rogue/untracked processes for this log_file on this
    //    node before starting a new one — e.g. daemons left behind by a
    //    prior `start` whose `stop` was skipped, or whose PID file got
    //    overwritten by a since-removed later start.
    pid_t self_pid = getpid();
    auto rogue = find_other_start_pids(binary_name, log_file, self_pid);
    if (!rogue.empty()) {
      for (pid_t pid : rogue) {
        std::cerr << "Found rogue " << binary_name << " process (PID " << pid
                  << ") for " << log_file << " on " << host
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
                  << " did not exit within 5s; sending SIGKILL." << std::endl;
        kill(pid, SIGKILL);
      }
    }

    auto libuv_threads = std::to_string(conf->libuv_thread_count);
    setenv("UV_THREADPOOL_SIZE", libuv_threads.c_str(), 1);

    // Start the server as a daemon
    const int ready_fd = daemonize(log_path);

    // Both streams to one log (truncated on start). Nothing can be reported
    // if this fails: the streams we would report on are the ones being
    // redirected.
    if (freopen(log_path.c_str(), "w", stdout) == nullptr ||
        dup2(fileno(stdout), STDERR_FILENO) < 0) {
      _exit(EXIT_FAILURE);
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // Write the server's PID to a file for later reference
    std::ofstream pid_file(pid_file_path);
    pid_file << getpid();
    pid_file.close();

    // Create and start the DFTracerService server. start() blocks until SIGINT.
    // Constructed means ready: in producer/consumer mode the ring is created
    // and accepting events. A constructor that throws closes the pipe empty,
    // and `start` reports the failure.
    auto server = dftracer::DFTracerService();
    notify_ready(ready_fd);
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