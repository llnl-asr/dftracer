#ifndef DFTRACER_TEST_UNIT_EVENT_FEED_H
#define DFTRACER_TEST_UNIT_EVENT_FEED_H

#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/common/event_draft.h>
#include <dftracer/core/ipc/event_codec.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"

inline void ring_decode(std::vector<uint8_t>& bytes,
                        dftracer::ipc::DecodedEvent& out, int index,
                        std::string_view name, std::string_view category,
                        TraceEventType type, TimeResolution ts,
                        TimeResolution dur, const dftracer::EventDraft* args,
                        ProcessID pid, ThreadID tid, uint8_t flags) {
  dftracer::ipc::ByteWriter w(bytes);
  dftracer::ipc::encode_event_entries(w, index, name, category, type, ts, dur,
                                      args, pid, tid, flags);
  DFT_CHECK(dftracer::ipc::decode_event(bytes.data(), bytes.size(), out));
}

// An event as the service decodes it from the ring.
struct Feed {
  const dftracer::ipc::DecodedEvent& make(const char* category,
                                          const char* name, TraceEventType type,
                                          TimeResolution ts, TimeResolution dur,
                                          ProcessID pid, ThreadID tid,
                                          const dftracer::EventDraft* args,
                                          int index = 0) {
    ring_decode(bytes, event, index, name, category, type, ts, dur, args, pid,
                tid,
                SHM_EVENT_INC_METADATA |
                    (args != nullptr ? SHM_EVENT_HAS_METADATA : 0));
    return event;
  }
  std::vector<uint8_t> bytes;
  dftracer::ipc::DecodedEvent event;
};

struct Rows : dftracer::Aggregator::RowSink {
  std::vector<char> buffer = std::vector<char>(1 << 20);
  size_t used = 0;
  char* row() override { return buffer.data() + used; }
  void wrote(size_t size) override { used += size; }
  std::string text() const { return std::string(buffer.data(), used); }
  std::vector<std::string> lines() const {
    std::vector<std::string> out;
    std::string line;
    for (size_t i = 0; i < used; ++i) {
      if (buffer[i] == '\n') {
        out.push_back(line);
        line.clear();
      } else {
        line += buffer[i];
      }
    }
    return out;
  }
};

#endif  // DFTRACER_TEST_UNIT_EVENT_FEED_H
