// df_gettid() asks the kernel once per thread and keeps the id; a fork()ed
// child must get its own id, not the parent's kept one.
#include <dftracer/core/utils/posix_bypass.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <thread>

#include "check.h"

static ThreadID kernel_tid() {
  return static_cast<ThreadID>(syscall(SYS_gettid));
}

int main() {
  // Twice: the second call comes from the kept id.
  DFT_CHECK(df_gettid() == kernel_tid());
  DFT_CHECK(df_gettid() == kernel_tid());

  ThreadID main_tid = df_gettid(), other = 0;
  bool other_ok = false;
  std::thread t([&]() {
    other = df_gettid();
    other_ok = other == kernel_tid() && df_gettid() == other;
  });
  t.join();
  DFT_CHECK(other_ok);
  DFT_CHECK(other != main_tid);

  pid_t child = fork();
  if (child == 0) _exit(df_gettid() == kernel_tid() ? 0 : 1);
  int status = 0;
  DFT_CHECK(waitpid(child, &status, 0) == child);
  DFT_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  DFT_CHECK(df_gettid() == main_tid);

  // A fresh thread, so the vfork child is the first to fill the shared cache.
  bool vfork_ok = false;
  std::thread v([&]() {
    pid_t pid = vfork();
    if (pid == 0) {
      df_gettid();
      _exit(0);
    }
    waitpid(pid, nullptr, 0);
    vfork_ok = df_gettid() == kernel_tid();
  });
  v.join();
  DFT_CHECK(vfork_ok);
  std::cout << "✓ df_gettid keeps the id per thread and renews it after fork"
            << std::endl;
  return 0;
}
