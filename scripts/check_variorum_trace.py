#!/usr/bin/env python3

from __future__ import annotations

import argparse
import gzip
import json
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate DFTracer variorum power traces by checking the power families and their readings"
    )
    parser.add_argument(
        "trace_files",
        nargs="+",
        help="Trace files to inspect (.pfw, .gz, .pfw.gz)",
    )
    parser.add_argument(
        "--min-events",
        type=int,
        required=True,
        help="Minimum total JSON events expected across all traces",
    )
    parser.add_argument(
        "--require-family",
        action="append",
        default=[],
        help=(
            "Power family that must appear at least once, e.g. cpu, gpu, memory. "
            "Which families a node has depends on its hardware and on how "
            "variorum was built, so only require what the target machine reports. "
            "Can be repeated."
        ),
    )
    parser.add_argument(
        "--min-power-records",
        type=int,
        default=1,
        help="Minimum number of variorum power records across all families",
    )
    parser.add_argument(
        "--skip-if-no-power",
        action="store_true",
        help=(
            "Exit successfully when the trace carries no variorum records at "
            "all. For environments that cannot read power registers -- a "
            "container with no msr-safe or /dev/hsmp, for instance -- where "
            "their absence says nothing about DFTracer. A trace that does have "
            "power records is still checked in full."
        ),
    )
    parser.add_argument(
        "--allow-static-power",
        action="store_true",
        help=(
            "Accept readings that never change. Off by default: a frozen watt "
            "value means the registers are not really being read, which a plain "
            "presence check would not catch."
        ),
    )
    return parser.parse_args()


# Keep in step with TraceEventType in include/dftracer/core/common/enumeration.h.
TRACE_TYPE_VARIORUM = 13
# Keep in step with TracePhaseType in the same header.
TRACE_PHASE_COUNTER = 2

# Written into every record by the buffer manager, not a power reading.
NON_READING_ARGS = {"hhash"}


def iter_trace_lines(path: Path):
    opener = gzip.open if path.suffix == ".gz" or path.name.endswith(".pfw.gz") else open
    with opener(path, "rt", encoding="utf-8", errors="replace") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or line in {"[", "]"}:
                continue
            if line.endswith(","):
                line = line[:-1].rstrip()
            if line and line[0] in "[{":
                yield line


def main() -> int:
    args = parse_args()
    required_families = [family for family in args.require_family if family]

    total_events = 0
    # family -> number of records, and family -> reading name -> values seen.
    family_records: dict[str, int] = {}
    family_values: dict[str, dict[str, list[float]]] = {}
    json_errors: list[str] = []
    phase_errors: list[str] = []

    for trace_name in args.trace_files:
        trace_path = Path(trace_name)
        if not trace_path.is_file():
            print(f"missing trace file: {trace_path}", file=sys.stderr)
            return 1

        for line_number, line in enumerate(iter_trace_lines(trace_path), start=1):
            try:
                payload = json.loads(line)
            except json.JSONDecodeError as exc:
                json_errors.append(f"{trace_path}:{line_number}: {exc}")
                continue

            total_events += 1

            # Power is written one record per family: the family is the record
            # "cat", the producing layer is "type", and every socket and device
            # in that family is a key in args. Matching on the type rather than
            # on a substring means a record has to really be a power sample.
            if payload.get("type") != TRACE_TYPE_VARIORUM:
                continue

            # Power is a time series with no duration, so it has to be a
            # counter. A complete event here would mean the phase was lost.
            if payload.get("ph") != TRACE_PHASE_COUNTER:
                phase_errors.append(
                    f"{trace_path}:{line_number}: expected ph={TRACE_PHASE_COUNTER}, "
                    f"got {payload.get('ph')!r}"
                )
                continue

            family = payload.get("cat")
            if not isinstance(family, str) or not family:
                phase_errors.append(
                    f"{trace_path}:{line_number}: variorum record has no family in cat"
                )
                continue

            family_records[family] = family_records.get(family, 0) + 1
            readings = family_values.setdefault(family, {})
            for key, value in (payload.get("args") or {}).items():
                if key in NON_READING_ARGS:
                    continue
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    readings.setdefault(key, []).append(float(value))

    if json_errors:
        print("failed to parse trace JSON:", file=sys.stderr)
        for entry in json_errors[:10]:
            print(entry, file=sys.stderr)
        return 1

    if phase_errors:
        print("malformed variorum records:", file=sys.stderr)
        for entry in phase_errors[:10]:
            print(entry, file=sys.stderr)
        return 1

    if total_events < args.min_events:
        print(
            f"expected at least {args.min_events} events, found {total_events}",
            file=sys.stderr,
        )
        return 1

    total_power_records = sum(family_records.values())

    # Checked before the minimum-count gate below, which would otherwise fire
    # first and fail the run.
    if args.skip_if_no_power and total_power_records == 0:
        print(
            "no variorum records in the trace; skipping power validation "
            "(this environment cannot read power registers)"
        )
        return 0

    if total_power_records < args.min_power_records:
        print(
            f"expected at least {args.min_power_records} variorum power records, "
            f"found {total_power_records}",
            file=sys.stderr,
        )
        return 1

    missing = [family for family in required_families if family not in family_records]
    if missing:
        print(
            "missing required power families: "
            + ", ".join(missing)
            + f" (found: {', '.join(sorted(family_records)) or 'none'})",
            file=sys.stderr,
        )
        return 1

    # A family record with no readings in it is an empty measurement dressed up
    # as a sample.
    empty = [family for family in family_records if not family_values.get(family)]
    if empty:
        print(
            "power records carried no numeric readings: " + ", ".join(sorted(empty)),
            file=sys.stderr,
        )
        return 1

    if not args.allow_static_power:
        # Only the families that were asked for: "other" carries topology counts
        # such as num_gpus_per_socket, which are supposed to stay put, and a
        # node's idle power can legitimately repeat for a couple of samples.
        frozen = []
        for family in required_families:
            values = family_values.get(family, {})
            varying = [
                name
                for name, series in values.items()
                if len(series) > 1 and len(set(series)) > 1
            ]
            samples = [name for name, series in values.items() if len(series) > 1]
            if samples and not varying:
                frozen.append(family)
        if frozen:
            print(
                "power readings never changed (registers are not being read): "
                + ", ".join(frozen),
                file=sys.stderr,
            )
            return 1

    print(f"validated {total_events} total events across {len(args.trace_files)} trace file(s)")
    summary = ", ".join(
        f"{family}={family_records[family]} records over "
        f"{len(family_values.get(family, {}))} reading(s)"
        for family in sorted(family_records)
    )
    print(f"validated variorum power families: {summary}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
