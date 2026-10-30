#!/usr/bin/env python3
"""Validate the traces of test/c/test_event_types.c and
test/cpp/test_event_types.cpp, run with aggregation on: events of a type that
is not aggregatable are all traced in full, the app's own events are still
aggregated, and a metadata type does not change its layer.

Each trace file (one process) is checked on its own.

usage: check_event_types_trace.py "<trace glob>"
"""
import glob
import gzip
import json
import sys

EXTERNAL = 128  # DFTRACER_TRACE_TYPE_EXTERNAL


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def check_file(f):
    steps, app_complete, app_aggregated, notes = [], 0, 0, []
    for line in gzip.open(f, "rt"):
        line = line.strip().rstrip(",")
        if not line.startswith("{"):
            continue
        ev = json.loads(line)
        name, ph = ev.get("name"), ev.get("ph")
        if name == "step":
            steps.append(ev)
        elif name == "app" and ph == 1:
            app_complete += 1
        elif name == "app" and ph == 3:
            app_aggregated += 1
        elif name == "NOTE":
            notes.append(ev)
    if len(steps) != 10:
        fail(f"{f}: {len(steps)} step events, expected 10 traced in full")
    for ev in steps:
        if ev.get("ph") != 1 or ev.get("type") != EXTERNAL:
            fail(f"{f}: step not a complete event of layer {EXTERNAL}: {ev}")
        if ev.get("cat") != "lineage":
            fail(f"{f}: step category {ev.get('cat')}")
        i = ev["args"].get("i")
        want = {"used": [f"{0xabcdef + i:016x}"]}
        if ev["args"].get("relations") != want:
            fail(f"{f}: step {i} relations {ev['args'].get('relations')} "
                 f"!= {want}")
    if app_complete or not app_aggregated:
        fail(f"{f}: app events: {app_complete} complete, "
             f"{app_aggregated} aggregated")
    if len(notes) != 1 or notes[0].get("ph") != 4 or \
            notes[0]["args"].get("text") != "hello":
        fail(f"{f}: NOTE record {notes}")
    print(f"OK: {f}: 10 steps, app aggregated")


def main():
    files = sorted(glob.glob(sys.argv[1]))
    if not files:
        fail(f"no trace files match {sys.argv[1]}")
    for f in files:
        check_file(f)


if __name__ == "__main__":
    main()
