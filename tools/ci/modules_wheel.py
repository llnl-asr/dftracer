#!/usr/bin/env python3
"""Package the module variants of a compose tree as the dftracer-modules wheel.

    modules_wheel.py <compose dir> <version> <out dir> [--libdir lib64]

The wheel holds no Python, only the archives under
dftracer/<libdir>/dftracer/compose/modules/, so it installs into the dftracer
package's compose tree, where dftracer-compose finds them. Its tag is
py3-none-manylinux_2_28_x86_64: one wheel serves every Python version, as the
archives do not depend on it.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import sys
import zipfile
from pathlib import Path

NAME = "dftracer_modules"
TAG = "py3-none-manylinux_2_28_x86_64"


def record_line(arcname: str, data: bytes) -> str:
    digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=")
    return f"{arcname},sha256={digest.decode()},{len(data)}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("compose", type=Path)
    parser.add_argument("version")
    parser.add_argument("out", type=Path)
    parser.add_argument("--libdir", default="lib64")
    args = parser.parse_args()

    modules = args.compose / "modules"
    if not modules.is_dir():
        sys.exit(f"{modules}: no module variants")
    variants = sorted(p.parent.relative_to(modules)
                      for p in modules.glob("*/*/component.json"))
    dist_info = f"{NAME}-{args.version}.dist-info"
    metadata = "\n".join([
        "Metadata-Version: 2.1",
        "Name: dftracer-modules",
        f"Version: {args.version}",
        "Summary: Prebuilt DFTracer module archives (MPI, HDF5, PAPI, hwloc, "
        "HIP, CUDA) for dftracer-compose",
        "License: MIT",
        "Home-page: https://github.com/llnl-asr/dftracer",
        "",
        "Module variants:",
        *[f"  {v}" for v in variants],
        "",
    ])
    wheel = "\n".join([
        "Wheel-Version: 1.0",
        "Generator: dftracer tools/ci/modules_wheel.py",
        "Root-Is-Purelib: false",
        f"Tag: {TAG}",
        "",
    ])

    args.out.mkdir(parents=True, exist_ok=True)
    path = args.out / f"{NAME}-{args.version}-{TAG}.whl"
    records = []
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as whl:
        for file in sorted(modules.rglob("*")):
            if not file.is_file():
                continue
            arcname = (f"dftracer/{args.libdir}/dftracer/compose/modules/"
                       f"{file.relative_to(modules)}")
            data = file.read_bytes()
            whl.writestr(arcname, data)
            records.append(record_line(arcname, data))
        for name, text in (("METADATA", metadata), ("WHEEL", wheel)):
            arcname = f"{dist_info}/{name}"
            whl.writestr(arcname, text)
            records.append(record_line(arcname, text.encode()))
        records.append(f"{dist_info}/RECORD,,")
        whl.writestr(f"{dist_info}/RECORD", "\n".join(records) + "\n")
    print(f"{path}: {len(variants)} module variants")
    return 0


if __name__ == "__main__":
    sys.exit(main())
