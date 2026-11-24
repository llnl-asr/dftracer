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

It also provides dftracer_service's power library: when this machine has no
Variorum 0.8 or later, or one that reads no power domain here, it builds one
(with E-SMI for AMD CPUs) into <libdir>/dftracer/variorum, which the service
loads first. --build-variorum always|never changes that.

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
import contextlib
import ctypes.util
import datetime
import fcntl
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


def build_env(env: dict[str, str] | None = None) -> dict[str, str]:
    """env (default os.environ) without Flux's PMI libraries on LD_LIBRARY_PATH.

    A Flux job puts its own lib dir first there, and GNU ld resolves a shared
    library's dependencies through LD_LIBRARY_PATH: Cray MPICH's libpmi2.so.0
    then resolves to Flux's, which lacks PMI2_Set_threaded, so FindMPI's test
    link fails and MPI is reported absent.
    """
    env = dict(os.environ if env is None else env)
    dirs = env.get("LD_LIBRARY_PATH", "").split(os.pathsep)
    kept = [d for d in dirs
            if not (Path(d).name == "flux" and (Path(d) / "libpmi2.so.0").exists())]
    if kept != dirs:
        env["LD_LIBRARY_PATH"] = os.pathsep.join(kept)
    return env


def run(cmd: list[str], log: Path, **kwargs) -> None:
    """Run cmd with its output in log; with --verbose, also on the terminal."""
    kwargs["env"] = build_env(kwargs.get("env"))
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


def log_warnings(log: Path, needles: tuple[str, ...]) -> None:
    """Show the [DFTRACER] warnings in `log` that name one of `needles`: the
    log is in the work dir, which a successful run deletes."""
    try:
        text = " ".join(log.read_text().split())
    except OSError:
        return
    for block in text.split("CMake Warning")[1:]:
        if "[DFTRACER] " not in block:
            continue
        warning = block.split("[DFTRACER] ", 1)[1]
        if any(needle in warning for needle in needles):
            for end in (" CMake ", " -- ", " Call Stack "):
                warning = warning.split(end)[0]
            step("warning: " + warning.strip())


def static_hwloc_root(tree: Path) -> Path:
    """Where the static hwloc for the hwloc module and Variorum is kept, so a
    later compose links it again without a download."""
    return tree.parent / "hwloc"


def compose(tree: Path, prefix: Path, work: Path, install: bool,
            cmake_args: list[str], build_hwloc: bool = True) -> list[tuple[str, str, str]]:
    """Run the compose project; return its (module, variant, status) lines."""
    build = work / "compose-build"
    if build.exists():
        shutil.rmtree(build)
    step("detecting this machine's libraries" if not install
         else "linking libdftracer_core from the tree")
    if install:
        cmake_args = [*cmake_args,
                      f"-DDFTRACER_COMPOSE_HWLOC_ROOT={static_hwloc_root(tree)}",
                      f"-DDFTRACER_COMPOSE_HWLOC_BUILD={'ON' if build_hwloc else 'OFF'}"]
    run(["cmake", "-S", str(tree), "-B", str(build),
         f"-DCMAKE_INSTALL_PREFIX={prefix}", *cmake_args],
        work / "compose-configure.log")
    if not install:
        # A variant linked by name (--variant) that is not the one this
        # machine's library needs: say so where it is seen.
        log_warnings(work / "compose-configure.log", ("as asked, but",))
    else:
        # The core then links the system hwloc.
        log_warnings(work / "compose-configure.log", ("hwloc",))
    if install:
        run(["cmake", "--build", str(build)], work / "compose-build.log")
        # Into a staging dir first, so a failed or partial install changes
        # nothing; then each file is moved into place (install_staged).
        stage = work / "stage"
        if stage.exists():
            shutil.rmtree(stage)
        run(["cmake", "--install", str(build), "--prefix", str(stage)],
            work / "compose-install.log")
        step(f"installing into {prefix}")
        install_staged(stage, prefix)
    records = []
    for line in (build / "composition.txt").read_text().splitlines():
        parts = line.split()
        if len(parts) == 3:
            records.append((parts[0], parts[1], parts[2]))
    return records


