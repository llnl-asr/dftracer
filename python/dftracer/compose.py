"""dftracer-compose: link libdftracer_core for this machine.

An installed DFTracer keeps the parts libdftracer_core is linked from under
<libdir>/dftracer/compose: the core archive, and an archive per module variant
(MPI by implementation and version, HDF5, PAPI, hwloc, HIP, CUDA) from the
wheel and, if installed, dftracer-modules. This command

  1. finds which of those libraries this machine has, and which variant of
     each module fits it (the compose project's detect mode),
  2. builds from source each variant the machine needs and the tree lacks,
     and anything named in --from-source / DFTRACER_BUILD_FROM_SOURCE,
  3. links libdftracer_core (and _dbg) from the tree and installs it in place.

    dftracer-compose                     compose for this machine
    dftracer-compose --check             only report what would be linked
    dftracer-compose --no-build          link what is here; build nothing
    dftracer-compose --from-source mpi   rebuild mpi from source, then compose
    dftracer-compose --verbose           show each step and stream its log
    DFTRACER_BUILD_FROM_SOURCE=all dftracer-compose

Editing a composition: the choices below are kept in
<libdir>/dftracer/compose-settings.json, and every later run starts from
them, so each command changes the composition one step at a time.

    dftracer-compose --status                  what is linked now (no relink)
    dftracer-compose --disable hip,papi        relink without those modules
    dftracer-compose --enable papi             relink with it again
    dftracer-compose --variant mpi=mpi-openmpi-4.1   link that variant, not
                                               the detected one (=auto undoes)
    dftracer-compose --reset                   forget all of the above
    dftracer-compose --disable hip --check     preview an edit; change nothing

It exits non-zero when a module the machine needs could not be linked, or
when a source build failed (a module asked for with --from-source that does
not build keeps its prebuilt variant, and the command says so).
dftracer_install_info shows the composition afterwards: what is linked,
and whether each variant came prebuilt or was built here.

--from-source takes a comma-separated list of modules, "core", or "all". A
source build needs the DFTracer sources (--source, DFTRACER_SOURCE_DIR, or the
copy the package ships), CMake and a C/C++ compiler, and fetches DFTracer's
dependencies as a source install does.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MODULES = ("mpi", "hdf5", "papi", "hwloc", "hip", "cuda")
SETTINGS_NAME = "compose-settings.json"


def package_prefix() -> Path:
    """The install prefix DFTracer's libraries live in (the package dir)."""
    return Path(__file__).resolve().parent


def find_compose_dir(prefix: Path) -> Path:
    for libdir in ("lib64", "lib"):
        candidate = prefix / libdir / "dftracer" / "compose"
        if (candidate / "CMakeLists.txt").exists():
            return candidate
    raise SystemExit(f"dftracer-compose: no compose tree under {prefix}/lib*/dftracer")


def find_source(explicit: str | None) -> Path | None:
    for candidate in (explicit, os.environ.get("DFTRACER_SOURCE_DIR"),
                      str(package_prefix() / "share" / "dftracer" / "source")):
        if candidate and (Path(candidate) / "tools" / "ci" / "build-module.sh").exists():
            return Path(candidate)
    return None


class StepFailed(Exception):
    """A build or link step failed; the message names its log."""


VERBOSE = False


def step(message: str) -> None:
    print(f"dftracer-compose: {message}", flush=True)


def run(cmd: list[str], log: Path, **kwargs) -> None:
    """Run cmd with its output in log; with --verbose, also on the terminal."""
    if VERBOSE:
        print("+ " + " ".join(cmd), flush=True)
        print(f"  (log {log})", flush=True)
    with log.open("w") as out:
        if VERBOSE:
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, **kwargs)
            assert proc.stdout is not None
            for line in proc.stdout:
                out.write(line)
                sys.stdout.write("  | " + line)
            returncode = proc.wait()
        else:
            returncode = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT,
                                        **kwargs).returncode
    if returncode != 0:
        tail = log.read_text().splitlines()[-15:]
        raise StepFailed(f"{' '.join(cmd[:3])}... failed (log {log}):\n" + "\n".join(tail))


