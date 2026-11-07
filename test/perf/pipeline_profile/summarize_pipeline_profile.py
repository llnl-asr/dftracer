#!/usr/bin/env python3
"""Summarize run_pipeline_profile.sh: per mode, thread count and metadata
setting, each phase's occurrences, its average time per occurrence and its
share of every event (ns per event), averaged over the repetitions, next to
the wall-clock overhead per event against the baseline.

Usage: summarize_pipeline_profile.py runs.csv profile.csv [summary.csv]
Prints the tables; writes the long form to summary.csv when given.
"""
import csv
import sys
from collections import defaultdict
from statistics import mean

PHASE_ORDER = ["capture", "filtered", "buffer", "aggregation",
               "serialization", "compression", "store"]
MODE_ORDER = ["local_agg", "local_noagg", "pc"]


def phase_key(phase, layer):
    return f"capture[{layer}]" if phase == "capture" else phase


def phase_rank(name):
    base = name.split("[")[0]
    return (PHASE_ORDER.index(base) if base in PHASE_ORDER else 99, name)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    runs_path, profile_path = sys.argv[1], sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else None

    # Wall time: ns per event (p50 over threads), per repetition.
    wall = defaultdict(list)
    with open(runs_path) as f:
        for r in csv.DictReader(f):
            wall[(r["label"], int(r["threads"]), int(r["meta"]))].append(
                float(r["ns_per_event_p50"]))

    # Phases: totals summed over repetitions, so the average is weighted by
    # count; ns per event averaged over repetitions.
    count = defaultdict(int)
    total = defaultdict(float)
    per_event = defaultdict(list)
    reps = defaultdict(set)
    with open(profile_path) as f:
        for r in csv.DictReader(f):
            cfg = (r["label"], int(r["threads"]), int(r["meta"]))
            key = cfg + (phase_key(r["phase"], r["layer"]),)
            count[key] += int(r["count"])
            total[key] += float(r["total_ns"])
            per_event[key].append(float(r["ns_per_event"]))
            reps[cfg].add(r["rep"])

    rows = []
    for key in count:
        label, threads, meta, phase = key
        n = len(reps[(label, threads, meta)])
        rows.append({
            "label": label, "threads": threads, "meta": meta,
            "phase": phase,
            "count_per_run": round(count[key] / n),
            "avg_ns": total[key] / count[key] if count[key] else 0.0,
            # A phase missing from a repetition contributed 0 there.
            "ns_per_event": sum(per_event[key]) / n,
        })

    configs = sorted({(t, m) for (_, t, m) in wall} |
                     {(r["threads"], r["meta"]) for r in rows})
    for threads, meta in configs:
        base = wall.get(("baseline", threads, meta))
        modes = [m for m in MODE_ORDER
                 if any(r["label"] == m and r["threads"] == threads and
                        r["meta"] == meta for r in rows)]
        print(f"\n== threads={threads} metadata={meta} "
              f"(avg ns per occurrence / ns per event, mean of reps)")
        width = 24
        print(f"{'phase':<26}" + "".join(f"{m:>{width}}" for m in modes))
        phases = sorted({r["phase"] for r in rows
                         if r["threads"] == threads and r["meta"] == meta},
                        key=phase_rank)
        sums = defaultdict(float)
        for phase in phases:
            line = f"{phase:<26}"
            for m in modes:
                hit = [r for r in rows if r["label"] == m and
                       r["threads"] == threads and r["meta"] == meta and
                       r["phase"] == phase]
                if hit:
                    r = hit[0]
                    if phase != "filtered":
                        sums[m] += r["ns_per_event"]
                    cell = f"{r['avg_ns']:.0f} / {r['ns_per_event']:.1f}"
                else:
                    cell = "-"
                line += f"{cell:>{width}}"
            print(line)
        print(f"{'sum of phases ns/event':<26}" +
              "".join(f"{sums[m]:>{width}.1f}" for m in modes))
        if base:
            b = mean(base)
            print(f"{'wall ns/event':<26}" + "".join(
                f"{mean(wall[(m, threads, meta)]):>{width}.1f}"
                if (m, threads, meta) in wall else f"{'-':>{width}}"
                for m in modes) + f"   (baseline {b:.1f})")
            print(f"{'wall overhead ns/event':<26}" + "".join(
                f"{mean(wall[(m, threads, meta)]) - b:>{width}.1f}"
                if (m, threads, meta) in wall else f"{'-':>{width}}"
                for m in modes))

    if out_path:
        with open(out_path, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=["label", "threads", "meta",
                                              "phase", "count_per_run",
                                              "avg_ns", "ns_per_event"])
            w.writeheader()
            for r in sorted(rows, key=lambda r: (r["threads"], r["meta"],
                                                 r["label"],
                                                 phase_rank(r["phase"]))):
                w.writerow({**r, "avg_ns": f"{r['avg_ns']:.1f}",
                            "ns_per_event": f"{r['ns_per_event']:.1f}"})


if __name__ == "__main__":
    main()
