"""Locate this installation's preload library without loading the tracer."""

import argparse
import sys
from pathlib import Path


def library_path(debug=False):
    package_dir = Path(__file__).resolve().parent
    try:
        from ._preload_config import LIBRARY_DIR
    except ModuleNotFoundError as error:
        if error.name != "dftracer._preload_config":
            raise
        directories = [package_dir / "lib64", package_dir / "lib"]
    else:
        directories = [package_dir / LIBRARY_DIR]

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
        print("{}: {}".format(parser.prog, error), file=sys.stderr)
        return 1
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
