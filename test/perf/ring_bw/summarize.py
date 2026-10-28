"""Summarize run_ring_bw.sh output: ring bandwidth as a fraction of STREAM Copy.

    python3 summarize.py stream.csv ring.csv

Every ring configuration (mode, producers, record size) was run REPS times and
STREAM was run REPS times per thread count. Reports p50/min/max and the CV of
each, then the p50 ring bandwidth as a fraction of the p50 STREAM Copy:

  * producer side (modes produce, both): against STREAM Copy with the same
    number of threads as producers, on the same cores;
  * consumer side (modes consume, both): the consumer is one thread, so against
    single-thread STREAM Copy.

Bandwidths use STREAM's accounting (bytes read + bytes written), i.e. the
copy_GBps columns of ring_bw.
"""

import csv
import statistics
import sys
from collections import defaultdict


def stats(values):
    values = sorted(values)
    p50 = statistics.median(values)
    cv = statistics.pstdev(values) / p50 if p50 > 0 and len(values) > 1 else 0.0
    return p50, values[0], values[-1], cv


def main(stream_path, ring_path):
    stream = defaultdict(list)
    with open(stream_path) as f:
        for row in csv.DictReader(f):
            stream[int(row["threads"])].append(float(row["copy_GBps"]))
    stream_p50 = {t: stats(v)[0] for t, v in stream.items()}

    groups = defaultdict(lambda: defaultdict(list))
    with open(ring_path) as f:
        for row in csv.DictReader(f):
            key = (row["mode"], int(row["producers"]), int(row["record_bytes"]))
            groups[key]["producer"].append(float(row["copy_GBps"]))
            groups[key]["consumer"].append(float(row["consumer_copy_GBps"]))
            groups[key]["mrec"].append(float(row["Mrec_per_s"]))
            groups[key]["full"].append(float(row["full_retries"]))

    out = csv.writer(sys.stdout)
    out.writerow([
        "mode", "producers", "record_bytes", "side", "copy_GBps_p50",
        "copy_GBps_min", "copy_GBps_max", "cv", "Mrec_per_s_p50",
        "stream_threads", "stream_copy_GBps_p50", "fraction_of_stream",
        "full_retries_p50",
    ])
    for (mode, producers, record), g in sorted(groups.items()):
        sides = []
        if mode in ("produce", "both"):
            sides.append(("producer", producers))
        if mode in ("consume", "both"):
            sides.append(("consumer", 1))
        for side, threads in sides:
            p50, lo, hi, cv = stats(g[side])
            ref = stream_p50.get(threads)
            mrec = stats(g["mrec"])[0] if side == "producer" else ""
            out.writerow([
                mode, producers, record, side, f"{p50:.2f}", f"{lo:.2f}",
                f"{hi:.2f}", f"{cv:.3f}",
                f"{mrec:.2f}" if mrec != "" else "", threads,
                f"{ref:.2f}" if ref else "",
                f"{p50 / ref:.3f}" if ref else "",
                f"{stats(g['full'])[0]:.0f}",
            ])


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
