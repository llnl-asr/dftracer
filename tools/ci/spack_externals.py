#!/usr/bin/env python3
"""The OS packages the CI deps image takes from dnf instead of spack.

spack builds every dependency of the libraries in dependency/variants.yaml
unless it is told the system already has it, which multiplies the image build
by the tools and libraries AlmaLinux 8 (the manylinux_2_28 base) ships.
`spack external find` only detects packages with an executable, so the
libraries are declared here, the way flux-framework/dyad's CI does, and
pinned unbuildable so that spack builds only the libraries we name.

    spack_externals.py dnf            the RPMs to install
    spack_externals.py packages-yaml  ~/.spack/packages.yaml for what is
                                      installed (run after the dnf install)
"""
import subprocess
import sys

# spack package -> the RPM to install, whose version spack is told (a -devel
# package has its library's version). Every prefix is /usr.
EXTERNALS = {
    "autoconf": "autoconf",
    "automake": "automake",
    "libtool": "libtool",
    "m4": "m4",
    "bison": "bison",
    "flex": "flex",
    "gettext": "gettext-devel",
    "perl": "perl",
    "pkgconf": "pkgconf-pkg-config",
    "diffutils": "diffutils",
    "findutils": "findutils",
    "tar": "tar",
    "gmake": "make",
    "xz": "xz-devel",
    "bzip2": "bzip2-devel",
    "zlib": "zlib-devel",
    "zstd": "libzstd-devel",
    "ncurses": "ncurses-devel",
    "readline": "readline-devel",
    "openssl": "openssl-devel",
    "curl": "libcurl-devel",
    "expat": "expat-devel",
    "libxml2": "libxml2-devel",
    "libffi": "libffi-devel",
    "sqlite": "sqlite-devel",
    "gdbm": "gdbm-devel",
    "libevent": "libevent-devel",
    "numactl": "numactl-devel",
    "libpciaccess": "libpciaccess-devel",
    "libnl": "libnl3-devel",
    "rdma-core": "rdma-core-devel",
    "libfabric": "libfabric-devel",
    "jansson": "jansson-devel",
    "libedit": "libedit-devel",
    "krb5": "krb5-devel",
    "util-linux-uuid": "libuuid-devel",
    "libxcrypt": "libxcrypt-devel",
    "libbsd": "libbsd-devel",
    "libmd": "libmd-devel",
    "berkeley-db": "libdb-devel",
    "util-macros": "xorg-x11-util-macros",
    "pigz": "pigz",
    "openssh": "openssh-clients",
    "python": "python3-devel",
    "cmake": "cmake",
}

# spack may still build these: something needs a version the OS does not
# have (hwloc is a module; mvapich2 2.3.7 pins automake 1.15.1), and spack
# takes the OS one wherever it fits.
BUILDABLE = {"hwloc", "automake"}
EXTRA = {"hwloc": "hwloc-devel"}

# Installed but not declared to spack: link-time libraries of the externals
# above. The OS libfabric has the PSM2 provider, so linking it needs -lpsm2.
LINK_ONLY = ["libpsm2-devel"]

PROVIDERS = {
    # spack's defaults are zlib-ng and libiconv; glibc has iconv.
    "zlib-api": ["zlib"],
    "iconv": ["glibc"],
}


def rpm_version(rpm: str) -> str:
    # --whatprovides: some names are capabilities of another package
    # (python3-devel is python36-devel's).
    out = subprocess.run(
        ["rpm", "-q", "--whatprovides", "--qf", "%{VERSION}\n", rpm],
        capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"{rpm} is not installed: {out.stdout.strip()}")
    return out.stdout.split()[0]


def main() -> None:
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    table = {**EXTERNALS, **EXTRA}
    if cmd == "dnf":
        print(" ".join(sorted(set(table.values()) | set(LINK_ONLY))))
    elif cmd == "packages-yaml":
        print("packages:")
        print("  all:")
        print("    providers:")
        for virtual, providers in PROVIDERS.items():
            print(f"      {virtual}: [{', '.join(providers)}]")
        for name, rpm in sorted(table.items()):
            print(f"  {name}:")
            print(f"    buildable: {'true' if name in BUILDABLE else 'false'}")
            print("    externals:")
            print(f"    - spec: {name}@{rpm_version(rpm)}")
            print("      prefix: /usr")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
