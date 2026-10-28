#!/usr/bin/env python3
"""Run CTest tests under valgrind and fail on unsuppressed errors.

Usage examples:
  # Run all discovered CTests
  python3 scripts/valgrind_ctest_runner.py --build-dir build/.../dftracer.dftracer

  # Run selected tests with suppressions
  python3 scripts/valgrind_ctest_runner.py \
      --build-dir build/.../dftracer.dftracer \
      --suppression test/valgrind/test_cpp_known_syscall.supp \
      --tests test_cpp_basic_meta test_cpp_basic_affinity
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run CTest tests under valgrind")
    parser.add_argument("--build-dir", required=True, help="CTest build directory")
    parser.add_argument(
        "--tests",
        nargs="*",
        default=None,
        help="Specific CTest names to run; omit to run all discovered tests",
    )
    parser.add_argument(
        "--exclude-regex",
        default=None,
        help="Regex of test names to skip (applied after --tests selection)",
    )
    parser.add_argument(
        "--suppression",
        action="append",
        default=[],
        help="Valgrind suppression file (can be provided multiple times)",
    )
    parser.add_argument(
        "--log-dir",
        default=None,
        help="Directory for valgrind logs (default: <build-dir>/valgrind-ctest)",
    )
    parser.add_argument(
        "--max-tests",
        type=int,
        default=0,
        help="Run at most N tests (0 means no limit)",
    )
    parser.add_argument(
        "--summary-json",
        default=None,
        help="Path to write machine-readable summary JSON (default: <log-dir>/summary.json)",
    )
    parser.add_argument(
        "--log-level",
        default="INFO",
        help="DFTRACER_LOG_LEVEL used for normal valgrind runs (default: INFO)",
    )
    parser.add_argument(
        "--debug-log-level",
        default="DEBUG",
        help="DFTRACER_LOG_LEVEL used when rerunning failed tests for diagnostics (default: DEBUG)",
    )
    parser.add_argument(
        "--debug-rerun-on-failure",
        action="store_true",
        help="Rerun failing tests once (without valgrind) using --debug-log-level and store output logs",
    )
    return parser.parse_args()


def load_ctest_json(build_dir: Path) -> dict:
    raw = subprocess.check_output(["ctest", "--show-only=json-v1"], cwd=build_dir, text=True)
    return json.loads(raw)


def parse_env_items(test_obj: dict) -> list[str]:
    merged: list[str] = []
    for prop in test_obj.get("properties", []):
        if prop.get("name") != "ENVIRONMENT":
            continue
        value = prop.get("value")
        if isinstance(value, list):
            merged.extend([x for x in value if isinstance(x, str) and x])
        if isinstance(value, str):
            merged.extend([x for x in value.split(";") if x])
    return merged


def test_will_fail(test_obj: dict) -> bool:
    for prop in test_obj.get("properties", []):
        if prop.get("name") == "WILL_FAIL":
            value = prop.get("value")
            if isinstance(value, bool):
                return value
            if isinstance(value, str):
                return value.strip().upper() in {"1", "ON", "TRUE", "YES"}
    return False


def selected_tests(all_tests: list[dict], names: list[str] | None, exclude_regex: str | None) -> list[dict]:
    tests_by_name = {t.get("name"): t for t in all_tests if t.get("name")}

    if names:
        missing = [name for name in names if name not in tests_by_name]
        if missing:
            raise ValueError(f"CTest names not found: {', '.join(missing)}")
        chosen = [tests_by_name[name] for name in names]
    else:
        chosen = [t for t in all_tests if t.get("name")]

    if exclude_regex:
        rx = re.compile(exclude_regex)
        chosen = [t for t in chosen if not rx.search(t["name"])]

    return chosen


def summarize_log(log_file: Path) -> tuple[str, str]:
    if not log_file.exists():
        return "(missing log)", "(no leak line)"

    error_summary = "(no ERROR SUMMARY line)"
    leak_line = "(no definitely lost line)"

    with log_file.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if "ERROR SUMMARY:" in line and error_summary.startswith("(no"):
                error_summary = line.strip()
            if "definitely lost:" in line and leak_line.startswith("(no"):
                leak_line = line.strip()
            if not error_summary.startswith("(no") and not leak_line.startswith("(no"):
                break

    return error_summary, leak_line


def error_count(error_summary: str) -> int | None:
    match = re.search(r"ERROR SUMMARY:\s+([0-9,]+)\s+errors", error_summary)
    if not match:
        return None
    return int(match.group(1).replace(",", ""))


def definitely_lost_bytes(leak_line: str) -> int | None:
    match = re.search(r"definitely lost:\s+([0-9,]+)\s+bytes", leak_line)
    if not match:
        return None
    return int(match.group(1).replace(",", ""))


def has_unsuppressed_valgrind_failure(error_summary: str, leak_line: str) -> bool:
    errors = error_count(error_summary)
    if errors is not None and errors > 0:
        return True

    lost = definitely_lost_bytes(leak_line)
    if lost is not None and lost > 0:
        return True

    return False


def is_python_token(token: str) -> bool:
    name = Path(token).name
    return name in {"python", "python3"} or name.startswith("python3.") or name.startswith("python.")


def should_skip_valgrind(test_obj: dict) -> tuple[bool, str, str]:
    name = test_obj.get("name", "")
    cmd = test_obj.get("command") or []
    if not cmd:
        return True, "empty command", "other"

    exe_name = Path(cmd[0]).name

    if name.startswith("check_file_exists_"):
        return True, "shell helper test (known valgrind/bash noise)", "other"

    if name == "unit_test_service":
        return True, "service startup test (known external libnl noise)", "other"

    # Same rule as valgrind_ctest_python_runner.is_python_ctest: a test run
    # through a wrapper (mpiexec, the pc_ twin script) that ends in Python is
    # a Python test too, so the two runners split the tests between them.
    if any(is_python_token(token) or token.endswith(".py") for token in cmd):
        return True, "python interpreter test (handled by valgrind-python-ctest runner)", "python"

    if exe_name in {"bash", "sh"}:
        helper_name = Path(cmd[1]).name if len(cmd) > 1 else ""
        if helper_name in {"check_file_at_least.sh", "check_file_not.sh", "check_file.sh"}:
            return True, "shell helper test (known valgrind/bash noise)", "other"

    return False, "", ""


def extract_error_excerpt(log_file: Path, max_lines: int = 80) -> str:
    if not log_file.exists():
        return "(missing valgrind log)"

    keep: list[str] = []
    with log_file.open("r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if "ERROR SUMMARY:" in line:
                keep.append(line)
                continue
            if not line.startswith("=="):
                continue
            if any(
                token in line
                for token in (
                    "Invalid",
                    "Use of uninitialised",
                    "Uninitialised",
                    "Conditional jump",
                    "Syscall param",
                    "Address 0x",
                    "Mismatched",
                    "Invalid free",
                    "definitely lost:",
                    "possibly lost:",
                    "still reachable:",
                    "    at 0x",
                    "    by 0x",
                )
            ):
                keep.append(line)
            if len(keep) >= max_lines:
                break

    if not keep:
        return "(no parsed error excerpt)"
    return "\n".join(keep)


def run_under_valgrind(
    build_dir: Path,
    log_dir: Path,
    test_obj: dict,
    suppression_files: Iterable[Path],
    log_level: str,
) -> tuple[int, Path, str, str, str]:
    test_name = test_obj["name"]
    cmd = test_obj.get("command") or []
    if not cmd:
        raise RuntimeError(f"CTest '{test_name}' has empty command")

    env = os.environ.copy()
    for kv in parse_env_items(test_obj):
        if "=" in kv:
            k, v = kv.split("=", 1)
            env[k] = v

    # Override test-provided level so CI can run INFO by default.
    env["DFTRACER_LOG_LEVEL"] = log_level

    # Disable signal rebinding for valgrind runs to avoid masking memcheck results.
    env["DFTRACER_BIND_SIGNALS"] = "0"

    log_file = log_dir / f"{test_name}.log"

    vg_cmd = [
        "valgrind",
        "--tool=memcheck",
        "--leak-check=full",
        "--show-leak-kinds=definite,possible",
        "--track-origins=yes",
        "--num-callers=40",
        "--error-exitcode=99",
        f"--log-file={log_file}",
    ]
    for suppression in suppression_files:
        vg_cmd.append(f"--suppressions={suppression}")

    vg_cmd.extend(cmd)

    printable_cmd = " ".join(shlex.quote(x) for x in cmd)
    print(f"[valgrind-ctest] RUN {test_name}: {printable_cmd}")
    rc = subprocess.call(vg_cmd, cwd=build_dir, env=env)

    error_summary, leak_line = summarize_log(log_file)
    error_excerpt = extract_error_excerpt(log_file)
    print(f"[valgrind-ctest] {test_name} rc={rc}")
    print(f"[valgrind-ctest] {test_name} {error_summary}")
    print(f"[valgrind-ctest] {test_name} {leak_line}")

    return rc, log_file, error_summary, leak_line, error_excerpt


def rerun_failed_test_with_debug(
    build_dir: Path,
    log_dir: Path,
    test_obj: dict,
    debug_log_level: str,
) -> tuple[int, Path]:
    test_name = test_obj["name"]
    cmd = test_obj.get("command") or []
    debug_log = log_dir / f"{test_name}.debug-rerun.log"
    if not cmd:
        return 127, debug_log

    env = os.environ.copy()
    for kv in parse_env_items(test_obj):
        if "=" in kv:
            k, v = kv.split("=", 1)
            env[k] = v
    env["DFTRACER_LOG_LEVEL"] = debug_log_level

    with debug_log.open("w", encoding="utf-8", errors="replace") as out:
        rc = subprocess.call(cmd, cwd=build_dir, env=env, stdout=out, stderr=subprocess.STDOUT)
    return rc, debug_log


def run_without_valgrind(
    build_dir: Path,
    test_obj: dict,
    reason: str,
) -> tuple[int, str, str]:
    test_name = test_obj["name"]
    cmd = test_obj.get("command") or []
    env = os.environ.copy()
    for kv in parse_env_items(test_obj):
        if "=" in kv:
            k, v = kv.split("=", 1)
            env[k] = v

    printable_cmd = " ".join(shlex.quote(x) for x in cmd)
    print(
        f"[valgrind-ctest] SKIP valgrind for {test_name} ({reason}): {printable_cmd}"
    )
    rc = subprocess.call(cmd, cwd=build_dir, env=env)
    return rc, f"(valgrind skipped: {reason})", "(no valgrind leak line)"


def write_summary(summary_path: Path, payload: dict) -> None:
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    with summary_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)


def print_clean_summary(payload: dict) -> None:
    print("[valgrind-ctest] ===== Summary =====")
    print(f"[valgrind-ctest] selected_tests={payload['selected_tests']}")
    print(f"[valgrind-ctest] valgrind_executed={payload['valgrind_executed']}")
    print(f"[valgrind-ctest] valgrind_skipped={payload['wrapper_skipped']}")
    print(f"[valgrind-ctest] valgrind_skipped_python={payload.get('wrapper_skipped_python', 0)}")
    print(f"[valgrind-ctest] valgrind_skipped_other={payload.get('wrapper_skipped_other', 0)}")
    print(f"[valgrind-ctest] failures={payload['failures']}")
    if payload["failed_tests"]:
        print("[valgrind-ctest] failing_tests:")
        for item in payload["failed_tests"]:
            print(f"  - {item['name']}: {item['error_summary']}")


def main() -> int:
    args = parse_args()
    build_dir = Path(args.build_dir).resolve()
    if not build_dir.is_dir():
        print(f"[valgrind-ctest] build dir not found: {build_dir}")
        return 2

    suppression_files = [Path(p).resolve() for p in args.suppression]
    for suppression in suppression_files:
        if not suppression.is_file():
            print(f"[valgrind-ctest] suppression file not found: {suppression}")
            return 2

    log_dir = Path(args.log_dir).resolve() if args.log_dir else (build_dir / "valgrind-ctest")
    log_dir.mkdir(parents=True, exist_ok=True)
    summary_json = Path(args.summary_json).resolve() if args.summary_json else (log_dir / "summary.json")

    try:
        ctest_info = load_ctest_json(build_dir)
    except Exception as exc:
        print(f"[valgrind-ctest] failed to read ctest json: {exc}")
        return 2

    try:
        tests = selected_tests(ctest_info.get("tests", []), args.tests, args.exclude_regex)
    except ValueError as exc:
        print(f"[valgrind-ctest] {exc}")
        return 2

    if args.max_tests and args.max_tests > 0:
        tests = tests[: args.max_tests]

    if not tests:
        print("[valgrind-ctest] no tests selected")
        return 0

    if shutil.which("valgrind") is None:
        print("[valgrind-ctest] valgrind not found in PATH")
        payload = {
            "selected_tests": len(tests),
            "valgrind_executed": 0,
            "wrapper_skipped": 0,
            "wrapper_skipped_python": 0,
            "wrapper_skipped_other": 0,
            "failures": 1,
            "failed_tests": [
                {
                    "name": "__valgrind__",
                    "log_file": "(runner)",
                    "error_summary": "valgrind not found in PATH",
                    "leak_line": "install valgrind and retry",
                }
            ],
        }
        write_summary(summary_json, payload)
        print_clean_summary(payload)
        print("[valgrind-ctest] FAIL: valgrind binary is required")
        return 1

    print(f"[valgrind-ctest] selected {len(tests)} tests")

    failing: list[tuple[str, str, str, str, str, str]] = []
    wrapper_skipped = 0
    wrapper_skipped_python = 0
    wrapper_skipped_other = 0
    valgrind_executed = 0
    for test_obj in tests:
        expect_fail = test_will_fail(test_obj)
        try:
            cmd = test_obj.get("command") or []
            skip, skip_reason, skip_kind = should_skip_valgrind(test_obj)
            if skip:
                wrapper_skipped += 1
                if skip_kind == "python":
                    wrapper_skipped_python += 1
                else:
                    wrapper_skipped_other += 1
                printable_cmd = " ".join(shlex.quote(x) for x in cmd)
                print(
                    f"[valgrind-ctest] SKIP valgrind for {test_obj['name']} ({skip_reason}): {printable_cmd}"
                )
                continue
            else:
                valgrind_executed += 1
                rc, log_file, error_summary, leak_line, error_excerpt = run_under_valgrind(
                    build_dir=build_dir,
                    log_dir=log_dir,
                    test_obj=test_obj,
                    suppression_files=suppression_files,
                    log_level=args.log_level,
                )
        except Exception as exc:
            print(f"[valgrind-ctest] {test_obj.get('name', '<unknown>')} crashed runner: {exc}")
            failing.append(
                (
                    test_obj.get("name", "<unknown>"),
                    "runner_error",
                    str(exc),
                    "",
                    "(runner exception)",
                    "",
                )
            )
            continue

        has_vg_errors = has_unsuppressed_valgrind_failure(error_summary, leak_line)
        debug_log_file = ""
        if expect_fail:
            if has_vg_errors:
                if args.debug_rerun_on_failure:
                    debug_rc, debug_log = rerun_failed_test_with_debug(
                        build_dir=build_dir,
                        log_dir=log_dir,
                        test_obj=test_obj,
                        debug_log_level=args.debug_log_level,
                    )
                    debug_log_file = str(debug_log)
                    print(
                        f"[valgrind-ctest] {test_obj['name']} debug-rerun rc={debug_rc} log={debug_log_file}"
                    )
                failing.append(
                    (test_obj["name"], str(log_file), error_summary, leak_line, error_excerpt, debug_log_file)
                )
        elif rc != 0:
            if args.debug_rerun_on_failure:
                debug_rc, debug_log = rerun_failed_test_with_debug(
                    build_dir=build_dir,
                    log_dir=log_dir,
                    test_obj=test_obj,
                    debug_log_level=args.debug_log_level,
                )
                debug_log_file = str(debug_log)
                print(
                    f"[valgrind-ctest] {test_obj['name']} debug-rerun rc={debug_rc} log={debug_log_file}"
                )
            failing.append(
                (test_obj["name"], str(log_file), error_summary, leak_line, error_excerpt, debug_log_file)
            )

    payload = {
        "selected_tests": len(tests),
        "valgrind_executed": valgrind_executed,
        "wrapper_skipped": wrapper_skipped,
        "wrapper_skipped_python": wrapper_skipped_python,
        "wrapper_skipped_other": wrapper_skipped_other,
        "failures": len(failing),
        "failed_tests": [
            {
                "name": name,
                "log_file": log_file,
                "error_summary": error_summary,
                "leak_line": leak_line,
                "error_excerpt": error_excerpt,
                "debug_rerun_log": debug_log_file,
            }
            for name, log_file, error_summary, leak_line, error_excerpt, debug_log_file in failing
        ],
    }
    write_summary(summary_json, payload)
    print_clean_summary(payload)

    if failing:
        print("[valgrind-ctest] FAIL: unsuppressed valgrind errors detected")
        for name, log_file, error_summary, leak_line, error_excerpt, debug_log_file in failing:
            print(f"  - {name}: {log_file}")
            print(f"    {error_summary}")
            print(f"    {leak_line}")
            if debug_log_file:
                print(f"    debug_rerun_log: {debug_log_file}")
            if error_excerpt and error_excerpt != "(no parsed error excerpt)":
                print("    --- excerpt ---")
                for line in error_excerpt.splitlines()[:40]:
                    print(f"    {line}")
        return 1

    print("[valgrind-ctest] PASS: all selected tests clean under valgrind")
    return 0


if __name__ == "__main__":
    sys.exit(main())