def install_staged(stage: Path, prefix: Path) -> None:
    """Move what `cmake --install` put in `stage` into `prefix`.

    cmake --install deletes the live libdftracer_core.so and writes the new
    one in its place, so a process starting meanwhile finds it missing or cut
    short. Here each file is copied next to its destination and renamed over
    it (os.replace is atomic within a directory): a process sees the old
    library or the new one, never neither. Processes that have the old one
    mapped keep its inode. The real files go first, then the symlinks to
    them, then composition.txt, which describes the result.
    """
    files, links = [], []
    for path in sorted(stage.rglob("*")):
        if path.is_symlink():
            links.append(path)
        elif path.is_file():
            files.append(path)
    record = [p for p in files if p.name == "composition.txt"]
    files = [p for p in files if p.name != "composition.txt"]
    for path in files + links + record:
        dest = prefix / path.relative_to(stage)
        dest.parent.mkdir(parents=True, exist_ok=True)
        tmp = dest.with_name(f".{dest.name}.dftracer-compose-{os.getpid()}")
        if tmp.is_symlink() or tmp.exists():
            tmp.unlink()
        if path.is_symlink():
            os.symlink(os.readlink(path), tmp)
        else:
            shutil.copy2(path, tmp)
        os.replace(tmp, dest)


def replace_dir(built: Path, dest: Path, scratch: Path, prepare=None) -> None:
    """Put a copy of `built` at `dest`, replacing what is there.

    The copy is made under a temporary name in `scratch` (on dest's file
    system, outside the directories the compose project scans), `prepare`
    runs on it, and only a complete copy is renamed into place; the old one is
    moved aside first and put back if that rename fails. A failed copy leaves
    `dest` as it was.
    """
    new = Path(tempfile.mkdtemp(prefix=f".{dest.name}-new-", dir=scratch))
    try:
        shutil.copytree(built, new, symlinks=True, dirs_exist_ok=True)
        if prepare is not None:
            prepare(new)
    except BaseException:
        shutil.rmtree(new, ignore_errors=True)
        raise
    dest.parent.mkdir(parents=True, exist_ok=True)
    old = None
    if dest.exists():
        old = Path(tempfile.mkdtemp(prefix=f".{dest.name}-old-", dir=scratch))
        os.rmdir(old)
        os.rename(dest, old)
    try:
        os.rename(new, dest)
    except BaseException:
        if old is not None:
            os.rename(old, dest)
        shutil.rmtree(new, ignore_errors=True)
        raise
    if old is not None:
        shutil.rmtree(old, ignore_errors=True)


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
    # An env var, not a flag, so a --source tree with an older build-module.sh
    # still runs; that one ignores it and takes python3 from PATH.
    run(cmd, work / f"source-{part}.log", env={**os.environ, "DFTRACER_PYTHON": sys.executable})
    built = next((out / "tree").glob("**/dftracer/compose"))
    origin = {"origin": "source", "built_on": platform.node(),
              "built_at": datetime.datetime.now().isoformat(timespec="seconds")}
    # Copied in under a temporary name and renamed into place, so a copy that
    # fails halfway leaves the tree's previous core or variant usable.
    scratch = tree.parent
    if part == "core":
        replace_dir(built / "core", tree / "core", scratch,
                    lambda new: (new / "origin.json").write_text(
                        json.dumps(origin, indent=2) + "\n"))
        return

    def mark_origin(new: Path) -> None:
        # Prebuilt variants (the wheels, the CI tree) have no "origin";
        # dftracer_install_info tells the two apart by it.
        meta = new / "component.json"
        if meta.exists():
            data = json.loads(meta.read_text())
            data.update(origin)
            meta.write_text(json.dumps(data, indent=2) + "\n")

    for variant in (built / "modules" / part).iterdir():
        replace_dir(variant, tree / "modules" / part / variant.name, scratch,
                    mark_origin)
        step(f"built {part} {variant.name}")


