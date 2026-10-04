#!/usr/bin/env python3
"""Exercise the opt-in native target gate through real CMake configuration."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class NativeWiring(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eiskalt-native-wiring-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "main.cpp").write_text("int main() { return 0; }\n")
        (self.root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.23)
project(NativeWiring LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core)
if(DEFINED TEST_APPLE)
    set(APPLE "${TEST_APPLE}")
endif()
include("${REPO}/cmake/MacUpdaterNative.cmake")
add_executable(fixture main.cpp)
target_link_libraries(fixture PRIVATE Qt6::Core)
eiskalt_add_native_updater(fixture)
get_target_property(moc_sources fixture SOURCES)
add_executable(gui main.cpp)
target_link_libraries(gui PRIVATE Qt6::Core)
eiskalt_add_native_updater(gui)
get_target_property(gui_sources gui SOURCES)
list(APPEND moc_sources ${gui_sources})
list(FILTER moc_sources INCLUDE REGEX "moc_.*[.]cpp$")
add_custom_target(native-moc-only DEPENDS ${moc_sources})
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/sources" CONTENT "$<TARGET_PROPERTY:fixture,SOURCES>")
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/definitions" CONTENT "$<TARGET_PROPERTY:fixture,COMPILE_DEFINITIONS>")
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/links" CONTENT "$<TARGET_PROPERTY:fixture,LINK_LIBRARIES>")
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/gui-sources" CONTENT "$<TARGET_PROPERTY:gui,SOURCES>")
''')

    def configure(self, *args):
        qt = ["-DQt6_DIR=" + os.environ["EISKALT_TEST_QT_DIR"]] if os.environ.get("EISKALT_TEST_QT_DIR") else []
        return subprocess.run(["cmake", "-S", str(self.root), "-B", str(self.root / "build"),
                               "-DREPO=" + str(ROOT), "-DTEST_APPLE=TRUE", *qt, *args], capture_output=True, text=True)

    def test_default_off_has_no_native_sources_or_dependency(self):
        result = self.configure("-DEISKALT_SPARKLE_ROOT=/not/a/framework")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(".mm", (self.root / "build/sources").read_text())
        self.assertNotIn("EISKALT_MAC_UPDATER_NATIVE", (self.root / "build/definitions").read_text())
        self.assertNotIn("Sparkle", (self.root / "build/links").read_text())

    def test_opt_in_requires_apple_metadata_and_audited_packaging(self):
        for args in (("-DTEST_APPLE=FALSE",),
                     ("-DEISKALT_MAC_UPDATER_METADATA=OFF", "-DEISKALT_MAC_UPDATER_PACKAGE=ON"),
                     ("-DEISKALT_MAC_UPDATER_METADATA=ON", "-DEISKALT_MAC_UPDATER_PACKAGE=OFF")):
            with self.subTest(args=args):
                result = self.configure("-DEISKALT_MAC_UPDATER_NATIVE=ON", *args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires macOS, validated updater metadata and audited packaging",
                              " ".join(result.stderr.split()))

    def test_opt_in_wires_real_native_sources_and_pinned_dependency(self):
        sdk = os.environ.get("EISKALT_TEST_SPARKLE_ROOT")
        if not sdk:
            self.fail("Provide the verified pinned Sparkle SDK for native build wiring QA")
        result = self.configure("-DEISKALT_MAC_UPDATER_NATIVE=ON", "-DEISKALT_MAC_UPDATER_METADATA=ON",
                                "-DEISKALT_MAC_UPDATER_PACKAGE=ON", "-DEISKALT_SPARKLE_ROOT=" + sdk)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        sources = (self.root / "build/sources").read_text().split(";")
        for name in ("MacUpdateBackend.mm", "MacUpdateDiscovery.mm", "MacUpdateInstaller.mm",
                     "BundleArchiveVerifier.mm"):
            self.assertEqual(sum(Path(path).name == name for path in sources), 1)
        for name in ("moc_MacUpdateBackend.cpp", "moc_MacUpdateDiscovery.cpp", "moc_MacUpdateInstaller.cpp"):
            self.assertEqual(sum(Path(path).name == name for path in sources), 1)
        gui_sources = (self.root / "build/gui-sources").read_text().split(";")
        app_moc = {path for path in sources if Path(path).name.startswith("moc_") and Path(path).suffix == ".cpp"}
        gui_moc = {path for path in gui_sources if Path(path).name.startswith("moc_") and Path(path).suffix == ".cpp"}
        self.assertEqual(len(gui_moc), 3)
        self.assertTrue(app_moc.isdisjoint(gui_moc))
        self.assertIn("EISKALT_MAC_UPDATER_NATIVE", (self.root / "build/definitions").read_text())
        links = (self.root / "build/links").read_text()
        self.assertIn("Sparkle::Sparkle", links)
        self.assertIn("-framework AppKit", links)
        result = subprocess.run(["cmake", "--build", str(self.root / "build"),
                                 "--target", "native-moc-only", "-j2"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for path in sources + gui_sources:
            if Path(path).name.startswith("moc_") and Path(path).suffix == ".cpp":
                self.assertTrue(Path(path).is_file(), path)


if __name__ == "__main__":
    unittest.main()
