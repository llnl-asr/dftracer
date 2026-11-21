#ifndef DFTRACER_BUFFER_CHUNK_SINK_H
#define DFTRACER_BUFFER_CHUNK_SINK_H

#include <cstddef>

namespace dftracer {
/**
 * Where a LaneBuffer hands a full chunk (see LaneBuffer::attach). The sink
 * compresses and writes the first `len` bytes of `chunk` before returning; the
 * manager then starts the same chunk over from the beginning.
 */
class ChunkSink {
 public:
  virtual ~ChunkSink() = default;
  virtual void flush(char* chunk, size_t len) = 0;
};
}  // namespace dftracer

#endif  // DFTRACER_BUFFER_CHUNK_SINK_H