# --- PAPI built from source ------------------------------------------------

def papi_has_rocp_sdk(library: Path) -> bool:
    """PAPI keeps each component's name as a string in the library."""
    try:
        return b"\0rocp_sdk\0" in library.read_bytes()
    except OSError:
        return False


def detected_papi_library(work: Path) -> Path | None:
    listing = work / "compose-build" / "papi-library.txt"
    if not listing.exists():
        return None
    for item in listing.read_text().strip().split(";"):
        if "libpapi" in Path(item).name:
            return Path(item)
    return None


def provide_papi(mode: str, tree: Path, work: Path, settings: dict,
                 records: list[tuple[str, str, str]]) -> bool:
    """Build PAPI into <libdir>/dftracer/papi when `mode` asks for it, and put
    it first on CMAKE_PREFIX_PATH. True when detection must run again."""
    if mode == "never" or "papi" in settings["disabled"]:
        return False
    dest = tree.parent / "papi"
    status = {module: state for module, _, state in records}
    rocm = os.environ.get("ROCM_PATH", "") if status.get("hip", "absent") != "absent" else ""
    built_before = any((dest / "share" / "dftracer").glob("papi-*.stamp"))
    reason = ""
    if mode == "always":
        reason = "--build-papi always"
    elif built_before:
        reason = "built by an earlier compose"
    elif rocm:
        library = detected_papi_library(work)
        if library is None:
            reason = "this ROCm machine has no PAPI"
        elif not papi_has_rocp_sdk(library):
            reason = f"{library} has no rocp_sdk component for GPU counters"
    if not reason:
        return False
    if built_before and mode != "always":
        # Not run again: on a node without ROCm it would rebuild without
        # rocp_sdk over the PAPI that has it.
        step(f"using the PAPI in {dest} ({reason})")
    else:
        step(f"building PAPI into {dest} ({reason}) ...")
        script = tree / "DFTracerPapi.cmake"
        try:
            run(["cmake", f"-DDFTRACER_PAPI_PREFIX={dest}", f"-DDFTRACER_PAPI_ROCM_ROOT={rocm}",
                 "-P", str(script)], work / "papi.log", cwd=work)
        except StepFailed as error:
            if mode == "always":
                raise
            # Not asked for by name: compose goes on with this machine's PAPI.
            step(f"warning: PAPI did not build, keeping this machine's PAPI: {error}")
            return False
    # Before any prefix a site module put there, so detection, module builds
    # and the relink all take this PAPI.
    old = os.environ.get("CMAKE_PREFIX_PATH")
    os.environ["CMAKE_PREFIX_PATH"] = str(dest) + (os.pathsep + old if old else "")
    os.environ["PAPI_DIR"] = str(dest)
    if rocm:
        step(f"set PAPI_ROCP_SDK_ROOT={rocm} when you trace, for GPU counters")
    return True


# --- Variorum for dftracer_service ---------------------------------------

VARIORUM_RECORD = "variorum.json"
VARIORUM_PROBE_TIMEOUT_S = 60
_VARIORUM_MARK = "DFTRACER-VARIORUM "

# Run in a child: Variorum ends the process on a topology it does not
# handle, and may hang on a driver that does not answer.
_VARIORUM_PROBE = r"""
import ctypes, json, sys
try:
    lib = ctypes.CDLL(sys.argv[1])
except OSError as error:
    sys.stdout.write("\n" + %r + json.dumps({"error": str(error)}) + "\n")
    sys.exit(0)
path = next((line.split()[-1] for line in open("/proc/self/maps")
             if "libvariorum" in line), sys.argv[1])
result = {"path": path, "api": hasattr(lib, "variorum_get_power_json")}
if result["api"]:
    call = lib.variorum_get_power_json
    call.argtypes = [ctypes.POINTER(ctypes.c_char_p)]
    out = ctypes.c_char_p()
    result["rc"] = call(ctypes.byref(out))
    result["payload"] = (out.value or b"").decode(errors="replace")
sys.stdout.write("\n" + %r + json.dumps(result) + "\n")
""" % (_VARIORUM_MARK, _VARIORUM_MARK)


