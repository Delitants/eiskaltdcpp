#!/usr/bin/env python3
"""Selected-recheck packaging gates using real Mach-O and CMake fixtures."""

from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(sys.argv.pop(1)).resolve()
TOOL = ROOT / "tools/dependencies/audit-selected-recheck.sh"
ARCH = subprocess.check_output(["uname", "-m"], text=True).strip()
FLOOR = "26.0" if ARCH == "arm64" else "14.0"
OTHER = "x86_64" if ARCH == "arm64" else "arm64"
OPERATIONS = ("prepare_selected_recheck", "start_selected_recheck",
              "cancel_selected_recheck", "finish_selected_recheck",
              "query_selected_recheck")


def run(*args):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True, text=True)


class SelectedRecheckBundle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="eiskalt-selected-package-")
        cls.work = Path(cls.tmp.name)
        definitions = "".join(f"void {name}() {{}}\n" for name in OPERATIONS)
        for name, arch, floor, abi in (("good", ARCH, FLOOR, 1),
                                       ("wrong-abi", ARCH, FLOOR, 2),
                                       ("wrong-cpu", OTHER, FLOOR, 1),
                                       ("newer", ARCH, "27.0", 1),
                                       ("missing", ARCH, FLOOR, None)):
            source = cls.work / (name + ".cpp")
            marker = "" if abi is None else f"int eiskalt_selected_recheck_abi() {{ return {abi}; }}"
            source.write_text("namespace libtorrent {\n" + definitions + marker + "\n}\n")
            run("xcrun", "clang++", "-arch", arch, "-mmacosx-version-min=" + floor,
                "-dynamiclib", source, "-Wl,-install_name,@rpath/libtorrent-rasterbar.2.1.dylib",
                "-o", cls.work / (name + ".dylib"))
        source = cls.work / "main.cpp"
        declarations = "".join(f"void {name}();\n" for name in OPERATIONS)
        calls = "".join(f"libtorrent::{name}();\n" for name in OPERATIONS)
        source.write_text("namespace libtorrent {\n" + declarations +
                          "int eiskalt_selected_recheck_abi();\n}\nint main() {\n" + calls +
                          "return libtorrent::eiskalt_selected_recheck_abi() != 1;\n}\n")
        run("xcrun", "clang++", "-arch", ARCH, "-mmacosx-version-min=" + FLOOR,
            source, cls.work / "good.dylib", "-Wl,-rpath,@executable_path/../Frameworks",
            "-o", cls.work / "main")
        source.write_text("int main() { return 0; }\n")
        run("xcrun", "clang++", "-arch", ARCH, "-mmacosx-version-min=" + FLOOR,
            source, "-o", cls.work / "plain-main")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(prefix="case with space ", dir=self.work))
        self.app = self.case / "EiskaltDC++.app"
        self.contents = self.app / "Contents"
        (self.contents / "MacOS").mkdir(parents=True)
        (self.contents / "Frameworks").mkdir()
        (self.contents / "Info.plist").write_bytes(plistlib.dumps({
            "CFBundleExecutable": "EiskaltDC++", "LSMinimumSystemVersion": FLOOR,
            "CFBundleShortVersionString": "3.0.0-pre.1", "CFBundleVersion": "3.0.0-pre.1"}))
        self.main = self.contents / "MacOS/EiskaltDC++"
        self.library = self.contents / "Frameworks/libtorrent-rasterbar.2.1.dylib"
        shutil.copy2(self.work / "main", self.main)
        shutil.copy2(self.work / "good.dylib", self.library)

    def invoke(self, *extra):
        if TOOL.exists():
            args = ["/bin/bash", TOOL, self.app, ARCH, FLOOR, *extra]
        else:
            # Exercise the actual pre-extension release gate to demonstrate its gaps.
            profile = "apple-silicon" if ARCH == "arm64" else "intel"
            args = [sys.executable, ROOT / "macos/release.py", "check", "--app", self.app,
                    "--profile", profile, "--version", "3.0.0-pre.1"]
        return subprocess.run(list(map(str, args)), capture_output=True, text=True)

    def rejected(self, reason):
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(reason, result.stderr)

    def test_accepts_complete_native_fixture(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_accepts_loader_relative_library_identity(self):
        identity = "@loader_path/../Frameworks/" + self.library.name
        run("install_name_tool", "-id", identity, self.library)
        run("install_name_tool", "-change", "@rpath/" + self.library.name, identity, self.main)
        self.assertEqual(run(self.main).returncode, 0)
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_accepts_reexported_extension_dependency(self):
        wrapper = self.contents / "Frameworks/wrapper.dylib"
        source = self.case / "wrapper.cpp"
        source.write_text("int wrapper() { return 0; }\n")
        run("xcrun", "clang++", "-arch", ARCH, "-mmacosx-version-min=" + FLOOR,
            "-dynamiclib", source, "-Wl,-install_name,@rpath/wrapper.dylib",
            "-Wl,-reexport_library," + str(self.library), "-o", wrapper)
        source.write_text("namespace libtorrent {\n" +
                          "".join(f"void {name}();\n" for name in OPERATIONS) +
                          "int eiskalt_selected_recheck_abi();\n}\nint main() {\n" +
                          "".join(f"libtorrent::{name}();\n" for name in OPERATIONS) +
                          "return libtorrent::eiskalt_selected_recheck_abi() != 1;\n}\n")
        run("xcrun", "clang++", "-arch", ARCH, "-mmacosx-version-min=" + FLOOR,
            source, wrapper, "-L" + str(wrapper.parent),
            "-Wl,-rpath,@executable_path/../Frameworks", "-o", self.main)
        self.assertNotIn("libtorrent-rasterbar", run("otool", "-L", self.main).stdout)
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_missing_extension_marker(self):
        shutil.copy2(self.work / "missing.dylib", self.library)
        shutil.copy2(self.work / "plain-main", self.main)
        self.rejected("extension")

    def test_rejects_wrong_runtime_abi(self):
        shutil.copy2(self.work / "wrong-abi.dylib", self.library)
        self.rejected("ABI")

    def test_static_only_never_claims_runtime_abi_acceptance(self):
        shutil.copy2(self.work / "wrong-abi.dylib", self.library)
        result = self.invoke("--static-only")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("runtime ABI remains PENDING", result.stdout)

    def test_rejects_missing_operation_export(self):
        source = self.case / "partial.cpp"
        source.write_text("namespace libtorrent {\n" +
                          "".join(f"void {name}() {{}}\n" for name in OPERATIONS[:-1]) +
                          "int eiskalt_selected_recheck_abi() { return 1; }\n}\n")
        run("xcrun", "clang++", "-arch", ARCH, "-mmacosx-version-min=" + FLOOR,
            "-dynamiclib", source, "-Wl,-install_name,@rpath/libtorrent-rasterbar.2.1.dylib",
            "-o", self.library)
        self.rejected("extension symbol/reference mismatch")

    def test_rejects_app_without_capability_reference(self):
        shutil.copy2(self.work / "plain-main", self.main)
        self.rejected("reference")

    def test_rejects_wrong_architecture(self):
        shutil.copy2(self.work / "wrong-cpu.dylib", self.library)
        self.rejected("architecture")

    def test_rejects_too_new_deployment_floor(self):
        shutil.copy2(self.work / "newer.dylib", self.library)
        self.rejected("requires macOS")

    def test_rejects_external_homebrew_dependency(self):
        run("install_name_tool", "-change", "/usr/lib/libSystem.B.dylib",
            "/opt/homebrew/lib/eiskalt-fixture-outside.dylib", self.library)
        self.rejected("external")

    def test_system_prefix_cannot_hide_path_traversal(self):
        run("install_name_tool", "-change", "/usr/lib/libSystem.B.dylib",
            "/usr/lib/../../opt/homebrew/lib/eiskalt-fixture-outside.dylib", self.library)
        result = self.invoke("--static-only")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("external", result.stderr)

    def test_rejects_external_runpath_even_if_unused(self):
        run("install_name_tool", "-add_rpath", "/opt/homebrew/lib", self.main)
        self.rejected("runpath")

    def test_rejects_missing_relative_dependency(self):
        run("install_name_tool", "-change", "/usr/lib/libSystem.B.dylib",
            "@rpath/missing-fixture.dylib", self.library)
        self.rejected("unresolved")

    def test_rejects_symlink_outside_bundle(self):
        (self.contents / "Frameworks/escape.dylib").symlink_to(self.work / "good.dylib")
        self.rejected("symlink")

    def cmake_pair(self, abi):
        include = self.case / "include/libtorrent"
        include.mkdir(parents=True)
        header = include / "eiskalt_selected_recheck.hpp"
        header.write_text(
            f"#define LIBTORRENT_EISKALT_SELECTED_RECHECK_ABI {abi}\n"
            "namespace libtorrent { int eiskalt_selected_recheck_abi(); }\n")
        project = self.case / "cmake"
        project.mkdir()
        (project / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\nproject(abi_pair LANGUAGES CXX)\n"
            "add_library(LibtorrentRasterbar::torrent-rasterbar SHARED IMPORTED)\n"
            f'set_target_properties(LibtorrentRasterbar::torrent-rasterbar PROPERTIES IMPORTED_LOCATION "{self.library}" INTERFACE_INCLUDE_DIRECTORIES "{include.parent}")\n'
            f'include("{ROOT / "cmake/SelectedRecheck.cmake"}")\n')
        return project, header

    def configure_pair(self, project):
        return subprocess.run(["cmake", "-S", str(project), "-B", str(self.case / "build"),
                               "-DEISKALT_REQUIRE_SELECTED_RECHECK=ON"], capture_output=True, text=True)

    def test_rejects_header_library_abi_mismatch_at_configure(self):
        project, _ = self.cmake_pair(2)
        result = self.configure_pair(project)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("matching maintained libtorrent extension", result.stderr)

    def test_reconfigure_cannot_reuse_stale_header_capability(self):
        project, header = self.cmake_pair(1)
        result = self.configure_pair(project)
        self.assertEqual(result.returncode, 0, result.stderr)
        header.write_text(header.read_text().replace("ABI 1", "ABI 2"))
        result = self.configure_pair(project)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("matching maintained libtorrent extension", result.stderr)


if __name__ == "__main__":
    unittest.main()
