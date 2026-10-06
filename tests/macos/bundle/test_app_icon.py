#!/usr/bin/env python3
"""Check the approved artwork and platform icon containers without launching apps."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
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
