#!/usr/bin/env python3
"""Turn dependency/variants.yaml into what the CI image and jobs need.

    variants.py spack-env           spack.yaml for the CI deps image
    variants.py spack-group <group> the specs of one install layer (mpi,
                                    hdf5-serial, hdf5-parallel, other)
    variants.py coverage-specs      MPIs and HDF5s built only to compile
                                    against (variants.yaml coverage:), last layer
    variants.py papi-tarballs       PAPI releases and their tarballs' SHA-256
    variants.py rocm-repos          one dnf repo file per ROCm release
    variants.py gpu-packages        versioned ROCm and CUDA packages to install
    variants.py matrix              the module variants, as a GitHub matrix (JSON)
    variants.py check               variants.yaml agrees with the source tree

Each module variant becomes one CI job that builds that module against that
library build (tools/ci/build-module.sh); see the comments in variants.yaml.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
VARIANTS = ROOT / "dependency" / "variants.yaml"
COMPAT_DIR = ROOT / "src" / "dftracer" / "core" / "brahma"

# spack package names of the MPI implementations the image builds.
SPACK_MPI = {"openmpi": "openmpi", "mpich": "mpich", "mvapich": "mvapich2"}


def load() -> dict:
    return yaml.safe_load(VARIANTS.read_text())


def major_minor(version: str) -> str:
    return ".".join(version.split(".")[:2])


def spack_mpis(cfg: dict) -> list[tuple[str, str]]:
    """(implementation, version) for every MPI the image builds with spack."""
    mpi = cfg["modules"]["mpi"]
    return [(impl, v) for impl in SPACK_MPI if impl in mpi for v in mpi[impl]]


def hdf5_parallel(cfg: dict) -> list[tuple[str, str, str]]:
    """(MPI ABI family, implementation, version) the parallel HDF5 of each
    family is built against (variants.yaml hdf5.parallel)."""
    par = cfg["modules"]["hdf5"].get("parallel") or {}
    return [(abi, ref["impl"], ref["version"]) for abi, ref in par.items()]


def coverage_mpis(cfg: dict) -> list[tuple[str, str]]:
    """(implementation, version) of the coverage MPIs (variants.yaml)."""
    mpi = cfg.get("coverage", {}).get("mpi", {})
    return [(impl, v) for impl in SPACK_MPI if impl in mpi for v in mpi[impl]]


def coverage_hdf5s(cfg: dict) -> list[str]:
    """Versions of the coverage HDF5s (variants.yaml)."""
    return list(cfg.get("coverage", {}).get("hdf5", []))


def hdf5_specs(cfg: dict, version: str) -> list[tuple[str, str]]:
    """(variant suffix, spack spec) of an HDF5 version: serial, then parallel
    per MPI ABI family, against that family's MPI as the MPI layer builds it."""
    specs = [("serial", f"hdf5@{version}~mpi~fortran~java")]
    for abi, impl, mpi_version in hdf5_parallel(cfg):
        specs.append((f"par+{abi}",
                      f"hdf5@{version}+mpi~fortran~java ^{mpi_spec(impl, mpi_version)}"))
    return specs


def mpi_spec(impl: str, version: str) -> str:
    """The one spec of an MPI, as a root and under the parallel HDF5s: written
    out in full in both places, spack concretizes them to a single build.

    Only the headers and libraries matter to the modules, so the variants keep
    the build small: no Fortran, and OpenMPI's own pmix/prrte instead of two
    more spack packages. (MPICH keeps its default datatype engine, yaksa: the
    alternative, dataloop, does not compile with GCC 14 in MPICH 4.2.)
    """
    extra = {
        "openmpi": " fabrics=none ~fortran +internal-pmix",
        "mpich": " ~fortran",
        "mvapich": " process_managers=hydra fabrics=mrail",
    }[impl]
    return f"{SPACK_MPI[impl]}@{version}{extra}"


def spack_groups(cfg: dict) -> dict[str, list[str]]:
    """The specs spack builds, in the layers the image installs them in.

    PAPI is not among them: spack's papi package looks up a rocp_sdk variant
    that only PAPI 7.2+ has, so it cannot build 6.0 or 7.1. PAPI has no
    dependencies and builds from ICL's tarballs instead (papi-tarballs).
    """
    mods = cfg["modules"]
    groups: dict[str, list[str]] = {
        "mpi": [], "hdf5-serial": [], "hdf5-parallel": [], "other": []}
    for impl, version in spack_mpis(cfg):
        groups["mpi"].append(mpi_spec(impl, version))
    for version in mods["hdf5"]["versions"]:
        for kind, spec in hdf5_specs(cfg, version):
            groups["hdf5-serial" if kind == "serial" else "hdf5-parallel"].append(spec)
    groups["other"] += [f"hwloc@{v}" for v in mods["hwloc"]]
    groups["other"] += [f"variorum@{v}" for v in mods["variorum"]]
    return groups


