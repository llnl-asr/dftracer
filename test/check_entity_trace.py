#!/usr/bin/env python3
"""Validate entity/relation records in traces written by test/c/test_entity.c
and test/cpp/test_entity.cpp.

usage: check_entity_trace.py "<trace glob>"
"""
import glob
import gzip
import json
import re
import sys

FILES = []
HEX = re.compile(r"^[0-9a-f]{16}$")
SAFE = re.compile(r"^[A-Za-z0-9_\-.:/@+=,~ ]*$")
# FNV-1a-64("protein_structure" 0x1f "MGYP0001"); pinned in test_entity.cpp
STRUCTURE_ID = "8f0de68c04eea0bf"


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def records(pattern):
    files = sorted(glob.glob(pattern))
    if not files:
        fail(f"no trace files match {pattern}")
    FILES.extend(files)
    for f in files:
        opener = gzip.open if f.endswith(".gz") else open
        with opener(f, "rt") as fh:
            for line in fh:
                line = line.strip().rstrip(",")
                if line.startswith("{"):
                    yield f, json.loads(line)


def main():
    # The expectations below are per process: the glob can match one trace per
    # process, so records are bucketed by pid and each bucket is checked on its
    # own. Every bucket must satisfy the whole contract.
    buckets = {}
    for src, ev in records(sys.argv[1]):
        buckets.setdefault(ev.get("pid"), []).append((src, ev))
    if not buckets:
        fail(f"no records in {', '.join(FILES)}")
    for pid, evs in sorted(buckets.items(), key=lambda kv: str(kv[0])):
        check_process(pid, evs)
    print(f"OK: {len(buckets)} process(es) in {len(FILES)} file(s)")


def check_process(pid, evs):
    entities, types, erels, rel_events = {}, {}, [], []
    seen = {}
    for src, ev in evs:
        args = ev.get("args") or {}
        name = ev.get("name")
        if name == "EH":
            for k in ("id", "type", "store", "uri"):
                if k not in args:
                    fail(f"EH missing {k}: {ev}")
            if not HEX.match(args["id"]) or not isinstance(args["store"], int):
                fail(f"bad EH fields: {args}")
            if not SAFE.match(args["type"]) or not SAFE.match(args["uri"]):
                fail(f"unsanitized EH field: {args}")
            if args["id"] in seen:
                prev_src, prev = seen[args["id"]]
                fail(
                    f"entity declared twice in process {pid}: {args}\n"
                    f"  first  : {prev_src} {prev}\n"
                    f"  second : {src} {ev}\n"
                    f"  files read ({len(FILES)}): " + ", ".join(FILES)
                )
            seen[args["id"]] = (src, ev)
            entities[args["id"]] = args
        elif name == "ET":
            if not isinstance(args.get("role"), int) or not SAFE.match(
                args.get("description", "")
            ):
                fail(f"bad ET fields: {args}")
            types[args["type"]] = args
        elif name == "ER":
            if args.get("relation", 0) < 16 or not HEX.match(args.get("subject", "")):
                fail(f"bad ER fields: {args}")
            erels.append(args)
        else:
            rels = args.get("relations")
            for k in ("used", "generated", "invalidated", "updated"):
                if k in args and isinstance(args[k], list):
                    fail(f"relation {k} outside the relations object: {ev}")
            if rels is not None:
                if not isinstance(rels, dict) or not rels:
                    fail(f"bad relations object: {ev}")
                if set(rels) - {"used", "generated", "invalidated", "updated"}:
                    fail(f"unknown relation name: {rels}")
                for k, ids in rels.items():
                    if not isinstance(ids, list) or not all(HEX.match(i) for i in ids):
                        fail(f"bad {k} array: {ev}")
                rel_events.append(rels)

    # entity declarations: 3 samples + structure + archive, each once
    by_type = {}
    for e in entities.values():
        by_type.setdefault(e["type"], []).append(e)
    if len(by_type.get("raw_sample", [])) != 3:
        fail(f"pid {pid}: expected 3 raw_sample entities, got {by_type.get('raw_sample')}")
    if STRUCTURE_ID not in entities:
        fail(f"protein_structure id {STRUCTURE_ID} missing (cross-language hash)")
    archive = by_type.get("archive", [None])[0]
    if not archive or archive["store"] != 3 or archive["uri"] != "/p/out/a.tar":
        fail(f"archive entity wrong: {archive}")
    # entity types with roles; description sanitized (quotes, '|' -> '_')
    if types.get("raw_sample", {}).get("role") != 1:
        fail(f"raw_sample role: {types.get('raw_sample')}")
    desc = types.get("protein_structure", {}).get("description", "")
    if types.get("protein_structure", {}).get("role") != 2 or '"' in desc or "|" in desc:
        fail(f"protein_structure type: {types.get('protein_structure')}")
    # event relations: 3 transforms + pack; every id declared
    if len(rel_events) != 4:
        fail(f"pid {pid}: expected 4 events with relations, got {len(rel_events)}")
    for rels in rel_events:
        for ids in rels.values():
            for i in ids:
                if i not in entities:
                    fail(f"relation references undeclared entity {i}")
    if not any(STRUCTURE_ID in r.get("generated", []) for r in rel_events):
        fail("no event generated the structure")
    # exactly one entity relation (the event-relation misuse is ignored)
    if len(erels) != 1 or erels[0]["relation"] != 18:
        fail(f"pid {pid}: expected one CONTAINS relation, got {erels}")
    if erels[0]["subject"] != archive["id"] or erels[0]["object"] != STRUCTURE_ID:
        fail(f"CONTAINS endpoints wrong: {erels[0]}")
    print(f"pid {pid}: {len(entities)} entities, {len(types)} types, "
          f"{len(rel_events)} related events, {len(erels)} entity relation")


if __name__ == "__main__":
    main()
