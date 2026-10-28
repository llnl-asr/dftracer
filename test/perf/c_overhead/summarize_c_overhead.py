#!/usr/bin/env python3
"""Summarize c_overhead.csv: per (threads, meta), the median over repetitions
of each mode's ns per call, what dftracer adds to the bare call, and how
producer/consumer mode compares with the in-process path.

usage: summarize_c_overhead.py c_overhead.csv > summary.csv
"""
import collections
import csv
import statistics
import sys

MODES = ["baseline", "orig_agg", "orig_noagg", "pc"]


def main():
    runs = collections.defaultdict(list)
    with open(sys.argv[1]) as f:
        for row in csv.DictReader(f):
            key = (int(row["threads"]), int(row["meta"]), row["label"])
            runs[key].append(row)
    out = csv.writer(sys.stdout)
    out.writerow(["threads", "meta", "mode", "reps", "ns_per_call_p50",
                  "ns_per_call_min", "ns_per_call_max", "added_ns",
                  "agg_Mcalls_per_s", "vs_orig_noagg", "vs_orig_agg"])
    for threads, meta in sorted({k[:2] for k in runs}):
        med = {}
        for mode in MODES:
            rows = runs.get((threads, meta, mode), [])
            if rows:
                med[mode] = statistics.median(
                    float(r["ns_per_op_p50"]) for r in rows)
        for mode in MODES:
            rows = runs.get((threads, meta, mode), [])
            if not rows:
                continue
            ns = [float(r["ns_per_op_p50"]) for r in rows]
            added = med[mode] - med.get("baseline", 0.0)

            def ratio(other):
                o = med.get(other, 0.0) - med.get("baseline", 0.0)
                return f"{added / o:.2f}" if o > 0 and mode != "baseline" else ""

            out.writerow([threads, meta, mode, len(rows), f"{med[mode]:.1f}",
                          f"{min(ns):.1f}", f"{max(ns):.1f}", f"{added:.1f}",
                          f"{statistics.median(float(r['agg_Mops']) for r in rows):.3f}",
                          ratio("orig_noagg"), ratio("orig_agg")])


if __name__ == "__main__":
    sys.exit(main())
