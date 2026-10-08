//
// Created by hariharan on 8/8/22.
//

#ifndef DFTRACER_POSIX_H
#define DFTRACER_POSIX_H

#include <brahma/brahma.h>
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/md5.h>
#include <dftracer/core/utils/posix_bypass.h>
#include <dftracer/core/utils/utils.h>
#include <dirent.h>
#include <fcntl.h>
#include <features.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/sendfile.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace brahma {
class POSIXDFTracer : public POSIX {
 private:
  static bool stop_trace;
  static std::shared_ptr<POSIXDFTracer> instance;
  static std::shared_ptr<POSIXDFTracer>& instance_ref() {
    static auto* leaked_instance = new std::shared_ptr<POSIXDFTracer>();
    return *leaked_instance;
  }
  // One slot per descriptor number, allocated once at startup. Slots are
  // zero when the descriptor is not traced, and calloc leaves pages that are
  // never touched unused, so a large table costs address space only.
  size_t max_fd;
  HashType* tracked_fd;

  std::shared_ptr<DFTLogger> logger;
  bool trace_all_files;

  inline HashType is_traced(int fd, const char* func) {
    if (stop_trace) return NO_HASH_DEFAULT;
    if (fd < 0 || static_cast<size_t>(fd) >= max_fd) return NO_HASH_DEFAULT;
    HashType trace = tracked_fd[fd];
    if (trace == NO_HASH_DEFAULT) {
      DFTRACER_LOG_DEBUG(
          "Calling POSIXDFTracer.is_traced for %s and"
          " fd %d trace %d",
          func, fd, trace != NO_HASH_DEFAULT);
    }
    return trace;
  }

  inline HashType is_traced(const char* filename, const char* func) {
    if (stop_trace) return NO_HASH_DEFAULT;
    if (trace_all_files) {
      return logger->hash_and_store(filename, METADATA_NAME_FILE_HASH);
    } else {
      const char* tracefile = is_traced_common(filename, func);
      if (tracefile != nullptr) {
        DFTRACER_LOG_DEBUG(
            "Calling POSIXDFTracer.is_traced with "
            "filename %s for %s trace %d",
            filename, func, tracefile != nullptr);
      }
      return logger->hash_and_store(tracefile, METADATA_NAME_FILE_HASH);
    }
  }

  struct AtRef {
    int dirfd;
    const char* path;
    AtRef(int d, const char* p) : dirfd(d), path(p) {}
  };

  // An AT_FDCWD or absolute path has no directory fd to look up, so those
  // are matched by path like the non-at variants.
  inline HashType is_traced(const AtRef& ref, const char* func) {
    if (ref.path != nullptr && (ref.dirfd == AT_FDCWD || ref.path[0] == '/'))
      return is_traced(ref.path, func);
    return is_traced(ref.dirfd, func);
  }

  inline int dir_fd(DIR* dir) {
    return dir == nullptr ? -1
                          : dftracer::POSIXBypass::get_instance().dirfd(dir);
  }

  inline void trace(int fd, HashType hash) {
    DFTRACER_LOG_DEBUG("Calling POSIXDFTracer.trace for %d and %s", fd, hash);
    if (fd < 0 || static_cast<size_t>(fd) >= max_fd) return;
    tracked_fd[fd] = hash;
  }

  inline void remove_trace(int fd) {
    DFTRACER_LOG_DEBUG("Calling POSIXDFTracer.remove_trace for %d", fd);
    if (fd < 0 || static_cast<size_t>(fd) >= max_fd) return;
    tracked_fd[fd] = NO_HASH_DEFAULT;
  }

 public:
  POSIXDFTracer(bool trace_all, size_t table_size)
      : POSIX(),
        max_fd(table_size),
        tracked_fd(static_cast<HashType*>(calloc(max_fd, sizeof(HashType)))),
        trace_all_files(trace_all) {
    DFTRACER_LOG_DEBUG("POSIX class intercepted");
    if (tracked_fd == nullptr) {
      DFTRACER_LOG_ERROR("unable to allocate the descriptor table of %zu",
                         max_fd);
      max_fd = 0;
    }
    logger = DFT_LOGGER_INIT();
  }
  // File hash of a traced fd, or NO_HASH_DEFAULT; lets STDIO follow fdopen().
  HashType fd_hash(int fd) { return is_traced(fd, __FUNCTION__); }

