#!/usr/bin/env python3
"""Signed disposable full-application handoff; never run against a live bundle."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from types import SimpleNamespace
import xml.etree.ElementTree as ET

import fixtures
from run_application_quit import compile_entrypoint


def run_mode(root, binary, sparkle, mode):
    root.mkdir(mode=0o700)
    fixture = root / "fixture"
    options = SimpleNamespace(root=fixture, sparkle=sparkle, binary=binary,
                              openssl="/opt/homebrew/opt/openssl@3/bin/openssl", adapter=True,
                              installer_xpc=True, wrong_minimum_os=False, wrong_identity=False)
    fixtures.create(options)
    fixtures.verify(SimpleNamespace(root=fixture))
    app = fixture / "old/UpdaterProbe.app"
    before_bundle = fixtures.bundle_snapshot(app)
    expected = fixtures.bundle_snapshot(fixture / "new/UpdaterProbe.app") if mode == "normal" else before_bundle
    home = root / "home"
    home.mkdir(mode=0o700)
    for leaf in (".config", ".local/share", "cache", "tmp", "downloads", "shares"):
        (home / leaf).mkdir(parents=True, mode=0o700)
    protected = {home / "downloads/untouched.part": b"download-fixture",
                 home / "shares/untouched.bin": b"share-fixture"}
    for path, data in protected.items():
        path.write_bytes(data)
    environment = dict(os.environ, HOME=str(home), CFFIXED_USER_HOME=str(home),
                       XDG_CONFIG_HOME=str(home / ".config"), XDG_DATA_HOME=str(home / ".local/share"),
                       XDG_CACHE_HOME=str(home / "cache"), TMPDIR=str(home / "tmp") + "/",
                       QT_QPA_PLATFORM="cocoa", EISKALT_APPLICATION_FIXTURE=str(root),
                       EISKALT_APPLICATION_TEST_MODE=mode)
    sandbox = '(version 1)(allow default)(deny network*)' \
              '(allow network-outbound (remote ip "localhost:*"))' \
              '(allow network-bind (local ip "localhost:*"))' \
              '(allow network-inbound (local ip "localhost:*"))'
    child = subprocess.Popen(["/usr/bin/sandbox-exec", "-p", sandbox, str(app / "Contents/MacOS/UpdaterProbe")],
                             env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        if mode == "kill-before-commit":
            deadline = time.monotonic() + 90
            while child.poll() is None and not (root / "kill-ready").is_file() and time.monotonic() < deadline:
                time.sleep(0.05)
            if not (root / "kill-ready").is_file():
                raise RuntimeError("owned child did not reach the ready precommit boundary")
            child.kill()
        output, _ = child.communicate(timeout=120)
        (root / "application.log").write_text(output)
        print(output[-2500:], flush=True)
        if mode == "kill-before-commit":
            if child.returncode != -signal.SIGKILL or "PRODUCTION_MAIN_RETURNED=" in output:
                raise RuntimeError("precommit kill was not an owned abnormal exit")
        else:
            if child.returncode != 0 or "PRODUCTION_MAIN_RETURNED=0 CHECKED=1" not in output:
                raise RuntimeError("actual application did not return through checked teardown")
            required = ["IPC_HELD_AT_QUIT=1", "IPC_HELD_DURING_GUI_TEARDOWN=1",
                        "NATIVE_QUIT_PREPARED=" + str(int(mode == "normal")) + " FIXTURE_REQUESTS=2"]
            if mode in ("hide", "cancel-quit"):
                required.append("NO_INSTALL_BOUNDARY=" + mode)
            if not all(marker in output for marker in required):
                raise RuntimeError("application handoff/consent/owner receipts missing")
            config = home / ".config/eiskaltdc++"
            for name in ("DCPlusPlus.xml", "Queue.xml", "HashIndex.xml"):
                ET.parse(config / name)
            hashes = ET.parse(config / "HashIndex.xml").getroot()
            files = hashes.findall("./Files/File")
            if len(files) != 1 or files[0].get("Name") != str(home / "shares/untouched.bin"):
                raise RuntimeError("application hash snapshot not preserved")
            tree = hashes.find("./Trees/Hash")
            if tree is None or tree.get("Root") != files[0].get("Root") or tree.get("Size") != "13":
                raise RuntimeError("application hash tree does not match fixture data")
            state = json.loads((config / "torrent/state/jobs.json").read_text())
            if state != {"version": 2, "jobs": []} or not (config / "EiskaltDC++_Qt.conf").is_file():
                raise RuntimeError("application GUI/Torrent checkpoint missing")
        deadline = time.monotonic() + 45
        while fixtures.bundle_snapshot(app) != expected and time.monotonic() < deadline:
            time.sleep(0.1)
        if fixtures.bundle_snapshot(app) != expected:
            raise RuntimeError("actual full-app replacement differs from exact expected signed tree")
        subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(app)], check=True, capture_output=True)
        time.sleep(2)
        if (root / "relaunched").exists():
            raise RuntimeError("updated application relaunched without consent")
        for path, data in protected.items():
            if path.read_bytes() != data:
                raise RuntimeError("protected download/share data changed")
        receipt = {"mode": mode, "normal_return": mode != "kill-before-commit", "installed": mode == "normal",
                   "exact_signed_bundle": True, "no_relaunch": True, "protected_data_unchanged": True}
        (root / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print("PASS:", mode, receipt, flush=True)
    finally:
        # Own exactly this disposable application child, never another app/helper.
        if child.poll() is None:
            child.kill()
            child.communicate(timeout=15)


def run(args):
    root = Path(tempfile.mkdtemp(prefix="eiskalt-app-quit-native-", dir="/private/tmp"))
    root.chmod(0o700)
    print("Disposable signed application root:", root, flush=True)
    binary = compile_entrypoint(args.build, root, args.native_build)
    for mode in args.mode or ["normal"]:
        run_mode(root / mode, binary, args.sparkle.resolve(), mode)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--native-build", type=Path, required=True)
    parser.add_argument("--sparkle", type=Path, required=True)
    parser.add_argument("--mode", action="append", choices=("normal", "no-consent", "hide", "cancel-quit", "kill-before-commit"))
    run(parser.parse_args())
