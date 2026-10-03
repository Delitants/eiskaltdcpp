#!/usr/bin/env python3
"""Actual CMake bundle metadata and re-signing, without updater activation."""
import base64
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PREFIX = "https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/"
KEY = base64.b64encode(bytes(range(1, 33))).decode()
OTHER_KEY = base64.b64encode(bytes(range(32, 0, -1))).decode()


class BundleMetadata(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eiskalt-updater-metadata-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        diagnostics = os.environ.get("EISKALT_METADATA_DIAGNOSTICS")
        if diagnostics:
            destination = Path(diagnostics) / self._testMethodName
            self.addCleanup(shutil.copytree, self.root, destination, dirs_exist_ok=True, symlinks=True)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "main.cpp").write_text("int main() { return 0; }\n")
        (self.source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.10)
project(UpdaterMetadataFixture LANGUAGES CXX)
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()
add_executable(${PROJECT_NAME} MACOSX_BUNDLE main.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${REPO}/eiskaltdcpp-qt/Info.plist.in"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
set(APP_VERSION "3.0.0-pre.1")
set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.eiskaltdcpp.test.metadata")
set(MACOSX_BUNDLE_BUNDLE_NAME "UpdaterMetadataFixture")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}")
include("${REPO}/cmake/MacBundleVersion.cmake")
include("${REPO}/cmake/MacOpenHandlers.cmake")
foreach(name PUBLIC_KEY FEED_URL)
    if(DEFINED ENV{EISKALT_TEST_RAW_${name}})
        set(EISKALT_MAC_UPDATER_${name} "$ENV{EISKALT_TEST_RAW_${name}}")
    endif()
endforeach()
include("${REPO}/cmake/MacUpdaterMetadata.cmake")
include("${REPO}/cmake/MacBundleMetadata.cmake")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E touch "${PROJECT_BINARY_DIR}/signed.stamp"
    VERBATIM)