  void finalize() {
    if (stop_trace) return;
    DFTRACER_LOG_DEBUG("Finalizing POSIXDFTracer");
    stop_trace = true;
  }
  POSIXDFTracer(const POSIXDFTracer&) = delete;
  POSIXDFTracer& operator=(const POSIXDFTracer&) = delete;
  ~POSIXDFTracer() { free(tracked_fd); }
  static std::shared_ptr<POSIXDFTracer> get_instance(
      bool trace_all = false, size_t max_fd = DFT_DEFAULT_MAX_FD) {
    DFTRACER_LOG_DEBUG("POSIX class get_instance");
    auto& instance = instance_ref();
    if (!stop_trace && instance == nullptr) {
      instance = std::make_shared<POSIXDFTracer>(trace_all, max_fd);
      POSIX::set_instance(instance);
    }
    return instance;
  }

  int open(const char* pathname, int flags, ...) override;

  int creat64(const char* path, mode_t mode) override;

  int open64(const char* path, int flags, ...) override;

  int close(int fd) override;

  ssize_t write(int fd, const void* buf, size_t count) override;

  ssize_t read(int fd, void* buf, size_t count) override;

  off_t lseek(int fd, off_t offset, int whence) override;

  off64_t lseek64(int fd, off64_t offset, int whence) override;

  ssize_t pread(int fd, void* buf, size_t count, off_t offset) override;

  ssize_t pread64(int fd, void* buf, size_t count, off64_t offset) override;

  ssize_t pwrite(int fd, const void* buf, size_t count,
                 off64_t offset) override;

  ssize_t pwrite64(int fd, const void* buf, size_t count,
                   off64_t offset) override;

  int fsync(int fd) override;

  int fdatasync(int fd) override;

  int openat(int dirfd, const char* pathname, int flags, ...) override;

  void* mmap(void* addr, size_t length, int prot, int flags, int fd,
             off_t offset) override;

  void* mmap64(void* addr, size_t length, int prot, int flags, int fd,
               off64_t offset) override;

  int __xstat(int vers, const char* path, struct stat* buf) override;

  int __xstat64(int vers, const char* path, struct stat64* buf) override;

  int __lxstat(int vers, const char* path, struct stat* buf) override;

  int __lxstat64(int vers, const char* path, struct stat64* buf) override;

  int __fxstat(int vers, int fd, struct stat* buf) override;

  int __fxstat64(int vers, int fd, struct stat64* buf) override;

  int __fxstatat(int vers, int dirfd, const char* path, struct stat* buf,
                 int flags) override;

  int __fxstatat64(int vers, int dirfd, const char* path, struct stat64* buf,
                   int flags) override;

  int __xmknod(int vers, const char* path, mode_t mode, dev_t* dev) override;

  // _FORTIFY_SOURCE entry points: distro builds call these instead of
  // open/read/pread/readlink/getcwd/realpath.
  int __open_2(const char* path, int flags) override;

  int __open64_2(const char* path, int flags) override;

  int __openat_2(int dirfd, const char* path, int flags) override;

  int __openat64_2(int dirfd, const char* path, int flags) override;

  ssize_t __read_chk(int fd, void* buf, size_t count, size_t buflen) override;

  ssize_t __pread_chk(int fd, void* buf, size_t count, off_t offset,
                      size_t buflen) override;

  ssize_t __pread64_chk(int fd, void* buf, size_t count, off64_t offset,
                        size_t buflen) override;

  ssize_t __readlink_chk(const char* path, char* buf, size_t bufsize,
                         size_t buflen) override;

  ssize_t __readlinkat_chk(int dirfd, const char* path, char* buf,
                           size_t bufsize, size_t buflen) override;

  char* __getcwd_chk(char* buf, size_t size, size_t buflen) override;

  char* __realpath_chk(const char* path, char* resolved,
                       size_t resolvedlen) override;

  int mkdir(const char* pathname, mode_t mode) override;

  int rmdir(const char* pathname) override;

  int chdir(const char* path) override;

  int link(const char* oldpath, const char* newpath) override;

  int linkat(int fd1, const char* path1, int fd2, const char* path2,
             int flag) override;

  int unlink(const char* pathname) override;

  int symlink(const char* path1, const char* path2) override;

  int symlinkat(const char* path1, int fd, const char* path2) override;

  ssize_t readlink(const char* path, char* buf, size_t bufsize) override;

  ssize_t readlinkat(int fd, const char* path, char* buf,
                     size_t bufsize) override;

  int rename(const char* oldpath, const char* newpath) override;

  int chmod(const char* path, mode_t mode) override;

  int chown(const char* path, uid_t owner, gid_t group) override;

  int lchown(const char* path, uid_t owner, gid_t group) override;

  int utime(const char* filename, const utimbuf* buf) override;

  DIR* opendir(const char* name) override;

  int fcntl(int fd, int cmd, ...) override;

  int dup(int oldfd) override;

  int dup2(int oldfd, int newfd) override;

  int mkfifo(const char* pathname, mode_t mode) override;

  mode_t umask(mode_t mask) override;

