#!/usr/bin/env python3
"""Exercise the read-only bundle auditor with real linked Mach-O images."""
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(sys.argv.pop(1)).resolve()
AUDIT = ROOT / "macos/check-bundle-external-links.sh"


class DependencyClosure(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eiskalt-closure-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app = self.root / "Space in name.app"
        self.contents = self.app / "Contents"
        self.main = self.contents / "MacOS/Main"
        self.frameworks = self.contents / "Frameworks"
        self.frameworks.mkdir(parents=True)
        self.main.parent.mkdir()
        (self.contents / "Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "Main"}))
        self.library = self.frameworks / "libLeaf.dylib"
        self.compile(self.library, "int leaf(void) { return 7; }", "-dynamiclib",
                     "-Wl,-install_name,@rpath/libLeaf.dylib")
        self.compile(self.main, "extern int leaf(void); int main(void) { return leaf() == 7 ? 0 : 1; }",
                     str(self.library), "-Wl,-rpath,@executable_path/../Frameworks")

    def compile(self, target, code, *args):
        target.parent.mkdir(parents=True, exist_ok=True)
        source = self.root / "input.c"
        source.write_text(code)
        subprocess.run(["xcrun", "clang", str(source), "-o", str(target),
                        "-Wl,-headerpad_max_install_names", *args],
                       check=True, capture_output=True)

    def edit(self, image, *args):
        subprocess.run(["install_name_tool", *args, str(image)], check=True, capture_output=True)

    def audit(self):
        return subprocess.run(["sh", str(AUDIT), str(self.app)], capture_output=True, text=True)

    def rejected(self, needle):
        result = self.audit()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(needle, result.stderr)

    def test_accepts_relocatable_rpath_bundle_with_spaces(self):
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)
        destination = self.root / "moved" / self.app.name
        destination.parent.mkdir()
        shutil.move(self.app, destination)
        self.app = destination
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_missing_relative_dependency(self):
        self.library.unlink()
        self.rejected("unresolved")

    def test_rejects_dependency_without_required_rpath(self):
        self.edit(self.main, "-delete_rpath", "@executable_path/../Frameworks")
        self.rejected("unresolved")

    def test_rejects_external_search_path_even_when_library_is_bundled(self):
        self.edit(self.main, "-add_rpath", str(self.root / "outside"))
        self.rejected("external runpath")

    def test_rejects_loader_path_escape(self):
        outside = self.root / "outside.dylib"
        shutil.copyfile(self.library, outside)
        self.edit(self.main, "-change", "@rpath/libLeaf.dylib", "@loader_path/../../../outside.dylib")
        self.rejected("escapes")

    def test_rejects_symlinked_dependency_outside_bundle(self):
        outside = self.root / "outside.dylib"
        shutil.move(self.library, outside)
        self.library.symlink_to(outside)
        self.rejected("symlink")

    def test_rejects_non_macho_payload_at_dependency_path(self):
        self.library.write_text("not a library")
        self.rejected("not a Mach-O")

    def test_ignores_dylib_install_id_as_a_dependency(self):
        self.edit(self.library, "-id", "@rpath/unused-install-id.dylib")
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_accepts_system_framework_even_when_os_redirects_to_cryptex(self):
        self.compile(self.main, "int main(void) { return 0; }", "-framework", "WebKit")
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_traversal_disguised_as_system_library(self):
        self.edit(self.main, "-change", "@rpath/libLeaf.dylib", "/usr/lib/../../private/libLeaf.dylib")
        self.rejected("escapes")

    def test_rejects_external_install_id_without_treating_id_as_import(self):
        self.edit(self.library, "-id", "/opt/homebrew/lib/libLeaf.dylib")
        self.rejected("external install ID")

    def test_rejects_current_directory_relative_search_path(self):
        self.edit(self.main, "-add_rpath", "../Frameworks")
        self.rejected("unsupported relative path")

    def test_rejects_wrong_architecture_at_dependency_path(self):
        arch = subprocess.check_output(["lipo", "-archs", str(self.main)], text=True).strip()
        other = "x86_64" if arch == "arm64" else "arm64"
        self.compile(self.library, "int leaf(void) { return 7; }", "-dynamiclib", "-arch", other,
                     "-Wl,-install_name,@rpath/libLeaf.dylib")
        self.rejected("mixed architecture")

    def test_rpath_order_checks_first_payload_instead_of_skipping_invalid_one(self):
        shadow = self.frameworks / "shadow"
        shadow.mkdir()
        (shadow / "libLeaf.dylib").write_text("invalid first candidate")
        self.edit(self.main, "-rpath", "@executable_path/../Frameworks", "@executable_path/../Frameworks/shadow")
        self.edit(self.main, "-add_rpath", "@executable_path/../Frameworks")
        self.rejected("not a Mach-O")

    def test_inherited_loader_relative_rpath_resolves_transitive_library(self):
        child = self.frameworks / "nested/libChild.dylib"
        self.compile(child, "int child(void) { return 7; }", "-dynamiclib",
                     "-Wl,-install_name,@rpath/libChild.dylib")
        self.compile(self.library, "extern int child(void); int leaf(void) { return child(); }",
                     "-dynamiclib", str(child), "-Wl,-install_name,@rpath/libLeaf.dylib",
                     "-Wl,-rpath,@loader_path/nested")
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)
        child.unlink()
        self.rejected("libChild.dylib")

    def test_reexported_library_is_part_of_closure(self):
        child = self.frameworks / "libChild.dylib"
        self.compile(child, "int child(void) { return 7; }", "-dynamiclib",
                     "-Wl,-install_name,@rpath/libChild.dylib")
        self.compile(self.library, "int leaf(void) { return 7; }", "-dynamiclib",
                     "-Wl,-reexport_library," + str(child), "-Wl,-install_name,@rpath/libLeaf.dylib")
        self.assertEqual(self.audit().returncode, 0)
        child.unlink()
        self.rejected("libChild.dylib")

    def test_dependency_cycle_terminates_without_skipping_other_imports(self):
        child = self.frameworks / "libChild.dylib"
        self.compile(child, "extern int leaf(void); int child(void) { return leaf(); }",
                     "-dynamiclib", str(self.library), "-Wl,-install_name,@rpath/libChild.dylib")
        self.compile(self.library, "extern int child(void); int leaf(void) { return child(); }",
                     "-dynamiclib", str(child), "-Wl,-install_name,@rpath/libLeaf.dylib")
        self.assertEqual(self.audit().returncode, 0)
        self.edit(child, "-change", "@rpath/libLeaf.dylib", "@rpath/missing-cycle.dylib")
        self.rejected("missing-cycle.dylib")

    def test_plugin_dependency_is_audited_even_when_not_linked_from_main(self):
        plugin = self.contents / "PlugIns/plugin.dylib"
        self.compile(plugin, "extern int leaf(void); int plugin(void) { return leaf(); }",
                     "-bundle", str(self.library))
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.edit(plugin, "-change", "@rpath/libLeaf.dylib", "@rpath/missing-plugin.dylib")
        self.rejected("missing-plugin.dylib")

    def test_helper_uses_its_own_executable_directory(self):
        helper = self.contents / "Frameworks/Helper.app/Contents/MacOS/Helper"
        self.compile(helper, "extern int leaf(void); int main(void) { return leaf(); }",
                     str(self.library), "-Wl,-rpath,@executable_path/../../..")
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.edit(helper, "-rpath", "@executable_path/../../..", "@executable_path/../Frameworks")
        self.rejected("Helper")

    def test_rejects_missing_non_system_weak_dependency(self):
        self.compile(self.main, "int main(void) { return 0; }", "-weak_library", str(self.library),
                     "-Wl,-rpath,@executable_path/../Frameworks")
        self.library.unlink()
        self.rejected("unresolved")

    def test_json_inventory_records_hashes_and_resolved_edges_without_mutation(self):
        import hashlib
        before = {p.relative_to(self.app).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in self.app.rglob("*") if p.is_file()}
        result = subprocess.run([sys.executable, str(ROOT / "macos/audit-bundle-closure.py"),
                                 str(self.app), "--json"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        inventory = json.loads(result.stdout)
        self.assertEqual(len(inventory["images"]), 2)
        self.assertEqual(inventory["images"]["Contents/MacOS/Main"]["sha256"], before["Contents/MacOS/Main"])
        edges = [e for e in inventory["edges"] if e["dependency"] == "@rpath/libLeaf.dylib"]
        self.assertEqual(edges[0]["target"], "Contents/Frameworks/libLeaf.dylib")
        self.assertEqual(edges[0]["executable"], "Contents/MacOS/Main")
        self.assertEqual(before, {p.relative_to(self.app).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in self.app.rglob("*") if p.is_file()})


if __name__ == "__main__":
    unittest.main()