def compose(tree: Path, prefix: Path, work: Path, install: bool,
            cmake_args: list[str]) -> list[tuple[str, str, str]]:
    """Run the compose project; return its (module, variant, status) lines."""
    build = work / "compose-build"
    if build.exists():
        shutil.rmtree(build)
    step("detecting this machine's libraries" if not install
         else "linking libdftracer_core from the tree")
    run(["cmake", "-S", str(tree), "-B", str(build),
         f"-DCMAKE_INSTALL_PREFIX={prefix}", *cmake_args],
        work / "compose-configure.log")
    if not install:
        # A variant linked by name (--variant) that is not the one this
        # machine's library needs: say so where it is seen.
        text = " ".join((work / "compose-configure.log").read_text().split())
        for warning in text.split("[DFTRACER] ")[1:]:
            if "as asked, but" in warning:
                step("warning: " + warning.split(" CMake ")[0].split(" -- ")[0].strip())
    if install:
        run(["cmake", "--build", str(build)], work / "compose-build.log")
        step(f"installing into {prefix}")
        run(["cmake", "--install", str(build)], work / "compose-install.log")
    records = []
    for line in (build / "composition.txt").read_text().splitlines():
        parts = line.split()
        if len(parts) == 3:
            records.append((parts[0], parts[1], parts[2]))
    return records


def build_from_source(part: str, source: Path, tree: Path, work: Path,
                      jobs: int, cmake_args: list[str]) -> None:
    """Build the core or one module from source into the compose tree."""
    out = work / f"source-{part}"
    cmd = ["bash", str(source / "tools" / "ci" / "build-module.sh"),
           "--src", str(source), "--out", str(out), "--jobs", str(jobs)]
    cmd += ["--core"] if part == "core" else ["--module", part]
    if part == "hip" and os.environ.get("ROCM_PATH"):
        cmd += ["--rocm", os.environ["ROCM_PATH"]]
    if part == "cuda":
        cuda = os.environ.get("DFTRACER_CUDA_PATH") or os.environ.get("CUDA_HOME")
        if cuda:
            cmd += ["--cuda", cuda]
    if part == "papi" and os.environ.get("PAPI_DIR"):
        cmd += ["--papi", os.environ["PAPI_DIR"]]
    for arg in cmake_args:
        cmd += ["--cmake-arg", arg]
    step(f"building {part} from source ...")
    run(cmd, work / f"source-{part}.log")
    built = next((out / "tree").glob("**/dftracer/compose"))
    origin = {"origin": "source", "built_on": platform.node(),
              "built_at": datetime.datetime.now().isoformat(timespec="seconds")}
    if part == "core":
        shutil.rmtree(tree / "core")
        shutil.copytree(built / "core", tree / "core", symlinks=True)
        (tree / "core" / "origin.json").write_text(json.dumps(origin, indent=2) + "\n")
        return
    for variant in (built / "modules" / part).iterdir():
        dest = tree / "modules" / part / variant.name
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(variant, dest, symlinks=True)
        # Prebuilt variants (the wheels, the CI tree) have no "origin";
        # dftracer_install_info tells the two apart by it.
        meta = dest / "component.json"
        if meta.exists():
            data = json.loads(meta.read_text())
            data.update(origin)
            meta.write_text(json.dumps(data, indent=2) + "\n")
        step(f"built {part} {variant.name}")


# --- Saved composition settings --------------------------------------------

def settings_path(tree: Path) -> Path:
    """<libdir>/dftracer/compose-settings.json, next to composition.txt."""
    return tree.parent / SETTINGS_NAME


def load_settings(tree: Path) -> dict:
    path = settings_path(tree)
    data = {}
    if path.exists():
        try:
            data = json.loads(path.read_text())
        except ValueError:
            step(f"ignoring unreadable {path}")
    return {"disabled": sorted(set(data.get("disabled", [])) & set(MODULES)),
            "variants": {m: v for m, v in data.get("variants", {}).items() if m in MODULES}}


def save_settings(tree: Path, settings: dict) -> None:
    path = settings_path(tree)
    if not settings["disabled"] and not settings["variants"]:
        path.unlink(missing_ok=True)
        return
    path.write_text(json.dumps(settings, indent=2, sort_keys=True) + "\n")


def parse_modules(value: str, flag: str) -> set[str]:
    names = {m.strip() for m in value.split(",") if m.strip()}
    unknown = names - set(MODULES)
    if unknown:
        raise SystemExit(f"dftracer-compose: unknown in {flag}: {sorted(unknown)} "
                         f"(modules: {', '.join(MODULES)})")
    return names


