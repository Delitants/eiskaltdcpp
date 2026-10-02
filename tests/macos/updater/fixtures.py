#!/usr/bin/env python3
"""Real, disposable Sparkle bridge fixtures. No Keychain or production profile."""
import argparse
import base64
import hashlib
import http.server
import json
import os
from pathlib import Path
import plistlib
import secrets
import shutil
import subprocess
import struct
import threading
import time
import uuid
import zipfile

LEGACY_INSTALL_MODES = ("normal", "hide", "cancel-quit", "kill-before-commit", "cancel-prepared", "bad-identity")
ADAPTER_INSTALL_MODES = ("bad-minimum-os", "cancel-before-native", "bad-feed", "bad-archive", "cancel-verifier",
                         "destroy-verifier", "cancel-check", "cancel-download", "cancel-extraction", "cancel-ready",
                         "deadline-verifier", "deadline-extraction", "cancel-after-handoff", "deadline-after-handoff", "worker-start-failure",
                          "cancel-worker-start", "deadline-worker-start")
INSTALLING_MODES = ("normal", "cancel-after-handoff", "deadline-after-handoff")


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, capture_output=True, **kwargs).stdout.strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bundle_snapshot(app):
    return {str(path.relative_to(app)): ("link", os.readlink(path)) if path.is_symlink()
            else ("file", digest(path)) for path in app.rglob("*") if path.is_symlink() or path.is_file()}


def create(args):
    root = args.root.resolve()
    root.mkdir(mode=0o700, parents=True, exist_ok=False)
    os.chmod(root, 0o700)
    sparkle = args.sparkle.resolve()
    key = root / "seed.b64"
    # OpenSSL DER encodes the Ed25519 seed/public key as the final 32 bytes.
    run(args.openssl, "genpkey", "-algorithm", "ED25519", "-out", str(root / "key.pem"))
    raw = subprocess.check_output([args.openssl, "pkey", "-in", str(root / "key.pem"), "-outform", "DER"])
    pub = subprocess.check_output([args.openssl, "pkey", "-in", str(root / "key.pem"), "-pubout", "-outform", "DER"])
    key.write_bytes(base64.b64encode(raw[-32:]))
    key.chmod(0o600)
    (root / "key.pem").chmod(0o600)
    public = base64.b64encode(pub[-32:]).decode()
    bundle_id = "org.eiskaltdcpp.updater-probe." + uuid.uuid4().hex
    new_version = "3.0.0-pre.2"
    for label, version in (("old", "3.0.0-pre.1"), ("new", new_version)):
        app = root / label / "UpdaterProbe.app"
        contents = app / "Contents"
        (contents / "MacOS").mkdir(parents=True)
        (contents / "Frameworks").mkdir()
        shutil.copy2(args.binary, contents / "MacOS" / "UpdaterProbe")
        shutil.copytree(sparkle / "Sparkle.framework", contents / "Frameworks" / "Sparkle.framework", symlinks=True)
        info = {
            "CFBundleIdentifier": bundle_id + (".wrong" if label == "new" and getattr(args, "wrong_identity", False) else ""), "CFBundleName": "UpdaterProbe",
            "CFBundleExecutable": "UpdaterProbe", "CFBundlePackageType": "APPL",
            "CFBundleVersion": "1" if label == "old" else "2", "CFBundleShortVersionString": version,
            "LSMinimumSystemVersion": "15.0" if label == "new" and args.wrong_minimum_os else "14.0", "LSUIElement": True,
            "SUPublicEDKey": public, "SURequireSignedFeed": True,
            "SUVerifyUpdateBeforeExtraction": True, "SUSignedFeedFailureExpirationInterval": 0,
            "SUEnableAutomaticChecks": False, "SUAutomaticallyUpdate": False,
            "SUEnableSystemProfiling": False, "SUEnableDownloaderService": False,
            "SUEnableInstallerLauncherService": args.installer_xpc,
            "NSAppTransportSecurity": {"NSAllowsLocalNetworking": True},
        }
        (contents / "Info.plist").write_bytes(plistlib.dumps(info))
        run("codesign", "--force", "--sign", "-", str(app))
    archive = root / "update.zip"
    run("ditto", "-c", "-k", "--keepParent", str(root / "new" / "UpdaterProbe.app"), str(archive))
    signer = str(sparkle / "bin" / "sign_update")
    signature = run(signer, "--ed-key-file", str(key), "-p", str(archive))
    # The fixture intentionally advertises an unreachable external origin. Only
    # the public request hook may map it to the private server.
    feed = root / "appcast.xml"
    feed.write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
