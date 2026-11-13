#!/usr/bin/env python3
"""Tests of dftracer-compose (python/dftracer/compose.py) changing an install.

    test_compose_cli.py [--build <DFTracer build dir>] [unittest args]

Without --build only the parts that need no compose tree run. With it, the
build's compose tree is installed into a temporary prefix (as
test/check_compose.sh does) and dftracer-compose relinks libdftracer_core
there, which needs CMake and the compiler the build used.
"""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

SOURCE = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location(
    "dftracer_compose", SOURCE / "python" / "dftracer" / "compose.py")
compose = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(compose)

BUILD: Path | None = None


def quiet():
    return contextlib.redirect_stdout(io.StringIO())


class Settings(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.tree = self.tmp / "lib64" / "dftracer" / "compose"
        self.tree.mkdir(parents=True)
        self.path = compose.settings_path(self.tree)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_wrong_shapes_count_as_empty(self):
        for text in ("[1]", '{"variants": []}', '{"disabled": "mpi"}',
                     '{"disabled": [1]}', '{"variants": {"mpi": 3}}', "{cut",
                     "null", '"text"'):
            self.path.write_text(text)
            with quiet() as out:
                got = compose.load_settings(self.tree)
            self.assertEqual(got, {"disabled": [], "variants": {}}, text)
            self.assertIn("ignoring unreadable", out.getvalue(), text)

    def test_valid_settings_load(self):
        self.path.write_text(json.dumps(
            {"disabled": ["hip", "nope"], "variants": {"mpi": "mpi-mpich-4.2", "x": "y"}}))
        self.assertEqual(compose.load_settings(self.tree),
                         {"disabled": ["hip"], "variants": {"mpi": "mpi-mpich-4.2"}})

    def test_save_is_atomic_and_empty_removes(self):
        settings = {"disabled": ["papi"], "variants": {}}
        with mock.patch.object(compose.os, "replace", wraps=os.replace) as replace:
            compose.save_settings(self.tree, settings)
        replace.assert_called_once()
        self.assertEqual(json.loads(self.path.read_text()), settings)
        self.assertEqual(sorted(p.name for p in self.path.parent.iterdir()),
                         ["compose", self.path.name])
        compose.save_settings(self.tree, {"disabled": [], "variants": {}})
        self.assertFalse(self.path.exists())


class ReplaceDir(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.built = self.tmp / "built"
        (self.built / "sub").mkdir(parents=True)
        (self.built / "a").write_text("new")
        (self.built / "sub" / "b").write_text("new-b")
        self.dest = self.tmp / "tree" / "core"
        self.dest.mkdir(parents=True)
        (self.dest / "a").write_text("old")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def leftovers(self):
        return [p.name for p in self.tmp.iterdir() if p.name.startswith(".")]

    def test_replaces(self):
        compose.replace_dir(self.built, self.dest, self.tmp,
                            lambda new: (new / "origin.json").write_text("{}"))
        self.assertEqual((self.dest / "a").read_text(), "new")
        self.assertEqual((self.dest / "sub" / "b").read_text(), "new-b")
        self.assertTrue((self.dest / "origin.json").exists())
        self.assertEqual(self.leftovers(), [])

    def test_failed_copy_keeps_old(self):
        def fail(_new):
            raise OSError("disk full")
        with self.assertRaises(OSError):
            compose.replace_dir(self.built, self.dest, self.tmp, fail)
        self.assertEqual((self.dest / "a").read_text(), "old")
        self.assertEqual(self.leftovers(), [])

    def test_failed_rename_puts_old_back(self):
        real = os.rename
        calls = []

        def rename(src, dst):
            calls.append(dst)
            if len(calls) == 2:  # moving the new copy into place
                raise OSError("rename failed")
            return real(src, dst)
        with mock.patch.object(compose.os, "rename", side_effect=rename):
            with self.assertRaises(OSError):
                compose.replace_dir(self.built, self.dest, self.tmp)
        self.assertEqual((self.dest / "a").read_text(), "old")
        self.assertEqual(self.leftovers(), [])


class InstallStaged(unittest.TestCase):
    def test_files_links_and_order(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            stage, prefix = tmp / "stage", tmp / "prefix"
            lib = stage / "lib64"
            (lib / "dftracer").mkdir(parents=True)
            (lib / "libx.so.1.0").write_bytes(b"\x7fELF new")
            os.symlink("libx.so.1.0", lib / "libx.so")
            (lib / "dftracer" / "composition.txt").write_text("mpi - off\n")
            (prefix / "lib64").mkdir(parents=True)
            (prefix / "lib64" / "libx.so.1.0").write_bytes(b"old")
            order = []
            real = os.replace

            def replace(src, dst):
                order.append(Path(dst).name)
                return real(src, dst)
            with mock.patch.object(compose.os, "replace", side_effect=replace):
                compose.install_staged(stage, prefix)
            self.assertEqual(order, ["libx.so.1.0", "libx.so", "composition.txt"])
            self.assertEqual((prefix / "lib64" / "libx.so.1.0").read_bytes(), b"\x7fELF new")
            self.assertEqual(os.readlink(prefix / "lib64" / "libx.so"), "libx.so.1.0")
            self.assertEqual(
                [p.name for p in prefix.rglob(".*")], [], "temporary files left behind")
        finally:
            shutil.rmtree(tmp)


class Writable(unittest.TestCase):
    @unittest.skipIf(os.geteuid() == 0, "root can write a read-only directory")
    def test_read_only_install_is_refused(self):
        tmp = Path(tempfile.mkdtemp())
        tree = tmp / "lib64" / "dftracer" / "compose"
        (tree / "core").mkdir(parents=True)
        try:
            compose.check_writable(tree)
            os.chmod(tmp / "lib64", 0o555)
            with self.assertRaises(SystemExit) as error:
                compose.check_writable(tree)
            self.assertIn("no write access", str(error.exception))
        finally:
            os.chmod(tmp / "lib64", 0o755)
            shutil.rmtree(tmp)


class Lock(unittest.TestCase):
    def test_second_compose_waits(self):
        tmp = Path(tempfile.mkdtemp())
        tree = tmp / "lib64" / "dftracer" / "compose"
        tree.mkdir(parents=True)
        lock = tree.parent / ".compose.lock"
        holder = subprocess.Popen(
            [sys.executable, "-c",
             "import fcntl, sys, time\n"
             "f = open(sys.argv[1], 'a'); fcntl.flock(f, fcntl.LOCK_EX)\n"
             "print('held', flush=True); time.sleep(2)\n", str(lock)],
            stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(holder.stdout.readline().strip(), "held")
            start = time.monotonic()
            with quiet() as out:
                with compose.compose_lock(tree):
                    waited = time.monotonic() - start
            self.assertGreater(waited, 1.0)
            self.assertIn("waiting for another dftracer-compose", out.getvalue())
        finally:
            holder.wait()
            holder.stdout.close()
            shutil.rmtree(tmp)


@unittest.skipIf(BUILD is None and "--build" not in sys.argv, "needs --build")
class EndToEnd(unittest.TestCase):
    """dftracer-compose on the build's compose tree, installed in a prefix."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        cls.prefix = cls.tmp / "prefix"
        subprocess.run(["cmake", "--install", str(BUILD), "--prefix", str(cls.prefix),
                        "--component", "dftracer_compose"],
                       check=True, stdout=subprocess.DEVNULL)
        cls.tree = compose.find_compose_dir(cls.prefix)
        cls.libdir = cls.tree.parent.parent
        cls.run_compose("--no-build")  # the library every test starts from

    @classmethod
    def tearDownClass(cls):
        os.chmod(cls.libdir, 0o755)
        shutil.rmtree(cls.tmp)

    @classmethod
    def run_compose(cls, *args):
        return subprocess.run(
            [sys.executable, str(SOURCE / "python" / "dftracer" / "compose.py"),
             "--prefix", str(cls.prefix), *args],
            capture_output=True, text=True)

    def library(self) -> Path:
        return next(p for p in self.libdir.glob("libdftracer_core.so.*")
                    if not p.is_symlink())

    def test_relink_never_leaves_the_library_missing(self):
        lib = self.library()
        size = lib.stat().st_size
        seen = {"missing": 0, "short": 0, "samples": 0}
        done = threading.Event()

        def poll():
            while not done.is_set():
                seen["samples"] += 1
                try:
                    if lib.stat().st_size < size // 2:
                        seen["short"] += 1
                except FileNotFoundError:
                    seen["missing"] += 1
        thread = threading.Thread(target=poll)
        thread.start()
        try:
            result = self.run_compose("--no-build")
        finally:
            done.set()
            thread.join()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreater(seen["samples"], 100)
        self.assertEqual((seen["missing"], seen["short"]), (0, 0), seen)
        self.assertEqual([p.name for p in self.libdir.glob(".*dftracer-compose*")], [])
        self.assertTrue((self.tree.parent / "composition.txt").exists())

    def test_failed_relink_changes_nothing(self):
        lib = self.library()
        before = (lib.stat().st_ino, lib.stat().st_mtime_ns)
        settings = compose.settings_path(self.tree)
        settings.unlink(missing_ok=True)
        archive = self.tree / "core" / "libdftracer_core_base.a"
        saved = archive.read_bytes()
        archive.write_bytes(b"not an archive")
        try:
            result = self.run_compose("--no-build", "--disable", "hip")
        finally:
            archive.write_bytes(saved)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((lib.stat().st_ino, lib.stat().st_mtime_ns), before)
        self.assertFalse(settings.exists(), "settings saved after a failed relink")

    def test_reset_removes_an_unreadable_settings_file(self):
        settings = compose.settings_path(self.tree)
        settings.write_text("[1]")
        result = self.run_compose("--no-build", "--reset")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("ignoring unreadable", result.stdout)
        self.assertFalse(settings.exists())

    @unittest.skipIf(os.geteuid() == 0, "root can write a read-only directory")
    def test_read_only_install_stops_before_building(self):
        os.chmod(self.libdir, 0o555)
        try:
            result = self.run_compose("--no-build")
        finally:
            os.chmod(self.libdir, 0o755)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no write access", result.stderr)
        self.assertNotIn("linking libdftracer_core", result.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--build", type=Path)
    known, rest = parser.parse_known_args()
    BUILD = known.build
    unittest.main(argv=[sys.argv[0], *rest])