def power_domains(payload: str) -> list[str]:
    """The power_<domain>_watts keys of a variorum_get_power_json() payload
    that hold a reading; Variorum writes -1 for a domain it cannot read."""
    try:
        data = json.loads(payload)
    except ValueError:
        return []

    def has_reading(value) -> bool:
        if isinstance(value, dict):
            return any(has_reading(v) for v in value.values())
        return isinstance(value, (int, float)) and not isinstance(value, bool) and value >= 0

    found: set[str] = set()

    def walk(node) -> None:
        if not isinstance(node, dict):
            return
        for key, value in node.items():
            # power_node_watts is Variorum's sum of the others.
            if (key.startswith("power_") and key.endswith("_watts")
                    and key != "power_node_watts" and has_reading(value)):
                found.add(key[len("power_"):-len("_watts")])
            walk(value)

    walk(data)
    return sorted(found)


def probe_variorum(library: str) -> dict:
    """Load `library` the way dftracer_service does and call it once.

    Returns {"library": the file loaded, or "" when there is no Variorum 0.8
    or later, "domains": the power domains it read, "note": why it read none}.
    """
    try:
        proc = subprocess.run([sys.executable, "-c", _VARIORUM_PROBE, library],
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              text=True, timeout=VARIORUM_PROBE_TIMEOUT_S)
        output = proc.stdout
    except subprocess.TimeoutExpired:
        return {"library": library, "domains": [],
                "note": f"variorum_get_power_json did not return in {VARIORUM_PROBE_TIMEOUT_S} s"}
    result = None
    for line in output.splitlines():
        if line.startswith(_VARIORUM_MARK):
            result = json.loads(line[len(_VARIORUM_MARK):])
    if result is None:
        return {"library": library, "domains": [],
                "note": "Variorum ended the process in variorum_get_power_json"}
    if "error" in result:
        return {"library": "", "domains": [], "note": f"{library} does not load: {result['error']}"}
    if not result["api"]:
        return {"library": "", "domains": [],
                "note": f"{result['path']} is older than Variorum 0.8"}
    domains = power_domains(result.get("payload", "")) if result.get("rc") == 0 else []
    note = "" if domains else f"{result['path']} reads no power domain here"
    return {"library": result["path"], "domains": domains, "note": note}


def _version_key(path: Path) -> tuple:
    return tuple(int(p) if p.isdigit() else -1
                 for p in path.name.split("-", 1)[-1].replace("-", ".").split("."))


def rocm_root() -> str:
    """A ROCm with ROCm SMI: the loaded one ($ROCM_PATH, $ROCM_DIR, hipcc on
    PATH), else /opt/rocm, else the newest /opt/rocm-<version>."""
    candidates = [os.environ.get("ROCM_PATH"), os.environ.get("ROCM_DIR")]
    hipcc = shutil.which("hipcc")
    if hipcc:
        candidates.append(str(Path(hipcc).resolve().parent.parent))
    candidates.append("/opt/rocm")
    candidates += [str(p) for p in sorted(Path("/opt").glob("rocm-*"), key=_version_key,
                                          reverse=True)]
    for candidate in filter(None, candidates):
        if any((Path(candidate) / d / "librocm_smi64.so").exists() for d in ("lib", "lib64")):
            return candidate
    return ""


def nvml_library() -> str:
    """The NVIDIA driver's NVML library, which Variorum reads GPU power with."""
    return ctypes.util.find_library("nvidia-ml") or ""


def nvml_include() -> str:
    """A directory with nvml.h, which only a CUDA toolkit ships."""
    roots = [os.environ.get(v) for v in ("CUDA_HOME", "CUDA_PATH", "DFTRACER_CUDA_PATH")]
    nvcc = shutil.which("nvcc")
    if nvcc:
        roots.append(str(Path(nvcc).resolve().parent.parent))
    roots.append("/usr/local/cuda")
    for root in filter(None, roots):
        for include in [Path(root) / "include", *sorted(Path(root).glob("targets/*/include"))]:
            if (include / "nvml.h").exists():
                return str(include)
    return ""


