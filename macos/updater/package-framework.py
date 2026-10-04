#!/usr/bin/env python3
"""Audit a pinned local stage, copy after Qt deployment, explicitly seal a build app."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release import PROFILES, check, check_binary, is_macho
from release_feeds import bounded_read, decode, require, unique_object

HERE = Path(__file__).resolve().parent
FRAMEWORK = Path("Contents/Frameworks/Sparkle.framework")
NOTICES = Path("Contents/Resources/ThirdPartyNotices/Sparkle")
REQUIRED = {
    "Versions/B/Sparkle", "Versions/B/Autoupdate",
    "Versions/B/Updater.app/Contents/MacOS/Updater",
    "Versions/B/XPCServices/Downloader.xpc/Contents/MacOS/Downloader",
    "Versions/B/XPCServices/Installer.xpc/Contents/MacOS/Installer",
}
LICENSE_SHA256 = "389a4e4e9a32f059775b13a06e25a591445ba229d2838d26dd3e7c0c45127cfe"
PREFIX = "https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/"


def invoke(*args):
    result = subprocess.run(list(map(str, args)), capture_output=True, timeout=120)
    require(result.returncode == 0, "Bundle audit/signing tool rejected input")


def digest_tree(root):
    require(root.is_dir() and not root.is_symlink(), "Tree must be a real directory")
    records, total = [], 0
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode):
            require(root.resolve() in path.resolve(strict=True).parents, "Tree symlink escapes root")
            records.append([name, "link", os.readlink(path)])
        elif stat.S_ISDIR(info.st_mode):
            records.append([name, "dir"])
        else:
            require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, "Tree contains nonregular or hard-linked file")
            require(info.st_size <= 64 * 1024 * 1024, "Tree file exceeds limit")
            total += info.st_size
            require(total <= 256 * 1024 * 1024, "Tree exceeds size limit")
            # bounded_read deliberately forbids empty inputs; bundle resource files may be empty.
            digest = hashlib.sha256(bounded_read(path, 64 * 1024 * 1024) if info.st_size else b"").hexdigest()
            records.append([name, "file", bool(info.st_mode & 0o111), digest])
        require(len(records) <= 10000, "Tree entry limit exceeded")
    return hashlib.sha256(json.dumps(records, ensure_ascii=True, separators=(",", ":")).encode()).hexdigest()


def audit_stage(stage, profile, expected):
    require(re.fullmatch(r"[0-9a-f]{64}", expected) is not None, "Explicit canonical stage checksum required")
    require(digest_tree(stage) == expected, "Audited stage checksum mismatch")
    framework = stage / FRAMEWORK
    allowed = {FRAMEWORK, NOTICES, NOTICES / "LICENSE", NOTICES / "provenance.json"}
    for path in (FRAMEWORK, NOTICES):
        allowed.update(path.parents)
    for path in stage.rglob("*"):
        relative = path.relative_to(stage)
        require(relative in allowed or FRAMEWORK in relative.parents, "Unexpected staged content")
    license_bytes = bounded_read(stage / NOTICES / "LICENSE", 1024 * 1024)
    require(hashlib.sha256(license_bytes).hexdigest() == LICENSE_SHA256, "Pinned source license mismatch")
    receipt = json.loads(bounded_read(stage / NOTICES / "provenance.json", 1024 * 1024),
                         object_pairs_hook=unique_object)
    lock = json.loads((HERE / "sparkle.lock.json").read_text())
    for key, value in {"version": lock["version"], "commit": lock["commit"],
                       "archive": lock["archive"], "archive_sha256": lock["sha256"],
                       "source": lock["source"], "source_sha256": lock["source_sha256"],
                       "license_sha256": LICENSE_SHA256, "timestamp": "none"}.items():
        require(receipt.get(key) == value, "Pinned stage provenance mismatch")
    arch, floor, _ = PROFILES[profile]
    require(receipt.get("architecture") == arch and receipt.get("minimum_os_ceiling") == floor,
            "Stage architecture/OS mismatch")
    require(type(receipt.get("binaries")) is dict and set(receipt["binaries"]) == REQUIRED,
            "Stage helper inventory mismatch")
    binaries = {}
    for path in framework.rglob("*"):
        if not path.is_symlink() and path.is_file() and is_macho(path):
            check_binary(path, arch, floor)
            binaries[path.relative_to(framework).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    require(binaries == receipt["binaries"], "Stage helper hashes mismatch")
    require(plistlib.loads(bounded_read(framework / "Versions/B/Resources/Info.plist", 1024 * 1024))
            .get("CFBundleShortVersionString") == lock["version"], "Stage framework version mismatch")
    for relative in REQUIRED:
        binary = framework / relative
        require(binary.is_file() and not binary.is_symlink() and os.access(binary, os.X_OK), "Missing stage helper")
        invoke("/usr/bin/codesign", "--verify", "--strict", binary)
    invoke("/usr/bin/codesign", "--verify", "--deep", "--strict", framework)


def metadata(app, profile):
    info = plistlib.loads(bounded_read(app / "Contents/Info.plist", 1024 * 1024))
    require(info.get("CFBundleIdentifier") == "com.github.eiskaltdcpp", "Bundle metadata identity mismatch")
    require(decode(info.get("SUPublicEDKey"), 32) != b"\0" * 32, "Bundle metadata key missing")
    feed = "intel.xml" if profile == "intel" else "apple-silicon.xml"
    require(type(info.get("SUFeedURL")) is str and len(info["SUFeedURL"]) <= 2048 and
            re.fullmatch(re.escape(PREFIX + feed) + r"(?:\?revision=[1-9][0-9]*)?", info["SUFeedURL"]),
            "Bundle metadata feed mismatch")
    for name in ("SURequireSignedFeed", "SUVerifyUpdateBeforeExtraction", "SUEnableInstallerLauncherService"):
        require(info.get(name) is True, "Unsafe bundle metadata")
    for name in ("SUEnableAutomaticChecks", "SUAutomaticallyUpdate", "SUEnableSystemProfiling", "SUEnableDownloaderService"):
        require(info.get(name) is False, "Unsafe bundle metadata")
    require(type(info.get("SUSignedFeedFailureExpirationInterval")) is int and
            info["SUSignedFeedFailureExpirationInterval"] == 0 and
            info.get("NSAppTransportSecurity") == {"NSAllowsLocalNetworking": True}, "Unsafe bundle metadata")
    check(app, profile, info.get("CFBundleShortVersionString"), info.get("CFBundleVersion"))
    invoke("/bin/sh", HERE.parent / "check-bundle-external-links.sh", app)


def confined_app(app, build_root):
    require(build_root.is_dir() and not build_root.is_symlink(), "Existing owned build root required")
    require(app.is_dir() and app.suffix == ".app" and not app.is_symlink(), "Real build app required")
    root = build_root.resolve(strict=True)
    require(root in app.resolve(strict=True).parents, "Application is outside owned build root")
    # Parent indirections are not accepted even when they point back into the root.
    for parent in (app, *app.parents):
        if parent == build_root:
            break
        require(not parent.is_symlink(), "Symlinked build output refused")
    digest_tree(app)


def code_digest(path, work, name):
    # Strip signatures only on an owned disposable copy, never on input code.
    copy = work / name
    shutil.copyfile(path, copy)
    invoke("/usr/bin/codesign", "--remove-signature", copy)
    return hashlib.sha256(copy.read_bytes()).hexdigest()


def bundle_executables(app):
    app = app.resolve(strict=True)
    managed = set()
    for bundle in (app, *app.rglob("*")):
        if (bundle.is_symlink() or not bundle.is_dir() or
                bundle.suffix not in (".app", ".xpc", ".framework", ".bundle") or
                bundle == app / FRAMEWORK or app / FRAMEWORK in bundle.parents):
            continue
        framework = bundle.suffix == ".framework"
        info_path = bundle / ("Resources/Info.plist" if framework else "Contents/Info.plist")
        if not info_path.is_file():
            continue
        name = plistlib.loads(bounded_read(info_path, 1024 * 1024)).get("CFBundleExecutable")
        if name is None:  # Resource-only bundles have no executable to defer.
            continue
        require(type(name) is str and name and Path(name).name == name, "Invalid nested bundle executable")
        executable = ((bundle if framework else bundle / "Contents/MacOS") / name).resolve(strict=True)
        require(bundle.resolve() in executable.parents and is_macho(executable), "Unsafe nested bundle executable")
        managed.add(executable.relative_to(app))
    return managed


def seal(app, stage, profile, expected, build_root, identity):
    require(identity and not identity.startswith("--"), "Invalid signing identity")
    confined_app(app, build_root)
    # The private snapshot, not a mutable input path, is verified and copied.
    with tempfile.TemporaryDirectory(prefix=".eiskalt-updater-package-", dir=build_root) as temp:
        snapshot = Path(temp) / "stage"
        digest_tree(stage)
        shutil.copytree(stage, snapshot, symlinks=True)
        audit_stage(snapshot, profile, expected)
        cocoa = app / "Contents/PlugIns/platforms/libqcocoa.dylib"
        require(cocoa.is_file() and not cocoa.is_symlink() and is_macho(cocoa), "Bundled Cocoa plugin missing")
        cocoa_code_hash = code_digest(cocoa, Path(temp), "cocoa-before.dylib")
        for relative in (FRAMEWORK, NOTICES):
            target = app / relative
            # All existing parent nodes must be owned real directories, never links.
            for parent in target.parents:
                if parent == app:
                    break
                require(not parent.is_symlink() and (not parent.exists() or parent.is_dir()),
                        "Unsafe build destination parent")
            require(not target.is_symlink(), "Symlinked build destination")
            if target.exists():
                require(digest_tree(target) == digest_tree(snapshot / relative),
                        "Existing updater destination differs from audited stage")
        receipt_path = app / "Contents/Resources/ThirdPartyNotices/EiskaltUpdaterPackaging.json"
        require(not receipt_path.is_symlink() and (not receipt_path.exists() or receipt_path.is_file()),
                "Unsafe packaging receipt destination")
        # The executable may already import Sparkle, which Qt deployment does
        # not copy. Audit the prospective closure without mutating the input.
        prospective = Path(temp) / app.name
        shutil.copytree(app, prospective, symlinks=True)
        for relative in (FRAMEWORK, NOTICES):
            target = prospective / relative
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(snapshot / relative, target, symlinks=True)
        metadata(prospective, profile)
        managed_executables = bundle_executables(prospective)
        # Deployment may invalidate a prior seal. Sign only this confined copied
        # leaf after all input/path checks, retaining its non-signature code bytes.
        invoke("/usr/bin/codesign", "--force", "--sign", identity, "--timestamp=none",
               "--preserve-metadata=identifier,entitlements,flags,runtime", cocoa)
        invoke("/usr/bin/codesign", "--verify", "--strict", cocoa)
        require(code_digest(cocoa, Path(temp), "cocoa-after.dylib") == cocoa_code_hash,
                "Cocoa loadable bytes changed during signing")
        cocoa_hash = hashlib.sha256(cocoa.read_bytes()).hexdigest()
        for relative in (FRAMEWORK, NOTICES):
            target = app / relative
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(snapshot / relative, target, symlinks=True)
        receipt_path.write_text(json.dumps({"schema_version": 1, "profile": profile,
                                           "stage_sha256": expected, "cocoa_sha256": cocoa_hash,
                                           "cocoa_code_sha256": cocoa_code_hash,
                                           "signing_identity": identity, "timestamp": "none",
                                           "preparation": "Qt deployment complete; exact audited stage copy; explicit inside-out seal"},
                                          indent=2, sort_keys=True) + "\n")
        framework = app / FRAMEWORK
        binaries, bundles = [], []
        for path in app.rglob("*"):
            if path.is_symlink() or path == framework or framework in path.parents or path == cocoa:
                continue
            if path.is_file() and is_macho(path):
                # Signing/verifying a bundle executable as a bare leaf can
                # inspect stale sibling plugin signatures. Defer to its bundle.
                if path.relative_to(app) not in managed_executables:
                    binaries.append(path)
            elif path.is_dir() and path.suffix in (".app", ".xpc", ".framework", ".bundle"):
                bundles.append(path)
        # Do not re-sign/rewrite audited Sparkle helpers or the patched Cocoa plugin.
        for path in [*sorted(binaries), *sorted(bundles, key=lambda p: len(p.parts), reverse=True), app]:
            invoke("/usr/bin/codesign", "--force", "--sign", identity, "--timestamp=none",
                   "--preserve-metadata=identifier,entitlements,flags,runtime", path)
            invoke("/usr/bin/codesign", "--verify", "--strict", path)
        require(hashlib.sha256(cocoa.read_bytes()).hexdigest() == cocoa_hash, "Cocoa plugin changed during seal")
        require(digest_tree(framework) == digest_tree(snapshot / FRAMEWORK), "Audited framework changed during seal")
        invoke("/usr/bin/codesign", "--verify", "--deep", "--strict", app)
        metadata(app, profile)
    print("PASS sealed audited updater dependencies; runtime remains separately gated")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--stage-sha256", required=True)
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--audit-stage", action="store_true")
    parser.add_argument("--app", type=Path)
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--identity", default="-")
    args = parser.parse_args()
    if args.audit_stage:
        require(args.app is None and args.build_root is None, "Audit mode cannot mutate a bundle")
        audit_stage(args.stage, args.profile, args.stage_sha256)
    else:
        require(args.app is not None and args.build_root is not None, "Build app/root required")
        seal(args.app, args.stage, args.profile, args.stage_sha256, args.build_root, args.identity)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError, plistlib.InvalidFileException) as error:
        print(f"Updater packaging refused: {error}", file=sys.stderr)
        sys.exit(1)