def spack_specs(cfg: dict) -> list[str]:
    return [spec for specs in spack_groups(cfg).values() for spec in specs]


def cmd_spack_env(cfg: dict) -> None:
    env = {
        "spack": {
            "specs": spack_specs(cfg),
            # reuse: take the OS packages Containerfile.deps registers as
            # externals (and anything already installed) over building them,
            # so spack builds little more than the libraries listed here.
            "concretizer": {"unify": False, "reuse": True},
            "view": False,
            "config": {"install_tree": {"root": "/opt/spack-deps"}},
            "packages": {
                # The manylinux image's gcc-toolset is the only compiler, so
                # every library is built with the toolchain the wheels use.
                # require, not just prefer, a generic CPU: spack targets the
                # build host, and brahma's CI image, built on a zen4 machine,
                # gave an mpicc that died with "Illegal instruction" on
                # GitHub's runners.
                "all": {"require": ["target=x86_64_v2"]},
            },
        }
    }
    yaml.safe_dump(env, sys.stdout, sort_keys=False)


def cmd_coverage_specs(cfg: dict) -> None:
    """The coverage MPIs' and HDF5s' specs, for the image's last layer. Not in
    spack-env: adding one must not change spack.yaml, which every spack layer
    is built from."""
    for impl, version in coverage_mpis(cfg):
        print(mpi_spec(impl, version))
    for version in coverage_hdf5s(cfg):
        for _kind, spec in hdf5_specs(cfg, version):
            print(spec)


def cmd_spack_group(cfg: dict, group: str) -> None:
    for spec in spack_groups(cfg)[group]:
        print(spec)


def cmd_papi_tarballs(cfg: dict) -> None:
    """One "<version> <sha256>" line per PAPI release."""
    sums = cfg.get("checksums", {}).get("papi", {})
    for version in cfg["modules"]["papi"]:
        print(version, sums[version])


def cmd_rocm_repos(cfg: dict) -> None:
    for release in cfg["modules"]["hip"]["rocm"]:
        print(
            f"[rocm-{release}]\nname=ROCm {release}\n"
            f"baseurl=https://repo.radeon.com/rocm/rhel8/{release}/main\n"
            "enabled=1\ngpgcheck=1\n"
            # AMD's package signing key: dnf checks every package against it.
            "gpgkey=https://repo.radeon.com/rocm/rocm.gpg.key\n"
        )


def cmd_gpu_packages(cfg: dict) -> None:
    packages = []
    for release in cfg["modules"]["hip"]["rocm"]:
        # hipcc: hip-config.cmake (which rocprofiler-sdk's config loads)
        # requires it, and only later releases' packages depend on it.
        packages += [
            f"rocprofiler-sdk{release}",
            f"hip-devel{release}",
            f"hipcc{release}",
            f"rocm-device-libs{release}",
        ]
    for release in cfg["modules"]["cuda"]["cuda"]:
        v = release.replace(".", "-")
        packages += [
            f"cuda-cupti-{v}",
            f"cuda-cudart-devel-{v}",
            f"cuda-driver-devel-{v}",
            f"cuda-nvcc-{v}",
        ]
    print(" ".join(packages))


