#!/usr/bin/env python3
"""Checks that the FH record of the file test/py/test_odd_names.py writes has
its real name, with every special character kept.

usage: check_odd_names_trace.py "<trace glob>"
"""
import glob
import gzip
import json
import os
import sys

ODD = "odd (\"q\") 'x'|y\\z\tw.txt"


def main():
    files = sorted(glob.glob(sys.argv[1]))
    if not files:
        print(f"FAIL: no trace files match {sys.argv[1]}")
        return 1
    names = []
    for f in files:
        for line in gzip.open(f, "rt"):
            line = line.strip().rstrip(",")
            if not line.startswith("{"):
                continue
            ev = json.loads(line)
            if ev.get("name") == "FH":
                names.append(ev["args"]["name"])
    if not any(os.path.basename(n) == ODD for n in names):
        print(f"FAIL: no FH record named {ODD!r}; FH names: {names!r}")
        return 1
    print(f"OK: FH record keeps {ODD!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
