#!/usr/bin/env python3
"""Fail unless the traces in a directory hold every named call of one layer.

Usage: check_trace_calls.py <trace dir> <type> <call> [<call>...]
<type> is a TraceEventType value from include/dftracer/core/common/enumeration.h
(append-only, so a stable on-disk contract): 5 is HDF5, 10 is MPI and MPI-IO.
"""
import glob
import gzip
import json
import os
import sys

trace_dir, layer, wanted = sys.argv[1], int(sys.argv[2]), set(sys.argv[3:])
files = sorted(set(glob.glob(os.path.join(trace_dir, "**", "*.pfw*"), recursive=True)))
if not files:
    sys.exit(f"FAIL: no trace files under {trace_dir}")
seen = set()
for path in files:
    with (gzip.open if path.endswith(".gz") else open)(path, "rt") as fh:
        for line in fh:
            line = line.strip().rstrip(",")
            if not line.startswith("{"):
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if event.get("type") == layer:
                seen.add(event.get("name", ""))
print(f"{len(files)} trace file(s); type {layer} calls: {sorted(seen)}")
missing = wanted - seen
if missing:
    sys.exit(f"FAIL: missing {sorted(missing)}")
print("PASS: all of", sorted(wanted))
