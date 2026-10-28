#!/usr/bin/env python3
"""Check that a program traced the same events in-process and through
dftracer_service in producer/consumer mode.

usage: compare_traces.py --node <node trace> [--spill <spill files>...]
                         -- [in-process traces ...]

The in-process run wrote one file per process; the service wrote one node
file holding those processes plus its own telemetry (pid 0), which is left
out, and a producer that found the ring full spilled the rest to a file of its
own, which counts with the node file. Two runs of the same program differ in pids, tids, timestamps and
durations, so events are compared by everything else -- name, category, type,
phase and args (minus run-specific values) -- as multisets, plus the set of
event ids (which event gets which id depends on how threads interleave).
Aggregated rows (ph 3) are compared by their summed dft_cnt per (name,
category), since interval boundaries depend on timing. Under selective
aggregation an event is aggregated or written out depending on its duration
(e.g. dur < 100), which also depends on timing, so for every (name, category)
that either trace aggregates, the check is the total -- events written out
plus dft_cnt -- rather than the split.

Only processes that finalized (wrote their "end" event) are compared. One
that exec()s or dies first loses its buffered events in-process -- its file
stays empty -- while in producer/consumer mode each event is already in the
ring and reaches the node file; such processes are listed, not compared.
Likewise a process that logged fewer than 5 events: in-process its trace file
is deleted at finalize (STDIOWriter), while the service keeps its events.
Exit 0 if equivalent.
"""
import argparse
import collections
import gzip
import json
import os
import re
import sys

# Values that legitimately differ between two runs or between the two modes.
VOLATILE_ARGS = {"date", "ppid", "log_file", "pc_mode"}
# Args that hold a memory address. Not comparable across runs or modes: ASLR
# moves them, and the two modes allocate differently (in-process holds a
# 16 MiB trace buffer), so later mappings land elsewhere. Only their presence
# is compared; the call, length and flags still are.
ADDRESS_ARGS = {"address", "addr", "result_address"}
# Hash records (name -> hash). A name can hold a pid (a file named after the
# process), so the hash differs too; every hash is compared as the normalized
# name it stands for.
HASH_RECORDS = {"FH", "SH", "HH"}
# Calls that make a file or directory under a random name; every such name is
# compared as <directory>/TEMP.
# Polls: how often a program waits on an asynchronous call (aio_error while
# it returns EINPROGRESS, aio_suspend) depends on when the kernel finishes it.
# They are left out, and from each process's num_events.
EINPROGRESS = 115


def is_poll(event):
    name = event.get("name")
    return name == "aio_suspend" or (
        name == "aio_error" and event.get("args", {}).get("ret") == EINPROGRESS)


TEMP_CALLS = {"mkstemp", "mkstemp64", "mkostemp", "mkostemp64", "mkstemps",
              "mkstemps64", "mkostemps", "mkostemps64", "mkdtemp", "mktemp"}
DIGITS = re.compile(r"\d+")
# An arg value inside the trace's own time window (from the first event to
# the end of the last, plus this margin in us) is a timestamp the program
# logged (e.g. epoch.start), which differs between any two runs.
TIME_MARGIN_US = 60 * 1000000
# Args that hold another process's pid, which may not appear in the trace.
PID_ARGS = {("fork", "ret"), ("vfork", "ret"), ("waitpid", "pid"),
            ("waitpid", "ret"), ("wait", "ret")}
SERVICE_PID = 0
# STDIOWriter deletes the trace of a process with fewer events than this.
MIN_KEPT_EVENTS = 5


def load(path):
    events, bad = [], 0
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt") as f:
        for line in f:
            line = line.strip().rstrip(",")
            if not line or line in ("[", "]"):
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                bad += 1
    return events, bad


def without_ids(text, ids):
    """`text` with every pid or tid in it replaced by ID."""
    return DIGITS.sub(lambda m: "ID" if m.group(0) in ids else m.group(0), text)


def scrub(value, ids, window, hashes):
    if isinstance(value, dict):
        return {k: ("ADDR" if k in ADDRESS_ARGS else
                    scrub(v, ids, window, hashes))
                for k, v in value.items() if k not in VOLATILE_ARGS}
    if isinstance(value, (str, int)) and not isinstance(value, bool):
        if str(value) in ids:
            return "ID"
        if isinstance(value, str) and value in hashes:
            return hashes[value]
        number = value if isinstance(value, int) else \
            (int(value) if value.isdigit() else None)
        if number is not None and window[0] <= number <= window[1]:
            return "TS"
        if isinstance(value, str):
            return without_ids(value, ids)
    return value


def finalized(events):
    """Split into the events of processes that wrote "end" with enough events
    to keep their trace, and the pids of the others."""
    done = {e.get("pid") for e in events
            if e.get("name") == "end" and e.get("cat") == "dftracer" and
            e.get("args", {}).get("num_events", MIN_KEPT_EVENTS) >=
            MIN_KEPT_EVENTS}
    rest = {e.get("pid") for e in events} - done
    return [e for e in events if e.get("pid") in done], rest


