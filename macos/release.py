#!/usr/bin/env python3
"""Audit and package the two supported macOS release flavors without changing apps."""

import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys
import tempfile
import zipfile

PROFILES = {
    "intel": ("x86_64", "14.0", "macOS14-plus-Intel-x86_64"),
    "apple-silicon": ("arm64", "26.0", "macOS26-plus-AppleSilicon-arm64"),
}


def run(*args):
    return subprocess.run(args, check=True, text=True, capture_output=True).stdout


def version_tuple(value):
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,2}", str(value)):
        raise ValueError("invalid macOS deployment version: " + str(value))
    parts = tuple(map(int, value.split(".")))
    return parts + (0,) * (3 - len(parts))


def is_macho(path):
    with path.open("rb") as stream:
        magic = stream.read(4)
    return magic in (b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
                     b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
                     b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
                     b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca")


def check_binary(path, arch, floor):
    arches = run("lipo", "-archs", str(path)).strip().split()
    if arches != [arch]:
        raise ValueError(f"{path}: architecture {arches}; expected only {arch}")
    commands = run("otool", "-l", str(path))
    blocks = re.split(r"Load command \d+", commands)
    versions = []
    for block in blocks:
        if re.search(r"\bcmd LC_BUILD_VERSION\b", block):
            platform = re.search(r"^\s*platform\s+(\S+)", block, re.M)
            if not platform or platform[1] not in ("1", "MACOS", "macOS"):
                raise ValueError(f"{path}: non-macOS deployment platform")
            match = re.search(r"^\s*minos\s+(\S+)", block, re.M)
            if match:
                versions.append(match[1])
        elif re.search(r"\bcmd LC_VERSION_MIN_MACOSX\b", block):
            match = re.search(r"^\s*version\s+(\S+)", block, re.M)
            if match:
                versions.append(match[1])
    if not versions:
        raise ValueError(f"{path}: missing macOS deployment metadata")
    for minimum in versions:
        if version_tuple(minimum) > version_tuple(floor):
            raise ValueError(f"{path}: requires macOS {minimum}, above {floor}")


def check(app, profile, version):
    arch, floor, _ = PROFILES[profile]
    contents = app / "Contents"
    with (contents / "Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    for key in ("CFBundleShortVersionString", "CFBundleVersion"):
        if info.get(key) != version:
            raise ValueError(f"{key} must be {version}, got {info.get(key)!r}")
    if version_tuple(info.get("LSMinimumSystemVersion", "")) != version_tuple(floor):
        raise ValueError(f"LSMinimumSystemVersion must be {floor}")
    executable = info.get("CFBundleExecutable", "")
    if not executable or Path(executable).name != executable:
        raise ValueError("invalid bundle executable name")
    main = contents / "MacOS" / executable
    if not main.is_file() or not os.access(main, os.X_OK):
        raise ValueError("missing bundle executable: " + str(main))
    if not is_macho(main):
        raise ValueError("bundle executable is not Mach-O: " + str(main))
    count = 0
    for path in sorted(app.rglob("*")):
        if path.is_symlink():
            resolved = path.resolve(strict=True)
            if app not in resolved.parents:
                raise ValueError("symlink escapes app bundle: " + str(path))
        elif path.is_file() and is_macho(path):
            check_binary(path, arch, floor)
            count += 1
    print(f"PASS {profile}: {count} thin {arch} Mach-O files; macOS <= {floor}; {version}")


def package(app, profile, version, output):
    name = f"EiskaltDCpp-{version}-{PROFILES[profile][2]}.zip"
    archive = output / name
    checksum = output / (name + ".sha256")
    if archive.exists() or archive.is_symlink() or checksum.exists() or checksum.is_symlink():
        raise ValueError("release asset already exists: " + str(archive))
    check(app, profile, version)
    notices = app / "Contents/Resources/ThirdPartyNotices"
    if not notices.is_dir() or not any(p.is_file() for p in notices.rglob("*")):
        raise ValueError("ThirdPartyNotices must be staged before packaging")
    run("codesign", "--verify", "--deep", "--strict", str(app))
    audit = Path(__file__).resolve().parent / "check-bundle-external-links.sh"
    run("/bin/sh", str(audit), str(app))
    output.mkdir(parents=True, exist_ok=True)
    # Build and validate in the destination filesystem, then publish without clobbering.
    with tempfile.TemporaryDirectory(prefix=".eiskalt-package-", dir=output) as temp:
        temp = Path(temp)
        packed = temp / name
        run("ditto", "-c", "-k", "--keepParent", "--norsrc", "--noextattr",
            str(app), str(packed))
        with zipfile.ZipFile(packed) as zipped:
            if zipped.testzip() is not None:
                raise ValueError("archive CRC validation failed")
        unpacked = temp / "unpacked"
        run("ditto", "-x", "-k", str(packed), str(unpacked))
        restored = unpacked / app.name
        check(restored, profile, version)
        run("codesign", "--verify", "--deep", "--strict", str(restored))
        run("/bin/sh", str(audit), str(restored))
        digest = hashlib.sha256()
        with packed.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        sums = temp / "checksum"
        sums.write_text(digest.hexdigest() + "  " + name + "\n", encoding="ascii")
        os.link(packed, archive)
        try:
            os.link(sums, checksum)
        except OSError:
            archive.unlink()
            raise
    print("Packaged " + str(archive))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check", "package"))
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", args.version):
        parser.error("version must be a safe release version such as 3.0.0-pre.1")
    app = args.app.resolve(strict=True)
    if args.command == "check":
        check(app, args.profile, args.version)
    else:
        if args.output is None:
            parser.error("package requires --output")
        output = args.output.resolve()
        if output == app or app in output.parents:
            parser.error("output must be outside the app bundle")
        package(app, args.profile, args.version, output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError,
            plistlib.InvalidFileException, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.stderr, file=sys.stderr)
        sys.exit(1)
