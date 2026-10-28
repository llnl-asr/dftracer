#ifndef DFTRACER_SERVICE_PIPELINE_CLIENT_TABLE_H
#define DFTRACER_SERVICE_PIPELINE_CLIENT_TABLE_H

#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/buffer/local_buffer_manager.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/service/common/datastructure.h>
#include <signal.h>

#include <cerrno>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace dftracer {

/**
 * The client processes the service has seen, and their lanes (one per
 * producer thread). Shared by every drain thread. Lock order: a lane's mutex,
 * then its process's, then the table's -- never the other way round.
 */
class ClientTable {
 public:
  explicit ClientTable(std::string hostname_hash)
      : hostname_hash_(std::move(hostname_hash)) {}

  /** Process `pid`, created on first sight. */
  std::shared_ptr<ClientProcess> process(uint32_t pid, uint64_t now_ns) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto& entry = processes_[pid];
    if (entry == nullptr) {
      entry = std::make_shared<ClientProcess>();
      entry->pid = pid;
    }
    entry->last_seen_ns.store(now_ns, std::memory_order_relaxed);
    return entry;
  }

  /** Lane of producer thread `tid` of `process`, created on first sight. */
  std::shared_ptr<ClientLane> lane(ClientProcess& process, uint32_t tid) {
    std::lock_guard<std::mutex> lock(process.mtx);
    auto& entry = process.lanes[tid];
    if (entry == nullptr) {
      entry = std::make_shared<ClientLane>();
      entry->serializer = std::make_shared<JsonLines>();
      entry->buffer = std::make_shared<LocalBufferManager>(
          std::make_shared<Aggregator>(), entry->serializer);
      entry->buffer->initialize(nullptr,
                                const_cast<HashType>(hostname_hash_.c_str()));
    }
    return entry;
  }

  /**
   * Bring `lane` up to date with its process's app name and rank. Call with
   * the lane's mutex held.
   */
  static void sync(ClientProcess& process, ClientLane& lane) {
    std::lock_guard<std::mutex> lock(process.mtx);
    if (lane.synced == process.version) return;
    lane.buffer->set_app_name(process.app_name.c_str());
    lane.buffer->set_rank(process.rank);
    lane.synced = process.version;
  }

  /**
   * Remove and return the processes that are finished: said GOODBYE, or are
   * gone without one, more than `grace_ns` ago. The grace lets the blocks of
   * their other threads, published or taken over after the GOODBYE, still
   * reach the same lanes.
   */
  std::vector<std::shared_ptr<ClientProcess>> take_finished(uint64_t now_ns,
                                                            uint64_t grace_ns) {
    std::vector<std::shared_ptr<ClientProcess>> done;
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto it = processes_.begin(); it != processes_.end();) {
      ClientProcess& p = *it->second;
      const uint64_t seen = p.last_seen_ns.load(std::memory_order_relaxed);
      bool finished;
      {
        std::lock_guard<std::mutex> plock(p.mtx);
        finished = p.said_goodbye ? now_ns - p.goodbye_ns > grace_ns
                                  : now_ns - seen > grace_ns && !alive(p.pid);
      }
      if (finished) {
        done.push_back(std::move(it->second));
        it = processes_.erase(it);
      } else {
        ++it;
      }
    }
    return done;
  }

  /** Remove and return every process (service shutdown). */
  std::vector<std::shared_ptr<ClientProcess>> take_all() {
    std::vector<std::shared_ptr<ClientProcess>> all;
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& entry : processes_) all.push_back(std::move(entry.second));
    processes_.clear();
    return all;
  }

 private:
  static bool alive(uint32_t pid) {
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH;
  }

  std::string hostname_hash_;
  std::mutex mtx_;
  std::unordered_map<uint32_t, std::shared_ptr<ClientProcess>> processes_;
};

}  // namespace dftracer

#endif  // DFTRACER_SERVICE_PIPELINE_CLIENT_TABLE_H
