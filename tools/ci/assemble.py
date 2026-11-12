#!/usr/bin/env python3
"""Merge the compose trees of the CI jobs into one.

    assemble.py --out DIR CORE_TREE MODULE_TREE... [--expect matrix.json]

Each job of tools/ci/build-module.sh installs a tree holding the core and at
most one module variant (<libdir>/dftracer/compose). The result takes the core,
the C++ runtime and the compose project from the core job's tree and every
module variant from the others, and checks that all of them were built from
the same DFTracer. With --expect (the output of `variants.py matrix`), every
variant listed there must be present.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path


def compose_dir(tree: Path) -> Path:
    found = sorted(tree.glob("**/dftracer/compose"))
    if not found:
        sys.exit(f"{tree}: no dftracer/compose in it")
    return found[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True,
                        help="the merged compose directory to create")
    parser.add_argument("--expect", type=Path,
                        help="variants.py matrix output; all must be present")
    parser.add_argument("core", type=Path, help="the core job's tree")
    parser.add_argument("modules", type=Path, nargs="*", help="module job trees")
    args = parser.parse_args()

    core = compose_dir(args.core)
    if args.out.exists():
        shutil.rmtree(args.out)
    # Everything but modules/ comes from the core job.
    shutil.copytree(core, args.out, ignore=shutil.ignore_patterns("modules"),
                    symlinks=True)
    version = None
    core_cmake = (args.out / "core" / "core.cmake").read_text()
    for line in core_cmake.splitlines():
        if line.startswith("set(DFTRACER_COMPOSE_VERSION"):
            version = line.split('"')[1]

    core_interface = None
    if (args.out / "core" / "interface_hash").exists():
        core_interface = (args.out / "core" / "interface_hash").read_text().strip()

    status = 0
    have: set[str] = set()
    for tree in args.modules:
        modules = compose_dir(tree) / "modules"
        for component in sorted(modules.glob("*/*/component.json")):
            data = json.loads(component.read_text())
            variant_dir = component.parent
            key = f"{data['module']}/{data['variant']}"
            if data.get("dftracer_library_version") != version:
                print(f"FAIL {key}: built for DFTracer "
                      f"{data.get('dftracer_library_version')}, the core is {version}")
                status = 1
                continue
            if core_interface and data.get("interface_hash") != core_interface:
                print(f"FAIL {key}: built against other core headers than the core")
                status = 1
                continue
            dest = args.out / "modules" / data["module"] / data["variant"]
            if dest.exists():
                print(f"FAIL {key}: built by more than one job")
                status = 1
                continue
            shutil.copytree(variant_dir, dest, symlinks=True)
            have.add(data["variant"])
            print(f"{key}  {data.get('source_hash', '')[:12]}")

    if args.expect:
        # coverage jobs only show the module compiles; they ship nothing
        expected = {job["variant"] for job in
                    json.loads(args.expect.read_text())["include"]
                    if not job.get("coverage")}
        for variant in sorted(expected - have):
            print(f"FAIL {variant}: listed in variants.yaml but not built")
            status = 1
    print(f"{len(have)} module variants in {args.out}")
    return status


if __name__ == "__main__":
    sys.exit(main())
