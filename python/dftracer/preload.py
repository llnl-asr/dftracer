"""Locate this installation's preload library without loading the tracer."""

import argparse
import runpy
import sys
from pathlib import Path


def library_path(debug=False):
    package_dir = Path(__file__).resolve().parent
    config_file = package_dir / "_preload_config.py"
    if config_file.is_file():
        # Read only this package's metadata. An editable import finder can
        # otherwise supply a config module from a different installation.
        config = runpy.run_path(str(config_file))
        directories = [package_dir / config["LIBRARY_DIR"]]
    else:
        directories = [package_dir / "lib64", package_dir / "lib"]

    name = "libdftracer_preload_dbg.so" if debug else "libdftracer_preload.so"
    for directory in directories:
        candidate = directory / name
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "{} not found; checked: {}. Reinstall DFTracer with the requested library.".format(
            name, ", ".join(str(directory / name) for directory in directories)
        )
    )


def main():
    parser = argparse.ArgumentParser(
        description="Print the absolute path to this installation's DFTracer preload library.",
        allow_abbrev=False,
    )
    parser.add_argument(
        "-d", "--debug", action="store_true", help="select the debug preload library"
    )
    args = parser.parse_args()
    try:
        path = library_path(args.debug)
    except FileNotFoundError as error:
        print(f"{parser.prog}: {error}", file=sys.stderr)
        return 1
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
