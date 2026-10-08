#!/usr/bin/env python3
"""Check dependency/manifest.txt against the pins in dependency/CMakeLists.txt.

The manifest lists the git tag of each C/C++ dependency the wheel build compiles.
dependency/CMakeLists.txt pins the same tags for the CMake source build, so the
two must agree:

    python scripts/wheel/manifest.py

gotcha is only pinned in the manifest: brahma fetches it, dftracer does not.
pybind11 is only pinned in CMake: the wheel build takes it from pip.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "dependency" / "manifest.txt"
CMAKELISTS = ROOT / "dependency" / "CMakeLists.txt"


def tag_to_version(name: str, tag: str) -> str:
    """v1.52.1 -> 1.52.1, yaml-cpp-0.6.3 -> 0.6.3."""
    tag = tag.strip('"')
    for prefix in (f"{name}-", f"{name.lower()}-", "v"):
        if tag.startswith(prefix):
            tag = tag[len(prefix) :]
            break
    return tag


def read_manifest() -> dict[str, str]:
    entries = {}
    for line in MANIFEST.read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        name, tag, _url = line.split()
        entries[name] = tag_to_version(name, tag)
    return entries


def cmake_pins() -> dict[str, str]:
    text = CMAKELISTS.read_text()
    pins = {}
    for name, tag in re.findall(
        r"dftracer_install_external_project\(\s*(\S+)\s+\S*\s+\"?\S+\"?\s+\S+\s+(\S+)",
        text,
    ):
        pins[name] = tag_to_version(name, tag)
    for name, tag in re.findall(
        r"ExternalProject_Add\(\s*(\w[\w-]*)(?:.|\n)*?GIT_TAG\s+(\S+)", text
    ):
        pins.setdefault(name, tag_to_version(name, tag))
    return pins


def main() -> int:
    entries = read_manifest()
    pins = cmake_pins()
    bad = []
    for name, version in sorted(entries.items()):
        pinned = pins.get(name)
        if pinned is None:
            print(f"skip     {name}: not pinned in {CMAKELISTS.name}")
        elif pinned != version:
            print(f"MISMATCH {name}: manifest {version} != cmake {pinned}")
            bad.append(name)
        else:
            print(f"ok       {name}: {version}")
    for name in sorted(set(pins) - set(entries)):
        print(f"skip     {name}: not in {MANIFEST.name} (cmake pins {pins[name]})")

    if bad:
        print(f"\n{len(bad)} dependency version(s) disagree", file=sys.stderr)
        return 1
    print("\ndependency versions agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
