#!/usr/bin/env python3
"""Exercise release gates with actual thin/fat Mach-O fixtures, not tool mocks."""

import hashlib
import importlib.util
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
                      "CFBundleVersion": "30000001",
                      "CFBundleShortVersionString": "3.0.0-pre.1",
                      "LSMinimumSystemVersion": "14.0"}
        self.write_plist()
        shutil.copy2(self.work / "x86_64-14.0", self.contents / "MacOS/EiskaltDC++")

    def write_plist(self):
        (self.contents / "Info.plist").write_bytes(plistlib.dumps(self.plist))

    def invoke(self, command="check", profile="intel", *args):
        return subprocess.run([sys.executable, str(TOOL), command, "--profile", profile,
                               "--version", "3.0.0-pre.1", "--build-number", "30000001", "--app", str(self.app),
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

    def test_rejects_stale_numeric_bundle_build(self):
        self.plist["CFBundleVersion"] = "30000000"
        self.write_plist()
        self.rejected("CFBundleVersion")

    def test_rejects_display_version_used_as_build_number(self):
        self.plist["CFBundleVersion"] = "3.0.0-pre.1"
        self.write_plist()
        self.rejected("CFBundleVersion")

    def test_rejects_nonstring_or_noncanonical_bundle_build(self):
        for value in (30000001, True, "030000001", "30000001.0", "30000001 "):
            with self.subTest(value=value):
                self.plist["CFBundleVersion"] = value
                self.write_plist()
                self.rejected("CFBundleVersion")

    def test_build_number_argument_is_required(self):
        result = subprocess.run([sys.executable, str(TOOL), "check", "--profile", "intel",
                                 "--version", "3.0.0-pre.1", "--app", str(self.app)],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--build-number", result.stderr)

    def test_rejects_invalid_expected_build_before_packaging(self):
        output = self.case / "packages"
        for value in ("0", "-1", "01", "1.0", "1e3", " 1", "+1", "", "9223372036854775808"):
            with self.subTest(value=value):
                result = self.invoke("package", "intel", "--output", output, "--build-number", value)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("build number", result.stderr)
                self.assertFalse(output.exists())

    def test_accepts_maximum_signed_feed_build_number(self):
        self.plist["CFBundleVersion"] = "9223372036854775807"
        self.write_plist()
        result = self.invoke("check", "intel", "--build-number", "9223372036854775807")
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

    def test_check_api_accepts_parent_alias_with_confined_framework_links(self):
        # The packaging consumer calls check() directly, unlike the release CLI
        # which already canonicalizes its app argument.
        (self.contents / "value").write_text("confined resource")
        (self.contents / "internal-link").symlink_to("value")
        alias = self.case / "alias"
        alias.symlink_to(self.case, target_is_directory=True)
        spec = importlib.util.spec_from_file_location("release_tool", TOOL)
        release = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(release)
        release.check(alias / self.app.name, "intel", "3.0.0-pre.1", "30000001")

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
            info = plistlib.loads(packed.read("EiskaltDC++.app/Contents/Info.plist"))
            self.assertEqual(info["CFBundleVersion"], "30000001")
            self.assertEqual(info["CFBundleShortVersionString"], "3.0.0-pre.1")
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
if(NOT EISKALT_REQUIRE_SELECTED_RECHECK)
    message(FATAL_ERROR "Release profiles must require the selected-recheck dependency")
endif()
set(APP_VERSION "${REPLACE_VERSION}")
set(MACOSX_BUNDLE_BUNDLE_VERSION "${APP_VERSION}")
include("${REPO}/cmake/MacBundleVersion.cmake")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${REPLACE_VERSION}")
add_executable(EiskaltDC++ MACOSX_BUNDLE main.c)
set_target_properties(EiskaltDC++ PROPERTIES MACOSX_BUNDLE_INFO_PLIST
    "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
''')
        (source / "Info.plist.in").write_text('''<?xml version="1.0"?>
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>EiskaltDC++</string>
<key>CFBundleVersion</key><string>${MACOSX_BUNDLE_BUNDLE_VERSION}</string>
<key>CFBundleShortVersionString</key><string>${REPLACE_VERSION}</string>
<key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>
</dict></plist>
''')
        env = dict(os.environ, DEPS_PREFIX=str(self.case), QT_PREFIX=str(self.case),
                   COCOA_PLUGIN=str(self.case / "plugin.dylib"), COCOA_PLUGIN_SHA256="fixture",
                   EISKALT_MACOS_BUILD_NUMBER="30000001")
        for profile in ("intel", "apple-silicon"):
            with self.subTest(profile=profile):
                build = self.case / ("build-" + profile)
                configured = subprocess.run(["cmake", "--preset", "macos-" + profile,
                                             "-B", str(build), "-DREPO=" + str(ROOT)], cwd=source, env=env,
                                            capture_output=True, text=True)
                self.assertEqual(configured.returncode, 0, configured.stderr)
                subprocess.run(["cmake", "--build", str(build), "-j2"],
                               check=True, capture_output=True)
                self.app = build / "EiskaltDC++.app"
                info = plistlib.loads((self.app / "Contents/Info.plist").read_bytes())
                self.assertEqual(info["CFBundleVersion"], "30000001")
                self.assertEqual(info["CFBundleShortVersionString"], "3.0.0-pre.1")
                result = self.invoke("check", profile)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_release_presets_require_explicit_build_allocation(self):
        source = self.case / "unallocated-presets"
        source.mkdir()
        shutil.copyfile(ROOT / "CMakePresets.json", source / "CMakePresets.json")
        (source / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.23)\nproject(Unallocated NONE)\n")
        env = dict(os.environ, DEPS_PREFIX=str(self.case), QT_PREFIX=str(self.case),
                   COCOA_PLUGIN=str(self.case / "plugin.dylib"), COCOA_PLUGIN_SHA256="fixture")
        env.pop("EISKALT_MACOS_BUILD_NUMBER", None)
        for profile in ("intel", "apple-silicon"):
            with self.subTest(profile=profile):
                build = self.case / ("unallocated-" + profile)
                result = subprocess.run(["cmake", "--preset", "macos-" + profile, "-B", str(build)],
                                        cwd=source, env=env, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("disabled", result.stderr)
                self.assertFalse((build / "CMakeCache.txt").exists())

    def deploy_provider(self, provider):
        # The actual staging script runs; dependency discovery is outside this
        # file-copy test, so the external Qt deploy process is a no-op boundary.
        return subprocess.run(["cmake", "-DMACDEPLOYQT=/usr/bin/true", "-DAPP_BUNDLE=" + str(self.app),
                               "-DTORRENT_LIBRARY=" + str(provider), "-DTORRENT_SONAME=libtorrent-rasterbar.2.1.dylib",
                               "-P", str(ROOT / "eiskaltdcpp-qt/cmake/deploy_macos.cmake")],
                              capture_output=True, text=True)

    def test_deployment_copies_readonly_provider_to_writable_owned_staging(self):
        provider = self.case / "source.dylib"
        provider.write_bytes(b"immutable linked provider fixture")
        provider.chmod(0o444)
        before = provider.stat().st_mode
        result = self.deploy_provider(provider)
        self.assertEqual(result.returncode, 0, result.stderr)
        staged = self.contents / "Frameworks/libtorrent-rasterbar.2.1.dylib"
        self.assertEqual(staged.read_bytes(), b"immutable linked provider fixture")
        self.assertTrue(staged.stat().st_mode & 0o200)
        self.assertEqual(provider.stat().st_mode, before)
        self.assertEqual(provider.read_bytes(), b"immutable linked provider fixture")

    def test_deployment_refuses_existing_staging_symlink_without_mutating_target(self):
        provider = self.case / "source.dylib"
        provider.write_bytes(b"linked provider fixture")
        target = self.case / "outside-staging.dylib"
        target.write_bytes(b"preserve outside staging")
        frameworks = self.contents / "Frameworks"
        frameworks.mkdir()
        (frameworks / "libtorrent-rasterbar.2.1.dylib").symlink_to(target)
        result = self.deploy_provider(provider)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(target.read_bytes(), b"preserve outside staging")


class BundleBuildNumbers(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eiskalt-bundle-numbers-")
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "source"
        self.source.mkdir()
        shutil.copyfile(ROOT / "eiskaltdcpp-qt/Info.plist.in", self.source / "Info.plist.in")
        (self.source / "main.c").write_text("int main(void) { return 0; }\n")
        (self.source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.23)
project(BuildNumberFixture C)
set(APP_VERSION "3.0.0-pre.1")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}")
set(MACOSX_BUNDLE_BUNDLE_VERSION "${APP_VERSION}")
if(DEFINED ENV{EISKALT_TEST_RAW_BUILD})
    set(EISKALT_MACOS_BUILD_NUMBER "$ENV{EISKALT_TEST_RAW_BUILD}")
endif()
include("${REPO}/cmake/MacBundleVersion.cmake")
add_executable(BuildNumberFixture MACOSX_BUNDLE main.c)
set_target_properties(BuildNumberFixture PROPERTIES MACOSX_BUNDLE_INFO_PLIST
    "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
''')

    def configure(self, *args, env=None):
        return subprocess.run(["cmake", "-S", str(self.source), "-B", str(self.source.parent / "build"),
                               "-DREPO=" + str(ROOT), *args], capture_output=True, text=True, env=env)

    def assert_bundle_number(self, expected, *args):
        configured = self.configure(*args)
        self.assertEqual(configured.returncode, 0, configured.stderr)
        subprocess.run(["cmake", "--build", str(self.source.parent / "build"), "-j2"],
                       check=True, capture_output=True)
        info = plistlib.loads((self.source.parent / "build/BuildNumberFixture.app/Contents/Info.plist").read_bytes())
        self.assertEqual(info["CFBundleVersion"], expected)
        self.assertEqual(info["CFBundleShortVersionString"], "3.0.0-pre.1")

    def test_development_build_uses_numeric_identity_without_release_allocation(self):
        self.assert_bundle_number("1")

    def test_explicit_release_number_survives_real_plist_generation(self):
        self.assert_bundle_number("30000001", "-DEISKALT_MACOS_BUILD_NUMBER=30000001")

    def test_maximum_build_matches_signed_feed_numeric_range(self):
        self.assert_bundle_number("9223372036854775807", "-DEISKALT_MACOS_BUILD_NUMBER=9223372036854775807")

    def test_noncanonical_zero_or_overflow_build_refuses_configuration(self):
        for value in ("", "0", "01", "-1", "+1", "1.0", " 1", "1 ", "1\n", "1e3", "ON", "9223372036854775808", "9" * 100):
            with self.subTest(value=value):
                # CMake's -D parser trims trailing spaces before our module sees
                # them. Supply raw values at the real configuration boundary.
                configured = self.configure(env=dict(os.environ, EISKALT_TEST_RAW_BUILD=value))
                self.assertNotEqual(configured.returncode, 0)
                self.assertIn("EISKALT_MACOS_BUILD_NUMBER", configured.stderr)


if __name__ == "__main__":
    unittest.main()
