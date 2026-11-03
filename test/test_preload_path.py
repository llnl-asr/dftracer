"""Exercise preload discovery without building or loading native libraries.

Run with: python3 -m unittest discover -s test -p test_preload_path.py
"""

import importlib.util
import os
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(importlib.util.find_spec("setuptools_scm"), "build dependencies required")
class PreloadBuildConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Load the real packaging helper without invoking setup or a compiler.
        with patch("setuptools.setup"):
            namespace = runpy.run_path(str(ROOT / "setup.py"))
        cls.write_config = staticmethod(namespace["write_preload_config"])

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="dftracer build-config ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.package = self.root / "site/dftracer"
        self.package.mkdir(parents=True)
        (self.package / "__init__.py").touch()
        shutil.copy(ROOT / "python/dftracer/preload.py", self.package)
        self.build = self.root / "build"
        self.build.mkdir()

    def check_config(self, configured_prefix, expected_prefix, libdir, external=False):
        (self.build / "CMakeCache.txt").write_text(
            f"CMAKE_INSTALL_PREFIX:PATH={configured_prefix}\n"
            f"CMAKE_INSTALL_LIBDIR:PATH={libdir}\n"
        )
        self.write_config(
            self.build, self.package, expected_prefix, external_install=external,
        )
        library = Path(configured_prefix) / (libdir or "lib") / "libdftracer_preload.so"
        library.parent.mkdir(parents=True, exist_ok=True)
        library.touch()
        result = subprocess.run(
            [sys.executable, str(ROOT / "script/dftracer-preload-library-path")],
            env=dict(os.environ, PYTHONPATH=str(self.package.parent)),
            cwd=self.root, check=False, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, str(library.resolve()) + "\n")
        return (self.package / "_preload_config.py").read_text()

    def test_wheel_build_keeps_relative_paths(self):
        config = self.check_config(self.package, self.package, "lib/custom")
        self.assertNotIn(str(self.root), config)

    def test_external_and_editable_install_records_actual_prefix(self):
        prefix = self.root / "external prefix"
        self.check_config(prefix, prefix, "lib64", external=True)

    def test_cmake_prefix_override_is_honored(self):
        self.check_config(self.root / "actual prefix", self.package, "lib64")

    def test_absolute_library_directory_is_honored(self):
        self.check_config(self.package, self.package, str(self.root / "absolute lib's"))

    def test_empty_library_directory_uses_lib(self):
        self.check_config(self.package, self.package, "")


class PreloadPathTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="dftracer helper ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / "site" / "dftracer"
        self.package.mkdir(parents=True)
        # Fail if discovery starts importing the native extension.
        (self.package / "__init__.py").write_text("")
        (self.package / "dftracer.py").write_text(
            "raise AssertionError('path discovery must not load the tracer')\n"
        )
        shutil.copy(ROOT / "python/dftracer/preload.py", self.package)

    def python_command(self, *args):
        env = dict(os.environ, PYTHONPATH=str(self.package.parent))
        return subprocess.run(
            [sys.executable, str(ROOT / "script/dftracer-preload-library-path"), *args],
            cwd=self.root, env=env, capture_output=True, text=True, check=False,
            timeout=10,
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
        for flag in ("-debug", "--de", "--unknown", "--debug=true", "unexpected"):
            result = command(flag)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
        for flag in ("-h", "--help"):
            result = command(flag)
            self.assertEqual(result.returncode, 0)
            self.assertIn("--debug", result.stdout)
            self.assertEqual(result.stderr, "")
        release.unlink()
        result = command()
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")

    def test_python_resolves_library_symlinks(self):
        directory = self.package / "lib64"
        directory.mkdir()
        actual = directory / "libdftracer_preload.so.2.2.0"
        actual.touch()
        link = directory / "libdftracer_preload.so"
        link.symlink_to(actual.name)
        result = self.python_command()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, str(actual) + "\n")
        actual.unlink()
        result = self.python_command()
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")

    def test_python_help_without_libraries(self):
        result = self.python_command("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--debug", result.stdout)

    def test_python_ignores_another_editable_installation_config(self):
        library = self.package / "lib64/libdftracer_preload.so"
        library.parent.mkdir()
        library.touch()
        foreign_config = self.root / "foreign_config.py"
        foreign_config.write_text("raise AssertionError('foreign config was loaded')\n")
        # Reproduce editable package finders handling a missing submodule even
        # when its parent package was imported from another installation.
        (self.package.parent / "sitecustomize.py").write_text(
            "import importlib.util\nimport sys\n"
            "class ForeignConfigFinder:\n"
            "    def find_spec(self, fullname, path=None, target=None):\n"
            "        if fullname == 'dftracer._preload_config':\n"
            f"            return importlib.util.spec_from_file_location(fullname, {str(foreign_config)!r})\n"
            "sys.meta_path.insert(0, ForeignConfigFinder())\n"
        )
        result = self.python_command()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, str(library) + "\n")

    def test_python_standard_layouts(self):
        for libdir in ("lib", "lib64"):
            with self.subTest(libdir=libdir):
                self.check_command(self.python_command, self.package / libdir)
                shutil.rmtree(self.package / libdir)

    def test_python_configured_external_directory(self):
        directory = self.root / "external prefix" / "custom-libs"
        (self.package / "_preload_config.py").write_text(
            f"LIBRARY_DIR = {str(directory)!r}\n"
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

    def install_native(self, libdir, bindir="bin", mode="normal"):
        source = Path(tempfile.mkdtemp(prefix="native-", dir=self.root))
        prefix = source / "prefix"
        module = ROOT / "cmake/modules/DFTracerPreloadHelper.cmake"
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.16)\n'
            'project(helper NONE)\n'
            f'set(CMAKE_INSTALL_BINDIR "{bindir}")\n'
            f'set(CMAKE_INSTALL_LIBDIR "{libdir}")\n'
            f'include("{module}")\n'
        )
        subprocess.run(
            ["cmake", "-S", str(source), "-B", str(source / "build"),
             "-DCMAKE_INSTALL_PREFIX=" + str(prefix)],
            check=True, capture_output=True, text=True, timeout=30,
        )
        install = ["cmake", "--install", str(source / "build")]
        env = dict(os.environ)
        if mode == "override":
            prefix = source / "override prefix"
            install += ["--prefix", str(prefix)]
        elif mode == "staged":
            stage = source / "stage"
            env["DESTDIR"] = str(stage)
            prefix = stage / str(prefix).lstrip("/")
        subprocess.run(
            install, env=env, check=True, capture_output=True, text=True, timeout=30,
        )
        return prefix

    @unittest.skipUnless(shutil.which("cmake"), "cmake is required")
    def test_native_install_and_relocation(self):
        for libdir in (
            "lib", "lib64", "lib/custom", "lib space's",
            str(self.root / "absolute libs"), str(self.root / "absolute lib's"),
        ):
            with self.subTest(libdir=libdir):
                prefix = self.install_native(libdir)
                source = prefix.parent
                relocated = source / "relocated prefix"
                prefix.rename(relocated)
                executable = relocated / "bin/dftracer-preload-library-path"
                link = source / "linked-helper"
                link.symlink_to(executable)

                def command(*args, executable=link):
                    return subprocess.run(
                        [str(executable), *args], capture_output=True, text=True,
                        check=False, timeout=10,
                    )

                self.check_command(command, relocated / libdir)

    @unittest.skipUnless(shutil.which("cmake"), "cmake is required")
    def test_native_staging_and_install_prefix_override(self):
        for mode in ("staged", "override"):
            with self.subTest(mode=mode):
                prefix = self.install_native("lib/custom", "tools/bin", mode)
                executable = prefix / "tools/bin/dftracer-preload-library-path"

                def command(*args, executable=executable):
                    return subprocess.run(
                        [str(executable), *args], check=False, capture_output=True,
                        text=True, timeout=10,
                    )

                self.check_command(command, prefix / "lib/custom")

    @unittest.skipUnless(shutil.which("cmake"), "cmake is required")
    def test_native_path_lookup_without_python(self):
        prefix = self.install_native("lib64")
        executable = prefix / "bin/dftracer-preload-library-path"
        # The runtime PATH contains only the helper and its two shell utilities.
        path = self.root / "path"
        path.mkdir()
        for name in ("readlink", "dirname"):
            (path / name).symlink_to(shutil.which(name))
        (path / executable.name).symlink_to(executable)
        library = prefix / "lib64/libdftracer_preload.so"
        library.parent.mkdir()
        library.touch()
        env = dict(os.environ, PATH=str(path))
        result = subprocess.run(
            [executable.name], env=env, cwd=self.root, check=False,
            capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, str(library) + "\n")

    @unittest.skipUnless(shutil.which("cmake"), "cmake is required")
    def test_native_absolute_bindir(self):
        bindir = self.root / "absolute bin"
        prefix = self.install_native("lib64", str(bindir))
        executable = bindir / "dftracer-preload-library-path"

        def command(*args):
            return subprocess.run(
                [str(executable), *args], check=False, capture_output=True,
                text=True, timeout=10,
            )

        self.check_command(command, prefix / "lib64")


if __name__ == "__main__":
    unittest.main()
