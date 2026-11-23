#ifndef DFTRACER_IPC_SHM_REGION_H
#define DFTRACER_IPC_SHM_REGION_H

#include <dftracer/core/ipc/shm_layout.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace dftracer {
namespace ipc {

/**
 * A named POSIX shared-memory segment holding one ring, created by the
 * service (create()) or attached to by a traced process (open()). Thin RAII
 * wrapper over Boost.Interprocess: the object owns the mapping, and the
 * creating side also removes the name when it is destroyed.
 */
class ShmRegion {
 public:
  /**
   * Bytes free in the filesystem behind POSIX shared memory (/dev/shm). A
   * container gets 64 MB there by default, a cgroup may cap it too.
   */
  static uint64_t available_bytes() {
    struct statvfs fs;
    if (statvfs("/dev/shm", &fs) != 0) return UINT64_MAX;
    return static_cast<uint64_t>(fs.f_bavail) * fs.f_frsize;
  }

  /**
   * Create and format a new segment, replacing any stale one of the same name
   * left behind by a service that did not shut down cleanly. Only the first
   * `reserve_blocks` blocks are reserved and armed; reserve() does the rest.
   * Producers only touch reserved blocks, so a full /dev/shm fails here or in
   * reserve(), and never as a SIGBUS in a traced process. Throws on failure.
   */
  static std::unique_ptr<ShmRegion> create(
      const std::string& name, uint32_t block_count, uint32_t block_size,
      uint32_t reserve_blocks = UINT32_MAX) {
    namespace bip = boost::interprocess;
    bip::shared_memory_object::remove(name.c_str());
    std::unique_ptr<ShmRegion> region(new ShmRegion(name, true));
    region->object_ = bip::shared_memory_object(bip::create_only, name.c_str(),
                                                bip::read_write);
    const size_t bytes = RingView::bytes_for(block_count, block_size);
    const int fd = region->object_.get_mapping_handle().handle;
    // Not object_.truncate(): Boost reserves the whole size with
    // posix_fallocate, which is what the partial reserve avoids.
    if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
      throw std::runtime_error("cannot size shared memory: " +
                               std::string(std::strerror(errno)));
    }
    const uint32_t reserved =
        reserve_blocks < block_count ? reserve_blocks : block_count;
    const size_t first = RingView::data_offset(block_count) +
                         static_cast<size_t>(reserved) * block_size;
    const int err = posix_fallocate(fd, 0, static_cast<off_t>(first));
    if (err != 0) {
      throw std::runtime_error("cannot reserve " + std::to_string(first >> 20) +
                               " MiB of shared memory: " + std::strerror(err));
    }
    region->region_ = bip::mapped_region(region->object_, bip::read_write);
    region->ring_ = RingView::format(region->region_.get_address(), block_count,
                                     block_size, reserved);
    region->reserved_ = reserved;
    return region;
  }

  /**
   * Attach to an existing segment. Returns nullptr if it does not exist, cannot
   * be opened, or does not carry this library's magic and ABI version.
   */
  static std::unique_ptr<ShmRegion> open(const std::string& name) {
    namespace bip = boost::interprocess;
    try {
      std::unique_ptr<ShmRegion> region(new ShmRegion(name, false));
      region->object_ = bip::shared_memory_object(bip::open_only, name.c_str(),
                                                  bip::read_write);
      region->region_ = bip::mapped_region(region->object_, bip::read_write);
      if (region->region_.get_size() < sizeof(SegmentHeader)) return nullptr;
      auto* header = static_cast<SegmentHeader*>(region->region_.get_address());
      if (header->magic != DFT_SHM_SEGMENT_MAGIC ||
          header->abi_version != DFT_SHM_ABI_VERSION ||
          !RingView::valid_geometry(header->block_count, header->block_size) ||
          region->region_.get_size() <
              RingView::bytes_for(header->block_count, header->block_size)) {
        return nullptr;
      }
      region->ring_ = RingView(region->region_.get_address());
      return region;
    } catch (const bip::interprocess_exception&) {
      return nullptr;
    }
  }

  ~ShmRegion() {
    if (owner_) {
      boost::interprocess::shared_memory_object::remove(name_.c_str());
    }
  }

  ShmRegion(const ShmRegion&) = delete;
  ShmRegion& operator=(const ShmRegion&) = delete;

  const RingView& ring() const { return ring_; }
  const std::string& name() const { return name_; }
  size_t size() const { return region_.get_size(); }

  /** Blocks reserved and armed for the first lap (creating side only). */
  uint32_t reserved_blocks() const { return reserved_; }

  /**
   * Reserve the shared memory of the blocks below `target` and arm them for
   * their first lap. Creating side only, from one thread. False, with
   * nothing armed, when /dev/shm cannot back them.
   */
  bool reserve(uint32_t target) {
    const uint32_t count = ring_.block_count();
    if (target > count) target = count;
    if (target <= reserved_) return true;
    const size_t block = ring_.block_size();
    const size_t from =
        RingView::data_offset(count) + static_cast<size_t>(reserved_) * block;
    const size_t length = static_cast<size_t>(target - reserved_) * block;
    if (posix_fallocate(object_.get_mapping_handle().handle,
                        static_cast<off_t>(from),
                        static_cast<off_t>(length)) != 0)
      return false;
    for (uint32_t i = reserved_; i < target; ++i) ring_.arm_first_lap(i);
    reserved_ = target;
    return true;
  }

 private:
  ShmRegion(std::string name, bool owner)
      : name_(std::move(name)), owner_(owner) {}

  std::string name_;
  bool owner_;
  boost::interprocess::shared_memory_object object_;
  boost::interprocess::mapped_region region_;
  RingView ring_;
  uint32_t reserved_ = 0;
};

}  // namespace ipc
}  // namespace dftracer

#endif  // DFTRACER_IPC_SHM_REGION_H
