#!/usr/bin/env python3
"""Read-only bundle validation plus an isolated native extension-ABI probe."""

import argparse
import importlib.util
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("eiskalt_macos_release", ROOT / "macos/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
ABI_SYMBOL = "__ZN10libtorrent28eiskalt_selected_recheck_abiEv"
OPERATIONS = ("prepare_selected_recheck", "start_selected_recheck",
              "cancel_selected_recheck", "finish_selected_recheck",
              "query_selected_recheck")


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True,
                          text=True, timeout=60, **kwargs).stdout


def inside(path, app):
    return path == app or app in path.parents


def system(path):
    path = Path(os.path.normpath(path))
    return any(path == root or root in path.parents
               for root in (Path("/System/Library"), Path("/usr/lib")))


def expand(path, owner, main, app, kind):
    if path == "@loader_path" or path.startswith("@loader_path/"):
        value = owner.parent / path[len("@loader_path"):].lstrip("/")
    elif path == "@executable_path" or path.startswith("@executable_path/"):
        value = main.parent / path[len("@executable_path"):].lstrip("/")
    elif Path(path).is_absolute() and system(Path(path)):
        return Path(path)
    else:
        raise ValueError(f"{owner}: external or unsupported {kind}: {path}")
    value = value.resolve()
    if not inside(value, app):
        raise ValueError(f"{owner}: {kind} escapes bundle: {path}")
    return value


def commands(binary):
    loads, rpaths, identity = [], [], None
    for block in re.split(r"Load command [0-9]+", run("otool", "-l", binary)):
        cmd = re.search(r"^\s*cmd\s+(\S+)", block, re.M)
        value = re.search(r"^\s*(?:name|path)\s+(.+?)\s+\(offset [0-9]+\)", block, re.M)
        if not cmd or not value:
            continue
        if cmd[1] == "LC_RPATH":
            rpaths.append(value[1])
        elif cmd[1] == "LC_ID_DYLIB":
            identity = value[1]
        elif cmd[1] in {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
                        "LC_LOAD_UPWARD_DYLIB", "LC_LAZY_LOAD_DYLIB"}:
            loads.append(value[1])
    return loads, rpaths, identity


def resolve_dependency(dep, owner, main, app, rpaths):
    if Path(dep).is_absolute():
        if system(Path(dep)):
            return None  # System libraries may exist only in the dyld shared cache.
        raise ValueError(f"{owner}: external dependency: {dep}")
    if dep.startswith("@rpath/"):
        candidates = [root / dep[len("@rpath/"):] for root in rpaths]
    else:
        candidates = [expand(dep, owner, main, app, "dependency")]
    for candidate in candidates:
        if system(candidate):
            return None
        resolved = candidate.resolve()
        if not inside(resolved, app):
            raise ValueError(f"{owner}: dependency escapes bundle: {dep}")
        if resolved.is_file():
            if not release.is_macho(resolved):
                raise ValueError(f"{owner}: dependency is not Mach-O: {dep}")
            return resolved
    raise ValueError(f"{owner}: unresolved dependency: {dep}")


