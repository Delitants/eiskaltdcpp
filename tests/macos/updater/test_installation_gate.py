#!/usr/bin/env python3
"""Configure-only installation admission contracts; never build or run a bundle.

For audited-input cases set EISKALT_TEST_UPDATER_BUILD to an existing qualified
native package build. Its cache supplies input paths/pins only; every configure
uses a fresh temporary build tree and reruns the real stage audit. Alternatively
provide EISKALT_TEST_SPARKLE_ROOT, EISKALT_TEST_UPDATER_STAGE,
EISKALT_TEST_UPDATER_STAGE_SHA256 and EISKALT_TEST_UPDATER_ARCH.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
GATE = "EISKALT_MAC_UPDATER_INSTALLATION"


class InstallationGate(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eiskalt-installation-gate-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sequence = 0
        (self.root / "main.cpp").write_text("int main() { return 0; }\n")
        (self.root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.23)
project(InstallationGate LANGUAGES CXX)
# Exercise release-profile validation, not compiler architecture support.
if(DEFINED TEST_ARCH)
    set(CMAKE_OSX_ARCHITECTURES "${TEST_ARCH}")
endif()
if(DEFINED TEST_APPLE)
    set(APPLE "${TEST_APPLE}")
endif()
if(TEST_AUDIT_PACKAGE)
    include("${REPO}/cmake/MacUpdaterPackaging.cmake")
endif()
include("${REPO}/cmake/MacUpdaterNative.cmake")
add_executable(fixture main.cpp)
if(EISKALT_MAC_UPDATER_NATIVE)
    find_package(Qt6 REQUIRED COMPONENTS Core)
    target_link_libraries(fixture PRIVATE Qt6::Core)
endif()
eiskalt_add_native_updater(fixture)
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/definitions"
    CONTENT "$<TARGET_PROPERTY:fixture,COMPILE_DEFINITIONS>")
file(WRITE "${PROJECT_BINARY_DIR}/packaging-hash" "${EISKALT_MAC_BUNDLE_PACKAGING_HASH}")
''')
        self.cache = {}
        if os.environ.get("EISKALT_TEST_UPDATER_BUILD"):
            path = Path(os.environ["EISKALT_TEST_UPDATER_BUILD"]) / "CMakeCache.txt"
            for line in path.read_text().splitlines():
                if not line.startswith(("//", "#")) and "=" in line and ":" in line.split("=", 1)[0]:
                    key, value = line.split("=", 1)
                    self.cache[key.split(":", 1)[0]] = value

    def configure(self, *arguments):
        self.sequence += 1
        build = self.root / f"build-{self.sequence}"
        qt = os.environ.get("EISKALT_TEST_QT_DIR", self.cache.get("Qt6_DIR"))
        command = ["cmake", "-S", str(self.root), "-B", str(build),
                   "-DREPO=" + str(ROOT), "-DTEST_APPLE=TRUE"]
        if qt:
            command.append("-DQt6_DIR=" + qt)
        result = subprocess.run(command + list(arguments), capture_output=True, text=True)
        return result, build

    def qualified_inputs(self):
        result = ["-DTEST_AUDIT_PACKAGE=ON", "-DEISKALT_MAC_UPDATER_NATIVE=ON",
                  "-DEISKALT_MAC_UPDATER_METADATA=ON", "-DEISKALT_MAC_UPDATER_PACKAGE=ON"]
        for key, environment in (
            ("EISKALT_SPARKLE_ROOT", "EISKALT_TEST_SPARKLE_ROOT"),
            ("EISKALT_MAC_UPDATER_STAGE", "EISKALT_TEST_UPDATER_STAGE"),
            ("EISKALT_MAC_UPDATER_STAGE_SHA256", "EISKALT_TEST_UPDATER_STAGE_SHA256"),
            ("CMAKE_OSX_ARCHITECTURES", "EISKALT_TEST_UPDATER_ARCH"),
        ):
            value = os.environ.get(environment, self.cache.get(key))
            self.assertTrue(value, f"Provide {environment} or EISKALT_TEST_UPDATER_BUILD")
            argument = "TEST_ARCH" if key == "CMAKE_OSX_ARCHITECTURES" else key
            result.append(f"-D{argument}={value}")
        return result

    def assert_configured(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_installation_is_declared_default_off(self):
        result, build = self.configure()
        self.assert_configured(result)
        self.assertIn(GATE + ":BOOL=OFF", (build / "CMakeCache.txt").read_text())
        self.assertNotIn(GATE, (build / "definitions").read_text().split(";"))

    def test_installation_cannot_be_enabled_without_native_selection(self):
        result, _ = self.configure("-D" + GATE + "=ON")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(GATE, result.stderr)

    def test_native_boolean_flags_do_not_substitute_for_audited_package(self):
        inputs = self.qualified_inputs()
        inputs.remove("-DTEST_AUDIT_PACKAGE=ON")
        result, _ = self.configure(*inputs, "-D" + GATE + "=ON")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(GATE, result.stderr)

    def test_qualified_inputs_grant_definition_only_with_explicit_opt_in(self):
        inputs = self.qualified_inputs()
        off, off_build = self.configure(*inputs, "-D" + GATE + "=OFF")
        self.assert_configured(off)
        self.assertNotIn(GATE, (off_build / "definitions").read_text().split(";"))
        on, on_build = self.configure(*inputs, "-D" + GATE + "=ON")
        self.assert_configured(on)
        self.assertIn(GATE, (on_build / "definitions").read_text().split(";"))
        off_hash = (off_build / "packaging-hash").read_text()
        on_hash = (on_build / "packaging-hash").read_text()
        self.assertRegex(off_hash, r"^[0-9a-f]{64}$")
        self.assertRegex(on_hash, r"^[0-9a-f]{64}$")
        self.assertNotEqual(off_hash, on_hash, "Admission must invalidate the metadata/relink receipt")

    def test_opt_in_rejects_invalid_stage_digest(self):
        result, _ = self.configure(*self.qualified_inputs(), "-D" + GATE + "=ON",
                                  "-DEISKALT_MAC_UPDATER_STAGE_SHA256=" + "0" * 64)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Audited updater stage rejected", result.stderr)

    def test_opt_in_rejects_missing_pinned_sdk(self):
        result, _ = self.configure(*self.qualified_inputs(), "-D" + GATE + "=ON",
                                  "-DEISKALT_SPARKLE_ROOT=" + str(self.root / "missing-sdk"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("EISKALT_SPARKLE_ROOT", result.stderr)

    def test_opt_in_rejects_unsupported_or_multiple_release_cpus(self):
        for architecture in ("", "arm64;x86_64", "i386"):
            with self.subTest(architecture=architecture):
                result, _ = self.configure(*self.qualified_inputs(), "-D" + GATE + "=ON",
                                          "-DTEST_ARCH=" + architecture)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("one release CPU", result.stderr)

    def test_opt_in_still_requires_mac_metadata_and_packaging(self):
        for argument in ("-DTEST_APPLE=FALSE", "-DEISKALT_MAC_UPDATER_METADATA=OFF",
                         "-DEISKALT_MAC_UPDATER_PACKAGE=OFF"):
            with self.subTest(argument=argument):
                result, _ = self.configure(*self.qualified_inputs(), "-D" + GATE + "=ON", argument)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
