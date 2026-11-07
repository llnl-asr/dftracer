#!/usr/bin/env python3
"""Check the trace test_pc_stress wrote: every event it made is there exactly
as many times as made, with its args intact, and every POSIX event's fhash
names a file of an FH record. Without aggregation.

usage: check_pc_stress.py <trace glob> [scale]
"""
import collections
import glob
import gzip
import json
import os
import sys

THREADS = 4


def newest(pattern):
    files = [f for f in glob.glob(pattern) if f.endswith(".pfw.gz")]
    if not files:
        sys.exit(f"no trace matches {pattern}")
    return max(files, key=os.path.getmtime)


def main():
    path = newest(sys.argv[1])
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    events, fh, bad, fh_paths = [], {}, 0, []
    with gzip.open(path, "rt") as f:
        for line in f:
            if not line.strip():
                continue
            try:
                e = json.loads(line)
            except json.JSONDecodeError:
                bad += 1
                continue
            if e.get("name") == "FH":
                fh[e["args"]["value"]] = e["args"]["name"]
                fh_paths.append(e["args"]["name"])
            elif e.get("ph") == 1:
                events.append(e)
    errors = []

    def expect(what, got, want):
        if got != want:
            errors.append(f"{what}: got {got}, want {want}")

    expect("unparsable lines", bad, 0)
    # One FH record per path, however often it is hashed.
    dup = [p for p, n in collections.Counter(fh_paths).items() if n > 1]
    expect("paths with more than one FH record", dup, [])
    names = collections.Counter(e["name"] for e in events)
    # long
    for t in range(THREADS):
        for n in (1, 15, 16, 17, 255, 256, 4095, 4096, 5000, 8000):
            expect(f"long t{t} len {n}", names[chr(ord("a") + t) * n], 3)
    # bigval: the arg and its length agree
    big = [e for e in events if e["name"] == "bigval"]
    expect("bigval events", len(big), THREADS * 3 * scale * 4)
    for e in big:
        a = e["args"]
        if a.get("big") != "v" * a.get("len", -1):
            errors.append(f"bigval arg of {len(a.get('big', ''))} bytes, "
                          f"len {a.get('len')}")
            break
    # many: 40 fixed keys
    many = [e for e in events if e["name"] == "many"]
    expect("many events", len(many), THREADS * 50 * scale)
    want_many = {f"k{k}": k for k in range(40)}
    for e in many:
        got = {k: v for k, v in e["args"].items() if k != "hhash"}
        if got != want_many:
            errors.append(f"many args {sorted(got)[:5]}...")
            break
    # fresh: 40 new keys each
    fresh = [e for e in events if e["name"] == "fresh"]
    expect("fresh events", len(fresh), THREADS * 50 * scale)
    seen_fresh = set()
    for e in fresh:
        keys = [k for k in e["args"] if k != "hhash"]
        if len(keys) != 40:
            errors.append(f"fresh event with {len(keys)} keys")
            break
        for k in keys:
            t, ev, i = k[1:].split("_")
            if e["args"][k] != int(i):
                errors.append(f"fresh {k}={e['args'][k]}")
            seen_fresh.add(k)
    expect("distinct fresh keys", len(seen_fresh), THREADS * 50 * scale * 40)
    # unique: new name and 8 new keys each
    unique = [e for e in events if e["name"].startswith("u")
              and e["name"][1:2].isdigit()]
    expect("unique events", len(unique), THREADS * 5000 * scale)
    expect("distinct unique names", len({e["name"] for e in unique}),
           THREADS * 5000 * scale)
    for e in unique:
        want = {f"{e['name']}_{k}": k for k in range(8)}
        got = {k: v for k, v in e["args"].items() if k != "hhash"}
        if got != want:
            errors.append(f"unique {e['name']} args {got}")
            break
    # addr: one buffer, 7 names
    per = 1000 * scale
    for k in range(7):
        expect(f"addr{k}", names[f"addr{k}"],
               THREADS * (per // 7 + (1 if k < per % 7 else 0)))
    expect("twice", names["twice"], 1)
    expect("leftover", names["leftover"], 3)
    # posix: every fhash resolves to its own file, one FH per file
    for op in ("pwrite", "pread"):
        expect(op, names[op], THREADS * 20 * 5)
    files = collections.Counter()
    for e in events:
        if e["name"] in ("pwrite", "pread", "open", "open64", "close"):
            h = e["args"].get("fhash")
            if h is None or h not in fh:
                errors.append(f"{e['name']} fhash {h} has no FH record")
                break
            if e["name"] == "pwrite":
                files[fh[h]] += 1
    expect("files written", len(files), THREADS * 20)
    for p, n in files.items():
        if n != 5 or "pc_stress_" + "p" * 60 not in p:
            errors.append(f"file {p}: {n} pwrites")
            break
    if errors:
        print(f"{path}:")
        for e in errors[:30]:
            print("  " + e)
        sys.exit(1)
    print(f"{path}: {len(events)} events as made")


if __name__ == "__main__":
    main()
