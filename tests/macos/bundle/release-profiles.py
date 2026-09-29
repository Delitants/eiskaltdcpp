#!/usr/bin/env python3
"""Exercise release gates with actual thin/fat Mach-O fixtures, not tool mocks."""

import hashlib
import os
import plistlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(sys.argv.pop(1)).resolve()
TOOL = ROOT / "macos/release.py"


class ReleaseProfiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="eiskalt-release-tests-")
        cls.work = Path(cls.tmp.name)
        source = cls.work / "main.c"
        source.write_text("int main(void) { return 0; }\n")
        for arch, floor in (("x86_64", "14.0"), ("x86_64", "15.0"),
                            ("arm64", "26.0")):
            subprocess.run(["xcrun", "clang", "-arch", arch,
                            "-mmacosx-version-min=" + floor, str(source),
                            "-o", str(cls.work / (arch + "-" + floor))], check=True)
        subprocess.run(["lipo", "-create", str(cls.work / "x86_64-14.0"),
                        str(cls.work / "arm64-26.0"), "-output",
                        str(cls.work / "universal")], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(dir=self.work))
        self.app = self.case / "EiskaltDC++.app"
        self.contents = self.app / "Contents"
        (self.contents / "MacOS").mkdir(parents=True)
        self.plist = {"CFBundleExecutable": "EiskaltDC++",
                      "CFBundleIdentifier": "org.eiskaltdcpp.release-fixture",
                      "CFBundleVersion": "3.0.0-pre.1",
                      "CFBundleShortVersionString": "3.0.0-pre.1",
                      "LSMinimumSystemVersion": "14.0"}
        self.write_plist()
        shutil.copy2(self.work / "x86_64-14.0", self.contents / "MacOS/EiskaltDC++")

    def write_plist(self):
        (self.contents / "Info.plist").write_bytes(plistlib.dumps(self.plist))

    def invoke(self, command="check", profile="intel", *args):
        return subprocess.run([sys.executable, str(TOOL), command, "--profile", profile,
                               "--version", "3.0.0-pre.1", "--app", str(self.app),
                               *map(str, args)], capture_output=True, text=True)

    def rejected(self, message):
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(message, result.stderr)

    def test_accepts_matching_intel_bundle(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_accepts_matching_silicon_bundle(self):
        shutil.copy2(self.work / "arm64-26.0", self.contents / "MacOS/EiskaltDC++")
        self.plist["LSMinimumSystemVersion"] = "26.0"
        self.write_plist()
        result = self.invoke("check", "apple-silicon")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_universal_executable(self):
        shutil.copy2(self.work / "universal", self.contents / "MacOS/EiskaltDC++")
        self.rejected("architecture")

    def test_rejects_wrong_arch_non_executable_framework(self):
        target = self.contents / "Frameworks/Fixture.framework/Versions/A/Fixture"
        target.parent.mkdir(parents=True)
        shutil.copy2(self.work / "arm64-26.0", target)
        target.chmod(0o644)
        self.rejected("architecture")

    def test_rejects_newer_os_dependency_even_when_plist_claims_14(self):
        target = self.contents / "Frameworks/newer.dylib"
        target.parent.mkdir(parents=True)
        shutil.copy2(self.work / "x86_64-15.0", target)
        self.rejected("requires macOS 15")

    def test_rejects_missing_deployment_metadata(self):
        obj = self.contents / "MacOS/EiskaltDC++"
        stripped = self.case / "no-deployment"
        subprocess.run(["xcrun", "vtool", "-remove-build-version", "macos",
                        "-output", str(stripped), str(obj)], check=True)
        shutil.copyfile(stripped, obj)
        self.rejected("deployment")

    def test_rejects_missing_executable(self):
        (self.contents / "MacOS/EiskaltDC++").unlink()
        self.rejected("executable")

    def test_rejects_non_macho_executable(self):
        (self.contents / "MacOS/EiskaltDC++").write_text("#!/bin/sh\nexit 0\n")
        self.rejected("Mach-O")

    def test_rejects_incorrect_plist_floor(self):
        self.plist["LSMinimumSystemVersion"] = "13.0"
        self.write_plist()
        self.rejected("LSMinimumSystemVersion")

    def test_rejects_stale_version(self):
        self.plist["CFBundleShortVersionString"] = "2.5.6"
        self.write_plist()
        self.rejected("CFBundleShortVersionString")

    def test_rejects_symlink_outside_bundle(self):
        (self.contents / "escape").symlink_to(self.work / "x86_64-14.0")
        self.rejected("symlink")

    def test_rejects_wrong_arch_executable_symlinked_to_bundle_root(self):
        main = self.contents / "MacOS/EiskaltDC++"
        main.unlink()
        shutil.copy2(self.work / "arm64-26.0", self.app / "payload")
        main.symlink_to("../../payload")
        self.rejected("architecture")

    def test_rejects_escape_symlink_at_bundle_root(self):
        (self.app / "escape").symlink_to(self.work / "x86_64-14.0")
        self.rejected("symlink")

    def test_archives_explicit_intel_name_and_preserves_existing_asset(self):
        notices = self.contents / "Resources/ThirdPartyNotices"
        notices.mkdir(parents=True)
        (notices / "fixture.txt").write_text("Fixture notice\n")
        subprocess.run(["codesign", "--force", "--sign", "-", str(self.app)],
                       check=True, capture_output=True)
        output = self.case / "packages"
        result = self.invoke("package", "intel", "--output", output)
        self.assertEqual(result.returncode, 0, result.stderr)
        archive = output / "EiskaltDCpp-3.0.0-pre.1-macOS14-plus-Intel-x86_64.zip"
        self.assertTrue(archive.is_file())
        before = archive.read_bytes()
        digest = hashlib.sha256(before).hexdigest()
        self.assertEqual(archive.with_suffix(".zip.sha256").read_text(),
                         digest + "  " + archive.name + "\n")
        with zipfile.ZipFile(archive) as packed:
            self.assertIsNone(packed.testzip())
            self.assertEqual(packed.read("EiskaltDC++.app/Contents/MacOS/EiskaltDC++"),
                             (self.contents / "MacOS/EiskaltDC++").read_bytes())
        result = self.invoke("package", "intel", "--output", output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("already exists", result.stderr)
        self.assertEqual(archive.read_bytes(), before)

    def test_invalid_bundle_does_not_create_archive(self):
        shutil.copy2(self.work / "universal", self.contents / "MacOS/EiskaltDC++")
        output = self.case / "packages"
        result = self.invoke("package", "intel", "--output", output)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(list(output.glob("*.zip")))

    def test_release_presets_produce_matching_thin_bundles(self):
        source = self.case / "preset-source"
        source.mkdir()
        shutil.copyfile(ROOT / "CMakePresets.json", source / "CMakePresets.json")
        (source / "main.c").write_text("int main(void) { return 0; }\n")
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.23)
project(ReleaseFixture C)
set(MACOSX_BUNDLE_BUNDLE_VERSION "${REPLACE_VERSION}")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${REPLACE_VERSION}")
add_executable(EiskaltDC++ MACOSX_BUNDLE main.c)
set_target_properties(EiskaltDC++ PROPERTIES MACOSX_BUNDLE_INFO_PLIST
    "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
''')
        (source / "Info.plist.in").write_text('''<?xml version="1.0"?>
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>EiskaltDC++</string>
<key>CFBundleVersion</key><string>${REPLACE_VERSION}</string>
<key>CFBundleShortVersionString</key><string>${REPLACE_VERSION}</string>
<key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>
</dict></plist>
''')
        env = dict(os.environ, DEPS_PREFIX=str(self.case), QT_PREFIX=str(self.case),
                   COCOA_PLUGIN=str(self.case / "plugin.dylib"), COCOA_PLUGIN_SHA256="fixture")
        for profile in ("intel", "apple-silicon"):
            with self.subTest(profile=profile):
                build = self.case / ("build-" + profile)
                configured = subprocess.run(["cmake", "--preset", "macos-" + profile,
                                             "-B", str(build)], cwd=source, env=env,
                                            capture_output=True, text=True)
                self.assertEqual(configured.returncode, 0, configured.stderr)
                subprocess.run(["cmake", "--build", str(build), "-j2"],
                               check=True, capture_output=True)
                self.app = build / "EiskaltDC++.app"
                result = self.invoke("check", profile)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