def edit_settings(settings: dict, args: argparse.Namespace, tree: Path) -> dict:
    """The settings with this command's --reset/--enable/--disable/--variant."""
    new = {"disabled": set(settings["disabled"]), "variants": dict(settings["variants"])}
    if args.reset:
        new = {"disabled": set(), "variants": {}}
    for value in args.enable:
        new["disabled"] -= parse_modules(value, "--enable")
    for value in args.disable:
        new["disabled"] |= parse_modules(value, "--disable")
    for value in args.variant:
        module, sep, variant = value.partition("=")
        module, variant = module.strip(), variant.strip()
        if not sep or module not in MODULES or not variant:
            raise SystemExit(f"dftracer-compose: --variant takes <module>=<variant> "
                             f"or <module>=auto, not {value!r}")
        if variant == "auto":
            new["variants"].pop(module, None)
            continue
        here = tree / "modules" / module
        have = sorted(p.name for p in here.iterdir()) if here.is_dir() else []
        if variant not in have:
            raise SystemExit(f"dftracer-compose: no {module} variant {variant} here "
                             f"(have: {', '.join(have) or 'none'})")
        new["variants"][module] = variant
        new["disabled"].discard(module)
    return {"disabled": sorted(new["disabled"]), "variants": new["variants"]}


def settings_cmake_args(settings: dict) -> list[str]:
    """The compose project's arguments for these settings."""
    args = []
    if settings["disabled"]:
        enabled = [m for m in MODULES if m not in settings["disabled"]]
        args.append("-DDFTRACER_COMPOSE_MODULES=" + (",".join(enabled) or "none"))
    for module, variant in sorted(settings["variants"].items()):
        args.append(f"-DDFTRACER_COMPOSE_{module.upper()}={variant}")
    return args


def describe_settings(settings: dict) -> str:
    parts = []
    if settings["disabled"]:
        parts.append("disabled: " + ", ".join(settings["disabled"]))
    if settings["variants"]:
        parts.append("pinned: " + ", ".join(f"{m}={v}" for m, v in
                                            sorted(settings["variants"].items())))
    return "; ".join(parts) or "none (every module this machine has, as detected)"


def print_status(tree: Path, settings: dict) -> int:
    """What the installed libdftracer_core is linked with, from the files the
    last compose wrote; nothing is detected or relinked."""
    record = tree.parent / "composition.txt"
    print(f"dftracer-compose: {tree.parent.parent}/libdftracer_core.so")
    if not record.exists():
        print("  not composed yet: run dftracer-compose")
    for line in (record.read_text().splitlines() if record.exists() else []):
        parts = line.split()
        if len(parts) != 3:
            continue
        module, variant, status = parts
        origin = ""
        meta = tree / "modules" / module / variant / "component.json"
        if meta.exists():
            data = json.loads(meta.read_text())
            origin = data.get("origin", "prebuilt")
            if origin == "source" and data.get("built_on"):
                origin += f" ({data['built_on']}, {data.get('built_at', '?')})"
        pin = " (pinned)" if settings["variants"].get(module) == variant else ""
        print(f"  {module:6} {variant:28} {status:8} {origin}{pin}")
    print("settings: " + describe_settings(settings))
    return 0