def summarize(events):
    ids = {str(e.get("pid")) for e in events} | \
        {str(e.get("tid")) for e in events}
    pids = {e.get("pid") for e in events}
    plain = collections.Counter()
    aggregated = collections.Counter()
    stamps = [e["ts"] for e in events if isinstance(e.get("ts"), int)]
    ends = [e["ts"] + e.get("dur", 0) for e in events
            if isinstance(e.get("ts"), int) and isinstance(e.get("dur", 0), int)]
    window = (min(stamps) - TIME_MARGIN_US, max(ends) + TIME_MARGIN_US) \
        if stamps else (1, 0)
    temps = {e.get("args", {}).get("fhash") for e in events
             if e.get("name") in TEMP_CALLS}
    hashes = {}
    for e in events:
        args = e.get("args", {})
        if e.get("name") in HASH_RECORDS and isinstance(args.get("value"), str):
            name = without_ids(str(args.get("name", "")), ids)
            if args["value"] in temps:
                # The name is random too, wherever it appears.
                name = os.path.join(os.path.dirname(name), "TEMP")
                hashes[str(args.get("name", ""))] = name
            hashes[args["value"]] = f"{e['name']}:{name}"
    polls = collections.Counter(e.get("pid") for e in events if is_poll(e))
    event_ids = []
    for e in events:
        if is_poll(e):
            continue
        args = scrub(e.get("args", {}), ids, window, hashes)
        if e.get("name") == "end" and isinstance(args.get("num_events"), int):
            args["num_events"] -= polls[e.get("pid")]
        for name, key in PID_ARGS:
            if e.get("name") == name and key in args and args[key] != -1:
                args[key] = "ID"
        if e.get("ph") == 3:
            aggregated[(e.get("name"), e.get("cat"))] += args.get("dft_cnt", 0)
            continue
        if "id" in e:
            event_ids.append(e["id"])
        key = (e.get("name"), e.get("cat"), e.get("type"), e.get("ph"),
               json.dumps(args, sort_keys=True))
        plain[key] += 1
    return {"events": plain, "aggregated": aggregated,
            "ids": sorted(event_ids), "polls": sum(polls.values()),
            "processes": len(pids), "threads": len({(e.get("pid"), e.get("tid"))
                                                    for e in events})}


def main():
    parser = argparse.ArgumentParser()
    # The node trace: one file, or several once it rolled over.
    parser.add_argument("--node", required=True, nargs="+")
    # Events a producer spilled to its own file when the ring stayed full.
    parser.add_argument("--spill", nargs="*", default=[])
    parser.add_argument("local", nargs="*")
    opts = parser.parse_args()

    local, bad_local = [], 0
    for path in opts.local:
        events, bad = load(path)
        local += events
        bad_local += bad
    node, bad_node = [], 0
    for path in opts.node:
        events, bad = load(path)
        node += events
        bad_node += bad
    for path in opts.spill:
        events, bad = load(path)
        node += events
        bad_node += bad
    node = [e for e in node if e.get("pid") != SERVICE_PID]
    local, local_open = finalized(local)
    node, node_open = finalized(node)
    if local_open or node_open:
        print(f"not finalized or under {MIN_KEPT_EVENTS} events, not "
              f"compared: {len(local_open)} in-process, {len(node_open)} "
              f"service processes")

    a, b = summarize(local), summarize(node)
    for name, s, bad in (("in-process", a, bad_local), ("service", b, bad_node)):
        print(f"{name:10s}: {sum(s['events'].values())} events, "
              f"{sum(s['aggregated'].values())} aggregated, {s['processes']} "
              f"processes, {s['threads']} threads, {bad} bad lines")
    # Events either side aggregates: compare totals, not which ones were
    # written out (timing decides that).
    folded = {k for k in set(a["aggregated"]) | set(b["aggregated"])}
    for s in (a, b):
        total = collections.Counter(s["aggregated"])
        for key in list(s["events"]):
            if (key[0], key[1]) in folded:
                total[(key[0], key[1])] += s["events"].pop(key)
        s["aggregated"] = total
        # Event ids number the events written out, so they follow the split.
        # They also number the polls, whose count depends on timing.
        if folded or a["polls"] != b["polls"]:
            s["ids"] = []
    ok = bad_local == 0 and bad_node == 0
    for key in ("events", "aggregated", "ids", "processes", "threads"):
        if a[key] != b[key]:
            ok = False
            print(f"  {key} differ")
    for key in list(a["events"] - b["events"])[:10]:
        print("  only in-process:", key)
    for key in list(b["events"] - a["events"])[:10]:
        print("  only service:   ", key)
    for key in set(a["aggregated"]) | set(b["aggregated"]):
        if a["aggregated"][key] != b["aggregated"][key]:
            print("  aggregated", key, a["aggregated"][key], "vs",
                  b["aggregated"][key])
    print("EQUIVALENT" if ok else "DIFFERENT")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
