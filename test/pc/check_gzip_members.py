#!/usr/bin/env python3
"""Check that a .pfw.gz is a sequence of independent gzip members, each
holding whole JSON lines -- what lets readers split the file by member and
decompress the members in parallel.

usage: check_gzip_members.py <trace.pfw.gz> [--min-members N]

Fails if a member does not end on a line boundary, a line does not parse, a
member is truncated, or bytes follow the last member. Prints the member count
and sizes. Exit 0 if the file is splittable.
"""
import argparse
import json
import sys
import zlib


def members(data):
    """Yield (offset, compressed size, decompressed bytes) per gzip member."""
    offset = 0
    while offset < len(data):
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        out = d.decompress(data[offset:])
        if not d.eof:
            raise ValueError(f"member at byte {offset} is truncated")
        size = len(data) - offset - len(d.unused_data)
        yield offset, size, out
        offset += size


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--min-members", type=int, default=1)
    opts = parser.parse_args()
    with open(opts.path, "rb") as f:
        data = f.read()
    count = lines = 0
    raw = []
    try:
        for offset, size, out in members(data):
            count += 1
            raw.append(len(out))
            if out and not out.endswith(b"\n"):
                raise ValueError(f"member {count} at byte {offset} does not "
                                 "end with a whole line")
            for line in out.splitlines():
                line = line.strip().rstrip(b",")
                if not line or line in (b"[", b"]"):
                    continue
                json.loads(line)
                lines += 1
    except (ValueError, zlib.error) as error:
        print(f"{opts.path}: NOT SPLITTABLE: {error}")
        return 1
    if count < opts.min_members:
        print(f"{opts.path}: {count} members, expected at least "
              f"{opts.min_members}")
        return 1
    print(f"{opts.path}: {count} independent gzip members, {lines} lines, "
          f"{len(data)} bytes; uncompressed member size min "
          f"{min(raw) if raw else 0} max {max(raw) if raw else 0}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
