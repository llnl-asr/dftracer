#!/usr/bin/env python3
"""Make a dftracer wheel self-contained without letting patchelf near the
native executables.

    python bundle_wheel.py prepare <wheel> <stash_dir>
    python bundle_wheel.py finish  <wheel> <stash_dir>

Why this exists
---------------
`auditwheel repair` rewrites DT_NEEDED and RPATH with patchelf on every ELF file
in the wheel. On shared libraries that is fine. On dftracer's executables
(dftracer/bin/dftracer_service) the rewrite has to grow .dynstr, and the result
is an unloadable file: PT_DYNAMIC ends up in a hole that no PT_LOAD covers and
the process dies with SIGSEGV before the loader prints anything. Re-doing the
same rewrite with a newer patchelf produces a structurally valid file that still
crashes, while the identical binary pointed at unmangled libraries runs -- so the
executables must simply not be rewritten.

So, around `auditwheel repair`:

  prepare  copy the vendored dependency libraries into dftracer/lib64 under
           their real sonames and give them RPATH $ORIGIN, then lift the ELF
           executables out of the wheel into <stash_dir>
  finish   put the executables back with RPATH $ORIGIN/../lib64, and prove each
           one starts from the unpacked wheel layout with a scrubbed environment

The executables keep their original DT_NEEDED entries -- every library they need
now sits in dftracer/lib64 inside the wheel -- so the only rewrite they get is a
short RPATH that fits in the existing .dynstr. That is the difference from what
auditwheel does: no name replacement, nothing to grow, nothing to relocate.
"""

from __future__ import annotations

import base64
import csv
import hashlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

PT_LOAD = 1
PT_DYNAMIC = 2

DEP_PATTERNS = (
    "libcpp-logger.so*",
    "libbrahma.so*",
    "libgotcha.so*",
    "libuv.so*",
    "libyaml-cpp.so*",
)


def log(msg: str) -> None:
    print(f"[bundle] {msg}", flush=True)


def record_hash(data: bytes) -> str:
    digest = hashlib.sha256(data).digest()
    return "sha256=" + base64.urlsafe_b64encode(digest).rstrip(b"=").decode()


def is_elf(data: bytes) -> bool:
    return data[:4] == b"\x7fELF"


def dynamic_is_mapped(path: Path) -> bool:
    """True if PT_DYNAMIC lies inside a PT_LOAD segment (64-bit little endian)."""
    data = path.read_bytes()
    if not is_elf(data) or data[4] != 2 or data[5] != 1:
        return True
    (e_phoff,) = struct.unpack_from("<Q", data, 0x20)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)
    loads: list[tuple[int, int]] = []
    dynamic: tuple[int, int] | None = None
    for index in range(e_phnum):
        offset = e_phoff + index * e_phentsize
        (p_type,) = struct.unpack_from("<I", data, offset)
        (p_offset,) = struct.unpack_from("<Q", data, offset + 8)
        (p_filesz,) = struct.unpack_from("<Q", data, offset + 32)
        if p_type == PT_LOAD:
            loads.append((p_offset, p_filesz))
        elif p_type == PT_DYNAMIC:
            dynamic = (p_offset, p_filesz)
    if dynamic is None:
        return True
    start, size = dynamic
    return any(off <= start and start + size <= off + filesz for off, filesz in loads)


def read_wheel(path: Path) -> list[tuple[zipfile.ZipInfo, bytes]]:
    with zipfile.ZipFile(path) as zf:
        return [(info, zf.read(info.filename)) for info in zf.infolist()]


def write_wheel(path: Path, entries: list[tuple[zipfile.ZipInfo, bytes]]) -> None:
    """Rewrite the wheel, refreshing RECORD for whatever changed."""
    names = [info.filename for info, _ in entries]
    record_name = next((n for n in names if n.endswith(".dist-info/RECORD")), None)
    if record_name:
        blobs = {info.filename: data for info, data in entries}
        old = blobs[record_name].decode()
        rows = {row[0]: row for row in csv.reader(io.StringIO(old)) if row}
        new_rows = []
        for name in names:
            if name == record_name:
                new_rows.append([name, "", ""])
            elif name.endswith("/"):
                new_rows.append(rows.get(name, [name, "", ""]))
            else:
                blob = blobs[name]
                new_rows.append([name, record_hash(blob), str(len(blob))])
        buffer = io.StringIO()
        csv.writer(buffer, lineterminator="\n").writerows(new_rows)
        entries = [
            (info, buffer.getvalue().encode() if info.filename == record_name else data)
            for info, data in entries
        ]

    tmp = path.with_name(path.name + ".tmp")
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as out:
        for info, data in entries:
            # Keep the unix mode bits, or the executables lose +x on install.
            new_info = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            new_info.compress_type = info.compress_type
            new_info.external_attr = info.external_attr
            new_info.internal_attr = info.internal_attr
            new_info.create_system = info.create_system
            out.writestr(new_info, data)
    os.replace(tmp, path)


def zipinfo_for(name: str, mode: int) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3  # unix
    info.external_attr = (mode & 0xFFFF) << 16
    return info


