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


class BuildEnv(unittest.TestCase):
    def test_flux_pmi_dir_leaves_ld_library_path(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            flux = tmp / "lib64" / "flux"
            cray = tmp / "cray" / "lib64"
            for d in (flux, cray):
                d.mkdir(parents=True)
                (d / "libpmi2.so.0").touch()
            other = tmp / "flux"
            other.mkdir()
            path = os.pathsep.join([str(flux), str(cray), str(other)])
            env = compose.build_env({"LD_LIBRARY_PATH": path, "X": "1"})
            self.assertEqual(env["LD_LIBRARY_PATH"], os.pathsep.join([str(cray), str(other)]))
            self.assertEqual(env["X"], "1")
            self.assertNotIn("LD_LIBRARY_PATH", compose.build_env({}))
        finally:
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


class ProvidePapi(unittest.TestCase):
    """When dftracer-compose builds PAPI; the build itself is mocked."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.tree = self.tmp / "lib64" / "dftracer" / "compose"
        self.tree.mkdir(parents=True)
        self.work = self.tmp / "work"
        (self.work / "compose-build").mkdir(parents=True)
        self.settings = {"disabled": [], "variants": {}}
        env = {"ROCM_PATH": "/opt/rocm", "CMAKE_PREFIX_PATH": "/site"}
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(shutil.rmtree, self.tmp)

    def site_papi(self, rocp_sdk: bool, patched: bool = True) -> None:
        library = self.tmp / "libpapi.so"
        names = b"\0perf_event\0" + (b"rocp_sdk\0" if rocp_sdk else b"rocm_7001\0")
        if patched:
            names += compose.PAPI_PATCH_MARKER + b"\0"
        library.write_bytes(b"\x7fELF" + names)
        (self.work / "compose-build" / "papi-library.txt").write_text(f"{library};/usr/lib/libm.so")

    def provide(self, mode, hip="linked", papi="linked"):
        records = [("papi", "papi-7.2", papi), ("hip", "rocprof-1.1", hip)]
        with mock.patch.object(compose, "run") as run, quiet():
            built = compose.provide_papi(mode, self.tree, self.work, self.settings, records)
        return built, run

    def test_rocm_machine_with_site_papi_lacking_rocp_sdk_builds(self):
        self.site_papi(rocp_sdk=False)
        built, run = self.provide("auto")
        self.assertTrue(built)
        cmd = run.call_args.args[0]
        dest = self.tree.parent / "papi"
        self.assertIn(f"-DDFTRACER_PAPI_PREFIX={dest}", cmd)
        self.assertIn("-DDFTRACER_PAPI_ROCM_ROOT=/opt/rocm", cmd)
        self.assertEqual(os.environ["CMAKE_PREFIX_PATH"], f"{dest}{os.pathsep}/site")
        self.assertEqual(os.environ["PAPI_DIR"], str(dest))

    def test_site_papi_with_rocp_sdk_is_kept(self):
        self.site_papi(rocp_sdk=True)
        built, run = self.provide("auto")
        self.assertFalse(built)
        run.assert_not_called()

    def test_site_papi_with_rocp_sdk_but_without_the_patches_builds(self):
        self.site_papi(rocp_sdk=True, patched=False)
        built, run = self.provide("auto")
        self.assertTrue(built)
        run.assert_called_once()

    def test_a_papi_built_before_without_the_patches_is_rebuilt_on_rocm(self):
        dest = self.tree.parent / "papi"
        (dest / "share" / "dftracer").mkdir(parents=True)
        (dest / "share" / "dftracer" / "papi-abc-rocp_sdk.stamp").write_text("x")
        (dest / "lib").mkdir()
        (dest / "lib" / "libpapi.so").write_bytes(b"\x7fELF\0rocp_sdk\0")
        self.site_papi(rocp_sdk=True)
        built, run = self.provide("auto")
        self.assertTrue(built)
        run.assert_called_once()

    def test_marker_next_to_other_bytes_is_found(self):
        library = self.tmp / "libpapi.so"
        library.write_bytes(b"\x7fELF\0rocp_sdk\0abcd" + compose.PAPI_PATCH_MARKER + b"\0")
        self.assertTrue(compose.papi_has_patches(library))

    def test_no_build_uses_a_papi_built_before_and_builds_none(self):
        dest = self.tree.parent / "papi"
        (dest / "share" / "dftracer").mkdir(parents=True)
        (dest / "share" / "dftracer" / "papi-abc-rocp_sdk.stamp").write_text("x")
        self.site_papi(rocp_sdk=True, patched=False)
        records = [("papi", "papi-7.2", "linked"), ("hip", "rocprof-1.1", "linked")]
        with mock.patch.object(compose, "run") as run, quiet():
            built = compose.provide_papi("auto", self.tree, self.work, self.settings,
                                         records, build=False)
        self.assertTrue(built)
        run.assert_not_called()
        self.assertEqual(os.environ["PAPI_DIR"], str(dest))

    def test_no_build_without_a_papi_built_before_does_nothing(self):
        self.site_papi(rocp_sdk=True, patched=False)
        records = [("papi", "papi-7.2", "linked"), ("hip", "rocprof-1.1", "linked")]
        with mock.patch.object(compose, "run") as run, quiet():
            built = compose.provide_papi("auto", self.tree, self.work, self.settings,
                                         records, build=False)
        self.assertFalse(built)
        run.assert_not_called()

    def test_rocm_machine_without_papi_builds(self):
        built, _ = self.provide("auto", papi="absent")
        self.assertTrue(built)

    def test_machine_without_rocm_is_left_alone(self):
        self.site_papi(rocp_sdk=False)
        built, run = self.provide("auto", hip="absent")
        self.assertFalse(built)
        run.assert_not_called()

    def test_never_and_disabled_never_build(self):
        self.site_papi(rocp_sdk=False)
        self.assertFalse(self.provide("never")[0])
        self.settings["disabled"] = ["papi"]
        self.assertFalse(self.provide("always")[0])

    def test_always_builds_without_rocm_and_without_rocp_sdk(self):
        built, run = self.provide("always", hip="absent")
        self.assertTrue(built)
        self.assertIn("-DDFTRACER_PAPI_ROCM_ROOT=", run.call_args.args[0])

    def test_a_papi_built_before_is_used_again_without_a_rebuild(self):
        stamp = self.tree.parent / "papi" / "share" / "dftracer"
        stamp.mkdir(parents=True)
        (stamp / "papi-abc-rocp_sdk.stamp").write_text("x")
        self.site_papi(rocp_sdk=True)
        built, run = self.provide("auto", hip="absent")
        self.assertTrue(built)
        run.assert_not_called()
        self.assertEqual(os.environ["PAPI_DIR"], str(self.tree.parent / "papi"))

    def test_failed_build_keeps_site_papi_in_auto_and_fails_in_always(self):
        self.site_papi(rocp_sdk=False)
        failed = compose.StepFailed("no network")
        with mock.patch.object(compose, "run", side_effect=failed), quiet():
            records = [("papi", "papi-7.2", "linked"), ("hip", "rocprof-1.1", "linked")]
            self.assertFalse(compose.provide_papi("auto", self.tree, self.work,
                                                  self.settings, records))
            self.assertEqual(os.environ["CMAKE_PREFIX_PATH"], "/site")
            with self.assertRaises(compose.StepFailed):
                compose.provide_papi("always", self.tree, self.work, self.settings, records)


MI300A_POWER = json.dumps({"tuo1": {
    "timestamp": 1, "num_gpus_per_socket": 1, "power_node_watts": 520.0,
    "socket_0": {"power_cpu_watts": 135.8, "power_mem_watts": -1.0,
                 "power_gpu_watts": {"GPU_0": 133.0}},
    "socket_1": {"power_cpu_watts": 121.0, "power_mem_watts": -1.0,
                 "power_gpu_watts": {"GPU_1": 121.0}}}})


class ProvideVariorum(unittest.TestCase):
    """When dftracer-compose builds Variorum; the build and the probe are mocked."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.tree = self.tmp / "lib64" / "dftracer" / "compose"
        self.tree.mkdir(parents=True)
        self.work = self.tmp / "work"
        self.work.mkdir()
        self.dest = self.tree.parent / "variorum"
        self.addCleanup(shutil.rmtree, self.tmp)

    def provide(self, mode, site=None, backends=(("AMD GPU (ROCm SMI)", "gpu"),),
                check=False):
        """`site` is the probe of the machine's libvariorum.so: None for none,
        else the domains it reads."""
        built = self.dest / "lib" / "libvariorum.so"

        def probe(library):
            if library == str(built):
                return {"library": library, "domains": ["cpu", "gpu"], "note": ""}
            if site is None:
                return {"library": "", "domains": [], "note": "no libvariorum.so"}
            return {"library": "/lib/libvariorum.so", "domains": list(site),
                    "note": "" if site else "/lib/libvariorum.so reads no power domain here"}

        with mock.patch.object(compose, "run") as run, \
                mock.patch.object(compose, "probe_variorum", side_effect=probe), \
                mock.patch.object(compose, "power_backends", return_value=list(backends)), \
                mock.patch.object(compose, "rocm_root", return_value="/opt/rocm"), \
                mock.patch.object(compose, "nvml_include", return_value=""), \
                quiet() as out:
            record = compose.provide_variorum(mode, self.tree, self.work, check=check)
        return record, run, out.getvalue()

    def test_power_domains_skip_unread_and_the_node_sum(self):
        self.assertEqual(compose.power_domains(MI300A_POWER), ["cpu", "gpu"])
        self.assertEqual(compose.power_domains('{"h": {"timestamp": 1}}'), [])
        self.assertEqual(compose.power_domains("not json"), [])

    def test_machine_without_variorum_builds_it(self):
        record, run, _ = self.provide("auto")
        cmd = run.call_args.args[0]
        self.assertIn(f"-DDFTRACER_VARIORUM_PREFIX={self.dest}", cmd)
        self.assertIn("-DDFTRACER_VARIORUM_ROCM_ROOT=/opt/rocm", cmd)
        self.assertEqual(cmd[-1], str(self.tree / "DFTracerVariorum.cmake"))
        self.assertEqual((record["status"], record["domains"]), ("built", ["cpu", "gpu"]))
        self.assertTrue(record["built_on"])

    def test_a_build_missing_a_domain_the_machine_has_warns(self):
        record, _, out = self.provide("auto", backends=(("AMD CPU (/dev/hsmp)", "cpu"),
                                                        ("NVIDIA GPU (NVML)", "gpu"),
                                                        ("Intel CPU (msr-safe)", "mem")))
        self.assertEqual(record["status"], "built")
        self.assertIn("reads no mem power, though this machine has Intel CPU", out)
        self.assertIn("no nvml.h for NVIDIA GPU power", out)
        self.assertNotIn("reads no cpu power", out)

    def test_variorum_that_reads_nothing_is_replaced(self):
        record, run, _ = self.provide("auto", site=[])
        run.assert_called_once()
        self.assertEqual(record["status"], "built")

    def test_variorum_that_reads_power_is_kept(self):
        record, run, _ = self.provide("auto", site=["gpu"])
        run.assert_not_called()
        self.assertEqual((record["status"], record["library"], record["domains"]),
                         ("found", "/lib/libvariorum.so", ["gpu"]))

    def test_machine_without_power_interface_builds_nothing(self):
        record, run, out = self.provide("auto", backends=())
        run.assert_not_called()
        self.assertEqual(record["status"], "absent")
        self.assertIn("no power interface", out)

    def rebuild(self, new_domains, fail=False):
        """--build-variorum always over a Variorum that reads cpu and gpu;
        the new build reads `new_domains`, or fails."""
        lib = self.dest / "lib" / "libvariorum.so"
        lib.parent.mkdir(parents=True)
        lib.write_text("old")

        def build(cmd, log, **kwargs):
            if fail:
                raise compose.StepFailed("variorum failed")
            lib.parent.mkdir(parents=True, exist_ok=True)
            lib.write_text("new")

        def probe(library):
            if library == str(lib):
                domains = ["cpu", "gpu"] if lib.read_text() == "old" else list(new_domains)
                return {"library": library, "domains": domains, "note": ""}
            return {"library": "", "domains": [], "note": "no libvariorum.so"}

        with mock.patch.object(compose, "run", side_effect=build), \
                mock.patch.object(compose, "probe_variorum", side_effect=probe), \
                mock.patch.object(compose, "power_backends", return_value=[]), \
                mock.patch.object(compose, "rocm_root", return_value=""), \
                mock.patch.object(compose, "nvml_include", return_value=""), \
                quiet() as out:
            record = compose.provide_variorum("always", self.tree, self.work)
        return record, lib.read_text(), out.getvalue()

    def test_rebuild_that_reads_fewer_domains_keeps_the_old_one(self):
        record, content, out = self.rebuild(["gpu"])
        self.assertEqual((content, record["domains"]), ("old", ["cpu", "gpu"]))
        self.assertIn("keeping the Variorum", out)
        self.assertEqual(sorted(p.name for p in self.dest.parent.iterdir()),
                         ["compose", "variorum"])

    def test_rebuild_that_reads_as_many_domains_replaces_the_old_one(self):
        record, content, _ = self.rebuild(["cpu", "gpu"])
        self.assertEqual((content, record["domains"]), ("new", ["cpu", "gpu"]))
        self.assertEqual(sorted(p.name for p in self.dest.parent.iterdir()),
                         ["compose", "variorum"])

    def test_failed_rebuild_keeps_the_old_one(self):
        with self.assertRaises(compose.StepFailed):
            self.rebuild([], fail=True)
        self.assertEqual((self.dest / "lib" / "libvariorum.so").read_text(), "old")

    def test_build_warnings_are_shown(self):
        log = self.work / "variorum.log"
        log.write_text("-- [DFTRACER] composing hwloc: hwloc-2\n"
                       "CMake Warning at x.cmake:1 (message):\n  [DFTRACER] hwloc download "
                       "failed (6;timeout);\n  linking the system hwloc\nCall Stack (most "
                       "recent call first):\n  t.cmake:8\n-- done\n")
        with quiet() as out:
            compose.log_warnings(log, ("hwloc",))
            compose.log_warnings(self.work / "missing.log", ("hwloc",))
        self.assertIn("warning: hwloc download failed (6;timeout); linking the system hwloc\n",
                      out.getvalue())
        self.assertNotIn("composing", out.getvalue())

    def test_never_builds_nothing_and_always_builds(self):
        record, run, _ = self.provide("never")
        run.assert_not_called()
        self.assertEqual(record["status"], "absent")
        record, run, _ = self.provide("always", site=["gpu"], backends=())
        run.assert_called_once()
        self.assertEqual(record["status"], "built")

    def test_check_only_reports(self):
        record, run, out = self.provide("auto", check=True)
        run.assert_not_called()
        self.assertEqual(record["status"], "absent")
        self.assertIn("would build Variorum", out)

    def test_a_variorum_built_before_is_used_again_without_a_rebuild(self):
        stamp = self.dest / "share" / "dftracer"
        stamp.mkdir(parents=True)
        (stamp / "variorum-v0.8.0-AMD_GPU.stamp").write_text("x")
        (self.dest / "lib").mkdir()
        (self.dest / "lib" / "libvariorum.so").write_bytes(b"")
        compose.save_variorum_record(self.tree, {"status": "built", "built_on": "tuo1",
                                                 "built_at": "2026-11-24T10:00:00"})
        for mode in ("auto", "never"):
            record, run, _ = self.provide(mode, backends=())
            run.assert_not_called()
            self.assertEqual((record["status"], record["built_on"]), ("built", "tuo1"))
        _, run, _ = self.provide("always")
        run.assert_called_once()
        self.assertEqual(list(stamp.glob("variorum-*.stamp")), [])

    def test_failed_build_keeps_the_machine_variorum_in_auto_and_fails_in_always(self):
        failed = compose.StepFailed("no network")
        with mock.patch.object(compose, "run", side_effect=failed), \
                mock.patch.object(compose, "power_backends", return_value=[("x", "gpu")]), \
                mock.patch.object(compose, "probe_variorum",
                                  return_value={"library": "", "domains": [], "note": ""}), \
                quiet():
            record = compose.provide_variorum("auto", self.tree, self.work)
            self.assertEqual(record["status"], "absent")
            with self.assertRaises(compose.StepFailed):
                compose.provide_variorum("always", self.tree, self.work)

    def test_describe(self):
        self.assertIn("absent", compose.describe_variorum(
            {"status": "absent", "library": "", "domains": [], "note": "no libvariorum.so"}))
        self.assertEqual(compose.describe_variorum(
            {"status": "found", "library": "/lib/libvariorum.so", "domains": ["cpu", "gpu"]}),
            "found /lib/libvariorum.so, domains: cpu, gpu")
        self.assertIn("not recorded", compose.describe_variorum({}))

    @unittest.skipUnless(sys.platform.startswith("linux"), "reads /proc/self/maps")
    def test_probe_of_a_library_that_is_not_variorum(self):
        missing = compose.probe_variorum(str(self.tmp / "libvariorum.so"))
        self.assertEqual(missing["library"], "")
        self.assertIn("does not load", missing["note"])
        got = compose.probe_variorum("libm.so.6")
        self.assertEqual(got["library"], "")
        self.assertIn("older than Variorum 0.8", got["note"])


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

    @unittest.skipUnless(sys.platform.startswith("linux"), "reads /proc/self/maps")
    def test_status_shows_the_variorum_compose_built(self):
        stub = next(BUILD.rglob("libdftracer_test_variorum_08.so"), None)
        if stub is None:
            self.skipTest("the build has no Variorum stub")
        dest = self.tree.parent / "variorum"
        (dest / "lib").mkdir(parents=True)
        (dest / "share" / "dftracer").mkdir(parents=True)
        shutil.copy(stub, dest / "lib" / "libvariorum.so")
        (dest / "share" / "dftracer" / "variorum-v0.8.0-AMD_GPU.stamp").write_text("x")
        try:
            result = self.run_compose("--no-build")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            status = self.run_compose("--status").stdout
        finally:
            shutil.rmtree(dest)
        # The probe reports the file it loaded, with symlinks resolved.
        library = (dest / "lib" / "libvariorum.so").resolve()
        self.assertIn(f"built here {library}, domains: cpu, gpu, mem", status)

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