def power_backends(rocm: str) -> list[tuple[str, str]]:
    """The vendor interfaces this machine has that Variorum can read power
    from, each with the power domain it gives."""
    found = []
    if Path("/dev/hsmp").exists():
        found.append(("AMD CPU (/dev/hsmp)", "cpu"))
    if rocm and Path("/dev/kfd").exists():
        found.append(("AMD GPU (ROCm SMI)", "gpu"))
    if Path("/dev/nvidiactl").exists() and nvml_library():
        found.append(("NVIDIA GPU (NVML)", "gpu"))
    if Path("/dev/cpu/msr_batch").exists():
        found.append(("Intel CPU (msr-safe)", "cpu"))
    return found


def variorum_record(status: str, probe: dict, **extra) -> dict:
    record = {"status": status, "library": probe["library"],
              "domains": probe["domains"], "note": probe.get("note", "")}
    record.update(extra)
    return record


def describe_variorum(record: dict | None) -> str:
    if not record:
        return "not recorded: run dftracer-compose"
    domains = ", ".join(record.get("domains") or []) or "none"
    if record.get("status") == "absent":
        return ("absent: " + (record.get("note") or "no Variorum 0.8 or later")
                + "; dftracer_service writes no power records")
    where = "built here" if record.get("status") == "built" else "found"
    if record.get("status") == "built" and record.get("built_on"):
        where += f" ({record['built_on']}, {record.get('built_at', '?')})"
    return f"{where} {record.get('library', '')}, domains: {domains}"


def provide_variorum(mode: str, tree: Path, work: Path, check: bool = False) -> dict:
    """Build Variorum into <libdir>/dftracer/variorum when `mode` asks for it.

    dftracer_service loads that library ahead of the loader's search. Returns
    the record of the Variorum the service will load (variorum_record); with
    `check`, only reports what would be built.
    """
    dest = tree.parent / "variorum"
    built = dest / "lib" / "libvariorum.so"
    built_before = built.exists() and any((dest / "share" / "dftracer").glob("variorum-*.stamp"))
    if built_before and mode != "always":
        # Not built again: on a node without the GPUs it would rebuild without
        # them over the Variorum that reads them.
        step(f"using the Variorum in {dest} (built by an earlier compose)")
        before = _read_record(tree)
        return variorum_record("built", probe_variorum(str(built)),
                               **{k: before[k] for k in ("built_on", "built_at") if k in before})
    found = probe_variorum("libvariorum.so")
    rocm = rocm_root()
    reason = ""
    if mode == "always":
        reason = "--build-variorum always"
    elif mode == "auto" and not found["domains"]:
        reason = found["note"] or "no Variorum reads power here"
        if not power_backends(rocm):
            step(f"Variorum: {reason}, and this machine has no power interface "
                 "(/dev/hsmp, ROCm SMI, NVML, msr-safe); not building it")
            reason = ""
    status = "found" if found["library"] else "absent"
    if not reason:
        return variorum_record(status, found)
    if check:
        print(f"would build Variorum into {dest} ({reason})")
        return variorum_record(status, found)
    step(f"building Variorum into {dest} ({reason}) ...")
    nvml = nvml_include()
    if not nvml and any(label.startswith("NVIDIA") for label, _ in power_backends(rocm)):
        step("warning: no nvml.h for NVIDIA GPU power; set CUDA_HOME to a CUDA "
             "toolkit that has include/nvml.h, or load a cuda module")
    # The build goes into `dest`, as its RPATH names it; a Variorum already
    # there waits aside, and comes back if the new one reads fewer domains.
    old = None
    if built.exists():
        old = (dest.with_name(f".{dest.name}-old-{os.getpid()}"),
               probe_variorum(str(built)), _read_record(tree))
        shutil.rmtree(old[0], ignore_errors=True)
        dest.rename(old[0])

    def restore() -> dict | None:
        if old is None:
            return None
        shutil.rmtree(dest, ignore_errors=True)
        old[0].rename(dest)
        step(f"keeping the Variorum that was in {dest}")
        return variorum_record("built", old[1], **{k: old[2][k] for k in ("built_on", "built_at")
                                                   if k in old[2]})

    try:
        run(["cmake", f"-DDFTRACER_VARIORUM_PREFIX={dest}",
             f"-DDFTRACER_VARIORUM_ROCM_ROOT={rocm}",
             f"-DDFTRACER_VARIORUM_NVML_INCLUDE={nvml}",
             f"-DDFTRACER_VARIORUM_WORK={work / 'variorum'}",
             f"-DDFTRACER_VARIORUM_HWLOC_ROOT={static_hwloc_root(tree)}",
             "-P", str(tree / "DFTracerVariorum.cmake")], work / "variorum.log", cwd=work)
    except StepFailed as error:
        log_warnings(work / "variorum.log", ("hwloc", "Variorum"))
        kept = restore()
        if mode == "always":
            raise
        # Not asked for by name: compose goes on with what the machine has.
        step(f"warning: Variorum did not build, keeping this machine's: {error}")
        return kept or variorum_record(status, found)
    log_warnings(work / "variorum.log", ("hwloc", "Variorum patch"))
    build = {"built_on": platform.node(),
             "built_at": datetime.datetime.now().isoformat(timespec="seconds")}
    record = variorum_record("built", probe_variorum(str(built)), **build)
    if old is not None:
        if len(record["domains"]) < len(old[1]["domains"]):
            step(f"warning: the new Variorum reads {record['domains'] or 'no'} domains, "
                 f"the one before {old[1]['domains']}")
            return restore()
        shutil.rmtree(old[0], ignore_errors=True)
    # Loud, since a Variorum release the patches no longer fit still builds.
    for label, domain in power_backends(rocm):
        if domain not in record["domains"]:
            step(f"warning: the Variorum built in {dest} reads no {domain} power, "
                 f"though this machine has {label}; run again with "
                 "--build-variorum always --keep --verbose for the build log")
    return record