  int access(const char* path, int amode) override;

  int faccessat(int fd, const char* path, int amode, int flag) override;

  int remove(const char* pathname) override;

  int truncate(const char* pathname, off_t length) override;

  int ftruncate(int fd, off_t length) override;

  int execl(const char* pathname, const char* arg, ...) override;

  int execlp(const char* file, const char* arg, ...) override;

  int execv(const char* pathname, char* const argv[]) override;

  int execvp(const char* file, char* const argv[]) override;

  int execvpe(const char* file, char* const argv[],
              char* const envp[]) override;

  int fork() override;

  void exit(int status) override;

  void _exit(int status) override;

  // --- New overrides mirroring brahma's expanded POSIX interception surface
  // ---

  ssize_t readv(int fd, const struct iovec* iov, int iovcnt) override;

  ssize_t writev(int fd, const struct iovec* iov, int iovcnt) override;

  ssize_t preadv(int fd, const struct iovec* iov, int iovcnt,
                 off_t offset) override;

  ssize_t preadv64(int fd, const struct iovec* iov, int iovcnt,
                   off64_t offset) override;

  ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt,
                  off_t offset) override;

  ssize_t pwritev64(int fd, const struct iovec* iov, int iovcnt,
                    off64_t offset) override;

  int statvfs(const char* path, struct statvfs* buf) override;

  int statvfs64(const char* path, struct statvfs64* buf) override;

  int fstatvfs(int fd, struct statvfs* buf) override;

  int fstatvfs64(int fd, struct statvfs64* buf) override;

  int flock(int fd, int operation) override;

  int execve(const char* pathname, char* const argv[],
             char* const envp[]) override;

  pid_t wait(int* wstatus) override;

  pid_t waitpid(pid_t pid, int* wstatus, int options) override;

  char* realpath(const char* path, char* resolved_path) override;

  int dirfd(DIR* dir) override;

  ssize_t sendfile(int out_fd, int in_fd, off_t* offset, size_t count) override;

  ssize_t sendfile64(int out_fd, int in_fd, off64_t* offset,
                     size_t count) override;

  ssize_t copy_file_range(int fd_in, off64_t* off_in, int fd_out,
                          off64_t* off_out, size_t len,
                          unsigned int flags) override;

  int closedir(DIR* dir) override;

  dirent* readdir(DIR* dir) override;

  dirent64* readdir64(DIR* dir) override;

  void rewinddir(DIR* dir) override;

  int mkdirat(int dirfd, const char* pathname, mode_t mode) override;

  int unlinkat(int dirfd, const char* pathname, int flags) override;

  int renameat(int olddirfd, const char* oldpath, int newdirfd,
               const char* newpath) override;

  int fchmod(int fd, mode_t mode) override;

  int fchmodat(int dirfd, const char* pathname, mode_t mode,
               int flags) override;

  int fchown(int fd, uid_t owner, gid_t group) override;

  int fchownat(int dirfd, const char* pathname, uid_t owner, gid_t group,
               int flags) override;

  int openat64(int dirfd, const char* pathname, int flags, ...) override;

  int fcntl64(int fd, int cmd, ...) override;

  int ftruncate64(int fd, off64_t length) override;

  int truncate64(const char* pathname, off64_t length) override;

  char* getcwd(char* buf, size_t size) override;

  int pipe(int pipefd[2]) override;

  long sysconf(int name) override;

  int munmap(void* addr, size_t len) override;

  int msync(void* addr, size_t len, int flags) override;

  int madvise(void* addr, size_t length, int advice) override;

  int mprotect(void* addr, size_t length, int prot) override;

  int mlock(const void* addr, size_t len) override;

  int munlock(const void* addr, size_t len) override;

  int mlockall(int flags) override;

  int munlockall(void) override;

  int mknod(const char* pathname, mode_t mode, dev_t dev) override;

  int stat(const char* path, struct stat* buf) override;

  int stat64(const char* path, struct stat64* buf) override;

  int lstat(const char* path, struct stat* buf) override;

  int lstat64(const char* path, struct stat64* buf) override;

  int fstat(int fd, struct stat* buf) override;

  int fstat64(int fd, struct stat64* buf) override;

  int fstatat(int dirfd, const char* path, struct stat* buf,
              int flags) override;

  int fstatat64(int dirfd, const char* path, struct stat64* buf,
                int flags) override;

  int posix_fadvise(int fd, off_t offset, off_t len, int advice) override;

  int posix_fadvise64(int fd, off64_t offset, off64_t len, int advice) override;

  int posix_fallocate(int fd, off_t offset, off_t len) override;

  int posix_fallocate64(int fd, off64_t offset, off64_t len) override;
};

}  // namespace brahma
#endif  // DFTRACER_POSIX_H
