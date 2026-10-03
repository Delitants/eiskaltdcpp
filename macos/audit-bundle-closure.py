#!/usr/bin/env python3
"""Read-only, environment-independent load-command closure for a thin macOS app.

This is a conservative packaging gate, not a dyld emulator or native startup test.
Even non-system weak imports must be bundled. Runtime-generated dlopen names,
symbols, API compatibility and system shared-cache availability need native QA.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys

from release import is_macho

IMPORTS = {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
           "LC_LOAD_UPWARD_DYLIB", "LC_LAZY_LOAD_DYLIB"}


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True, timeout=30).stdout


def system_path(path):
    # Normalize before classification so /usr/lib/../../private is not exempted.
    # System frameworks may be redirected to a Cryptex by the OS. Their load
    # command remains a system path; do not follow host shared-cache symlinks.
    value = Path(os.path.normpath(path))
    return any(value == root or root in value.parents
               for root in (Path("/usr/lib"), Path("/System/Library")))


def inspect(path):
    arches = run("/usr/bin/lipo", "-archs", str(path)).split()
    if len(arches) != 1:
        raise ValueError(f"{path.name}: closure requires thin Mach-O images")
    header = run("/usr/bin/otool", "-hv", str(path))
    kind = re.search(r"\b(EXECUTE|DYLIB|BUNDLE)\b", header)
    if not kind:
        raise ValueError(f"{path.name}: unsupported Mach-O image type")
    dependencies, rpaths, install_id = [], [], None
    for block in re.split(r"Load command \d+", run("/usr/bin/otool", "-l", str(path)))[1:]:
        cmd = re.search(r"^\s*cmd (\S+)\s*$", block, re.M)
        if not cmd:
            raise ValueError(f"{path.name}: malformed load command")
        if cmd[1] in IMPORTS or cmd[1] in ("LC_RPATH", "LC_ID_DYLIB"):
            field = "path" if cmd[1] == "LC_RPATH" else "name"
            value = re.search(r"^\s*" + field + r" (.+) \(offset \d+\)\s*$", block, re.M)
            if not value:
                raise ValueError(f"{path.name}: malformed {cmd[1]}")
            if field == "path":
                rpaths.append(value[1])
            elif cmd[1] == "LC_ID_DYLIB":
                install_id = value[1]
            else:
                dependencies.append((cmd[1], value[1]))
    return {"architecture": arches[0], "kind": kind[1], "rpaths": rpaths, "install_id": install_id,
            "dependencies": dependencies, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def audit(app):
    app = app.resolve(strict=True)
    if not app.is_dir():
        raise ValueError("app bundle not found")
    images = {}
    for path in sorted(app.rglob("*")):
        if path.is_symlink():
            target = path.resolve(strict=True)
            if app not in target.parents:
                raise ValueError(f"symlink escapes app bundle: {path.relative_to(app)}")
        elif path.is_file() and is_macho(path):
            image = inspect(path)
            install_id = image["install_id"]
            if (install_id and install_id.startswith("/") and not system_path(install_id)
                    and app not in Path(install_id).resolve().parents):
                raise ValueError(f"{path.relative_to(app)}: external install ID: {install_id}")
            images[path] = image
    info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
    name = info.get("CFBundleExecutable")
    if not isinstance(name, str) or not name or Path(name).name != name:
        raise ValueError("invalid main executable name")
    main = (app / "Contents/MacOS" / name).resolve(strict=True)
    if main not in images or images[main]["kind"] != "EXECUTE":
        raise ValueError("main executable is not a Mach-O executable")
    if any(image["architecture"] != images[main]["architecture"] for image in images.values()):
        raise ValueError("mixed architecture bundle")

    def relative(path):
        return path.relative_to(app).as_posix()

    def expand(value, loader, executable, runpath=False):
        for token, base in (("@loader_path", loader.parent), ("@executable_path", executable.parent)):
            if value == token or value.startswith(token + "/"):
                candidate = base / value[len(token):].lstrip("/")
                break
        else:
            if not value.startswith("/"):
                raise ValueError(f"{relative(loader)}: unsupported relative path {value}")
            candidate = Path(value)
        target = Path(os.path.normpath(candidate)) if system_path(candidate) else candidate.resolve()
        if app not in target.parents and target != app and not system_path(target):
            reason = "external runpath" if runpath else "dependency escapes bundle"
            raise ValueError(f"{relative(loader)}: {reason}: {value}")
        # Fixed absolute paths inside the current app are not relocatable.
        if value.startswith("/") and not system_path(target):
            raise ValueError(f"{relative(loader)}: non-relocatable absolute path {value}")
        return target

    visited, reached, edges = set(), set(), []

    def walk(loader, executable, inherited=(), chain=()):
        if loader in chain:
            return
        own = tuple(expand(value, loader, executable, True) for value in images[loader]["rpaths"])
        rpaths = tuple(dict.fromkeys(own + inherited))
        key = (loader, executable, rpaths)
        if key in visited:
            return
        visited.add(key)
        reached.add(loader)
        if len(visited) > 10000 or len(chain) > 256:
            raise ValueError("dependency graph limit exceeded")
        for command, dependency in images[loader]["dependencies"]:
            if dependency.startswith("@rpath/"):
                candidates = [expand(str(base / dependency[7:]), loader, executable)
                              if system_path(base) else (base / dependency[7:]).resolve()
                              for base in rpaths]
                target = None
                for candidate in candidates:
                    if app not in candidate.parents and not system_path(candidate):
                        raise ValueError(f"{relative(loader)}: dependency escapes bundle: {dependency}")
                    if system_path(candidate) or candidate.exists():
                        target = candidate
                        break
                if target is None:
                    raise ValueError(f"{relative(loader)}: unresolved {dependency}")
            else:
                target = expand(dependency, loader, executable)
            is_system = system_path(target)
            if not is_system:
                if not target.is_file():
                    raise ValueError(f"{relative(loader)}: unresolved {dependency}")
                if target not in images or images[target]["kind"] != "DYLIB":
                    raise ValueError(f"{relative(loader)}: dependency is not a Mach-O dylib: {dependency}")
            edges.append({"image": relative(loader), "executable": relative(executable),
                          "command": command, "dependency": dependency,
                          "target": str(target) if is_system else relative(target), "system": is_system})
            if not is_system:
                walk(target, executable, rpaths, chain + (loader,))

    for path in images:
        if images[path]["kind"] == "EXECUTE":
            walk(path, path)
    # Dlopen plugins need their containing app's main runpaths. Unreached dylibs
    # are audited too, but reported as assumptions rather than runtime evidence.
    detached = []
    for path in images:
        if path not in reached:
            executable = main
            for parent in path.parents:
                if parent == app:
                    break
                plist = parent / "Contents/Info.plist"
                if parent.suffix in (".app", ".xpc") and plist.is_file():
                    nested = plistlib.loads(plist.read_bytes()).get("CFBundleExecutable", "")
                    candidate = (parent / "Contents/MacOS" / nested).resolve()
                    if candidate in images and images[candidate]["kind"] == "EXECUTE":
                        executable = candidate
                        break
            inherited = tuple(expand(value, executable, executable, True)
                              for value in images[executable]["rpaths"])
            detached.append({"image": relative(path), "executable": relative(executable)})
            walk(path, executable, inherited)
    return {"schema": 1, "images": {relative(p): image for p, image in images.items()},
            "edges": edges, "detached_roots": detached,
            "limitations": ["No runtime-generated dlopen names or symbol/API checks",
                            "Apple system paths assumed available via OS/shared cache",
                            "Detached images use containing executable runpaths; native QA required"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--json", action="store_true", help="Emit relative-path hashed dependency inventory")
    args = parser.parse_args()
    result = audit(args.app)
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"bundle dependency closure passed: {len(result['images'])} images, {len(result['edges'])} edges")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, plistlib.InvalidFileException) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
