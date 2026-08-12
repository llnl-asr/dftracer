#!/usr/bin/env python3

from __future__ import annotations

import argparse
import gzip
import json
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate DFTracer PAPI traces by counting events and checking for configured PAPI counters"
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
        "--require-papi-event",
        action="append",
        default=[],
        help="PAPI event name that must appear at least once in the trace payload; can be repeated",
    )
    parser.add_argument(
        "--min-papi-lines",
        type=int,
        default=1,
        help="Minimum number of PAPI counter records for the required events",
    )
    parser.add_argument(
        "--allow-static-counters",
        action="store_true",
        help=(
            "Accept counters whose value never advances. Off by default: a "
            "frozen counter means the samples are not really being read, which "
            "a plain presence check would not catch."
        ),
    )
    return parser.parse_args()


# Keep in step with TraceEventType in include/dftracer/core/common/enumeration.h.
TRACE_TYPE_PAPI = 11


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
    required_events = [event for event in args.require_papi_event if event]

    total_events = 0
    papi_hits = {event: 0 for event in required_events}
    papi_values: dict[str, list[int]] = {event: [] for event in required_events}
    json_errors: list[str] = []

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

            # PAPI counters are written one record per counter: the counter is
            # the record "name", the producing layer is "type", and the reading
            # is args.value. Matching on that rather than on a substring of the
            # line means a record has to really be a PAPI sample to count.
            if payload.get("type") != TRACE_TYPE_PAPI:
                continue
            name = payload.get("name")
            if name in papi_hits:
                papi_hits[name] += 1
                value = payload.get("args", {}).get("value")
                if isinstance(value, int):
                    papi_values[name].append(value)

    if json_errors:
        print("failed to parse trace JSON:", file=sys.stderr)
        for entry in json_errors[:10]:
            print(entry, file=sys.stderr)
        return 1

    if total_events < args.min_events:
        print(
            f"expected at least {args.min_events} events, found {total_events}",
            file=sys.stderr,
        )
        return 1

    total_papi_lines = sum(papi_hits.values()) if papi_hits else 0
    if required_events and total_papi_lines < args.min_papi_lines:
        print(
            f"expected at least {args.min_papi_lines} PAPI-tagged events, found {total_papi_lines}",
            file=sys.stderr,
        )
        return 1

    missing = [event for event, count in papi_hits.items() if count == 0]
    if missing:
        print(
            "missing required PAPI events: " + ", ".join(missing),
            file=sys.stderr,
        )
        return 1

    if not args.allow_static_counters:
        frozen = [
            event
            for event, values in papi_values.items()
            if len(values) > 1 and len(set(values)) == 1
        ]
        if frozen:
            print(
                "PAPI counters never advanced (samples are not being read): "
                + ", ".join(frozen),
                file=sys.stderr,
            )
            return 1

        empty = [
            event
            for event, values in papi_values.items()
            if papi_hits[event] and not values
        ]
        if empty:
            print(
                "PAPI records carried no integer value: " + ", ".join(empty),
                file=sys.stderr,
            )
            return 1

    print(f"validated {total_events} total events across {len(args.trace_files)} trace file(s)")
    if required_events:
        summary = ", ".join(
            f"{event}={papi_hits[event]} samples, last={papi_values[event][-1]}"
            if papi_values[event]
            else f"{event}={papi_hits[event]} samples"
            for event in required_events
        )
        print(f"validated PAPI counters: {summary}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())