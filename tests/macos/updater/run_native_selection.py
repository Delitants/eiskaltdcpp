#!/usr/bin/env python3
"""Reuse native-enabled app objects; run selection in an isolated signed copy."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def deploy_probe_copy(build, app, cache):
    # Relinking app objects restores build-machine Qt/Sparkle install names.
    # Replay the production deployment recipe on the disposable copy only;
    # otherwise its bundled Cocoa plugin can load a second copy of Qt.
    deploy = Path(cache.get("MACDEPLOYQT_EXECUTABLE", ""))
    if not deploy.is_absolute() or not deploy.is_file():
        raise RuntimeError("Probe requires the build's recorded MACDEPLOYQT_EXECUTABLE")
    library_paths = [str(app / "Contents/Frameworks")]
    library_paths.extend(filter(None, cache.get("EISKALT_MACDEPLOYQT_LIBRARY_PATHS", "").split(";")))
    if cache.get("EISKALT_MACOS_DEPS_PREFIX"):
        library_paths.append(str(Path(cache["EISKALT_MACOS_DEPS_PREFIX"]) / "lib"))
    subprocess.run(["cmake", "-DMACDEPLOYQT=" + str(deploy), "-DAPP_BUNDLE=" + str(app),
                    "-DLIBRARY_PATHS=" + ";".join(library_paths),
                    "-P", str(ROOT / "eiskaltdcpp-qt/cmake/deploy_macos.cmake")],
                   cwd=build, check=True)
    subprocess.run(["/bin/sh", str(ROOT / "macos/normalize-bundle-dylib-ids.sh"), str(app)], check=True)
    # Fail closed before signing/execution if any build-machine dependency or
    # search path remains (including an escaped symlink or mixed CPU image).
    subprocess.run([sys.executable, str(ROOT / "macos/audit-bundle-closure.py"), str(app)], check=True)


def run(build, expect_installation="disabled"):
    build = build.resolve()
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if not line.startswith(("//", "#")) and "=" in line and ":" in line.split("=", 1)[0]:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    if cache.get("FORCE_XDG") != "ON":
        raise RuntimeError("Selection QA requires a FORCE_XDG build")
    objects = build / "eiskaltdcpp-qt/CMakeFiles/eiskaltdcpp-qt.dir"
    flags = {}
    for line in (objects / "flags.make").read_text().splitlines():
        if line.startswith("CXX_"):
            key, value = line.split(" = ", 1)
            flags[key] = shlex.split(value)
    if "-DEISKALT_MAC_UPDATER_NATIVE" not in flags["CXX_DEFINES"]:
        raise RuntimeError("Probe requires the actual native-enabled application build")
    source_app = build / "eiskaltdcpp-qt/EiskaltDC++.app"
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(source_app)], check=True)
    with tempfile.TemporaryDirectory(prefix="eiskalt-native-selection-", dir="/private/tmp") as temp:
        root = Path(temp)
        root.chmod(0o700)
        output = root / "probe.o"
        subprocess.run(["/usr/bin/c++", *flags["CXX_DEFINES"], *flags["CXX_INCLUDES"], *flags["CXX_FLAGS"],
                        "-I" + str(ROOT), "-I" + str(ROOT / "dcpp"),
                        "-c", str(ROOT / "tests/macos/updater/native_selection_probe.cpp"),
                        "-o", str(output)], check=True)
        app = root / "EiskaltDC++.app"
        shutil.copytree(source_app, app, symlinks=True)
        command = shlex.split((objects / "link.txt").read_text())
        main = "CMakeFiles/eiskaltdcpp-qt.dir/src/main.cpp.o"
        if command.count(main) != 1:
            raise RuntimeError("Unknown application link layout")
        command[command.index(main)] = str(output)
        command[command.index("-o") + 1] = str(app / "Contents/MacOS/EiskaltDC++")
        subprocess.run(command, cwd=build / "eiskaltdcpp-qt", check=True)
        deploy_probe_copy(build, app, cache)
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
        home = root / "home"
        home.mkdir(mode=0o700)
        environment = os.environ.copy()
        for name in ("HOME", "CFFIXED_USER_HOME"):
            environment[name] = str(home)
        for name, subdir in (("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                             ("XDG_CACHE_HOME", "cache"), ("TMPDIR", "tmp")):
            path = home / subdir
            path.mkdir(mode=0o700)
            environment[name] = str(path) + ("/" if name == "TMPDIR" else "")
        environment["QT_QPA_PLATFORM"] = "cocoa"
        policy = '(version 1)(allow default)(deny network*)'
        subprocess.run(["/usr/bin/sandbox-exec", "-p", policy,
                        str(app / "Contents/MacOS/EiskaltDC++"),
                        "--expect-installation=" + expect_installation], env=environment,
                       timeout=30, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--expect-installation", choices=("enabled", "disabled"), default="disabled",
                        help="Assert actual production capability; never grants admission")
    arguments = parser.parse_args()
    run(arguments.build, arguments.expect_installation)
