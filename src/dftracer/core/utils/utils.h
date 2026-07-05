//
// Created by haridev on 8/23/23.
//

#ifndef DFTRACER_UTILS_H
#define DFTRACER_UTILS_H

#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/utils/posix_bypass.h>
#include <execinfo.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>

#include <any>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

void dft_finalize(bool force = false);

// Shared helper: dump backtrace to stderr and flush.  Safe to call from
// both signal handlers (uses async-signal-safe backtrace_symbols_fd) and
// from C++ terminate handlers.
inline void dftracer_dump_stack() {  // GCOVR_EXCL_START
  const int STACK_SIZE = 40;
  void* buffer[STACK_SIZE];
  int nptrs = backtrace(buffer, STACK_SIZE);
  const char hdr[] = "=== DFTRACER STACK TRACE ===\n";
  (void)write(STDERR_FILENO, hdr, sizeof(hdr) - 1);
  backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
  const char ftr[] = "=== END STACK TRACE ===\n";
  (void)write(STDERR_FILENO, ftr, sizeof(ftr) - 1);
  // fsync so the FD buffer is flushed to the OS before we die
  (void)fsync(STDERR_FILENO);
}  // GCOVR_EXCL_STOP

// C++ terminate handler: catches any unhandled exception, prints its
// message (if available) and a stack trace, then exits.
inline void dftracer_terminate_handler() {  // GCOVR_EXCL_START
  const char msg[] = "[DFTRACER] unhandled exception — dumping stack trace\n";
  (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
  // Try to extract the exception message
  try {
    auto eptr = std::current_exception();
    if (eptr) std::rethrow_exception(eptr);
  } catch (const std::exception& e) {
    const char* what = e.what();
    const char pfx[] = "[DFTRACER] exception: ";
    (void)write(STDERR_FILENO, pfx, sizeof(pfx) - 1);
    (void)write(STDERR_FILENO, what, strlen(what));
    (void)write(STDERR_FILENO, "\n", 1);
  } catch (...) {
    const char unk[] = "[DFTRACER] exception: (unknown type)\n";
    (void)write(STDERR_FILENO, unk, sizeof(unk) - 1);
  }
  dftracer_dump_stack();
  dft_finalize();
  fflush(stderr);
  _Exit(1);
}  // GCOVR_EXCL_STOP

inline void set_terminate_handler() {  // GCOVR_EXCL_START
  std::set_terminate(dftracer_terminate_handler);
}  // GCOVR_EXCL_STOP

// Re-raise sig with default disposition so the process exits with the
// correct signal status (visible to waitpid/shell $?).
inline void dftracer_reraise(int sig) {  // GCOVR_EXCL_START
  struct sigaction sa_dfl;
  sa_dfl.sa_handler = SIG_DFL;
  sigemptyset(&sa_dfl.sa_mask);
  sa_dfl.sa_flags = 0;
  sigaction(sig, &sa_dfl, nullptr);
  fflush(stderr);
  raise(sig);
  // If raise somehow returns, fall back to _Exit so we always die.
  _Exit(128 + sig);
}  // GCOVR_EXCL_STOP

inline void signal_handler(int sig) {  // GCOVR_EXCL_START
  DFTRACER_LOG_DEBUG("signal_handler");
  switch (sig) {
    case SIGINT:
    case SIGTERM: {
      DFTRACER_LOG_ERROR("signal caught %d", sig);
      dft_finalize();
      dftracer_reraise(sig);
      break;
    }
    default: {
      DFTRACER_LOG_ERROR("signal caught %d", sig);
      // Capture and emit stack trace BEFORE finalize: if the heap is
      // corrupted (e.g. SIGABRT from free/malloc), dft_finalize() may
      // re-abort and we would never reach backtrace() otherwise.
      dftracer_dump_stack();
      dft_finalize();
      dftracer_reraise(sig);
    }
  }
}  // GCOVR_EXCL_STOP

inline void signal_handler_simple(int sig) {  // GCOVR_EXCL_START
  DFTRACER_LOG_DEBUG("signal_handler");
  DFTRACER_LOG_INFO("signal caught %d", sig);
  dft_finalize();
  dftracer_reraise(sig);
}

inline void set_signal(bool debug_symbols = true) {
  DFTRACER_LOG_DEBUG("set_signal");
  set_terminate_handler();
  struct sigaction sa;
  if (debug_symbols)
    sa.sa_handler = signal_handler;
  else
    sa.sa_handler = signal_handler_simple;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGUSR1, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
  sigaction(SIGHUP, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
}  // GCOVR_EXCL_STOP

class Trie {
 private:
  // create structure of TrieNode
  static const int MAX_INDEX = 256;
  struct TrieNode {
    bool end;
    TrieNode* child[MAX_INDEX];
    TrieNode() {
      DFTRACER_LOG_DEBUG("TrieNode.TrieNode");
      end = false;
      for (int i = 0; i < MAX_INDEX; i++) {
        child[i] = nullptr;
      }
    }
  };
  TrieNode* inclusion_prefix;
  TrieNode* exclusion_prefix;

  void insert(TrieNode* root, const char* word, unsigned long n,
              bool reverse = false) {
    DFTRACER_LOG_DEBUG("Trie.insert inserting string %s with size %d", word, n);
    TrieNode* curr = root;
    unsigned long start = 0, end = n, inc = 1;
    if (reverse) start = n - 1, end = -1, inc = -1;
    for (unsigned long i = start; i != end; i += inc) {
      int idx = get_id(word[i]);
      if (curr->child[idx] == nullptr) {
        curr->child[idx] = new TrieNode();
      }
      curr = curr->child[idx];
    }
    curr->end = true;
  }
  bool startsWith(TrieNode* root, const char* prefix, unsigned long n,
                  bool reverse = false) {
    DFTRACER_LOG_DEBUG("Trie.startsWith");
    TrieNode* curr = root;
    if (curr == nullptr || curr->end) return false;
    unsigned long start = 0, end = n, inc = 1;
    if (reverse) start = n - 1, end = -1, inc = -1;
    for (unsigned long i = start; i != end; i += inc) {
      int idx = get_id(prefix[i]);
      if (curr->child[idx] == nullptr) return curr->end;
      curr = curr->child[idx];
    }
    return curr->end;
  }

 public:
  Trie() {
    DFTRACER_LOG_DEBUG("Trie.Trie We have %d child in prefix tree", MAX_INDEX);
    inclusion_prefix = new TrieNode();
    exclusion_prefix = new TrieNode();
  }

  inline int get_id(char c) {
    const auto uc = static_cast<unsigned char>(c);
    DFTRACER_LOG_DEBUG("Trie.get_id for %u", static_cast<unsigned int>(uc));
    return static_cast<int>(uc);
  }

  void include(const char* word, unsigned long n) {
    DFTRACER_LOG_DEBUG("Trie.include");
    if (inclusion_prefix == nullptr) return;
    insert(inclusion_prefix, word, n, false);
  }
  void exclude(const char* word, unsigned long n) {
    DFTRACER_LOG_DEBUG("Trie.exclude");
    if (exclusion_prefix == nullptr) return;
    insert(exclusion_prefix, word, n, false);
  }
  void include_reverse(const char* word, unsigned long n) {
    DFTRACER_LOG_DEBUG("Trie.include_reverse");
    if (inclusion_prefix == nullptr) return;
    insert(inclusion_prefix, word, n, true);
  }
  void exclude_reverse(const char* word, unsigned long n) {
    DFTRACER_LOG_DEBUG("Trie.exclude_reverse");
    if (exclusion_prefix == nullptr) return;
    insert(exclusion_prefix, word, n, true);
  }
  bool is_included(const char* word, unsigned long n, bool reverse = false) {
    DFTRACER_LOG_DEBUG("Trie.is_included");
    if (inclusion_prefix == nullptr) return false;
    return startsWith(inclusion_prefix, word, n, reverse);
  }
  bool is_excluded(const char* word, unsigned long n, bool reverse = false) {
    DFTRACER_LOG_DEBUG("Trie.is_excluded");
    if (exclusion_prefix == nullptr) return false;
    return startsWith(exclusion_prefix, word, n, reverse);
  }
  void finalize_root(TrieNode* node) {
    DFTRACER_LOG_DEBUG("Trie.finalize_root");
    if (node != nullptr) {
      for (unsigned long i = 0; i < MAX_INDEX; i++) {
        if (node->child[i] != nullptr) {
          finalize_root(node->child[i]);
          node->child[i] = nullptr;
        }
      }
      delete (node);
    }
  }

  void finalize() {
    DFTRACER_LOG_DEBUG("Finalizing Trie");
    if (inclusion_prefix != nullptr) {
      finalize_root(inclusion_prefix);
      inclusion_prefix = nullptr;
    }
    if (exclusion_prefix != nullptr) {
      finalize_root(exclusion_prefix);
      exclusion_prefix = nullptr;
    }
  }
};

const int MAX_PREFIX = 128;
const int MAX_EXT = 4;

inline std::vector<std::string> split(std::string str, char delimiter) {
  DFTRACER_LOG_DEBUG("split");
  std::vector<std::string> res;
  if (str.find(delimiter) == std::string::npos) {
    res.push_back(str);
  } else {
    size_t first;
    size_t last = 0;
    while ((first = str.find_first_not_of(delimiter, last)) !=
           std::string::npos) {
      last = str.find(delimiter, first);
      res.push_back(str.substr(first, last - first));
    }
  }
  return res;
}

inline std::string get_filename(int fd) {
  DFTRACER_LOG_DEBUG("get_filename");
  char proclnk[PATH_MAX];
  char filename[PATH_MAX];
  snprintf(proclnk, PATH_MAX, "/proc/self/fd/%d", fd);
  size_t r = dftracer::POSIXBypass::get_instance().readlink(proclnk, filename,
                                                            PATH_MAX);
  filename[r] = '\0';
  return filename;
}

inline const char* is_traced_common(const char* filename, const char* func) {
  DFTRACER_LOG_DEBUG("is_traced_common");
  auto tri_ptr = dftracer::Singleton<Trie>::get_instance();
  if (tri_ptr == nullptr) return nullptr;
  auto file_len = strlen(filename);
  if (file_len == 0) return nullptr;
  if (tri_ptr->is_excluded(filename, file_len, true)) return nullptr;
  bool is_traced = tri_ptr->is_included(filename, file_len);
  if (!is_traced) {
    DFTRACER_LOG_DEBUG(
        "Profiler Intercepted POSIX not tracing file %s for func %s", filename,
        func);
    return nullptr;
  }
  DFTRACER_LOG_INFO("Profiler Intercepted POSIX tracing file %s for func %s",
                    filename, func);
  return filename;
}

template <typename T>
auto any_cast_and_apply(const std::any& value) -> std::optional<T> {
  if (value.type() == typeid(T)) {
    return std::any_cast<T>(value);
  }
  return std::nullopt;
}

#endif  // DFTRACER_UTILS_H