def prepare(wheel: Path, stash: Path) -> int:
    prefix = Path(os.environ.get("DFTRACER_DEPS_PREFIX", "/opt/dftracer-deps"))
    lib_src = prefix / "lib64"
    if not lib_src.is_dir():
        log(f"dependency prefix {lib_src} not found")
        return 1

    entries = read_wheel(wheel)
    names = {info.filename for info, _ in entries}

    pkg_lib = next(
        (n.rsplit("/", 1)[0] for n in sorted(names) if n.endswith("/lib64/libdftracer_core.so")),
        None,
    )
    if pkg_lib is None:
        log("could not locate <package>/lib64 in the wheel")
        return 1
    log(f"library directory in wheel: {pkg_lib}")

    stash.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        added = 0
        for pattern in DEP_PATTERNS:
            for src in sorted(lib_src.glob(pattern)):
                arcname = f"{pkg_lib}/{src.name}"
                if arcname in names:
                    continue
                staged = tmp / src.name
                # A zip member must be a real file, and the loader resolves by
                # soname rather than link name.
                shutil.copy(src, staged)
                staged.chmod(0o755)
                subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(staged)], check=True)
                entries.append((zipinfo_for(arcname, 0o755), staged.read_bytes()))
                names.add(arcname)
                added += 1
                log(f"bundled    : {src.name}")
        log(f"bundled {added} dependency librar{'y' if added == 1 else 'ies'} into {pkg_lib}")

    kept: list[tuple[zipfile.ZipInfo, bytes]] = []
    stashed = 0
    for info, data in entries:
        if "/bin/" in info.filename and is_elf(data):
            target = stash / Path(info.filename).name
            target.write_bytes(data)
            target.chmod(0o755)
            (stash / f"{target.name}.arcname").write_text(info.filename)
            (stash / f"{target.name}.mode").write_text(str(info.external_attr))
            log(f"stashed    : {info.filename}")
            stashed += 1
            continue
        kept.append((info, data))

    if stashed == 0:
        log("no ELF executables in the wheel")

    write_wheel(wheel, kept)
    log(f"rewrote {wheel.name}: +{added} libraries, -{stashed} executables")
    return 0


def finish(wheel: Path, stash: Path) -> int:
    arcnames = sorted(stash.glob("*.arcname"))
    if not arcnames:
        log("nothing stashed, leaving the wheel alone")
        return 0

    entries = read_wheel(wheel)
    for marker in arcnames:
        binary = marker.with_suffix("")
        arcname = marker.read_text().strip()
        # <package>/bin/<exe> -> the libraries live in <package>/lib64
        depth = len(Path(arcname).parts) - 1
        rpath = ":".join(
            f"$ORIGIN/{'../' * (depth - 1)}{leaf}" for leaf in ("lib64", "lib")
        )
        subprocess.run(["patchelf", "--set-rpath", rpath, str(binary)], check=True)
        mode_file = stash / f"{binary.name}.mode"
        info = zipinfo_for(arcname, 0o755)
        if mode_file.exists():
            info.external_attr = int(mode_file.read_text())
        entries.append((info, binary.read_bytes()))
        log(f"restored   : {arcname} (rpath: {rpath})")

    write_wheel(wheel, entries)

    broken: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        layout = Path(tmp) / "layout"
        with zipfile.ZipFile(wheel) as zf:
            zf.extractall(layout)
        for candidate in layout.rglob("*"):
            if candidate.is_file() and is_elf(candidate.read_bytes()):
                candidate.chmod(0o755)

        # LD_LIBRARY_PATH is set for the build; the wheel must not depend on it.
        env = {
            key: value
            for key, value in os.environ.items()
            if key not in ("LD_LIBRARY_PATH", "LD_PRELOAD")
        }

        for marker in arcnames:
            arcname = marker.read_text().strip()
            target = layout / arcname
            mapped = dynamic_is_mapped(target)
            proc = subprocess.run(
                [str(target)],
                env=env,
                cwd="/",
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=60,
            )
            # Death by signal means it cannot start; a non-zero exit is normal
            # for a tool invoked without arguments.
            status = (
                f"signal {-proc.returncode}"
                if proc.returncode < 0
                else f"exit {proc.returncode}"
            )
            log(f"verified   : {arcname} pt_dynamic_mapped={mapped} startup={status}")
            unresolved = "error while loading shared libraries" in proc.stdout
            if proc.returncode < 0 or not mapped or unresolved:
                log(f"  output: {proc.stdout.strip()[:400]}")
                ldd = subprocess.run(
                    ["ldd", str(target)],
                    env=env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                )
                log(f"  ldd: {ldd.stdout.strip()[:600]}")
                broken.append(arcname)

    if broken:
        log(f"{len(broken)} executable(s) cannot start: {broken}")
        return 1
    return 0


def main(argv: list[str]) -> int:
    if len(argv) != 4 or argv[1] not in ("prepare", "finish"):
        print(__doc__)
        return 2
    command, wheel, stash = argv[1], Path(argv[2]), Path(argv[3])
    return prepare(wheel, stash) if command == "prepare" else finish(wheel, stash)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
