"""Exercise preload discovery without building or loading native libraries.

Run with: python3 -m unittest discover -s test -p test_preload_path.py
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PreloadPathTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="dftracer helper ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / "site" / "dftracer"
        self.package.mkdir(parents=True)
        # Fail if discovery starts importing the native extension.
        (self.package / "__init__.py").write_text("")
        shutil.copy(ROOT / "python/dftracer/preload.py", self.package)

    def python_command(self, *args):
        env = dict(os.environ, PYTHONPATH=str(self.package.parent))
        return subprocess.run(
            [sys.executable, "-m", "dftracer.preload", *args],
            cwd=self.root, env=env, capture_output=True, text=True,
        )

    def check_command(self, command, directory):
        directory.mkdir(parents=True, exist_ok=True)
        release = directory / "libdftracer_preload.so"
        debug = directory / "libdftracer_preload_dbg.so"
        release.touch()
        for flag in ("--debug", "-d"):
            result = command(flag)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertIn(debug.name, result.stderr)
        debug.touch()
        for args, expected in (((), release), (("--debug",), debug), (("-d",), debug)):
            result = command(*args)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, str(expected.resolve()) + "\n")
            self.assertEqual(result.stderr, "")
        for flag in ("-debug", "--de", "--unknown"):
            result = command(flag)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
        for flag in ("-h", "--help"):
            result = command(flag)
            self.assertEqual(result.returncode, 0)
            self.assertIn("--debug", result.stdout)
        release.unlink()
        result = command()
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")

    def test_python_standard_layouts(self):
        for libdir in ("lib", "lib64"):
            with self.subTest(libdir=libdir):
                self.check_command(self.python_command, self.package / libdir)
                shutil.rmtree(self.package / libdir)

    def test_python_configured_external_directory(self):
        directory = self.root / "external prefix" / "custom-libs"
        (self.package / "_preload_config.py").write_text(
            "LIBRARY_DIR = {!r}\n".format(str(directory))
        )
        self.check_command(self.python_command, directory)

    def test_python_relative_config_survives_relocation(self):
        (self.package / "_preload_config.py").write_text("LIBRARY_DIR = 'custom-libs'\n")
        moved = self.root / "relocated site"
        self.package.parent.rename(moved)
        self.package = moved / "dftracer"
        self.check_command(self.python_command, self.package / "custom-libs")

    def test_configured_install_does_not_fall_back_to_another_library(self):
        (self.package / "_preload_config.py").write_text("LIBRARY_DIR = 'missing'\n")
        directory = self.package / "lib64"
        directory.mkdir()
        (directory / "libdftracer_preload.so").touch()
        result = self.python_command()
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn(str(self.package / "missing"), result.stderr)

    @unittest.skipUnless(shutil.which("cmake"), "cmake is required")
    def test_native_install_and_relocation(self):
        for libdir in ("lib", "lib64", "lib/custom", str(self.root / "absolute libs")):
            with self.subTest(libdir=libdir):
                source = self.root / ("native-" + libdir.replace("/", "_"))
                source.mkdir()
                prefix = source / "prefix"
                module = ROOT / "cmake/modules/DFTracerPreloadHelper.cmake"
                (source / "CMakeLists.txt").write_text(
                    'cmake_minimum_required(VERSION 3.16)\n'
                    'project(helper NONE)\n'
                    'set(CMAKE_INSTALL_BINDIR "bin")\n'
                    'set(CMAKE_INSTALL_LIBDIR "{}")\n'
                    'include("{}")\n'.format(libdir, module)
                )
                for command in (
                    ["cmake", "-S", str(source), "-B", str(source / "build"),
                     "-DCMAKE_INSTALL_PREFIX=" + str(prefix)],
                    ["cmake", "--install", str(source / "build")],
                ):
                    subprocess.run(command, check=True, capture_output=True, text=True)
                relocated = source / "relocated prefix"
                prefix.rename(relocated)
                executable = relocated / "bin/dftracer-preload-library-path"
                link = source / "linked-helper"
                link.symlink_to(executable)

                def command(*args):
                    return subprocess.run(
                        [str(link), *args], capture_output=True, text=True
                    )

                self.check_command(command, relocated / libdir)


if __name__ == "__main__":
    unittest.main()