<channel><title>Disposable updater probe</title><item><title>Probe update</title>
<sparkle:version>2</sparkle:version>
<sparkle:shortVersionString>{new_version}</sparkle:shortVersionString>
<sparkle:minimumSystemVersion>14.0</sparkle:minimumSystemVersion>
<sparkle:releaseNotesLink>https://203.0.113.1/forbidden-notes.html</sparkle:releaseNotesLink>
<enclosure url="https://203.0.113.1/update.zip" length="{archive.stat().st_size}"
 type="application/octet-stream" sparkle:edSignature="{signature}"/>
</item></channel></rss>''')
    if args.adapter:
        cpu = run("lipo", "-archs", str(args.binary)).strip()
        if cpu not in ("arm64", "x86_64"):
            raise RuntimeError("adapter fixture must have a thin executable")
        feed.write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"
 xmlns:eiskalt="https://eiskaltdcpp.org/xml-namespaces/updates/1">
<channel><title>Disposable updater probe</title><item><title>Probe update</title>
<sparkle:version>2</sparkle:version><sparkle:shortVersionString>{new_version}</sparkle:shortVersionString>
<sparkle:minimumSystemVersion>14.0</sparkle:minimumSystemVersion>
<eiskalt:bundleIdentifier>{bundle_id}</eiskalt:bundleIdentifier><eiskalt:cpu>{cpu}</eiskalt:cpu>
<enclosure url="https://github.com/Delitants/eiskaltdcpp/releases/download/v3/fixture-{cpu}.zip"
 length="{archive.stat().st_size}" type="application/octet-stream" sparkle:os="macos"
 sparkle:edSignature="{signature}" eiskalt:sha256="{digest(archive)}"/>
</item></channel></rss>''')
    run(signer, "--ed-key-file", str(key), str(feed))
    feed.chmod(0o600)
    archive.chmod(0o600)
    (root / "bad-feed.xml").write_bytes(feed.read_bytes().replace(b"Probe update", b"Wrong update"))
    corrupt = bytearray(archive.read_bytes())
    corrupt[len(corrupt) // 2] ^= 1
    (root / "bad-archive.zip").write_bytes(corrupt)
    for name in ("bad-feed.xml", "bad-archive.zip"):
        (root / name).chmod(0o600)
    manifest = {"bundle_id": bundle_id, "public_key": public, "signature": signature, "adapter": args.adapter,
                "sparkle": str(sparkle), "files": {p.name: digest(p) for p in
                (feed, archive, root / "bad-feed.xml", root / "bad-archive.zip")}}
    (root / "fixture.json").write_text(json.dumps(manifest, indent=2) + "\n")
    archive_cases(root)
    print("Created disposable signed fixture (private keys remain in mode-0700 root)")


def archive_cases(root):
    """Unsigned parser inputs only: never offered to the installer or trust gate."""
    target = root / "parser-cases"
    target.mkdir()
    app = root / "new/UpdaterProbe.app/Contents"
    info = (app / "Info.plist").read_bytes()
    header = (app / "MacOS/UpdaterProbe").read_bytes()[:64]
    base = [("UpdaterProbe.app/", b"", 0o40755),
            ("UpdaterProbe.app/Contents/", b"", 0o40755),
            ("UpdaterProbe.app/Contents/MacOS/", b"", 0o40755),
            ("UpdaterProbe.app/Contents/Info.plist", info, 0o100644),
            ("UpdaterProbe.app/Contents/MacOS/UpdaterProbe", header, 0o100755)]
    def make(name, entries, method=zipfile.ZIP_DEFLATED):
        with zipfile.ZipFile(target / (name + ".zip"), "w", compression=method) as archive:
            for path, data, mode in entries:
                entry = zipfile.ZipInfo(path)
                entry.create_system = 3
                entry.external_attr = mode << 16
                entry.compress_type = method
                archive.writestr(entry, data)
    make("valid-stored", base, zipfile.ZIP_STORED)
    make("valid-deflated", base)
    make("valid-binary-plist", base[:3] + [(base[3][0], plistlib.dumps(plistlib.loads(info), fmt=plistlib.FMT_BINARY), 0o100644)] + base[4:])
    nul = plistlib.loads(info)
    nul["CFBundleExecutable"] = "UpdaterProbe\x00other"
    make("nul-executable", base[:3] + [(base[3][0], plistlib.dumps(nul, fmt=plistlib.FMT_BINARY), 0o100644)] + base[4:])
    make("traversal", base + [("UpdaterProbe.app/../escape", b"", 0o100644)])
    make("second-app", base + [("Other.app/", b"", 0o40755)])
    make("aliased-plist", base + [("UpdaterProbe.app/Contents/info.plist", info, 0o100644)])
    make("aliased-directory", base + [("UpdaterProbe.app/Contents", b"elsewhere", 0o120777)])
    make("symlink-plist", base[:3] + [(base[3][0], info, 0o120777)] + base[4:])
    make("symlink-parent", [base[0], ("UpdaterProbe.app/Contents", b"elsewhere", 0o120777)] + base[2:])
    make("missing-executable", base[:-1])
    make("fat-executable", base[:-1] + [(base[-1][0], b"\xca\xfe\xba\xbe" + header[4:], 0o100755)])
    make("oversize-plist", base[:3] + [(base[3][0], b" " * (1024 * 1024 + 1), 0o100644)] + base[4:])
    make("plist-array", base[:3] + [(base[3][0], plistlib.dumps([]), 0o100644)] + base[4:])
    for field, value in (("CFBundleExecutable", "../UpdaterProbe"), ("CFBundleVersion", 2),
                         ("CFBundleIdentifier", "other.bundle"), ("CFBundlePackageType", "FMWK")):
        changed = plistlib.loads(info)
        changed[field] = value
        make("wrong-" + field, base[:3] + [(base[3][0], plistlib.dumps(changed), 0o100644)] + base[4:])
    stored = (target / "valid-stored.zip").read_bytes()
    (target / "truncated.zip").write_bytes(stored[:-5])
    (target / "trailing.zip").write_bytes(stored + b"garbage")
    for name, offset, fmt, value in (("entry-count", len(stored) - 12, "H", 65535),
                                    ("central-offset", len(stored) - 6, "I", 0xffffffff),
                                    ("local-method", 8, "H", 8),
                                    ("local-name", 30, "B", ord("X"))):
        changed = bytearray(stored)
        struct.pack_into("<" + fmt, changed, offset, value)
        (target / (name + ".zip")).write_bytes(changed)
    changed = bytearray(stored)
    at = stored.index(b"<?xml")
    changed[at] ^= 1
    (target / "plist-crc.zip").write_bytes(changed)


def verify(args):
    root = args.root.resolve()
    manifest = json.loads((root / "fixture.json").read_text())
    for name, sha in manifest["files"].items():
        if digest(root / name) != sha:
            raise ValueError("fixture changed: " + name)
    signer = str(Path(manifest["sparkle"]) / "bin" / "sign_update")
    for name in ("appcast.xml", "update.zip", "bad-feed.xml", "bad-archive.zip"):
        command = [signer, "--ed-key-file", str(root / "seed.b64"), "--verify", str(root / name)]
        if name.endswith(".zip"):
            command.append(manifest["signature"])
        result = subprocess.run(command, capture_output=True, text=True)
        expected = not name.startswith("bad-")
        if (result.returncode == 0) != expected:
            raise RuntimeError("unexpected verification result: " + name + " " + result.stdout + result.stderr)
    print("PASS: signed feed/archive verify; both tampered files rejected")


def probe(args):
    root = args.root.resolve()
    info_path = root / "old/UpdaterProbe.app/Contents/Info.plist"
    before_info = info_path.read_bytes()
    before_executable = digest(root / "old/UpdaterProbe.app/Contents/MacOS/UpdaterProbe")
    token = secrets.token_hex(32)
    requests = []
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path.replace(token, "CAPABILITY"))
            name = {f"/{token}/feed": "bad-feed.xml" if args.mode == "bad-feed" else "appcast.xml",
                    f"/{token}/update.zip": "bad-archive.zip" if args.mode == "bad-archive" else "update.zip"}.get(self.path)
            if not name:
                self.send_error(404)
                return
            data = (root / name).read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "application/xml" if name.endswith("xml") else "application/zip")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        def log_message(self, *unused):
            pass
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    base = f"http://127.0.0.1:{server.server_port}/{token}"
    home = root / ("home-" + args.mode)
    home.mkdir(exist_ok=True)
    env = dict(os.environ, HOME=str(home), CFFIXED_USER_HOME=str(home),
               EISKALT_PROBE_FEED=base + "/feed", EISKALT_PROBE_ARCHIVE=base + "/update.zip",
               EISKALT_PROBE_MODE=args.mode)
    profile = root / "network.sb"
    profile.write_text('''(version 1)
(allow default)
(deny network*)
(allow network-outbound (remote ip "localhost:*"))
(allow network-bind (local ip "localhost:*"))
(allow network-inbound (local ip "localhost:*"))
''')
    try:
        result = subprocess.run(["sandbox-exec", "-f", str(profile),
            str(root / "old/UpdaterProbe.app/Contents/MacOS/UpdaterProbe"), args.filter],
            env=env, capture_output=True, text=True, timeout=45)
        text = result.stdout + result.stderr
        (root / (args.mode + ".log")).write_text(text.replace(token, "CAPABILITY"))
        print(text.replace(token, "CAPABILITY"))
        print("HTTP requests:", requests)
        (root / (args.mode + "-requests.json")).write_text(json.dumps(requests))
        expected_requests = ["/CAPABILITY/feed"]
        if args.mode in ("valid", "bad-archive"):
            expected_requests.append("/CAPABILITY/update.zip")
        if requests != expected_requests:
            raise RuntimeError("unexpected feed/archive/notes requests: " + repr(requests))
        # The installer helper is asynchronous. Check after its cancellation,
        # not just before the parent exits (Dismiss used to replace this copy).
        time.sleep(2)
        if info_path.read_bytes() != before_info or digest(root / "old/UpdaterProbe.app/Contents/MacOS/UpdaterProbe") != before_executable:
            raise RuntimeError("disposable app was replaced despite cancellation")
        raise SystemExit(result.returncode)
    finally:
        server.shutdown()
        server.server_close()


