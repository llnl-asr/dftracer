#!/usr/bin/env python3
"""Validate the typed values, lists and metadata records written by
test/c/test_args.c, test/cpp/test_args.cpp and test/py/test_args.py.

usage: check_args_trace.py <c|cpp|py> "<trace glob>"
"""
import glob
import gzip
import json
import sys

TYPED = {"i64": -9007199254740993, "u64": 2**64 - 1, "f64": 2.5, "flag": 1}
LISTS = {
    "files": ["a.h5", 'b "q".h5'],
    "ids": [1, -2],
    "links": {"in": ["x"], "out": ["y", "z"]},
}
# A record's fields; a field set twice keeps the last value.
SAMPLE = {"id": "s1", "n": 4, "ratio": 0.5, "tags": ["x", "y"]}


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def main():
    lang, pattern = sys.argv[1], sys.argv[2]
    files = sorted(glob.glob(pattern))
    if not files:
        fail(f"no trace files match {pattern}")
    typed = dict(TYPED)
    if lang == "py":
        del typed["u64"]  # int_args are signed 64-bit
    seen = set()
    for f in files:
        for line in gzip.open(f, "rt"):
            line = line.strip().rstrip(",")
            if not line.startswith("{"):
                continue
            ev = json.loads(line)
            name = ev.get("name")
            plain = {k: v for k, v in (ev.get("args") or {}).items() if k != "hhash"}
            if name == "typed" and ev.get("ph") == 1:
                got = {k: plain[k] for k in typed if k in plain}
                if got != typed:
                    fail(f"typed args {got} != {typed}")
                seen.add(name)
            elif name == "lists" and ev.get("ph") == 1:
                got = {k: plain.get(k) for k in LISTS}
                if got != LISTS:
                    fail(f"list args {got} != {LISTS}")
                seen.add(name)
            elif name == "SAMPLE":
                if plain != SAMPLE:
                    fail(f"SAMPLE fields {plain} != {SAMPLE}")
                if ev.get("ph") != 4:
                    fail(f"SAMPLE is not a metadata record: {ev}")
                seen.add(name)
            elif plain.get("name") == "plain_key":
                # A metadata region without fields: the key/value event.
                if plain.get("value") != "plain_value":
                    fail(f"plain metadata event {ev}")
                seen.add("plain_key")
    missing = {"typed", "lists", "SAMPLE", "plain_key"} - seen
    if missing:
        fail(f"missing {sorted(missing)} in {', '.join(files)}")
    print(f"OK: {lang} in {len(files)} file(s)")


if __name__ == "__main__":
    main()
