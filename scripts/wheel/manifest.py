#!/usr/bin/env python3
"""Maintain dependency/source/manifest.txt.

The manifest is the lock file the wheel build reads: which archive to use for each
C/C++ dependency, its sha256, and where to download it. dependency/CMakeLists.txt
pins the same versions for the CMake source build, so the two must agree.

    python scripts/wheel/manifest.py --check              # do they agree?
    python scripts/wheel/manifest.py --set libuv 1.53.0   # upgrade one dependency

--set rewrites the manifest entry, downloads the new archive, records its sha256
and removes the superseded one, so upgrading is: edit the pin in
dependency/CMakeLists.txt, run --set with the same version, commit.

gotcha is only pinned here: brahma fetches it, dftracer does not. pybind11 is only
pinned in CMake: the wheel build takes it from pip.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "dependency" / "source" / "manifest.txt"
CMAKELISTS = ROOT / "dependency" / "CMakeLists.txt"

ARCHIVE = re.compile(r"^(?P<name>.+?)-(?P<version>\d[^-]*)\.tar\.(?:gz|bz2|xz)$")


def tag_to_version(name: str, tag: str) -> str:
    """v1.52.1 -> 1.52.1, yaml-cpp-0.6.3 -> 0.6.3."""
    tag = tag.strip('"')
    for prefix in (f"{name}-", f"{name.lower()}-", "v"):
        if tag.startswith(prefix):
            tag = tag[len(prefix):]
            break
    return tag


def read_manifest() -> tuple[list[str], dict[str, dict[str, str]]]:
    lines = MANIFEST.read_text().splitlines()
    entries = {}
    for index, line in enumerate(lines):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        name, archive, sha, url = line.split()
        match = ARCHIVE.match(archive)
        if not match:
            sys.exit(f"cannot read a version out of {archive}")
        entries[name] = {
            "line": index,
            "archive": archive,
            "sha256": sha,
            "url": url,
            "version": match.group("version"),
        }
    return lines, entries


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


def check() -> int:
    _, entries = read_manifest()
    pins = cmake_pins()
    bad = []
    for name, entry in sorted(entries.items()):
        pinned = pins.get(name)
        if pinned is None:
            print(f"skip     {name}: not pinned in {CMAKELISTS.name}")
        elif pinned != entry["version"]:
            print(f"MISMATCH {name}: manifest {entry['version']} != cmake {pinned}")
            bad.append(name)
        else:
            print(f"ok       {name}: {entry['version']}")
    for name in sorted(set(pins) - set(entries)):
        print(f"skip     {name}: not in {MANIFEST.name} (cmake pins {pins[name]})")

    if bad:
        print(f"\n{len(bad)} dependency version(s) disagree", file=sys.stderr)
        return 1
    print("\ndependency versions agree")
    return 0


def set_version(name: str, version: str) -> int:
    lines, entries = read_manifest()
    if name not in entries:
        sys.exit(f"{name} is not in {MANIFEST}")
    entry = entries[name]
    old_version = entry["version"]
    if old_version == version:
        print(f"{name} is already {version}")
        return 0

    archive = entry["archive"].replace(old_version, version)
    url = entry["url"].replace(old_version, version)
    target = MANIFEST.parent / archive

    if url == "-":
        if not target.exists():
            sys.exit(
                f"{name} has no public download; place {archive} in {MANIFEST.parent} "
                "before running --set"
            )
    elif not target.exists():
        print(f"downloading {url}")
        subprocess.run(
            ["curl", "-fsSL", "--retry", "3", "-o", str(target), url], check=True
        )

    sha = hashlib.sha256(target.read_bytes()).hexdigest()
    # keep the columns lined up with the entries that are not being rewritten
    name_width = max(len(other) for other in entries)
    archive_width = max(
        [len(other["archive"]) for other in entries.values()] + [len(archive)]
    )
    lines[entry["line"]] = (
        f"{name:<{name_width}}   {archive:<{archive_width}}  {sha}  {url}"
    )
    MANIFEST.write_text("\n".join(lines) + "\n")

    superseded = MANIFEST.parent / entry["archive"]
    if superseded.exists() and superseded != target:
        superseded.unlink()
        print(f"removed {superseded.name}")

    print(f"{name}: {old_version} -> {version}\n  {archive}\n  sha256 {sha}")
    print(f"\nupdate the matching pin in {CMAKELISTS.name}, then run --check")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true", help="compare with CMake pins")
    group.add_argument(
        "--set", nargs=2, metavar=("NAME", "VERSION"), help="upgrade one dependency"
    )
    args = parser.parse_args()
    return check() if args.check else set_version(*args.set)


if __name__ == "__main__":
    sys.exit(main())