def _read_record(tree: Path) -> dict:
    try:
        data = json.loads((tree.parent / VARIORUM_RECORD).read_text())
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def save_variorum_record(tree: Path, record: dict) -> None:
    path = tree.parent / VARIORUM_RECORD
    tmp = path.with_name(f".{path.name}.{os.getpid()}")
    tmp.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, path)


# --- Saved composition settings --------------------------------------------

def settings_path(tree: Path) -> Path:
    """<libdir>/dftracer/compose-settings.json, next to composition.txt."""
    return tree.parent / SETTINGS_NAME


def load_settings(tree: Path) -> dict:
    """The saved settings; a file that is not the expected shape (cut short,
    edited by hand) counts as empty, with a message, rather than an error."""
    path = settings_path(tree)
    data: object = {}
    if path.exists():
        try:
            data = json.loads(path.read_text())
        except ValueError:
            data = None
    disabled = data.get("disabled", []) if isinstance(data, dict) else None
    variants = data.get("variants", {}) if isinstance(data, dict) else None
    if (not isinstance(disabled, list) or not isinstance(variants, dict)
            or not all(isinstance(m, str) for m in disabled)
            or not all(isinstance(v, str) for v in variants.values())):
        step(f"ignoring unreadable {path} (--reset removes it)")
        disabled, variants = [], {}
    return {"disabled": sorted(set(disabled) & set(MODULES)),
            "variants": {m: v for m, v in variants.items() if m in MODULES}}


def save_settings(tree: Path, settings: dict) -> None:
    """Write through a temporary file and a rename, so the file is the old
    settings or the new ones, never cut short."""
    path = settings_path(tree)
    if not settings["disabled"] and not settings["variants"]:
        path.unlink(missing_ok=True)
        return
    tmp = path.with_name(f".{path.name}.{os.getpid()}")
    tmp.write_text(json.dumps(settings, indent=2, sort_keys=True) + "\n")
    os.replace(tmp, path)


