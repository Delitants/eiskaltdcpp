#!/usr/bin/env python3
"""Read real main-bundle metadata in private fixtures; no signature/route claim."""
import argparse
import base64
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


def run(args):
    binary = args.build.resolve() / "tests/qt/eiskalt-update-runtime-probe"
    if not binary.is_file():
        raise RuntimeError("Build eiskalt-update-runtime-probe first")
    root = Path(tempfile.mkdtemp(prefix="eiskalt-runtime-identity-", dir="/private/tmp"))
    root.chmod(0o700)
    home = root / "home"
    home.mkdir(mode=0o700)
    environment = dict(os.environ, HOME=str(home), CFFIXED_USER_HOME=str(home),
                       XDG_CONFIG_HOME=str(home / "config"), XDG_DATA_HOME=str(home / "data"),
                       XDG_CACHE_HOME=str(home / "cache"))
    sandbox = '(version 1)(allow default)(deny network*)'

    def probe(path):
        result = subprocess.run(["/usr/bin/sandbox-exec", "-p", sandbox, str(path)], env=environment,
                                capture_output=True, text=True, timeout=10, check=True)
        return result.stdout.strip()

    print("Private runtime fixture root:", root, flush=True)
    output = probe(binary)
    if not output.startswith("ELIGIBLE=0 CPU="):
        raise RuntimeError("nonbundled probe unexpectedly eligible")
    cpu = output.split("CPU=", 1)[1]
    if cpu not in ("arm64", "x86_64"):
        raise RuntimeError("unsupported probe architecture")
    feed = "https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/"
    feed += "apple-silicon.xml" if cpu == "arm64" else "intel.xml"
    metadata = {"CFBundleIdentifier": "com.github.eiskaltdcpp", "CFBundleExecutable": "EiskaltDC++",
                "CFBundlePackageType": "APPL", "CFBundleVersion": "30000001",
                "CFBundleShortVersionString": "3.0.0-pre.1",
                "LSMinimumSystemVersion": "26.0" if cpu == "arm64" else "14.0",
                "SUPublicEDKey": base64.b64encode(b"\x11" * 32).decode(), "SUFeedURL": feed,
                "SURequireSignedFeed": True, "SUVerifyUpdateBeforeExtraction": True,
                "SUEnableDownloaderService": False, "SUEnableAutomaticChecks": False,
                "SUAutomaticallyUpdate": False, "SUEnableSystemProfiling": False,
                "SUEnableInstallerLauncherService": True, "SUSignedFeedFailureExpirationInterval": 0,
                "NSAppTransportSecurity": {"NSAllowsLocalNetworking": True}}
    cases = [("preview-xml", {}, True, False), ("preview-binary", {}, True, True),
             ("stable", {"CFBundleShortVersionString": "3.0.0"}, True, False),
             ("revision", {"SUFeedURL": feed + "?revision=10"}, True, False),
             ("foreign-id", {"CFBundleIdentifier": "org.test.foreign"}, False, False),
             ("foreign-executable", {"CFBundleExecutable": "other"}, False, False),
             ("numeric-build", {"CFBundleVersion": 30000001}, False, False),
             ("leading-zero-build", {"CFBundleVersion": "030000001"}, False, False),
             ("overflow-build", {"CFBundleVersion": "9223372036854775808"}, False, False),
             ("nul-build", {"CFBundleVersion": "30000001\0junk"}, False, True),
             ("nul-key", {"SUPublicEDKey": metadata["SUPublicEDKey"] + "\0junk"}, False, True),
             ("wrong-floor", {"LSMinimumSystemVersion": "25.0"}, False, False),
             ("foreign-feed", {"SUFeedURL": "https://example.com/feed.xml"}, False, False),
             ("integer-true", {"SURequireSignedFeed": 1}, False, False),
             ("string-false", {"SUEnableAutomaticChecks": "false"}, False, False),
             ("bool-expiration", {"SUSignedFeedFailureExpirationInterval": False}, False, False),
             ("float-expiration", {"SUSignedFeedFailureExpirationInterval": 0.0}, False, False),
             ("missing-switch", {"SUEnableSystemProfiling": None}, False, False),
             ("unsafe-ats", {"NSAppTransportSecurity": {"NSAllowsLocalNetworking": True,
                                                       "NSAllowsArbitraryLoads": True}}, False, False)]
    receipts = [{"case": "nonbundle", "eligible": False}]
    for label, changes, eligible, binary_plist in cases:
        info = dict(metadata)
        for key, value in changes.items():
            if value is None:
                info.pop(key)
            else:
                info[key] = value
        app = root / label / "EiskaltDC++.app/Contents"
        (app / "MacOS").mkdir(parents=True)
        executable = app / "MacOS/EiskaltDC++"
        shutil.copy2(binary, executable)
        (app / "Info.plist").write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY if binary_plist else plistlib.FMT_XML))
        result = probe(executable)
        expected = "ELIGIBLE=" + str(int(eligible))
        if not result.startswith(expected + " "):
            raise RuntimeError(label + ": unexpected main-bundle identity result: " + result)
        if eligible:
            channel = "stable" if label == "stable" else "preview"
            if result != f"ELIGIBLE=1 BUILD=30000001 CPU={cpu} CHANNEL={channel}":
                raise RuntimeError(label + ": wrong numeric identity/channel")
        receipts.append({"case": label, "eligible": eligible})
        print("PASS:", label, result, flush=True)
    # An executable alias into a different directory is not the main-bundle image.
    alias = root / "aliased/EiskaltDC++.app/Contents"
    (alias / "MacOS").mkdir(parents=True)
    (alias / "Info.plist").write_bytes(plistlib.dumps(metadata))
    (alias / "MacOS/EiskaltDC++").symlink_to(binary)
    if not probe(alias / "MacOS/EiskaltDC++").startswith("ELIGIBLE=0 "):
        raise RuntimeError("foreign executable alias unexpectedly eligible")
    receipts.append({"case": "foreign-executable-alias", "eligible": False})
    (root / "receipt.json").write_text(json.dumps(receipts, indent=2) + "\n")
    print("PASS:", len(receipts), "native main-bundle cases; no signature, routing or minimum-OS claim", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    run(parser.parse_args())
