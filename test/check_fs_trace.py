#!/usr/bin/env python3
"""Validate the "FS" file-system records (DFTRACER_ENABLE_FS_METADATA, on by default).

FS records come at three levels, each fact stored once where it can change:
  mount  keyed by mhash: fs, bsize, capacity, namelen, read_only, mount, ...
  dir    keyed by dhash, linked to its mount by mhash
  file   keyed by fhash, linked to its directory by dhash: size, blksize
Every key must resolve to a path through an FH record, every link must point
at a record of the level above in the same process, and each mount, directory
and file is described at most once per process. Lustre objects, where present,
must be well-formed. The process cwd is always hashed, so a trace has at least
one mount and one dir record.

usage: check_fs_trace.py "<trace glob>" [--lustre]
  --lustre  also require at least one Lustre file record with OSTs (for a run
            whose data directory is on Lustre)
"""
import glob
import gzip
import json
import sys

KEYS = {"mount": "mhash", "dir": "dhash", "file": "fhash"}


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def check_osts(where, osts):
    if not isinstance(osts, list) or not all(
        isinstance(o, int) and o >= 0 for o in osts
    ):
        fail(f"{where}: osts is not a list of OST indices: {osts!r}")
    if osts != sorted(set(osts)):
        fail(f"{where}: osts is not sorted and unique: {osts!r}")


def check_layout(where, lustre):
    # A plain layout has its stripe settings at the top level; a composite one
    # has them per component and only the OST union at the top level.
    keys = ("osts",) if "components" in lustre else (
        "stripe_count", "stripe_size", "osts")
    for key in keys:
        if key not in lustre:
            fail(f"{where}: missing {key}: {lustre}")
    for key in keys[:-1]:
        if not isinstance(lustre[key], int) or lustre[key] < -1:
            fail(f"{where}: bad {key}: {lustre[key]!r}")
    check_osts(where, lustre["osts"])
    union = set()
    for i, comp in enumerate(lustre.get("components", [])):
        for key in ("start", "end", "stripe_count", "stripe_size", "osts"):
            if key not in comp:
                fail(f"{where} component {i}: missing {key}: {comp}")
        check_osts(f"{where} component {i}", comp["osts"])
        union.update(comp["osts"])
    if "components" in lustre and union != set(lustre["osts"]):
        fail(f"{where}: osts {lustre['osts']} != union of components {union}")


def check_mount(where, a):
    if not isinstance(a.get("fs"), str) or not a["fs"]:
        fail(f"{where}: missing fs type: {a}")
    for key in ("bsize", "namelen"):
        if not isinstance(a.get(key), int) or a[key] <= 0:
            fail(f"{where}: bad {key}: {a}")
    cap = a.get("capacity", {})
    for key in ("total_bytes", "free_bytes", "avail_bytes", "total_inodes",
                "free_inodes"):
        if not isinstance(cap.get(key), int) or cap[key] < 0:
            fail(f"{where}: bad capacity.{key}: {a}")
    if a.get("read_only") not in (0, 1):
        fail(f"{where}: bad read_only: {a}")
    if "rotational" in a and a["rotational"] not in (0, 1):
        fail(f"{where}: bad rotational: {a}")
    if "lustre" in a:
        if a["fs"] != "lustre":
            fail(f"{where}: lustre object on fs {a['fs']}")
        if a["lustre"].get("liblustreapi") not in (0, 1):
            fail(f"{where}: lustre without liblustreapi flag: {a}")
    elif a["fs"] == "lustre":
        fail(f"{where}: Lustre mount without lustre object: {a}")


def main():
    pattern = sys.argv[1]
    want_lustre = "--lustre" in sys.argv[2:]
    files = sorted(glob.glob(pattern))
    if not files:
        fail(f"no trace files match {pattern}")
    fh, fs = {}, []
    for f in files:
        for line in gzip.open(f, "rt"):
            line = line.strip().rstrip(",")
            if not line.startswith("{"):
                continue
            ev = json.loads(line)
            args = ev.get("args", {})
            if ev.get("name") == "FH":
                fh[args.get("value")] = args.get("name")
            elif ev.get("name") == "FS":
                fs.append((ev.get("pid"), args))
    if not fs:
        fail("no FS records")

    # Per process: level -> {key: args}, each described at most once.
    seen = {}
    for pid, a in fs:
        level = a.get("level")
        if level not in KEYS:
            fail(f"FS record with bad level: {a}")
        key = a.get(KEYS[level])
        if key not in fh:
            fail(f"FS {level} record for unknown {KEYS[level]} {key}: {a}")
        records = seen.setdefault(pid, {l: {} for l in KEYS})[level]
        if key in records:
            fail(f"pid {pid}: {level} {fh[key]} described twice")
        records[key] = a

    lustre_mounts, lustre_dirs, lustre_files = set(), set(), 0
    for pid, levels in seen.items():
        if not levels["mount"] or not levels["dir"]:
            fail(f"pid {pid}: no mount or dir record (the cwd has both)")
        for mhash, a in levels["mount"].items():
            check_mount(f"mount {fh[mhash]}", a)
            if a.get("lustre", {}).get("liblustreapi") == 1:
                lustre_mounts.add((pid, mhash))
        for dhash, a in levels["dir"].items():
            where = f"dir {fh[dhash]}"
            if a.get("mhash") not in levels["mount"]:
                fail(f"{where}: mhash {a.get('mhash')} has no mount record")
            if "lustre" in a:
                if (pid, a["mhash"]) not in lustre_mounts:
                    fail(f"{where}: lustre object on a non-Lustre mount")
                if not isinstance(a["lustre"].get("mdt"), int):
                    fail(f"{where}: missing mdt: {a}")
                lustre_dirs.add((pid, dhash))
        for fhash, a in levels["file"].items():
            where = f"file {fh[fhash]}"
            if a.get("dhash") not in levels["dir"]:
                fail(f"{where}: dhash {a.get('dhash')} has no dir record")
            for key in ("size", "blksize"):
                if not isinstance(a.get(key), int) or a[key] < 0:
                    fail(f"{where}: bad {key}: {a}")
            if "lustre" in a:
                if (pid, a["dhash"]) not in lustre_dirs:
                    fail(f"{where}: lustre layout in a non-Lustre dir")
                check_layout(where, a["lustre"])
                if a["lustre"]["osts"]:
                    lustre_files += 1

    if want_lustre and lustre_files == 0:
        fail("no Lustre file record with OSTs")
    counts = {l: sum(len(v[l]) for v in seen.values()) for l in KEYS}
    print(f"OK: {len(fs)} FS records ({counts['mount']} mount, "
          f"{counts['dir']} dir, {counts['file']} file), "
          f"{lustre_files} Lustre files with OSTs")


if __name__ == "__main__":
    main()
