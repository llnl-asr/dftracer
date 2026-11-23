#!/usr/bin/env python3
"""Check a trace's ts and dur against the wall clock that timing.c read
around each of its calls.

usage: check_timing.py <expected file> <trace>...

Each expected call must have an event of its name that starts no earlier and
ends no later than the call (give or take TOL_US), and lasts as long as the
call less TOL_US. Every complete event of the program must lie in its run.
The calls take 20 ms, so a wrong unit, a zero or a time from another clock
fails. Exit 0 if all hold.
"""
import sys

from trace_io import iter_events

TOL_US = 2000
# glibc's open() may reach the tracer as open64.
NAMES = {"open": {"open", "open64"}, "read": {"read"}, "nap": {"nap"}}


def events(paths):
    for e in iter_events(paths):
        # pid 0 is the service's own telemetry.
        if e.get("ph") == 1 and e.get("pid") not in (0, None):
            yield e


def main():
    expected, paths = sys.argv[1], sys.argv[2:]
    calls, run = [], None
    for line in open(expected):
        name, before, after = line.split()
        if name == "run":
            run = (int(before), int(after))
        else:
            calls.append((name, int(before), int(after)))
    if run is None or not calls:
        print("FAIL: the program did not finish")
        return 1
    evs = list(events(paths))
    bad = 0
    for e in evs:
        ts, dur = e["ts"], e["dur"]
        if dur < 0 or ts < run[0] - TOL_US or ts + dur > run[1] + TOL_US:
            print(f"FAIL: {e['name']} ts={ts} dur={dur} is outside the run "
                  f"{run[0]}..{run[1]}")
            bad += 1
    used = set()
    for name, before, after in calls:
        if after - before < 10000:
            print(f"FAIL: {name} at {before} did not block ({after - before} us)")
            bad += 1
            continue
        match = None
        for i, e in enumerate(evs):
            if i in used or e["name"] not in NAMES[name]:
                continue
            ts, dur = e["ts"], e["dur"]
            if (ts >= before - TOL_US and ts + dur <= after + TOL_US
                    and dur >= after - before - TOL_US):
                match = i
                break
        if match is None:
            near = [(e["ts"], e["dur"]) for e in evs
                    if e["name"] in NAMES[name]
                    and abs(e["ts"] - before) < 1000000]
            print(f"FAIL: no {name} event for the call at {before}..{after} "
                  f"({after - before} us); {name} events near it (ts, dur): "
                  f"{near[:4]}")
            bad += 1
        else:
            used.add(match)
    print(f"{len(calls)} calls checked against {len(evs)} events: "
          f"{'ok' if bad == 0 else f'{bad} failed'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
