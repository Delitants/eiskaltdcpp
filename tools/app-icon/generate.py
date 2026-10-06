#!/usr/bin/env python3
"""Package the approved Modern Classic artwork; never touch an installed app."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MASTER_HASH = "680af070714219501476f20b22e9c39ed4e2c840aec2b41237ab109e228bc442"
SIZES = (16, 22, 24, 32, 48, 64, 128, 256, 512, 1024)
THEMES = ("default", "apex", "faenza", "haiku", "monochrome")


def generate(source, output):
    if hashlib.sha256(source.read_bytes()).hexdigest() != MASTER_HASH:
        raise ValueError("source is not the maintainer-approved variant 1")
    with tempfile.TemporaryDirectory(prefix="eiskalt-app-icon-") as directory:
        stage = Path(directory)
        icons = stage / "data/icons"
        icons.mkdir(parents=True)
        shutil.copyfile(source, icons / "modern-classic-master.png")
        for side in SIZES:
            subprocess.run(["/usr/bin/sips", "-z", str(side), str(side), str(source),
                            "--out", str(icons / f"icon_{side}x{side}.png")],
                           check=True, stdout=subprocess.DEVNULL)

        iconset = stage / "modern-classic.iconset"
        iconset.mkdir()
        for side in (16, 32, 128, 256, 512):
            for scale, suffix in ((1, ""), (2, "@2x")):
                physical = side * scale
                shutil.copyfile(icons / f"icon_{physical}x{physical}.png",
                                iconset / f"icon_{side}x{side}{suffix}.png")
        subprocess.run(["/usr/bin/iconutil", "-c", "icns", "-o",
                        str(icons / "eiskaltdcpp-borderless.icns"), str(iconset)], check=True)
        shutil.rmtree(iconset)

        # Modern Windows accepts PNG payloads in ICO, preserving the alpha channel.
        windows_sizes = (16, 24, 32, 48, 64, 128, 256)
        header = bytearray(struct.pack("<HHH", 0, 1, len(windows_sizes)))
        payload = bytearray()
        start = 6 + 16 * len(windows_sizes)
        for side in windows_sizes:
            png = (icons / f"icon_{side}x{side}.png").read_bytes()
            dimension = side if side < 256 else 0
            header.extend(struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32,
                                      len(png), start + len(payload)))
            payload.extend(png)
        (icons / "eiskaltdcpp.ico").write_bytes(header + payload)

        for theme in THEMES:
            destination = stage / f"eiskaltdcpp-qt/icons/appl/{theme}"
            destination.mkdir(parents=True)
            for name, side in (("icon_appl", 32), ("icon_appl_big", 128)):
                shutil.copyfile(icons / f"icon_{side}x{side}.png", destination / f"{name}.png")

        manifest = {
            "variant": "Modern Classic (variant 1)",
            "approved": "2026-10-06",
            "reference": "data/icons/eiskaltdcpp-borderless.icns at 31cc27e83",
            "generation": "OpenAI image_gen; generated 2026-10-05; selected by maintainer",
            "packaging": "sips resizing; iconutil ICNS; PNG-in-ICO; no retouching",
            "sha256": {path.relative_to(stage).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                       for path in sorted(stage.rglob("*")) if path.is_file()},
        }
        (icons / "modern-classic-manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="ascii")
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                destination = output / path.relative_to(stage)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "data/icons/modern-classic-master.png")
    parser.add_argument("--output", type=Path, required=True, help="repository or disposable staging root")
    arguments = parser.parse_args()
    generate(arguments.source, arguments.output)
