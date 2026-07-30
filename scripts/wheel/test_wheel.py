#!/usr/bin/env python3
"""Post-build validation of a dftracer wheel.

Runs as cibuildwheel's CIBW_TEST_COMMAND, i.e. inside the manylinux container
against the repaired wheel installed into a fresh virtualenv -- never against
the source tree. Nothing here imports the repo's test suite: those tests target
the in-tree Python API, while this only exercises what the wheel itself ships.

    python scripts/wheel/test_wheel.py [--no-trace]

Checks, in order:
  1. `import dftracer` works and reports the expected version
  2. the pybind11 extension modules import and expose the C API
  3. every bundled shared library dlopen()s
  4. every native executable in dftracer/bin starts (catches ELF corruption
     from the auditwheel/patchelf rewrite, which segfaults before main)
  5. a real trace is produced: dftracer_service + an LD_PRELOAD'd workload,
     and the resulting .pfw trace contains the I/O events
  6. the pydftracer Python API layer, if installed, can log through the wheel
"""

from __future__ import annotations

import argparse
import ctypes
import glob
import gzip
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

FAILURES: list[str] = []
SKIPS: list[str] = []


def check(name: str):
    """Decorator turning a function into a reported check."""

    def wrap(fn):
        print(f"\n=== {name} ===", flush=True)
        try:
            result = fn()
        except Skip as exc:
            print(f"SKIP: {exc}", flush=True)
            SKIPS.append(f"{name}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report, do not abort
            print(f"FAIL: {type(exc).__name__}: {exc}", flush=True)
            FAILURES.append(f"{name}: {exc}")
        else:
            print("PASS", flush=True)
            return result
        return None

    return wrap


class Skip(Exception):
    pass


def read_trace(path: Path) -> str:
    if path.suffix == ".gz":
        with gzip.open(path, "rt", errors="replace") as fh:
            return fh.read()
    return path.read_text(errors="replace")


def find_traces(directory: Path) -> list[Path]:
    return sorted(
        p
        for p in directory.rglob("*")
        if p.is_file() and (".pfw" in p.name) and p.stat().st_size > 0
    )


def is_elf(path: Path) -> bool:
    try:
        with open(path, "rb") as fh:
            return fh.read(4) == b"\x7fELF"
    except OSError:
        return False


def clean_env(**overrides: str) -> dict[str, str]:
    """The ambient environment minus anything that could mask a bad RPATH."""
    env = {
        key: value
        for key, value in os.environ.items()
        if key not in ("LD_LIBRARY_PATH", "LD_PRELOAD")
    }
    env.update(overrides)
    return env


def wait_for(predicate, what: str, timeout: float = 30.0) -> bool:
    """Poll until predicate() is true. Returns False on timeout."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.1)
    print(f"timed out after {timeout:g}s waiting for {what}", flush=True)
    return False


def run(cmd, env=None, cwd=None, timeout=120):
    return subprocess.run(
        cmd,
        env=env,
        cwd=cwd,
        timeout=timeout,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--no-trace",
        action="store_true",
        help="skip the runtime tracing checks (packaging checks only)",
    )
    args = parser.parse_args()

    @check("import dftracer")
    def _import():
        import dftracer

        pkg_dir = Path(dftracer.__file__).parent
        print(f"package    : {pkg_dir}")
        print(f"version    : {dftracer.__version__}")
        if str(pkg_dir).endswith(os.sep + "python" + os.sep + "dftracer"):
            raise AssertionError("imported from the source tree, not the wheel")
        expected = os.environ.get("DFTRACER_EXPECTED_VERSION")
        if expected and dftracer.__version__ != expected:
            raise AssertionError(
                f"version {dftracer.__version__} != expected {expected}"
            )
        return pkg_dir

    pkg_dir = _import
    if pkg_dir is None:
        print("\nFATAL: dftracer is not importable", flush=True)
        return 1

    libs_dir = pkg_dir.parent / f"{pkg_dir.name}.libs"

    @check("extension modules")
    def _extensions():
        import importlib

        required = {
            "initialize",
            "finalize",
            "log_event",
            "get_time",
            "enter_event",
            "exit_event",
            "get_config",
        }
        for mod_name in ("dftracer.dftracer", "dftracer.dftracer_dbg"):
            mod = importlib.import_module(mod_name)
            missing = required - set(dir(mod))
            if missing:
                raise AssertionError(f"{mod_name} missing API: {sorted(missing)}")
            print(f"{mod_name}: {mod.__file__}")

    @check("bundled shared libraries")
    def _libraries():
        candidates = sorted(
            {
                p
                for pattern in ("lib64/*.so*", "lib/*.so*")
                for p in pkg_dir.glob(pattern)
                if p.is_file()
            }
        )
        if libs_dir.is_dir():
            candidates += sorted(p for p in libs_dir.glob("*.so*") if p.is_file())
        if not candidates:
            raise AssertionError("no shared libraries found in the wheel")
        for so in candidates:
            ctypes.CDLL(str(so))
            print(f"dlopen ok  : {so.relative_to(pkg_dir.parent)}")

    @check("native executables start")
    def _executables():
        bin_dir = pkg_dir / "bin"
        if not bin_dir.is_dir():
            raise Skip("wheel ships no bin/ directory")
        elves = sorted(p for p in bin_dir.iterdir() if p.is_file() and is_elf(p))
        if not elves:
            raise Skip("no ELF executables in bin/")
        broken = []
        for exe in elves:
            # Neutral cwd and no LD_LIBRARY_PATH: the wheel must stand alone.
            proc = run([str(exe)], env=clean_env(), cwd="/", timeout=60)
            # A signal death means it cannot start; a non-zero exit is normal
            # for a tool invoked without arguments.
            if proc.returncode < 0:
                broken.append(f"{exe.name} (signal {-proc.returncode})")
                print(f"CRASH      : {exe.name} signal {-proc.returncode}")
            elif "error while loading shared libraries" in proc.stdout:
                broken.append(f"{exe.name} ({proc.stdout.strip()})")
                print(f"UNRESOLVED : {exe.name}: {proc.stdout.strip()}")
            else:
                print(f"starts ok  : {exe.name} (exit {proc.returncode})")
        if broken:
            raise AssertionError("executables crash on startup: " + ", ".join(broken))

    if args.no_trace:
        print("\nskipping runtime tracing checks (--no-trace)", flush=True)
        return report()

    @check("end to end trace (service + LD_PRELOAD)")
    def _trace():
        service = pkg_dir / "bin" / "dftracer_service"
        preload = pkg_dir / "lib64" / "libdftracer_preload.so"
        if not service.is_file():
            raise Skip("dftracer_service not in the wheel")
        if not preload.is_file():
            raise Skip("libdftracer_preload.so not in the wheel")

        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            log_dir, svc_dir, data_dir = tmp / "logs", tmp / "svc", tmp / "data"
            for d in (log_dir, svc_dir, data_dir):
                d.mkdir()

            env = clean_env()
            env.update(
                DFTRACER_ENABLE="1",
                DFTRACER_INIT="PRELOAD",
                DFTRACER_LOG_FILE=str(log_dir / "trace"),
                DFTRACER_DATA_DIR=str(data_dir),
                DFTRACER_TRACE_INTERVAL_MS="200",
                DFTRACER_INC_METADATA="1",
            )

            start = run([str(service), "start", str(svc_dir)], env=env)
            print(f"service start (exit {start.returncode}): {start.stdout.strip()}")
            if start.returncode != 0:
                raise AssertionError(f"dftracer_service start failed: {start.stdout}")
            wait_for(lambda: any(svc_dir.glob("*.pid")), "service pid file")

            workload = (
                "import os\n"
                f"p = {str(data_dir / 'payload.bin')!r}\n"
                "with open(p, 'wb') as fh:\n"
                "    fh.write(b'x' * (1 << 20))\n"
                "with open(p, 'rb') as fh:\n"
                "    assert len(fh.read()) == (1 << 20)\n"
                "os.remove(p)\n"
                "print('workload done')\n"
            )
            wl_env = dict(env)
            wl_env["LD_PRELOAD"] = str(preload)
            proc = run([sys.executable, "-c", workload], env=wl_env, cwd=str(tmp))
            print(f"workload (exit {proc.returncode}): {proc.stdout.strip()}")
            if proc.returncode != 0:
                raise AssertionError(f"LD_PRELOAD'd workload failed: {proc.stdout}")

            stop = run([str(service), "stop", str(svc_dir)], env=env)
            print(f"service stop (exit {stop.returncode}): {stop.stdout.strip()}")
            # The service flushes and compresses on shutdown.
            wait_for(
                lambda: bool(find_traces(log_dir) or find_traces(svc_dir)),
                "trace file",
            )

            traces = find_traces(log_dir) + find_traces(svc_dir)
            if not traces:
                listing = sorted(str(p.relative_to(tmp)) for p in tmp.rglob("*"))
                raise AssertionError(f"no trace file produced; tree: {listing}")

            events = 0
            posix = 0
            for trace in traces:
                text = read_trace(trace)
                print(f"trace      : {trace.name} ({len(text)} bytes)")
                for line in text.splitlines():
                    line = line.strip().rstrip(",")
                    if not line or line in ("[", "]"):
                        continue
                    try:
                        event = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    events += 1
                    if str(event.get("cat", "")).upper().startswith("POSIX"):
                        posix += 1
            print(f"events     : {events} ({posix} POSIX)")
            if events == 0:
                raise AssertionError("trace file contains no parsable events")
            if posix == 0:
                raise AssertionError(
                    "no POSIX events captured -- gotcha/brahma interception is broken"
                )

    @check("pydftracer API layer")
    def _api():
        try:
            from dftracer.python import dftracer as dft_logger
        except ImportError as exc:
            raise Skip(f"pydftracer not usable: {exc}") from exc

        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            os.environ["DFTRACER_ENABLE"] = "1"
            os.environ["DFTRACER_INIT"] = "FUNCTION"
            os.environ["DFTRACER_LOG_FILE"] = str(tmp / "api")
            inst = dft_logger.initialize_log(
                logfile=str(tmp / "api"), data_dir=str(tmp), process_id=-1
            )
            start = inst.get_time()
            inst.log_event("WHEEL_SMOKE", "WHEEL_TEST", start, 5)
            inst.finalize()
            print(f"logged through {type(inst).__module__}")

    return report()


def report() -> int:
    print("\n================ wheel test summary ================")
    for skip in SKIPS:
        print(f"SKIP: {skip}")
    if FAILURES:
        for failure in FAILURES:
            print(f"FAIL: {failure}")
        print(f"{len(FAILURES)} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
