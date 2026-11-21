#ifndef DFTRACER_COMMON_TRACER_SCOPE_H
#define DFTRACER_COMMON_TRACER_SCOPE_H

namespace dftracer {

inline thread_local int tl_tracer_depth = 0;

/**
 * @brief Marks the calling thread as running dftracer's own code while it
 * lives.
 *
 * An interceptor called meanwhile does not trace its call. dftracer
 * allocates while it records an event, and an application's malloc can call
 * traced functions (MVAPICH2's calls sysconf when it makes an arena), so
 * tracing those would record another event inside this one, recursing until
 * the stack overflows or the thread waits on a lock it holds. Nests.
 */
class TracerScope {
 public:
  TracerScope() : TracerScope(tl_tracer_depth) {}
  /** With the calling thread's tl_tracer_depth, looked up once by a caller. */
  explicit TracerScope(int& depth) : depth_(depth) { ++depth_; }
  ~TracerScope() { --depth_; }
  TracerScope(const TracerScope&) = delete;
  TracerScope& operator=(const TracerScope&) = delete;

 private:
  int& depth_;
};

/** True while the calling thread runs dftracer's own code (TracerScope). */
inline bool in_tracer() { return tl_tracer_depth != 0; }

}  // namespace dftracer

#endif  // DFTRACER_COMMON_TRACER_SCOPE_H
