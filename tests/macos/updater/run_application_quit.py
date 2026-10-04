#!/usr/bin/env python3
"""Reuse built GUI objects to test the actual entrypoint, never a live app."""
import argparse
import json
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def run(args):
    build = args.build.resolve()
    source = Path(__file__).resolve().parent / "application_quit_main.cpp"
    root = Path(tempfile.mkdtemp(prefix="eiskalt-app-quit-", dir="/private/tmp"))
    root.chmod(0o700)
    print("Disposable full-entrypoint root:", root, flush=True)
    subprocess.run(["cmake", "--build", str(build), "--target", "eiskaltdcpp-qt", "-j4"], check=True)
    objects = build / "eiskaltdcpp-qt/CMakeFiles/eiskaltdcpp-qt.dir"
    flags = {}
    for line in (objects / "flags.make").read_text().splitlines():
        if line.startswith("CXX_"):
            key, value = line.split(" = ", 1)
            flags[key] = shlex.split(value)
    cache = (build / "CMakeCache.txt").read_text()
    if "FORCE_XDG:BOOL=ON" not in cache:
        raise RuntimeError("full-entrypoint fixture requires the FORCE_XDG build")
    output = root / "entry.o"
    subprocess.run(["/usr/bin/c++", *flags["CXX_DEFINES"], *flags["CXX_INCLUDES"], *flags["CXX_FLAGS"],
                    "-DFORCE_XDG", "-c", str(source), "-o", str(output)], check=True)
    command = shlex.split((objects / "link.txt").read_text())
    main_object = "CMakeFiles/eiskaltdcpp-qt.dir/src/main.cpp.o"
    if command.count(main_object) != 1:
        raise RuntimeError("unknown production link layout")
    command[command.index(main_object)] = str(output)
    binary = root / "eiskalt-quit-entry"
    command[command.index("-o") + 1] = str(binary)
    subprocess.run(command, cwd=build / "eiskaltdcpp-qt", check=True)
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
                       XDG_CACHE_HOME=str(home / "cache"), TMPDIR=str(home / "tmp") + "/", QT_QPA_PLATFORM="cocoa")
    sandbox = '(version 1)(allow default)(deny network*)' \
              '(allow network-outbound (remote ip "localhost:*"))' \
              '(allow network-bind (local ip "localhost:*"))' \
              '(allow network-inbound (local ip "localhost:*"))'
    arguments = [str(binary)] + (["--quit-via-event"] if args.mode == "quit-event" else [])
    completed = subprocess.run(["/usr/bin/sandbox-exec", "-p", sandbox, *arguments],
                               env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, timeout=60)
    (root / "entry.log").write_text(completed.stdout)
    print(completed.stdout[-5000:], flush=True)
    if completed.returncode != 0 or "PRODUCTION_MAIN_RETURNED=0 CHECKED=1" not in completed.stdout:
        raise RuntimeError("production normal quit did not return through checked GUI/core teardown")
    if "IPC_HELD_AT_QUIT=1" not in completed.stdout or "IPC_HELD_DURING_GUI_TEARDOWN=1" not in completed.stdout:
        raise RuntimeError("profile owner was released before GUI/core teardown")
    if args.mode == "quit-event" and "QUIT_EVENT_DEFERRED=1" not in completed.stdout:
        raise RuntimeError("application Quit event was accepted before checked exit readiness")
    for path, data in protected.items():
        if path.read_bytes() != data:
            raise RuntimeError("protected fixture data changed")
    config = home / ".config/eiskaltdc++"
    for name in ("DCPlusPlus.xml", "Queue.xml", "HashIndex.xml"):
        ET.parse(config / name)
    hashes = ET.parse(config / "HashIndex.xml").getroot()
    entries = hashes.findall("./Files/File")
    if len(entries) != 1 or entries[0].get("Name") != str(home / "shares/untouched.bin"):
        raise RuntimeError("hashed file snapshot not preserved")
    tree = hashes.find("./Trees/Hash")
    if tree is None or tree.get("Root") != entries[0].get("Root") or tree.get("Size") != "13":
        raise RuntimeError("hash tree snapshot does not match the fixture file")
    torrent_state = json.loads((config / "torrent/state/jobs.json").read_text())
    if torrent_state.get("version") != 2 or torrent_state.get("jobs") != []:
        raise RuntimeError("Torrent checkpoint not persisted")
    if not (config / "EiskaltDC++_Qt.conf").is_file():
        raise RuntimeError("GUI settings not persisted")
    print("PASS: checked production main returned; valid persisted snapshots and unchanged data", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--mode", choices=("window", "quit-event"), default="window")
    run(parser.parse_args())
