#!/usr/bin/env python3
"""Summarize run_pc_ring.sh. Per event size: the time per event without
dftracer, with in-process dftracer and with producer/consumer dftracer (per
ring size); the overhead of each as with / without; and for producer/consumer
mode the production rate, consumption rate and events spilled. Medians over
the replicates.

usage: summarize_pc_ring.py runs.csv
"""
import collections
import csv
import statistics
import sys


def med(rows, key):
    vals = [float(r[key]) for r in rows if r.get(key, "") != ""]
    return statistics.median(vals) if vals else None


def rate(v):
    return "-" if v is None else f"{v / 1e6:.2f}M/s"


def main():
    rows = collections.defaultdict(list)
    for r in csv.DictReader(open(sys.argv[1])):
        rows[(int(r["work_ns"]), r["mode"], r["ring_mb"])].append(r)
    header = (f"{'event':>7}  {'tracing':<16} {'reps':>4} {'time/event':>11} "
              f"{'overhead':>9} {'produced':>10} {'consumed':>10} "
              f"{'spilled/run':>12} {'spill %':>7} {'dropped':>7}")
    print(header)
    print("-" * len(header))
    for work in sorted({k[0] for k in rows}):
        base = med(rows.get((work, "base", ""), []), "ns_per_event_p50")
        configs = [("base", ""), ("local", "")] + sorted(
            ((m, ring) for (w, m, ring) in rows if w == work and m == "pc"),
            key=lambda x: int(x[1]))
        for mode, ring in configs:
            rs = rows.get((work, mode, ring))
            if not rs:
                continue
            ns = med(rs, "ns_per_event_p50")
            pc = mode == "pc"
            if mode == "base":
                label = "without dftracer"
            elif mode == "local":
                label = "in-process"
            elif int(ring) >= 1024:
                label = f"PC, {int(ring) // 1024} GB ring"
            else:
                label = f"PC, {ring} MB ring"
            over = f"{ns / base:.2f}x" if base else "-"
            spilled = (f"{statistics.median(int(r['spilled']) for r in rs):.0f}"
                       if pc else "-")
            spill_pct = (f"{statistics.median(100.0 * int(r['spilled']) / max(1, int(r['produced'])) for r in rs):.2f}"
                         if pc else "-")
            dropped = f"{max(int(r['dropped']) for r in rs)}" if pc else "-"
            consumed = rate(med(rs, "consumed_per_s")) if pc else "-"
            print(f"{work / 1000:>5g}us  {label:<16} {len(rs):>4} "
                  f"{ns / 1000:>9.2f}us {over:>9} "
                  f"{rate(med(rs, 'produced_per_s')):>10} {consumed:>10} "
                  f"{spilled:>12} {spill_pct:>7} {dropped:>7}")
        print()
    print("time/event: median over processes and replicates, including the "
          "event's own duration.\noverhead: time/event with dftracer / without "
          "(1.00x = no overhead). produced: events the processes logged per\n"
          "second (node). consumed: events the service wrote per second (run + "
          "drain). spilled: events that did not fit in the ring and went to\n"
          "the producers' spill files (appended to the node trace at stop, not "
          "lost). dropped: max over replicates of events in neither.")


if __name__ == "__main__":
    main()
