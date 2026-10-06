#!/usr/bin/env python3
"""Check the approved artwork and platform icon containers without launching apps."""
import hashlib
import fnmatch
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]
SIZES = (16, 22, 24, 32, 48, 64, 128, 256, 512, 1024)
THEMES = ("default", "apex", "faenza", "haiku", "monochrome")
MASTER_HASH = "680af070714219501476f20b22e9c39ed4e2c840aec2b41237ab109e228bc442"


def png_size(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    return struct.unpack(">II", data[16:24])


class AppIcon(unittest.TestCase):
    def test_mingw_icon_and_rc_changes_rebuild_resource(self):
        source = (ROOT / "eiskaltdcpp-qt/CMakeLists.txt").read_text()
        command = re.search(r'    add_custom_command \(OUTPUT.*?(?=    set \(WIN_RESOURCES)',
                            source, re.S).group()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            qt = root / "eiskaltdcpp-qt"
            qt.mkdir()
            icon = root / "data/icons/eiskaltdcpp.ico"
            icon.parent.mkdir(parents=True)
            icon.write_bytes(b"icon one")
            rc = qt / "eiskaltdcpp-qt.rc"
            rc.write_bytes((ROOT / "eiskaltdcpp-qt/eiskaltdcpp-qt.rc").read_bytes())
            compiler = root / "resource-compiler.py"
            # Record the command's inputs, not a native Windows compiler result.
            compiler.write_text(
                "import pathlib,sys\n"
                "args=sys.argv[1:]\n"
                "rc=pathlib.Path(args[args.index('-i')+1])\n"
                "out=pathlib.Path(args[args.index('-o')+1])\n"
                "out.write_bytes(rc.read_bytes()+"
                "(rc.parent/'../data/icons/eiskaltdcpp.ico').read_bytes())\n")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\nproject(icon_graph NONE)\n"
                f'set(PROJECT_SOURCE_DIR "{qt}")\n'
                f'set(CMAKE_RC_COMPILER "{sys.executable}" "{compiler}")\n'
                + command + '\nadd_custom_target(icon ALL DEPENDS "${PROJECT_BINARY_DIR}/eiskaltdcpp-qt_rc.o")\n')
            build = root / "build"
            subprocess.run(["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja"],
                           check=True, capture_output=True)
            def rebuild():
                subprocess.run(["cmake", "--build", str(build)], check=True, capture_output=True)
                self.assertEqual((build / "eiskaltdcpp-qt_rc.o").read_bytes(),
                                 rc.read_bytes() + icon.read_bytes())
            rebuild()
            time.sleep(0.02)
            icon.write_bytes(b"icon two")
            rebuild()
            time.sleep(0.02)
            rc.write_bytes(rc.read_bytes() + b"\n// changed resource script\n")
            rebuild()

    def test_generated_assets_have_specific_provenance_entries(self):
        paragraphs = (ROOT / "full.copyrights.info.in.Debian.style").read_text().split("\n\n")
        records = []
        for paragraph in paragraphs:
            fields = {}
            current = None
            for line in paragraph.splitlines():
                if line.startswith(" ") and current:
                    fields[current] += " " + line.strip()
                elif ":" in line:
                    current, value = line.split(":", 1)
                    fields[current] = value.strip()
            if "Files" in fields:
                records.append(fields)
        def metadata(path):
            return [r for r in records if any(fnmatch.fnmatchcase(path, p)
                                              for p in r["Files"].split())][-1]
        manifest = json.loads((ROOT / "data/icons/modern-classic-manifest.json").read_text())
        for path in manifest["sha256"]:
            with self.subTest(asset=path):
                entry = metadata(path)
                self.assertEqual(entry["License"], "LGPL-3")
                self.assertIn("Modern Classic", entry.get("Comment", ""))
                self.assertIn("OpenAI image_gen", entry["Comment"])
        # Specific brand entries must not relabel other legacy theme artwork.
        for theme, license in (("faenza", "GPL-3"), ("haiku", "Expat")):
            self.assertEqual(metadata(f"eiskaltdcpp-qt/icons/appl/{theme}/icon_user.png")["License"], license)

    def test_nonapple_installs_retain_artwork_notices(self):
        # Exercise actual install rules with platform paths on the current host.
        for platform, docs in (("linux", "share/eiskaltdcpp/docs"), ("windows", "docs")):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                build = root / "build"
                destination = root / "install"
                (root / "CMakeLists.txt").write_text(
                    "cmake_minimum_required(VERSION 3.20)\nproject(icon_install NONE)\n"
                    "set(USE_QT6 ON)\nset(APPLE FALSE)\nset(HAIKU FALSE)\n"
                    f'set(UNIX {"TRUE" if platform == "linux" else "FALSE"})\n'
                    f'set(WIN32 {"TRUE" if platform == "windows" else "FALSE"})\n'
                    f'set(CLIENT_DOCS_DIR "{docs}")\nset(SHARE_DIR "share")\n'
                    'set(CLIENT_ICONS_DEST "icons")\n'
                    f'add_subdirectory("{ROOT / "data"}" data)\n')
                subprocess.run(["cmake", "-S", str(root), "-B", str(build)],
                               check=True, capture_output=True)
                subprocess.run(["cmake", "--install", str(build), "--prefix", str(destination)],
                               check=True, capture_output=True)
                for name in ("READ_ME.txt", "modern-classic-manifest.json"):
                    actual = destination / docs / "AppIcon" / name
                    self.assertTrue(actual.is_file(), str(actual))
                    self.assertEqual(actual.read_bytes(), (ROOT / "data/icons" / name).read_bytes())

    def test_approved_master_is_exact(self):
        master = ROOT / "data/icons/modern-classic-master.png"
        self.assertTrue(master.is_file(), "approved master has not been integrated")
        data = master.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), MASTER_HASH)
        self.assertEqual(png_size(data), (1254, 1254))

    def test_manifest_pins_all_consumed_assets(self):
        self.assertTrue((ROOT / "data/icons/modern-classic-manifest.json").is_file())
        manifest = json.loads((ROOT / "data/icons/modern-classic-manifest.json").read_text())
        expected = {f"data/icons/icon_{s}x{s}.png" for s in SIZES}
        expected.update({"data/icons/modern-classic-master.png",
                         "data/icons/eiskaltdcpp-borderless.icns", "data/icons/eiskaltdcpp.ico"})
        expected.update({f"eiskaltdcpp-qt/icons/appl/{theme}/{name}.png"
                         for theme in THEMES
                         for name in ("icon_appl", "icon_appl_big")})
        self.assertEqual(set(manifest["sha256"]), expected)
        for name, digest in manifest["sha256"].items():
            with self.subTest(asset=name):
                self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), digest)

    def test_png_sizes_and_transparency(self):
        for size in SIZES:
            data = (ROOT / f"data/icons/icon_{size}x{size}.png").read_bytes()
            with self.subTest(size=size):
                self.assertEqual(png_size(data), (size, size))
                self.assertEqual(data[25], 6, "RGBA PNG required")
        for theme in THEMES:
            for name, size in (("icon_appl", 32), ("icon_appl_big", 128)):
                self.assertEqual((ROOT / f"eiskaltdcpp-qt/icons/appl/{theme}/{name}.png").read_bytes(),
                                 (ROOT / f"data/icons/icon_{size}x{size}.png").read_bytes())

    def test_windows_container_has_all_png_resolutions(self):
        data = (ROOT / "data/icons/eiskaltdcpp.ico").read_bytes()
        self.assertEqual(struct.unpack("<HHH", data[:6]), (0, 1, 7))
        offset = 6 + 7 * 16
        for index, side in enumerate((16, 24, 32, 48, 64, 128, 256)):
            width, height, colors, reserved, planes, bits, length, start = struct.unpack_from(
                "<BBBBHHII", data, 6 + 16 * index)
            self.assertEqual((width or 256, height or 256, colors, reserved, planes, bits, start),
                             (side, side, 0, 0, 1, 32, offset))
            image = data[start:start + length]
            self.assertEqual(png_size(image), (side, side))
            self.assertEqual(image, (ROOT / f"data/icons/icon_{side}x{side}.png").read_bytes())
            offset += length
        self.assertEqual(offset, len(data))

    def test_bundle_retains_artwork_attribution_and_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "AppIcon"
            result = subprocess.run(["cmake", "-DEISKALT_APP_ICON_SOURCE_DIR=" + str(ROOT / "data/icons"),
                                     "-DEISKALT_APP_ICON_NOTICE_DIR=" + str(destination), "-P",
                                     str(ROOT / "cmake/AppIconNotices.cmake")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in ("READ_ME.txt", "modern-classic-manifest.json"):
                self.assertEqual((destination / name).read_bytes(), (ROOT / "data/icons" / name).read_bytes())

    def test_unapproved_artwork_cannot_write_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "unapproved.png"
            original = (ROOT / "data/icons/modern-classic-master.png").read_bytes()
            source.write_bytes(original + b"unapproved")
            output = Path(directory) / "outputs"
            result = subprocess.run([sys.executable, str(ROOT / "tools/app-icon/generate.py"),
                                     "--source", str(source), "--output", str(output)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("source is not the maintainer-approved variant 1", result.stderr)
            self.assertFalse(output.exists())
            self.assertEqual((ROOT / "data/icons/modern-classic-master.png").read_bytes(), original)

    @unittest.skipUnless(sys.platform == "darwin", "Apple iconutil is required")
    def test_macos_container_decodes_all_standard_and_retina_sizes(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "decoded.iconset"
            subprocess.run(["/usr/bin/iconutil", "-c", "iconset", "-o", str(destination),
                            str(ROOT / "data/icons/eiskaltdcpp-borderless.icns")], check=True)
            names = {}
            for side in (16, 32, 128, 256, 512):
                names[f"icon_{side}x{side}.png"] = side
                names[f"icon_{side}x{side}@2x.png"] = side * 2
            self.assertEqual({p.name for p in destination.iterdir()}, set(names))
            for name, side in names.items():
                self.assertEqual(png_size((destination / name).read_bytes()), (side, side))

    @unittest.skipUnless(sys.platform == "darwin", "Apple icon tools are required")
    def test_assets_are_reproducible_with_current_tools(self):
        self.assertTrue((ROOT / "tools/app-icon/generate.py").is_file())
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([sys.executable, str(ROOT / "tools/app-icon/generate.py"),
                            "--output", directory], check=True)
            manifest = json.loads((ROOT / "data/icons/modern-classic-manifest.json").read_text())
            for name in manifest["sha256"]:
                with self.subTest(asset=name):
                    self.assertEqual((ROOT / name).read_bytes(), (Path(directory) / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
