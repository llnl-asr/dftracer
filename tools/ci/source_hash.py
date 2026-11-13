#!/usr/bin/env python3
"""The source hash of a DFTracer part: what decides whether an archive built
earlier can be reused instead of compiling again.

    source_hash.py <root> core|<module> [--compiler <cxx>] [--brahma <tag>]

--brahma defaults to the brahma commit dependency/CMakeLists.txt pins, the
one a build links unless DFTRACER_BRAHMA_GIT_TAG says otherwise, so every
caller hashes a default build the same way.
    source_hash.py <root> interface

The interface hash covers only what decides how the core's classes look to a
module compiled against them: the headers, the generated config headers'
templates and the compiler flags (INTERFACE). A module archive links with a
core archive only when both carry the same one, whatever their own sources.

A part's hash covers every file that can change its archive: its own sources,
all of DFTracer's headers (the core's classes are compiled into each module),
the CMake that builds it and the dependency pins, plus the compiler's version
and the brahma commit it links. Two archives with the same hash were built
from the same inputs, so either serves. It is deliberately broad: a change to
any header rebuilds every part, never too few.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

# Each module's own sources, relative to the source root (globs).
MODULE_SOURCES = {
    "core": [
        "src/dftracer/core/*.cpp",
        "src/dftracer/core/aggregator/*.cpp",
        "src/dftracer/core/brahma/posix.cpp",
        "src/dftracer/core/brahma/stdio.cpp",
        "src/dftracer/core/buffer/*.cpp",
        "src/dftracer/core/common/*.cpp",
        "src/dftracer/core/compression/*.cpp",
        "src/dftracer/core/finstrument/*.cpp",
        "src/dftracer/core/serialization/*.cpp",
        "src/dftracer/core/utils/*.cpp",
        "src/dftracer/core/writer/*.cpp",
    ],
    "mpi": ["src/dftracer/core/brahma/mpi*.cpp"],
    "hdf5": ["src/dftracer/core/brahma/hdf5*.cpp"],
    "papi": ["src/dftracer/core/function/papi/*.cpp"],
    "hwloc": ["src/dftracer/core/utils/hwloc_affinity.cpp"],
    "hip": ["src/dftracer/core/function/hip/*.cpp"],
    "cuda": ["src/dftracer/core/function/cuda/*.cpp"],
}

# Read by every part's build.
SHARED = [
    "src/**/*.h",
    "src/**/*.hpp",
    "include/**/*",
    "cmake/**/*",
    "CMakeLists.txt",
    "dependency/CMakeLists.txt",
    "dependency/manifest.txt",
]


# What the interface hash covers (see the docstring).
INTERFACE = [
    "src/**/*.h",
    "src/**/*.hpp",
    "include/**/*",
    "cmake/configure_files/**/*",
    "cmake/modules/SetupCompiler.cmake",
]


def files(root: Path, part: str) -> list[Path]:
    found: set[Path] = set()
    if part == "interface":
        for pattern in INTERFACE:
            found.update(p for p in root.glob(pattern) if p.is_file())
        return sorted(found)
    for pattern in MODULE_SOURCES[part] + SHARED:
        found.update(p for p in root.glob(pattern) if p.is_file())
    if part == "core":
        # The module files share directories with the core; keep them out.
        for module, patterns in MODULE_SOURCES.items():
            if module != "core":
                for pattern in patterns:
                    found.difference_update(root.glob(pattern))
    return sorted(found)


def source_hash(root: Path, part: str, compiler: str, brahma: str) -> str:
    digest = hashlib.sha256()
    for path in files(root, part):
        digest.update(str(path.relative_to(root)).encode())
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    digest.update(f"compiler={compiler}\0brahma={brahma}\0".encode())
    return digest.hexdigest()


def pinned_brahma(root: Path) -> str:
    """The DFTRACER_BRAHMA_GIT_TAG default in dependency/CMakeLists.txt."""
    text = (root / "dependency" / "CMakeLists.txt").read_text()
    m = re.search(r'set\(DFTRACER_BRAHMA_GIT_TAG\s+"([^"]+)"', text)
    if not m:
        raise SystemExit("source_hash.py: no DFTRACER_BRAHMA_GIT_TAG default in "
                         "dependency/CMakeLists.txt")
    return m.group(1)


def compiler_version(cxx: str) -> str:
    try:
        out = subprocess.run([cxx, "--version"], capture_output=True, text=True,
                             check=True).stdout
        return out.splitlines()[0].strip()
    except (OSError, subprocess.CalledProcessError, IndexError):
        return cxx


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", type=Path)
    parser.add_argument("part", choices=sorted(MODULE_SOURCES) + ["interface"])
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--brahma", default="",
                        help="brahma commit linked (default: the pinned one)")
    args = parser.parse_args()
    if args.part == "interface":
        print(source_hash(args.root.resolve(), "interface", "", ""))
    else:
        root = args.root.resolve()
        print(source_hash(root, args.part, compiler_version(args.compiler),
                          args.brahma or pinned_brahma(root)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
