#!/usr/bin/env python3
"""Validate a trace written by a provenance-style API built on dftracer's
event types (test/c/test_prov_style.c, test/cpp/test_prov_style.cpp and the
external project test/external_types), run with aggregation on:

* LT (entity type) and LE (entity) records are written once per process, LR
  (entity relation) records relate declared entities;
* every activity event (name `activity`) is traced in full -- its type is not
  aggregatable -- on an external layer, and relates only declared entities;
* the app's plain events (name `work`) are still aggregated.

usage: check_prov_style_trace.py <activities per process> "<trace glob>"
"""
import glob
import gzip
import json
import sys

EXTERNAL = 128  # DFTRACER_TRACE_TYPE_EXTERNAL


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def check_process(f, events, activities):
    types, entities, relations, acts, work_complete, work_aggregated = (
        [], {}, [], [], 0, 0)
    for ev in events:
        name, ph, args = ev.get("name"), ev.get("ph"), ev.get("args") or {}
        if name == "LT":
            types.append(args.get("type"))
        elif name == "LE":
            if args.get("id") in entities:
                fail(f"{f}: entity {args.get('id')} written twice")
            entities[args.get("id")] = args
        elif name == "LR":
            relations.append(args)
        elif name == "activity":
            acts.append(ev)
        elif name == "work":
            work_complete += ph == 1
            work_aggregated += ph == 3
    if not types or len(types) != len(set(types)):
        fail(f"{f}: entity types {types}")
    for e in entities.values():
        if e.get("type") not in types:
            fail(f"{f}: entity {e} of an undeclared type")
    for r in relations:
        if r.get("subject") not in entities or r.get("object") not in entities:
            fail(f"{f}: dangling entity relation {r}")
    if len(acts) != activities:
        fail(f"{f}: {len(acts)} activity events, expected {activities} "
             "traced in full")
    for ev in acts:
        if ev.get("ph") != 1 or ev.get("cat") != "lineage" or \
                not EXTERNAL <= ev.get("type", 0) <= 255:
            fail(f"{f}: activity is not a complete lineage event: {ev}")
        rel = ev["args"].get("relations")
        if not rel:
            fail(f"{f}: activity without relations: {ev}")
        for ids in rel.values():
            for i in ids:
                if i not in entities:
                    fail(f"{f}: activity relates undeclared entity {i}")
    if work_complete or not work_aggregated:
        fail(f"{f}: work events: {work_complete} complete, "
             f"{work_aggregated} aggregated")
    return len(entities)


def main():
    activities, pattern = int(sys.argv[1]), sys.argv[2]
    files = sorted(glob.glob(pattern))
    if not files:
        fail(f"no trace files match {pattern}")
    for f in files:
        events = []
        for line in gzip.open(f, "rt"):
            line = line.strip().rstrip(",")
            if line.startswith("{"):
                events.append(json.loads(line))
        n = check_process(f, events, activities)
        print(f"OK: {f}: {activities} activities, {n} entities")


if __name__ == "__main__":
    main()