''')

    def configure(self, *args, generator="Unix Makefiles", arch="arm64", key=KEY,
                  feed=None, enabled="ON", env=None, build=None):
        build = build or self.root / "build"
        feed = feed if feed is not None else PREFIX + ("intel.xml" if arch == "x86_64" else "apple-silicon.xml")
        command = [
            "cmake", "-S", str(self.source), "-B", str(build), "-G", generator,
            "-DREPO=" + str(ROOT), "-DCMAKE_OSX_ARCHITECTURES=" + arch,
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + ("14.0" if arch == "x86_64" else "26.0"),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DEISKALT_MAC_UPDATER_PUBLIC_KEY=" + key,
            "-DEISKALT_MAC_UPDATER_FEED_URL=" + feed, *args]
        if enabled is not None:
            command.append("-DEISKALT_MAC_UPDATER_METADATA=" + enabled)
        result = subprocess.run(command, capture_output=True, text=True, env=env)
        build.mkdir(parents=True, exist_ok=True)
        with (build / "configure-history.log").open("a") as log:
            log.write(result.stdout + result.stderr)
        return result

    def build(self, build=None):
        build = build or self.root / "build"
        result = subprocess.run(["cmake", "--build", str(build), "--config", "Release", "-j2"],
                                capture_output=True, text=True)
        with (build / "build-history.log").open("a") as log:
            log.write(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        apps = list(build.rglob("UpdaterMetadataFixture.app"))
        self.assertEqual(len(apps), 1)
        result = subprocess.run(["codesign", "--verify", "--strict", str(apps[0])],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return apps[0], plistlib.loads((apps[0] / "Contents/Info.plist").read_bytes())

    def test_default_disabled_emits_no_updater_or_network_exceptions(self):
        for enabled in (None, "OFF"):
            with self.subTest(enabled=enabled):
                result = self.configure(enabled=enabled, key="bad", feed="http://untrusted.invalid")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                _, info = self.build()
                self.assertFalse(any(key.startswith("SU") for key in info))
                self.assertNotIn("NSAppTransportSecurity", info)

    def test_both_flavors_emit_exact_trust_and_single_policy_owner(self):
        for arch in ("x86_64", "arm64"):
            with self.subTest(arch=arch):
                build = self.root / arch
                result = self.configure(arch=arch, build=build)
                self.assertEqual(result.returncode, 0, result.stderr)
                _, info = self.build(build)
                self.assertEqual(info.get("SUPublicEDKey"), KEY)
                self.assertEqual(info.get("SUFeedURL"), PREFIX + ("intel.xml" if arch == "x86_64" else "apple-silicon.xml"))
                for name in ("SURequireSignedFeed", "SUVerifyUpdateBeforeExtraction", "SUEnableInstallerLauncherService"):
                    self.assertIs(info.get(name), True)
                for name in ("SUEnableAutomaticChecks", "SUAutomaticallyUpdate", "SUEnableSystemProfiling", "SUEnableDownloaderService"):
                    self.assertIs(info.get(name), False)
                self.assertEqual(type(info.get("SUSignedFeedFailureExpirationInterval")), int)
                self.assertEqual(info["SUSignedFeedFailureExpirationInterval"], 0)
                self.assertEqual(info.get("NSAppTransportSecurity"), {"NSAllowsLocalNetworking": True})
                self.assertEqual(info["CFBundleVersion"], "1")

    def test_enabled_configuration_rejects_absent_or_noncanonical_public_keys(self):
        for key in ("", "bad", KEY[:-1], KEY + "=", " " + KEY, KEY + " ", KEY + "\n",
                    "A" * 43 + "=", KEY[:-2] + "B=", KEY[:5] + "<" + KEY[6:],
                    KEY[:5] + "_" + KEY[6:], KEY[:5] + ";" + KEY[6:]):
            with self.subTest(key=key):
                result = self.configure(env=dict(os.environ, EISKALT_TEST_RAW_PUBLIC_KEY=key))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("EISKALT_MAC_UPDATER_PUBLIC_KEY", result.stderr)

    def test_enabled_configuration_rejects_missing_or_wrong_feed_contract(self):
        for feed in ("", PREFIX + "intel.xml", PREFIX + "arm64.xml", PREFIX + "../apple-silicon.xml",
                     PREFIX.replace("https:", "http:") + "apple-silicon.xml",
                     PREFIX.replace("githubusercontent.com", "githubusercontent.com.evil.invalid") + "apple-silicon.xml",
                     PREFIX.replace("https://", "https://user:pass@") + "apple-silicon.xml",
                     PREFIX + "apple-silicon.xml#fragment", PREFIX + "apple-silicon.xml?token=secret",
                     PREFIX + "apple-silicon.xml?revision=02", PREFIX + "apple-silicon.xml?revision=0",
                     PREFIX + "apple-silicon.xml?revision=2&token=secret", PREFIX + "apple-silicon.xml ",
                     PREFIX + "apple-silicon.xml\n", PREFIX + "apple-silicon.xml?revision=" + "9" * 2048):
            with self.subTest(feed=feed):
                result = self.configure(env=dict(os.environ, EISKALT_TEST_RAW_FEED_URL=feed))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("EISKALT_MAC_UPDATER_FEED_URL", result.stderr)

    def test_updater_metadata_rejects_unknown_empty_or_universal_cpu(self):
        for arch in ("", "aarch64", "i386", "arm64;x86_64"):
            with self.subTest(arch=arch):
                result = self.configure(arch=arch)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("CMAKE_OSX_ARCHITECTURES", result.stderr)

    def test_invalid_trust_reconfiguration_preserves_the_previous_signed_bundle(self):
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stderr)
        app, _ = self.build()
        paths = (app / "Contents/Info.plist", app / "Contents/MacOS/UpdaterMetadataFixture")
        before = [path.read_bytes() for path in paths]
        for arguments in ({"key": "bad"}, {"feed": "http://untrusted.invalid"}):
            result = self.configure(**arguments)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual([path.read_bytes() for path in paths], before)

    def test_make_and_xcode_key_and_feed_only_changes_relink_and_resign(self):
        for generator in ("Unix Makefiles", "Xcode"):
            with self.subTest(generator=generator):
                build = self.root / generator.replace(" ", "-")
                result = self.configure(generator=generator, build=build)
                self.assertEqual(result.returncode, 0, result.stderr)
                app, info = self.build(build)
                self.assertEqual(info.get("SUPublicEDKey"), KEY)
                metadata = (build / "MacBundleMetadata.cpp").read_bytes()
                executable = app / "Contents/MacOS/UpdaterMetadataFixture"
                digest = hashlib.sha256(executable.read_bytes()).digest()
                for key, feed in ((OTHER_KEY, PREFIX + "apple-silicon.xml"),
                                  (OTHER_KEY, PREFIX + "apple-silicon.xml?revision=2")):
                    stamp = (build / "signed.stamp").stat().st_mtime_ns
                    result = self.configure(generator=generator, build=build, key=key, feed=feed)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    app, info = self.build(build)
                    self.assertEqual(info["SUPublicEDKey"], key)
                    self.assertEqual(info["SUFeedURL"], feed)
                    updated = (build / "MacBundleMetadata.cpp").read_bytes()
                    self.assertNotEqual(updated, metadata)
                    new_digest = hashlib.sha256(executable.read_bytes()).digest()
                    self.assertNotEqual(new_digest, digest)
                    self.assertGreater((build / "signed.stamp").stat().st_mtime_ns, stamp)
                    metadata, digest = updated, new_digest
                self.build(build)
                self.assertEqual((build / "MacBundleMetadata.cpp").read_bytes(), metadata)
                self.assertEqual(hashlib.sha256(executable.read_bytes()).digest(), digest)
                result = self.configure(generator=generator, build=build, enabled="OFF")
                self.assertEqual(result.returncode, 0, result.stderr)
                _, info = self.build(build)
                self.assertFalse(any(key.startswith("SU") for key in info))
                self.assertNotIn("NSAppTransportSecurity", info)
                self.assertNotEqual((build / "MacBundleMetadata.cpp").read_bytes(), metadata)

    def test_make_relinks_metadata_when_source_and_object_timestamps_collide(self):
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.build()
        build = self.root / "build"
        objects = list(build.rglob("MacBundleMetadata*.cpp.o"))
        self.assertEqual(len(objects), 1)
        old_timestamp = objects[0].stat().st_mtime_ns
        for key in (OTHER_KEY, KEY):
            result = self.configure(key=key)
            self.assertEqual(result.returncode, 0, result.stderr)
            # Reproduce a timestamp collision without waiting for a lucky fast build.
            # Returning to an earlier hash must also rebuild, not reuse a stale link.
            for source in build.glob("MacBundleMetadata*.cpp"):
                os.utime(source, ns=(old_timestamp, old_timestamp))
            _, info = self.build()
            self.assertEqual(info["SUPublicEDKey"], key)

    def test_metadata_gate_never_removes_an_external_or_symlinked_executable(self):
        build = self.root / "gate-build"
        build.mkdir()
        external = self.root / "outside-build"
        external.write_bytes(b"preserve this file")
        link = build / "symlinked-executable"
        link.symlink_to(external)
        for executable in (external, link):
            with self.subTest(executable=executable):
                result = subprocess.run([
                    "cmake", "-DBUILD_ROOT=" + str(build), "-DEXECUTABLE=" + str(executable),
                    "-DRECEIPT=" + str(build / "receipt"), "-DGENERATION=2", "-P",
                    str(ROOT / "cmake/MacBundleMetadataGate.cmake")], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("owned build root", result.stderr)
                self.assertEqual(external.read_bytes(), b"preserve this file")
                self.assertTrue(link.is_symlink())
        owned = build / "owned-executable"
        owned.write_bytes(b"owned build output")
        outside_dir = self.root / "outside-directory"
        outside_dir.mkdir()
        parent_link = build / "symlinked-directory"
        parent_link.symlink_to(outside_dir, target_is_directory=True)
        dangling_parent = build / "dangling-directory"
        dangling_parent.symlink_to(outside_dir / "not-created", target_is_directory=True)
        for receipt in (external, link, parent_link / "missing-receipt", dangling_parent / "receipt"):
            with self.subTest(receipt=receipt):
                result = subprocess.run([
                    "cmake", "-DBUILD_ROOT=" + str(build), "-DEXECUTABLE=" + str(owned),
                    "-DRECEIPT=" + str(receipt), "-DGENERATION=2", "-DRECORD=TRUE", "-P",
                    str(ROOT / "cmake/MacBundleMetadataGate.cmake")], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("owned build root", result.stderr)
                self.assertEqual(external.read_bytes(), b"preserve this file")
                self.assertTrue(link.is_symlink())
                self.assertEqual(owned.read_bytes(), b"owned build output")
                self.assertFalse((outside_dir / "missing-receipt").exists())
                self.assertFalse((outside_dir / "not-created").exists())


if __name__ == "__main__":
    unittest.main()