def matrix(cfg: dict) -> list[dict]:
    """Every module variant, with what tools/ci/build-module.sh needs."""
    mods = cfg["modules"]
    jobs = []
    for impl, version in spack_mpis(cfg):
        jobs.append({
            "module": "mpi",
            "variant": f"mpi-{impl}-{major_minor(version)}",
            "spack": f"{SPACK_MPI[impl]}@{version}",
        })
    # Built and checked, not shipped: named by the full version (the variant
    # key would repeat a shipped one), and left out of the assembled tree.
    for impl, version in coverage_mpis(cfg):
        jobs.append({
            "module": "mpi",
            "variant": f"mpi-{impl}-{version}",
            "spack": f"{SPACK_MPI[impl]}@{version}",
            "coverage": True,
        })
    for version, info in mods["mpi"].get("craympich", {}).items():
        jobs.append({
            "module": "mpi",
            "variant": f"mpi-craympich-{version}",
            "spack": info["base"],
            "cray": version,
        })
    for version in mods["hdf5"]["versions"]:
        mm = major_minor(version)
        jobs.append({
            "module": "hdf5",
            "variant": f"hdf5-{mm}-serial",
            "spack": f"hdf5@{version}~mpi",
        })
        for abi, impl, mpi_version in hdf5_parallel(cfg):
            jobs.append({
                "module": "hdf5",
                "variant": f"hdf5-{mm}-par+{abi}",
                "spack": f"hdf5@{version}+mpi ^{SPACK_MPI[impl]}@{mpi_version}",
                "mpi_spack": f"{SPACK_MPI[impl]}@{mpi_version}",
            })
    for version in coverage_hdf5s(cfg):
        jobs.append({"module": "hdf5", "variant": f"hdf5-{version}-serial",
                     "spack": f"hdf5@{version}~mpi", "coverage": True})
        for abi, impl, mpi_version in hdf5_parallel(cfg):
            jobs.append({
                "module": "hdf5",
                "variant": f"hdf5-{version}-par+{abi}",
                "spack": f"hdf5@{version}+mpi ^{SPACK_MPI[impl]}@{mpi_version}",
                "mpi_spack": f"{SPACK_MPI[impl]}@{mpi_version}",
                "coverage": True,
            })
    for version in mods["papi"]:
        jobs.append({"module": "papi", "variant": f"papi-{major_minor(version)}",
                     "papi": f"/opt/papi-{version}"})
    for version in mods["hwloc"]:
        jobs.append({"module": "hwloc", "variant": f"hwloc-{version.split('.')[0]}",
                     "spack": f"hwloc@{version}"})
    for release, sdk in mods["hip"]["rocm"].items():
        jobs.append({"module": "hip", "variant": f"rocprof-{sdk}",
                     "rocm": f"/opt/rocm-{release}"})
    for release in mods["cuda"]["cuda"]:
        jobs.append({"module": "cuda", "variant": f"cupti-{release}",
                     "cuda": f"/usr/local/cuda-{release}"})
    return jobs


def cmd_matrix(cfg: dict) -> None:
    print(json.dumps({"include": matrix(cfg)}))


def cmd_check(cfg: dict) -> int:
    """Each MPI in variants.yaml has a compat tracer, and the reverse."""
    compat = set()
    for path in COMPAT_DIR.glob("mpi_*_compat.cpp"):
        m = re.fullmatch(r"mpi_([a-z]+)_(\d+)_(\d+)_compat\.cpp", path.name)
        if m:
            compat.add((m.group(1), f"{m.group(2)}.{m.group(3)}"))
    listed = set()
    for impl, versions in cfg["modules"]["mpi"].items():
        for version in versions:
            listed.add((impl, major_minor(version)))
    status = 0
    for impl, mm in sorted(listed - compat):
        print(f"FAIL {impl} {mm}: in variants.yaml, but no mpi_{impl}_"
              f"{mm.replace('.', '_')}_compat.cpp")
        status = 1
    for impl, mm in sorted(compat - listed):
        print(f"FAIL {impl} {mm}: a compat tracer exists, but variants.yaml "
              "does not build it")
        status = 1
    built = set(spack_mpis(cfg))
    for abi, impl, version in hdf5_parallel(cfg):
        if (impl, version) not in built:
            print(f"FAIL hdf5 {abi}: {impl}@{version} is not in modules.mpi")
            status = 1
    sums = cfg.get("checksums", {}).get("papi", {})
    for version in cfg["modules"]["papi"]:
        if not re.fullmatch(r"[0-9a-f]{64}", str(sums.get(version, ""))):
            print(f"FAIL papi {version}: no SHA-256 under checksums.papi in variants.yaml")
            status = 1
    keys = [job["variant"] for job in matrix(cfg)]
    dups = sorted({k for k in keys if keys.count(k) > 1})
    if dups:
        print("FAIL duplicate variants:", ", ".join(dups))
        status = 1
    if status == 0:
        print(f"variants.yaml agrees with the tree: {len(keys)} module variants")
    return status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=[
        "spack-env", "spack-group", "coverage-specs", "papi-tarballs",
        "rocm-repos", "gpu-packages", "matrix", "check"])
    parser.add_argument("group", nargs="?")
    args = parser.parse_args()
    cfg = load()
    if args.command == "check":
        return cmd_check(cfg)
    if args.command == "spack-group":
        cmd_spack_group(cfg, args.group)
        return 0
    if args.command == "papi-tarballs":
        cmd_papi_tarballs(cfg)
        return 0
    {"spack-env": cmd_spack_env, "coverage-specs": cmd_coverage_specs,
     "rocm-repos": cmd_rocm_repos,
     "gpu-packages": cmd_gpu_packages, "matrix": cmd_matrix}[args.command](cfg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
