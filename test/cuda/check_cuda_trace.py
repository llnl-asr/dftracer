#!/usr/bin/env python3
"""Verify a DFTracer trace produced with CUDA (CUPTI) tracing enabled.

Checks three things:

1. CUDA events are present at all, and cover the expected categories
   (runtime API, memory copies, ...).
2. Every CUDA event carries the ``TRACE_TYPE_CUDA`` type id, so downstream
   tooling can filter on it.
3. **Clock alignment** -- the CUPTI-sourced events fall inside the
   ``CUDA_WORKLOAD`` region that the application logged through DFTracer's own
   clock.  CUPTI timestamps come off a different time base than
   ``DFTLogger::get_time()``, so this is the assertion that proves the
   backend's rebasing works and that GPU and CPU events share one timeline.

Each trace file is checked independently against its *own* region: DFTracer
keeps one file per process and traces accumulate across runs, so events from
one run must never be compared against another run's region.

Usage: check_cuda_trace.py <trace-file-glob>

If no trace file exists at all the workload was skipped (no NVIDIA device) and
this exits with the CTest skip code. If a trace does exist, it must contain
CUDA events -- an empty one means the CUPTI backend produced nothing.
"""

import argparse
import glob
import gzip
import json
import sys

# Must match TraceEventType::TRACE_TYPE_CUDA in
# include/dftracer/core/common/enumeration.h
TRACE_TYPE_CUDA = 14

REGION_NAME = "CUDA_WORKLOAD"

# CTest SKIP_RETURN_CODE, kept in sync with test/CMakeLists.txt.
SKIP_EXIT_CODE = 77

# A traced runtime workload must at minimum produce these.
REQUIRED_CATEGORIES = ("CUDA_RUNTIME_API", "CUDA_MEMCPY")

# CUPTI flushes some records (notably its own OVERHEAD accounting, emitted when
# the activity buffers are drained at finalize) after the workload region has
# closed, so only the GPU-work categories are required to fall inside it.
ALIGNED_CATEGORIES = {
    "CUDA_RUNTIME_API",
    "CUDA_MEMCPY",
    "CUDA_MEMCPY_P2P",
    "CUDA_MEMSET",
    "CUDA_KERNEL",
    "CUDA_SYNC",
}


def load_events(path):
    """Read DFTracer line-delimited JSON events from one trace file."""
    events = []
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", errors="replace") as handle:
        for line in handle:
            line = line.strip().rstrip(",")
            # DFTracer traces open with a bare "[" and may carry blank lines.
            if not line or line in ("[", "]"):
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(event, dict):
                events.append(event)
    return events


def check_trace(path):
    """Verify one trace file. Returns (ok, cuda_event_count)."""
    events = load_events(path)
    cuda_events = [e for e in events if e.get("type") == TRACE_TYPE_CUDA]
    print(f"  {path}: {len(events)} events, {len(cuda_events)} CUDA events")

    if not cuda_events:
        return True, 0

    categories = sorted({e.get("cat", "") for e in cuda_events})
    print(f"    categories: {categories}")

    for required in REQUIRED_CATEGORIES:
        if required not in categories:
            print(
                f"ERROR: expected category {required} missing from {categories}",
                file=sys.stderr,
            )
            return False, len(cuda_events)

    for event in cuda_events:
        if not event.get("name"):
            print(f"ERROR: CUDA event without a name: {event}", file=sys.stderr)
            return False, len(cuda_events)
        if event.get("ts", -1) < 0 or event.get("dur", -1) < 0:
            print(
                f"ERROR: CUDA event with negative ts/dur: {event}", file=sys.stderr
            )
            return False, len(cuda_events)

    regions = [e for e in events if e.get("name") == REGION_NAME]
    if not regions:
        print(
            f"ERROR: application region {REGION_NAME!r} not found in {path}; "
            "cannot verify clock alignment",
            file=sys.stderr,
        )
        return False, len(cuda_events)

    region = regions[0]
    region_start = region["ts"]
    region_end = region["ts"] + region.get("dur", 0)
    print(f"    region {REGION_NAME}: ts={region_start} dur={region.get('dur')}")

    checked = 0
    for event in cuda_events:
        if event.get("cat") not in ALIGNED_CATEGORIES:
            continue
        start = event["ts"]
        end = start + event.get("dur", 0)
        # A tolerance would hide exactly the bug this test exists to catch, so
        # the window is exact: a wrong time base is off by seconds-to-years,
        # never by a rounding unit.
        if start < region_start or end > region_end:
            print(
                "ERROR: CUDA event outside the application region -- CUPTI "
                "timestamps are not on the DFTracer clock.\n"
                f"  file:   {path}\n"
                f"  event:  name={event.get('name')} cat={event.get('cat')} "
                f"ts={start} end={end}\n"
                f"  region: [{region_start}, {region_end}]",
                file=sys.stderr,
            )
            return False, len(cuda_events)
        checked += 1

    print(f"    clock alignment verified for {checked} CUDA events")
    return True, len(cuda_events)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pattern", help="glob for the trace file(s)")
    args = parser.parse_args()

    paths = sorted(glob.glob(args.pattern))
    if not paths:
        # No trace at all means the workload never ran -- normally because the
        # machine has no NVIDIA device and test_cuda exited with the skip code.
        # Report the same skip rather than a failure.
        print(f"No trace files matched {args.pattern!r}; workload was skipped.")
        return SKIP_EXIT_CODE

    print(f"Checking {len(paths)} trace file(s)")
    total_cuda = 0
    for path in paths:
        ok, count = check_trace(path)
        if not ok:
            return 1
        total_cuda += count

    if total_cuda == 0:
        # Traces exist, so the workload ran; no CUDA events means the CUPTI
        # backend produced nothing, which is a failure, not a skip.
        print(
            "ERROR: traces are present but contain no CUDA events -- the CUPTI "
            "backend produced nothing",
            file=sys.stderr,
        )
        return 1

    print(f"OK ({total_cuda} CUDA events across {len(paths)} file(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