# --- Changing the install ----------------------------------------------------

def check_writable(tree: Path) -> None:
    """Stop before any build when this install cannot be changed: compose
    writes the tree (source builds), the library directory and the settings."""
    libdir = tree.parent.parent
    dirs = [tree, tree / "core", tree / "modules", tree.parent, libdir]
    denied = [str(d) for d in dirs if d.exists() and not os.access(d, os.W_OK)]
    if denied:
        raise SystemExit(
            "dftracer-compose: cannot change this install, no write access to "
            + ", ".join(denied) + ". Run it as the install's owner, or install "
            "DFTracer where you can write (a virtual environment, pip install "
            "--user) and compose that.")


@contextlib.contextmanager
def compose_lock(tree: Path):
    """Hold <libdir>/dftracer/.compose.lock for the whole run: two composes of
    one install would otherwise build and replace the same files at once."""
    path = tree.parent / ".compose.lock"
    with open(path, "a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            step(f"waiting for another dftracer-compose of this install ({path})")
            fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


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
    print(f"  {'variorum':6} {describe_variorum(_read_record(tree))}")
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
    parser.add_argument("--build-papi", choices=("auto", "always", "never"),
                        default=os.environ.get("DFTRACER_BUILD_PAPI", "auto").lower(),
                        help="build PAPI from source: auto builds it on a ROCm machine "
                             "whose PAPI is missing or has no rocp_sdk component "
                             "(default: $DFTRACER_BUILD_PAPI or auto)")
    parser.add_argument("--build-variorum", choices=("auto", "always", "never"),
                        default=os.environ.get("DFTRACER_BUILD_VARIORUM", "auto").lower(),
                        help="build Variorum (and E-SMI) for dftracer_service's power "
                             "records: auto builds it when no Variorum 0.8 or later "
                             "reads a power domain here and the machine has one "
                             "(default: $DFTRACER_BUILD_VARIORUM or auto)")
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
    if args.status:
        return print_status(tree, load_settings(tree))
    if args.check:
        return _run(args, prefix, tree)
    # A run that changes the install: stop now if it cannot, and hold the
    # lock from reading the settings to saving them.
    check_writable(tree)
    with compose_lock(tree):
        return _run(args, prefix, tree)


def _run(args: argparse.Namespace, prefix: Path, tree: Path) -> int:
    saved = load_settings(tree)
    settings = edit_settings(saved, args, tree)
    forced = parse_from_source(args.from_source)
    off = forced & set(settings["disabled"])
    if off:
        step("not building disabled modules: " + ", ".join(sorted(off)))
        forced -= off
    work = Path(tempfile.mkdtemp(prefix="dftracer-compose-"))
    try:
        status = _main(args, prefix, tree, forced, work, settings)
        # An edit sticks once the library it describes is installed; --reset
        # always writes, which also clears an unreadable file.
        if not args.check and (settings != saved or args.reset):
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
    if not args.check and provide_papi(args.build_papi, tree, work, settings, records):
        records = compose(tree, prefix, work, install=False, cmake_args=cmake_args)
    variorum = provide_variorum("never" if args.no_build else args.build_variorum,
                                tree, work, check=args.check)
    needed = {m for m, _, status in records if status in ("missing", "stale")}
    detected = {m for m, _, status in records if status in ("linked", "missing", "stale")}
    to_build = sorted(needed | (forced & detected))
    if "core" in forced:
        to_build.insert(0, "core")
    for module, variant, status in records:
        note = " (rebuild from source)" if module in forced and status == "linked" else ""
        print(f"  {module:6} {variant:28} {status}{note}")
    print(f"  {'variorum':6} {describe_variorum(variorum)}")
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
    records = compose(tree, prefix, work, install=True, cmake_args=cmake_args,
                      build_hwloc=not args.no_build)
    save_variorum_record(tree, variorum)
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