def check(app, arch, floor, static_only):
    app = app.resolve(strict=True)
    contents = app / "Contents"
    info_path = contents / "Info.plist"
    if not inside(info_path.resolve(strict=True), app):
        raise ValueError("Info.plist symlink escapes bundle")
    with info_path.open("rb") as stream:
        info = plistlib.load(stream)
    if release.version_tuple(info.get("LSMinimumSystemVersion", "")) != release.version_tuple(floor):
        raise ValueError(f"LSMinimumSystemVersion must be {floor}")
    name = info.get("CFBundleExecutable", "")
    if not name or Path(name).name != name:
        raise ValueError("invalid bundle executable name")
    main = (contents / "MacOS" / name).resolve(strict=True)
    if not inside(main, app) or not main.is_file() or not os.access(main, os.X_OK):
        raise ValueError("missing or unsafe bundle executable")
    if not release.is_macho(main):
        raise ValueError("bundle executable is not Mach-O")
    binaries = set()
    for path in contents.rglob("*"):
        if path.is_symlink() and not inside(path.resolve(strict=True), app):
            raise ValueError("symlink escapes bundle: " + str(path))
        if path.is_file() and release.is_macho(path):
            binaries.add(path.resolve(strict=True))
    metadata = {}
    for binary in sorted(binaries):
        release.check_binary(binary, arch, floor)
        loads, paths, identity = commands(binary)
        if identity and identity.startswith(("@loader_path/", "@executable_path/")):
            expand(identity, binary, main, app, "library identity")
        elif identity and not identity.startswith("@rpath/"):
            raise ValueError(f"{binary}: external or nonrelocatable library identity: {identity}")
        metadata[binary] = (loads, tuple(expand(path, binary, main, app, "runpath") for path in paths))
    # Retain dyld's inherited runpaths, but validate every binary, including orphans.
    main_paths = metadata[main][1]
    queue = [(main, main_paths)] + [(binary, main_paths) for binary in sorted(binaries) if binary != main]
    visited = set()
    adjacency = {binary: set() for binary in binaries}
    while queue:
        binary, inherited = queue.pop()
        own_paths = metadata[binary][1]
        paths = tuple(dict.fromkeys((*own_paths, *inherited)))
        key = (binary, paths)
        if key in visited:
            continue
        visited.add(key)
        if len(visited) > 10000:
            raise ValueError("bundle dependency/runpath graph exceeds audit limit")
        for dep in metadata[binary][0]:
            resolved = resolve_dependency(dep, binary, main, app, paths)
            if resolved is not None:
                if resolved not in metadata:
                    raise ValueError("dependency is outside audited bundle binaries: " + str(resolved))
                adjacency[binary].add(resolved)
                queue.append((resolved, paths))
    reachable, queue = set(), [main]
    while queue:
        binary = queue.pop()
        if binary not in reachable:
            reachable.add(binary)
            queue.extend(adjacency[binary])
    libraries = [p for p in binaries if p.name.startswith("libtorrent-rasterbar")]
    if len(libraries) != 1:
        raise ValueError("bundle must contain exactly one libtorrent extension library")
    library = libraries[0]
    exports = set(run("nm", "-g", "-U", "-j", library).splitlines())
    if ABI_SYMBOL not in exports:
        raise ValueError("bundled library has no selected-recheck extension ABI marker")
    imports = set(run("nm", "-u", "-j", main).splitlines())
    if ABI_SYMBOL not in imports or library not in reachable:
        raise ValueError("application has no reachable selected-recheck capability reference")
    for operation in OPERATIONS:
        used = {symbol for symbol in imports if re.fullmatch(
            r"__ZN10libtorrent[0-9]+" + operation + r".+", symbol)}
        if not used or not used.issubset(exports):
            raise ValueError(f"extension symbol/reference mismatch: {operation}")
    if static_only:
        print(f"STATIC PASS: {len(binaries)} thin {arch} binaries, macOS <= {floor}; "
              "runtime ABI remains PENDING; not release acceptance")
        return
    translated = subprocess.run(["sysctl", "-in", "sysctl.proc_translated"],
                                capture_output=True, text=True).stdout.strip()
    if arch != os.uname().machine or translated == "1":
        raise ValueError("runtime ABI requires a matching native host; --static-only is not release acceptance")
    # Execute only a tiny ABI caller, never the application or a Torrent session.
    with tempfile.TemporaryDirectory(prefix="eiskalt-bundle-abi-") as work:
        work = Path(work)
        staged = work / app.name
        shutil.copytree(app, staged, symlinks=True)
        source, probe = work / "abi.cpp", staged / main.relative_to(app)
        source.write_text("#include <cstdio>\nnamespace libtorrent { int eiskalt_selected_recheck_abi(); }\n"
                          "int main() { std::printf(\"%d\\n\", libtorrent::eiskalt_selected_recheck_abi()); }\n")
        args = ["xcrun", "clang++", "-arch", arch, "-mmacosx-version-min=" + floor,
                source, staged / library.relative_to(app), "-o", probe]
        probe_paths = set(main_paths) | set(metadata[library][1]) | {library.parent}
        for path in sorted(probe_paths):
            staged_path = staged / path.relative_to(app) if inside(path, app) else path
            args.extend(("-Xlinker", "-rpath", "-Xlinker", str(staged_path)))
        run(*args)
        env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
        value = run(probe, env=env).strip()
        if value != "2":
            raise ValueError("bundled extension runtime ABI must be 2, got " + value)
    print(f"PASS: selected-recheck ABI 2; {len(binaries)} thin {arch} binaries, macOS <= {floor}; "
          "native minimum-OS acceptance remains a separate gate")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("architecture", choices=("arm64", "x86_64"))
    parser.add_argument("deployment_floor")
    parser.add_argument("--static-only", action="store_true",
                        help="Inspect cross-architecture bundles without claiming runtime ABI acceptance")
    args = parser.parse_args()
    release.version_tuple(args.deployment_floor)
    check(args.bundle, args.architecture, args.deployment_floor, args.static_only)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError, plistlib.InvalidFileException) as error:
        print(f"error: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.stderr, file=sys.stderr)
        sys.exit(1)
