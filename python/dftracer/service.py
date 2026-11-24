"""Run this installation's dftracer_service from the Python package."""

import os
import runpy
import sys
from pathlib import Path


def binary_path():
    """The dftracer_service CMake installed with this package."""
    package_dir = Path(__file__).resolve().parent
    candidates = [package_dir / "bin" / "dftracer_service"]
    # With DFTRACER_INSTALL_DIR the CMake prefix is outside the package, and
    # the library directory recorded at build time is the only link to it.
    config_file = package_dir / "_preload_config.py"
    if config_file.is_file():
        library_dir = package_dir / runpy.run_path(str(config_file))["LIBRARY_DIR"]
        candidates.append(library_dir.parent / "bin" / "dftracer_service")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "dftracer_service not found; checked: {}. It is built by the CMake "
        "build; reinstall DFTracer, or run <prefix>/bin/dftracer_service for a "
        "native installation.".format(", ".join(str(c) for c in candidates))
    )


def main(argv=None):
    try:
        binary = binary_path()
    except FileNotFoundError as error:
        print("dftracer_service: {}".format(error), file=sys.stderr)
        return 1
    args = sys.argv[1:] if argv is None else argv
    # exec, not a child: the service's start and stop keep their exit status
    # and signals, as when the binary is run directly.
    os.execv(str(binary), [str(binary)] + list(args))


if __name__ == "__main__":
    sys.exit(main())
