#!/usr/bin/env python3
"""Offline framework staging tests against the actual pinned universal SDK."""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "macos/updater/stage-framework.py"
BINARIES = (
    "Versions/B/Sparkle", "Versions/B/Autoupdate",
    "Versions/B/Updater.app/Contents/MacOS/Updater",
    "Versions/B/XPCServices/Downloader.xpc/Contents/MacOS/Downloader",
    "Versions/B/XPCServices/Installer.xpc/Contents/MacOS/Installer",
)


class FrameworkStaging(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get("EISKALT_SPARKLE_ARCHIVE")
        source = os.environ.get("EISKALT_SPARKLE_SOURCE_ARCHIVE")
        if not sdk or not source:
            raise unittest.SkipTest("Provide pinned SDK and source archives for actual framework QA")
        cls.sdk, cls.source = Path(sdk), Path(source)
        cls.sdk_hash = hashlib.sha256(cls.sdk.read_bytes()).hexdigest()
        cls.source_hash = hashlib.sha256(cls.source.read_bytes()).hexdigest()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eiskalt-framework-stage-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "staged"

    def cli(self, *args):
        return subprocess.run([sys.executable, str(TOOL), "--sdk-archive", str(self.sdk),
                               "--source-archive", str(self.source), "--profile", "intel",
                               "--output", str(self.output), *map(str, args)],
                              capture_output=True, text=True)

    def test_both_flavors_have_thin_signed_helpers_and_exact_provenance(self):
        for profile, arch in (("intel", "x86_64"), ("apple-silicon", "arm64")):
            with self.subTest(profile=profile):
                output = self.root / profile
                result = self.cli("--profile", profile, "--output", output)
                self.assertEqual(result.returncode, 0, result.stderr)
                framework = output / "Contents/Frameworks/Sparkle.framework"
                for binary in BINARIES:
                    self.assertEqual(subprocess.check_output(["lipo", "-archs", str(framework / binary)],
                                                            text=True).strip(), arch)
                subprocess.run(["codesign", "--verify", "--deep", "--strict", str(framework)],
                               check=True, capture_output=True)
                for bundle, identifier in (
                    ("Versions/B/Updater.app", "org.sparkle-project.Sparkle.Updater"),
                    ("Versions/B/XPCServices/Downloader.xpc", "org.sparkle-project.DownloaderService"),
                    ("Versions/B/XPCServices/Installer.xpc", "org.sparkle-project.InstallerLauncher"),
                ):
                    display = subprocess.run(["codesign", "-d", "-v", str(framework / bundle)],
                                             check=True, capture_output=True, text=True)
                    self.assertIn("Identifier=" + identifier, display.stderr)
                self.assertEqual(os.readlink(framework / "Versions/Current"), "B")
                info = plistlib.loads((framework / "Resources/Info.plist").read_bytes())
                self.assertEqual(info["CFBundleShortVersionString"], "2.10.0")
                notices = output / "Contents/Resources/ThirdPartyNotices/Sparkle"
                license_hash = hashlib.sha256((notices / "LICENSE").read_bytes()).hexdigest()
                self.assertEqual(license_hash, "389a4e4e9a32f059775b13a06e25a591445ba229d2838d26dd3e7c0c45127cfe")
                provenance = json.loads((notices / "provenance.json").read_text())
                self.assertEqual(provenance["archive_sha256"], "c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c")
                self.assertEqual(provenance["source_sha256"], "fd2a5de329be3619111f9a73a1adeac9725a92292f0431c93679e5e2b0d48a04")
                self.assertEqual(provenance["commit"], "eef1a539a373c1f1a320624b1130fc5de7b2e100")
                self.assertEqual(provenance["architecture"], arch)
                self.assertEqual(provenance["signing_identity"], "-")
                self.assertEqual(set(provenance["binaries"]), set(BINARIES))
                for relative, digest in provenance["binaries"].items():
                    self.assertEqual(hashlib.sha256((framework / relative).read_bytes()).hexdigest(), digest)
                self.assertFalse((output / "bin").exists())
                self.assertFalse((output / "Sparkle Test App.app").exists())
        self.assertEqual(hashlib.sha256(self.sdk.read_bytes()).hexdigest(), self.sdk_hash)
        self.assertEqual(hashlib.sha256(self.source.read_bytes()).hexdigest(), self.source_hash)

    def test_signed_zip_roundtrip_preserves_framework_helpers_and_notices(self):
        result = self.cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        app = self.root / "StageFixture.app"
        shutil.copytree(self.output / "Contents", app / "Contents", symlinks=True)
        contents = app / "Contents"
        (contents / "MacOS").mkdir()
        source = self.root / "main.c"
        source.write_text("int main(void) { return 0; }\n")
        subprocess.run(["xcrun", "clang", "-arch", "x86_64", "-mmacosx-version-min=14.0",
                        str(source), "-o", str(contents / "MacOS/StageFixture")], check=True, capture_output=True)
        (contents / "Info.plist").write_bytes(plistlib.dumps({
            "CFBundleExecutable": "StageFixture", "CFBundleIdentifier": "org.eiskaltdcpp.stage-fixture",
            "CFBundleShortVersionString": "3.0.0-pre.1", "CFBundleVersion": "30000001",
            "LSMinimumSystemVersion": "14.0", "CFBundlePackageType": "APPL",
        }))
        # Nested code was signed explicitly by staging; seal only the outer app.
        subprocess.run(["codesign", "--force", "--sign", "-", str(app)], check=True, capture_output=True)
        packages = self.root / "packages"
        packaged = subprocess.run([sys.executable, str(ROOT / "macos/release.py"), "package",
                                   "--profile", "intel", "--version", "3.0.0-pre.1",
                                   "--build-number", "30000001", "--app", str(app), "--output", str(packages)],
                                  capture_output=True, text=True)
        self.assertEqual(packaged.returncode, 0, packaged.stderr)
        archive = packages / "EiskaltDCpp-3.0.0-pre.1-macOS14-plus-Intel-x86_64.zip"
        with zipfile.ZipFile(archive) as zipped:
            prefix = "StageFixture.app/Contents/Resources/ThirdPartyNotices/Sparkle/"
            self.assertEqual(zipped.read(prefix + "LICENSE"),
                             (contents / "Resources/ThirdPartyNotices/Sparkle/LICENSE").read_bytes())
            provenance = json.loads(zipped.read(prefix + "provenance.json"))
            for relative, digest in provenance["binaries"].items():
                self.assertEqual(hashlib.sha256(zipped.read("StageFixture.app/Contents/Frameworks/Sparkle.framework/" + relative)).hexdigest(), digest)
        unpacked = self.root / "unpacked"
        subprocess.run(["ditto", "-x", "-k", str(archive), str(unpacked)], check=True, capture_output=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(unpacked / app.name)],
                       check=True, capture_output=True)

    def test_tampered_sdk_is_rejected_before_output_creation(self):
        bad = self.root / "bad-sdk.tar.xz"
        bad.write_bytes(b"not the pinned SDK")
        result = self.cli("--sdk-archive", bad)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checksum", result.stderr.lower())
        self.assertFalse(self.output.exists())

    def test_tampered_source_is_rejected_before_output_creation(self):
        bad = self.root / "bad-source.tar.gz"
        bad.write_bytes(b"not the pinned source")
        result = self.cli("--source-archive", bad)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checksum", result.stderr.lower())
        self.assertFalse(self.output.exists())

    def test_existing_output_remains_untouched(self):
        self.output.mkdir()
        sentinel = self.output / "preserve"
        sentinel.write_bytes(b"existing destination")
        result = self.cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_bytes(), b"existing destination")

    def test_symlink_output_remains_untouched(self):
        outside = self.root / "preserve"
        outside.mkdir()
        self.output.symlink_to(outside)
        result = self.cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.output.is_symlink())
        self.assertEqual(list(outside.iterdir()), [])

    def test_failed_signing_never_promotes_partial_framework(self):
        result = self.cli("--identity", "org.eiskaltdcpp.nonexistent-qa-signing-identity")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("sign", result.stderr.lower())
        self.assertFalse(self.output.exists())
        self.assertFalse(any(p.name.startswith(".eiskalt-sparkle-") for p in self.root.iterdir()))


if __name__ == "__main__":
    unittest.main()
