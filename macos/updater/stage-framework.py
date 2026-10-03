#!/usr/bin/env python3
"""Prepare a thin, signed Sparkle dependency offline; never edit an application."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release import PROFILES, check_binary, is_macho
from release_feeds import bounded_read, publish_directory, require

LOCK = Path(__file__).with_name("sparkle.lock.json")
REQUIRED = (
    "Versions/B/Sparkle", "Versions/B/Autoupdate",
    "Versions/B/Updater.app/Contents/MacOS/Updater",
    "Versions/B/XPCServices/Downloader.xpc/Contents/MacOS/Downloader",
    "Versions/B/XPCServices/Installer.xpc/Contents/MacOS/Installer",
)


def invoke(*args):
    result = subprocess.run(list(map(str, args)), capture_output=True, timeout=120)
    require(result.returncode == 0, "Framework preparation/signing tool rejected input")


def pinned_archive(path, expected):
    data = bounded_read(path, 64 * 1024 * 1024)
    require(hashlib.sha256(data).hexdigest() == expected, "Pinned Sparkle archive checksum mismatch")
    return data


def stage(sdk_archive, source_archive, profile, output, identity):
    require(not output.exists() and not output.is_symlink(), "Staging output already exists")
    require(output.parent.is_dir(), "Staging output parent must already exist")
    require(bool(identity) and not identity.startswith("--"), "Invalid signing identity")
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    # Snapshot/check before extraction: input changes cannot substitute unpinned
    # tar bytes after verification. Only this fixed trusted SDK is extracted.
    sdk = pinned_archive(sdk_archive, lock["sha256"])
    source = pinned_archive(source_archive, lock["source_sha256"])
    with tarfile.open(fileobj=io.BytesIO(source), mode="r:gz") as archive:
        license_member = archive.getmember("Sparkle-" + lock["commit"] + "/" + lock["license"])
        require(license_member.isfile(), "Pinned source license must be a regular file")
        license_bytes = archive.extractfile(license_member).read()
    arch, floor, _ = PROFILES[profile]
    with tempfile.TemporaryDirectory(prefix=".eiskalt-sparkle-", dir=output.parent) as work:
        work = Path(work)
        work.chmod(0o700)
        snapshot = work / "sdk.tar.xz"
        snapshot.write_bytes(sdk)
        extracted = work / "sdk"
        extracted.mkdir()
        invoke("/usr/bin/tar", "-xf", snapshot, "-C", extracted)
        require((extracted / "LICENSE").read_bytes() == license_bytes,
                "Pinned binary/source license mismatch")
        prepared = work / "prepared"
        framework = prepared / "Contents/Frameworks/Sparkle.framework"
        framework.parent.mkdir(parents=True)
        shutil.copytree(extracted / "Sparkle.framework", framework, symlinks=True)
        info = plistlib.loads((framework / "Resources/Info.plist").read_bytes())
        require(info.get("CFBundleShortVersionString") == lock["version"], "Pinned framework version mismatch")
        for relative in REQUIRED:
            path = framework / relative
            require(not path.is_symlink() and path.is_file() and is_macho(path),
                    "Pinned framework/helper binary missing")
        binaries = []
        for path in sorted(framework.rglob("*")):
            if path.is_symlink():
                require(framework in path.resolve(strict=True).parents, "Framework symlink escapes staging")
            elif path.is_file() and is_macho(path):
                path.chmod(path.stat().st_mode | 0o200)
                thin = path.with_name(path.name + ".eiskalt-thin")
                invoke("/usr/bin/lipo", path, "-thin", arch, "-output", thin)
                os.replace(thin, path)
                check_binary(path, arch, floor)
                binaries.append(path)
        # Sign leaf code first, then nested bundles, then the framework seal.
        # Preserve the upstream helper IDs/entitlements; do not use --deep to
        # overwrite nested signing choices. No helper is ever executed here.
        bundles = [p for p in framework.rglob("*")
                   if not p.is_symlink() and p.is_dir() and p.suffix in (".app", ".xpc", ".framework")]
        sign_targets = [*binaries, *sorted(bundles, key=lambda p: len(p.parts), reverse=True), framework]
        for path in sign_targets:
            invoke("/usr/bin/codesign", "--force", "--sign", identity, "--timestamp=none",
                   "--preserve-metadata=identifier,entitlements,flags,runtime", path)
        for path in sign_targets:
            invoke("/usr/bin/codesign", "--verify", "--strict", path)
        invoke("/usr/bin/codesign", "--verify", "--deep", "--strict", framework)
        notices = prepared / "Contents/Resources/ThirdPartyNotices/Sparkle"
        notices.mkdir(parents=True)
        (notices / "LICENSE").write_bytes(license_bytes)
        provenance = {
            "version": lock["version"], "commit": lock["commit"],
            "archive": lock["archive"], "archive_sha256": lock["sha256"],
            "source": lock["source"], "source_sha256": lock["source_sha256"],
            "architecture": arch, "minimum_os_ceiling": floor,
            "signing_identity": identity, "timestamp": "none",
            "license_sha256": hashlib.sha256(license_bytes).hexdigest(),
            "preparation": "Verified pinned archives; lipo thin; inside-out codesign; strict verification",
            "binaries": {str(p.relative_to(framework)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in binaries},
        }
        (notices / "provenance.json").write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        publish_directory(prepared, output)
    print(f"PASS staged Sparkle {lock['version']}: {len(binaries)} thin signed {arch} binaries; not an installed app")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-archive", type=Path, required=True)
    parser.add_argument("--source-archive", type=Path, required=True)
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--identity", default="-", help="Existing codesign identity; default is ad-hoc, no key generation")
    args = parser.parse_args()
    # Resolve the parent, not the leaf: never follow an output symlink.
    output = args.output.parent.resolve(strict=True) / args.output.name
    stage(args.sdk_archive, args.source_archive, args.profile, output, args.identity)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, subprocess.SubprocessError, tarfile.TarError) as error:
        print(f"Framework staging refused: {error}", file=sys.stderr)
        sys.exit(1)