def install_probe(args):
    """Post-exit assertions for disposable bundles, never the installed app."""
    root = args.root.resolve()
    manifest = json.loads((root / "fixture.json").read_text())
    if bool(manifest.get("adapter")) != args.adapter:
        raise RuntimeError("create a matching adapter/non-adapter fixture")
    if args.mode in ADAPTER_INSTALL_MODES and not args.adapter:
        raise RuntimeError("this mode requires the production adapter fixture")
    app = root / "old/UpdaterProbe.app"
    info = app / "Contents/Info.plist"
    if not manifest["bundle_id"].startswith("org.eiskaltdcpp.updater-probe.") or (root.stat().st_mode & 0o077):
        raise RuntimeError("not a private disposable fixture")
    if plistlib.loads(info.read_bytes())["CFBundleIdentifier"] != manifest["bundle_id"]:
        raise RuntimeError("unexpected target identity")
    if plistlib.loads(info.read_bytes())["CFBundleVersion"] != "1":
        raise RuntimeError("use a fresh version-1 fixture for each install case")
    # Authenticate the staged archive before its native identity preflight.
    verify(args)
    before_info = info.read_bytes()
    before_executable = digest(app / "Contents/MacOS/UpdaterProbe")
    before_bundle = bundle_snapshot(app)
    home = root / "install-home"
    home.mkdir(mode=0o700, exist_ok=False)
    protected = root / "user-data"
    for name in ("profile/settings.xml", "shares/shared.dat", "downloads/partial.dat"):
        file = protected / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(secrets.token_bytes(73))
    def snapshot():
        return {str(p.relative_to(protected)): digest(p) for p in protected.rglob("*") if p.is_file()}
    before_data = snapshot()
    token = secrets.token_hex(32)
    requests = []
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path.replace(token, "CAPABILITY"))
            name = {f"/{token}/feed": "appcast.xml", f"/{token}/update.zip": "update.zip"}.get(self.path)
            if name is None:
                self.send_error(404)
                return
            data = (root / name).read_bytes()
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        def log_message(self, *unused):
            pass
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    base = f"http://127.0.0.1:{server.server_port}/{token}"
    relaunch_marker, kill_marker = root / "relaunched", root / "kill-ready"
    environment = dict(os.environ, HOME=str(home), CFFIXED_USER_HOME=str(home),
        EISKALT_INSTALL_MODE=args.mode, EISKALT_PROBE_FEED=base + "/feed",
        EISKALT_PROBE_ARCHIVE=base + "/update.zip", EISKALT_PROBE_RELAUNCH_MARKER=str(relaunch_marker),
        EISKALT_PROBE_KILL_MARKER=str(kill_marker), EISKALT_PROBE_LOCAL_ARCHIVE=str(root / "update.zip"),
        EISKALT_PROBE_LOCAL_FEED=str(root / "appcast.xml"))
    sandbox = '(version 1)(allow default)(deny network*)' \
        '(allow network-outbound (remote ip "localhost:*"))' \
        '(allow network-bind (local ip "localhost:*"))' \
        '(allow network-inbound (local ip "localhost:*"))'
    child = None
    try:
        child = subprocess.Popen(["sandbox-exec", "-p", sandbox,
            str(app / "Contents/MacOS/UpdaterProbe"), "[update-install-adapter]" if args.adapter else "[update-install]"], env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if args.mode == "kill-before-commit":
            deadline = time.monotonic() + 35
            while not kill_marker.exists() and child.poll() is None and time.monotonic() < deadline:
                time.sleep(0.05)
            if not kill_marker.exists():
                raise RuntimeError("disposable child did not reach pre-commit discovery boundary")
            child.kill()
        output, _ = child.communicate(timeout=40)
        (root / "install.log").write_text(output.replace(token, "CAPABILITY"))
        print(output.replace(token, "CAPABILITY"))
        if child.returncode != (-9 if args.mode == "kill-before-commit" else 0):
            raise RuntimeError("native install probe failed: " + str(child.returncode))
        committed = args.mode not in ("hide", "cancel-quit", "kill-before-commit")
        expected = ["/CAPABILITY/feed"] + (["/CAPABILITY/update.zip"] if committed else [])
        if args.mode == "bad-identity":
            expected = []
        if args.adapter:
            expected = []  # The production adapter owns its separate local bridge.
        if requests != expected:
            raise RuntimeError("unexpected requests: " + repr(requests))
        # Installation is asynchronous and can complete after the old process exits.
        installs = args.mode in INSTALLING_MODES
        deadline = time.monotonic() + (15 if installs else 3)
        while time.monotonic() < deadline:
            if installs and info.exists() and plistlib.loads(info.read_bytes())["CFBundleVersion"] == "2":
                break
            time.sleep(0.1)
        time.sleep(2)
        if installs:
            if info.read_bytes() != (root / "new/UpdaterProbe.app/Contents/Info.plist").read_bytes() or \
                digest(app / "Contents/MacOS/UpdaterProbe") != digest(root / "new/UpdaterProbe.app/Contents/MacOS/UpdaterProbe"):
                raise RuntimeError("normal quit did not install the exact signed new bundle")
        elif info.read_bytes() != before_info or digest(app / "Contents/MacOS/UpdaterProbe") != before_executable:
            raise RuntimeError("uncommitted/cancelled/invalid update replaced the old bundle")
        expected_bundle = bundle_snapshot(root / "new/UpdaterProbe.app") if installs else before_bundle
        if bundle_snapshot(app) != expected_bundle:
            raise RuntimeError("installed bundle tree differs from the exact expected signed bundle")
        run("codesign", "--verify", "--deep", "--strict", str(app))
        if relaunch_marker.exists():
            raise RuntimeError("application relaunched despite no-relaunch policy")
        if snapshot() != before_data:
            raise RuntimeError("profile/share/download fixture changed")
        result = {"mode": args.mode, "requests": requests, "installed": installs,
                  "relaunched": False, "user_data_unchanged": True, "exact_bundle": True, "signature_valid": True}
        (root / "install-result.json").write_text(json.dumps(result, indent=2) + "\n")
        print("PASS:", json.dumps(result))
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.communicate(timeout=5)
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("create", "verify", "probe", "install-probe"):
        sub = commands.add_parser(name)
        sub.add_argument("--root", type=Path, required=True)
        if name == "create":
            sub.add_argument("--sparkle", type=Path, required=True)
            sub.add_argument("--binary", type=Path, required=True)
            sub.add_argument("--openssl", default="/opt/homebrew/opt/openssl@3/bin/openssl")
            sub.add_argument("--installer-xpc", action="store_true",
                             help="Use Sparkle's documented sandbox installer service")
            sub.add_argument("--wrong-identity", action="store_true", help="Sign a disposable archive with a mismatched bundle ID")
            sub.add_argument("--wrong-minimum-os", action="store_true", help="Sign a disposable archive with a mismatched minimum OS")
            sub.add_argument("--adapter", action="store_true", help="Create production-schema installer adapter fixtures")
        if name == "probe":
            sub.add_argument("--mode", choices=("valid", "bad-feed", "bad-archive", "unmapped", "information-only"), default="valid")
            sub.add_argument("--filter", default="[bridge-probe]")
        if name == "install-probe":
            sub.add_argument("--mode", choices=LEGACY_INSTALL_MODES + ADAPTER_INSTALL_MODES, required=True)
            sub.add_argument("--adapter", action="store_true", help="Exercise the production Qt installer adapter")
        sub.set_defaults(function=globals()[name.replace("-", "_")])
    args = parser.parse_args()
    args.function(args)