def parse_from_source(value: str) -> set[str]:
    parts = {p.strip() for p in value.split(",") if p.strip()}
    if "all" in parts:
        return {"core", *MODULES}
    unknown = parts - {"core", *MODULES}
    if unknown:
        raise SystemExit(f"dftracer-compose: unknown in --from-source: {sorted(unknown)}")
    return parts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="dftracer-compose", description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="\n".join(__doc__.splitlines()[2:]))
    parser.add_argument("--prefix", type=Path, default=package_prefix(),
                        help="DFTracer install prefix (default: this package)")
    parser.add_argument("--check", action="store_true",
                        help="report what would be linked; change nothing")
    parser.add_argument("--no-build", action="store_true",
                        help="link the variants that are here and build none")
    parser.add_argument("--from-source",
                        default=os.environ.get("DFTRACER_BUILD_FROM_SOURCE", ""),
                        help="modules, core or all to build from source")
    parser.add_argument("--source", help="DFTracer source tree for source builds")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--cmake-arg", action="append", default=[],
                        help="extra CMake argument for source builds (repeatable)")
    parser.add_argument("--keep", action="store_true", help="keep the work dir")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="print each step's command and stream its output")
    edit = parser.add_argument_group("editing the composition (kept for later runs)")
    edit.add_argument("--status", action="store_true",
                      help="show what is linked now and the saved settings; relink nothing")
    edit.add_argument("--enable", action="append", default=[], metavar="MODULES",
                      help="link these modules again (comma-separated)")
    edit.add_argument("--disable", action="append", default=[], metavar="MODULES",
                      help="leave these modules out (comma-separated)")
    edit.add_argument("--variant", action="append", default=[], metavar="MODULE=VARIANT",
                      help="link this variant instead of the detected one; "
                           "MODULE=auto goes back to detecting")
    edit.add_argument("--reset", action="store_true",
                      help="forget saved --disable and --variant choices")
    args = parser.parse_args(argv)
    global VERBOSE
    VERBOSE = args.verbose

    prefix = args.prefix.resolve()
    tree = find_compose_dir(prefix)
    saved = load_settings(tree)
    if args.status:
        return print_status(tree, saved)
    settings = edit_settings(saved, args, tree)
    forced = parse_from_source(args.from_source)
    off = forced & set(settings["disabled"])
    if off:
        step("not building disabled modules: " + ", ".join(sorted(off)))
        forced -= off
    work = Path(tempfile.mkdtemp(prefix="dftracer-compose-"))
    try:
        status = _main(args, prefix, tree, forced, work, settings)
        # An edit sticks once the library it describes is installed.
        if not args.check and settings != saved:
            save_settings(tree, settings)
            step(f"settings saved ({settings_path(tree)}): "
                 + describe_settings(settings))
        return status
    except StepFailed as error:
        args.keep = True
        raise SystemExit(f"dftracer-compose: {error}") from None
    finally:
        if args.keep:
            print(f"dftracer-compose: work dir {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


def _main(args: argparse.Namespace, prefix: Path, tree: Path, forced: set[str],
          work: Path, settings: dict) -> int:
    step(f"compose tree {tree}")
    step("settings: " + describe_settings(settings))
    cmake_args = settings_cmake_args(settings)
    records = compose(tree, prefix, work, install=False, cmake_args=cmake_args)
    needed = {m for m, _, status in records if status in ("missing", "stale")}
    detected = {m for m, _, status in records if status in ("linked", "missing", "stale")}
    to_build = sorted(needed | (forced & detected))
    if "core" in forced:
        to_build.insert(0, "core")
    for module, variant, status in records:
        note = " (rebuild from source)" if module in forced and status == "linked" else ""
        print(f"  {module:6} {variant:28} {status}{note}")
    if args.check:
        if to_build:
            print("would build from source: " + ", ".join(to_build))
        return 0
    if to_build and args.no_build:
        print("dftracer-compose: not building " + ", ".join(to_build) + " (--no-build)")
        to_build = []
    if to_build:
        source = find_source(args.source)
        if source is None:
            raise SystemExit(
                "dftracer-compose: " + ", ".join(to_build) + " must be built from "
                "source, but no DFTracer source tree was found; pass --source or "
                "set DFTRACER_SOURCE_DIR")
        failed = []
        for part in to_build:
            try:
                build_from_source(part, source, tree, work, args.jobs, args.cmake_arg)
            except StepFailed as error:
                # The others can still be built and linked.
                failed.append(part)
                print(f"dftracer-compose: {part} did not build: {error}")
                args.keep = True
        if "core" in failed:
            raise SystemExit("dftracer-compose: the core did not build; nothing composed")
    else:
        failed = []
    records = compose(tree, prefix, work, install=True, cmake_args=cmake_args)
    linked = [f"{m}:{v}" for m, v, s in records if s == "linked"]
    step("libdftracer_core linked with "
         + (", ".join(linked) if linked else "no optional modules"))
    status = 0
    # A module asked for from source that did not build is still linked,
    # from its prebuilt variant; that is not what was asked for.
    kept = [p for p in failed if p in forced and (p, "linked") in
            {(m, s) for m, _, s in records}]
    if kept:
        step("built from source as asked, but failed, so the prebuilt variant is "
             "linked instead: " + ", ".join(kept))
        status = 1
    missing = [m for m, _, s in records if s in ("missing", "stale")]
    if missing:
        step("still missing: " + ", ".join(missing))
        if not args.no_build:
            status = 1
    if status:
        step(f"build logs in {work}")
    step("dftracer_install_info shows the composition")
    return status


if __name__ == "__main__":
    sys.exit(main())
