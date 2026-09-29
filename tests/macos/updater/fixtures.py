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
import threading
import time
import uuid


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, capture_output=True, **kwargs).stdout.strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


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
            "CFBundleIdentifier": bundle_id, "CFBundleName": "UpdaterProbe",
            "CFBundleExecutable": "UpdaterProbe", "CFBundlePackageType": "APPL",
            "CFBundleVersion": "1" if label == "old" else "2", "CFBundleShortVersionString": version,
            "LSMinimumSystemVersion": "14.0", "LSUIElement": True,
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
    run(signer, "--ed-key-file", str(key), str(feed))
    (root / "bad-feed.xml").write_bytes(feed.read_bytes().replace(b"Probe update", b"Wrong update"))
    corrupt = bytearray(archive.read_bytes())
    corrupt[len(corrupt) // 2] ^= 1
    (root / "bad-archive.zip").write_bytes(corrupt)
    manifest = {"bundle_id": bundle_id, "public_key": public, "signature": signature,
                "sparkle": str(sparkle), "files": {p.name: digest(p) for p in
                (feed, archive, root / "bad-feed.xml", root / "bad-archive.zip")}}
    (root / "fixture.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Created disposable signed fixture (private keys remain in mode-0700 root)")


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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("create", "verify", "probe"):
        sub = commands.add_parser(name)
        sub.add_argument("--root", type=Path, required=True)
        if name == "create":
            sub.add_argument("--sparkle", type=Path, required=True)
            sub.add_argument("--binary", type=Path, required=True)
            sub.add_argument("--openssl", default="/opt/homebrew/opt/openssl@3/bin/openssl")
            sub.add_argument("--installer-xpc", action="store_true",
                             help="Use Sparkle's documented sandbox installer service")
        if name == "probe":
            sub.add_argument("--mode", choices=("valid", "bad-feed", "bad-archive", "unmapped", "information-only"), default="valid")
            sub.add_argument("--filter", default="[bridge-probe]")
        sub.set_defaults(function=globals()[name])
    args = parser.parse_args()
    args.function(args)
