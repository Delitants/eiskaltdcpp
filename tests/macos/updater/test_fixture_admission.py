#!/usr/bin/env python3
"""Authorization-fixture guards without launching native helpers or changing rights."""
import importlib.util
import json
from pathlib import Path
import plistlib
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location("updater_fixtures", Path(__file__).with_name("fixtures.py"))
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


class AuthorizationAdmission(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eiskalt-fixture-admission-")
        self.addCleanup(self.temp.cleanup)
        self.args = SimpleNamespace(root=Path(self.temp.name) / "missing-fixture",
                                    mode="authorization-refused", adapter=True, disposable_vm=True)

    def assert_refused(self):
        with self.assertRaisesRegex(RuntimeError, "explicitly selected disposable macOS VM"):
            fixtures.install_probe(self.args)
        self.assertFalse(self.args.root.exists())

    def test_refusal_mode_never_expects_installation(self):
        self.assertIn("authorization-refused", fixtures.ADAPTER_INSTALL_MODES)
        self.assertNotIn("authorization-refused", fixtures.INSTALLING_MODES)

    def test_explicit_vm_opt_in_required_before_file_access(self):
        self.args.disposable_vm = False
        with patch.object(fixtures, "run") as run:
            self.assert_refused()
            run.assert_not_called()

    def test_missing_vm_opt_in_is_refused(self):
        del self.args.disposable_vm
        with patch.object(fixtures, "run") as run:
            self.assert_refused()
            run.assert_not_called()

    def test_legacy_adapter_is_refused_before_file_access(self):
        self.args.adapter = False
        with patch.object(fixtures, "run") as run:
            self.assert_refused()
            run.assert_not_called()

    def test_physical_mac_is_refused_before_file_access(self):
        with patch.object(fixtures, "run", return_value="Mac14,2") as run:
            self.assert_refused()
            run.assert_called_once_with("/usr/sbin/sysctl", "-n", "hw.model")

    def test_explicit_virtual_mac_reaches_fixture_validation(self):
        with patch.object(fixtures, "run", return_value="VirtualMac2,1") as run:
            with self.assertRaises(FileNotFoundError):
                fixtures.install_probe(self.args)
            run.assert_called_once_with("/usr/sbin/sysctl", "-n", "hw.model")
        self.assertFalse(self.args.root.exists())

    def run_cleanup_fixture(self, canceled):
        self.args.root.mkdir(mode=0o700)
        old = self.args.root / "old"
        old.mkdir(mode=0o755)
        self.addCleanup(old.chmod, 0o755)
        app = old / "UpdaterProbe.app/Contents"
        (app / "MacOS").mkdir(parents=True)
        (app / "MacOS/UpdaterProbe").write_bytes(b"unexecuted cleanup fixture")
        identity = "org.eiskaltdcpp.updater-probe.cleanup-test"
        (app / "Info.plist").write_bytes(plistlib.dumps(
            {"CFBundleIdentifier": identity, "CFBundleVersion": "1"}))
        (self.args.root / "fixture.json").write_text(json.dumps({"adapter": True, "bundle_id": identity}))
        child = Mock()
        child.returncode = 0 if canceled else 6
        child.poll.return_value = child.returncode
        child.communicate.return_value = (
            "AUTHORIZATION_PROBE ready=0 failed=1 code=4007 prepared=0" if canceled else "unconfirmed native error", None)
        server = Mock(server_port=9999)
        missing_right = subprocess.CompletedProcess([], 255, "", "NO (-60005)")
        with patch.object(fixtures, "run", side_effect=lambda *args: "VirtualMac2,1" if args[0] == "/usr/sbin/sysctl" else ""), \
             patch.object(fixtures, "verify"), \
             patch.object(fixtures.http.server, "ThreadingHTTPServer", return_value=server), \
             patch.object(fixtures.subprocess, "Popen", return_value=child), \
             patch.object(fixtures.subprocess, "run", return_value=missing_right), \
             patch.object(fixtures.time, "monotonic", side_effect=[0, 4]), \
             patch.object(fixtures.time, "sleep"):
            if canceled:
                fixtures.install_probe(self.args)
            else:
                with self.assertRaisesRegex(RuntimeError, "native install probe failed: 6"):
                    fixtures.install_probe(self.args)
        child.kill.assert_not_called()
        server.shutdown.assert_called_once()
        return old

    def test_unconfirmed_authorization_failure_retains_denied_destination(self):
        old = self.run_cleanup_fixture(canceled=False)
        self.assertEqual(old.stat().st_mode & 0o777, 0o500)

    def test_confirmed_cancellation_restores_destination(self):
        old = self.run_cleanup_fixture(canceled=True)
        self.assertEqual(old.stat().st_mode & 0o777, 0o755)


if __name__ == "__main__":
    unittest.main()
