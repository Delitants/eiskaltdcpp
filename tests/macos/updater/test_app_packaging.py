#!/usr/bin/env python3
"""Consume real audited Sparkle stages into disposable signed build bundles."""
import base64
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

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "macos/updater/package-framework.py"
PREFIX = "https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/"
KEY = base64.b64encode(bytes(range(1, 33))).decode()


def digest_tree(root):
    records = []
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            records.append([name, "link", os.readlink(path)])
        elif path.is_dir():
            records.append([name, "dir"])
        else:
            records.append([name, "file", bool(path.stat().st_mode & 0o111),
                            hashlib.sha256(path.read_bytes()).hexdigest()])
    return hashlib.sha256(json.dumps(records, ensure_ascii=True, separators=(",", ":")).encode()).hexdigest()


class AppPackaging(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stages = {}
        for flavor, variable in (("intel", "EISKALT_SPARKLE_INTEL_STAGE"),
                                 ("apple-silicon", "EISKALT_SPARKLE_ARM_STAGE")):
            if not os.environ.get(variable):
                raise unittest.SkipTest("Provide audited Intel and ARM stages for actual packaging QA")
            cls.stages[flavor] = Path(os.environ[variable]).resolve()
        cls.source_hashes = {name: digest_tree(path) for name, path in cls.stages.items()}

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eiskalt-app-packaging-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        if os.environ.get("EISKALT_PACKAGE_DIAGNOSTICS"):
            self.addCleanup(shutil.copytree, self.root,
                            Path(os.environ["EISKALT_PACKAGE_DIAGNOSTICS"]) / self._testMethodName,
                            dirs_exist_ok=True, symlinks=True)

    def tearDown(self):
        self.assertEqual({name: digest_tree(path) for name, path in self.stages.items()}, self.source_hashes)

    def fixture(self, flavor="apple-silicon", name="Fixture"):
        arch, floor, feed = (("x86_64", "14.0", "intel.xml") if flavor == "intel"
                            else ("arm64", "26.0", "apple-silicon.xml"))
        app = self.root / (name + ".app")
        contents = app / "Contents"
        (contents / "MacOS").mkdir(parents=True)
        plugin = contents / "PlugIns/platforms/libqcocoa.dylib"
        plugin.parent.mkdir(parents=True)
        source = self.root / "fixture.c"
        source.write_text("int main(void) { return 0; }\n")
        for dest, extra in ((contents / "MacOS/Fixture", []), (plugin, ["-dynamiclib"])):
            subprocess.run(["xcrun", "clang", "-arch", arch, "-mmacosx-version-min=" + floor,
                            *extra, str(source), "-o", str(dest)], check=True, capture_output=True)
        info = {"CFBundleExecutable": "Fixture", "CFBundleIdentifier": "com.github.eiskaltdcpp",
                "CFBundleVersion": "1", "CFBundleShortVersionString": "3.0.0-pre.1",
                "CFBundlePackageType": "APPL", "LSMinimumSystemVersion": floor,
                "SUPublicEDKey": KEY, "SUFeedURL": PREFIX + feed,
                "SURequireSignedFeed": True, "SUVerifyUpdateBeforeExtraction": True,
                "SUSignedFeedFailureExpirationInterval": 0, "SUEnableAutomaticChecks": False,
                "SUAutomaticallyUpdate": False, "SUEnableSystemProfiling": False,
                "SUEnableDownloaderService": False, "SUEnableInstallerLauncherService": True,
                "NSAppTransportSecurity": {"NSAllowsLocalNetworking": True}}
        (contents / "Info.plist").write_bytes(plistlib.dumps(info))
        subprocess.run(["codesign", "--force", "--sign", "-", str(plugin)], check=True, capture_output=True)
        subprocess.run(["codesign", "--force", "--sign", "-", str(app)], check=True, capture_output=True)
        return app

    def cli(self, app=None, stage=None, flavor="apple-silicon", digest=None, *extra):
        stage = stage or self.stages[flavor]
        arguments = [sys.executable, str(TOOL), "--stage", str(stage), "--profile", flavor,
                     "--stage-sha256", digest or digest_tree(stage)]
        arguments += (["--app", str(app), "--build-root", str(self.root)] if app else ["--audit-stage"])
        return subprocess.run([*arguments, *extra], text=True, capture_output=True)

    def test_both_flavors_preserve_helpers_notices_and_cocoa_then_seal_app(self):
        for flavor in self.stages:
            app = self.fixture(flavor, flavor)
            cocoa = app / "Contents/PlugIns/platforms/libqcocoa.dylib"
            before = cocoa.read_bytes()
            result = self.cli(app, flavor=flavor)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(cocoa.read_bytes(), before)
            copied = app / "Contents/Frameworks/Sparkle.framework"
            original = self.stages[flavor] / "Contents/Frameworks/Sparkle.framework"
            self.assertEqual(digest_tree(copied), digest_tree(original))
            for name in ("LICENSE", "provenance.json"):
                relative = Path("Contents/Resources/ThirdPartyNotices/Sparkle") / name
                self.assertEqual((app / relative).read_bytes(), (self.stages[flavor] / relative).read_bytes())
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)],
                           check=True, capture_output=True)
            receipt = json.loads((app / "Contents/Resources/ThirdPartyNotices/EiskaltUpdaterPackaging.json").read_text())
            self.assertEqual(receipt["stage_sha256"], digest_tree(self.stages[flavor]))
            self.assertEqual(receipt["cocoa_sha256"], hashlib.sha256(before).hexdigest())
            # Repeat safely over exactly matching owned framework/notice bytes.
            result = self.cli(app, flavor=flavor)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_unsigned_cocoa_is_signed_without_changing_its_loadable_bytes(self):
        app = self.fixture()
        cocoa = app / "Contents/PlugIns/platforms/libqcocoa.dylib"
        subprocess.run(["codesign", "--remove-signature", str(cocoa)], check=True, capture_output=True)
        before = cocoa.read_bytes()
        result = self.cli(app)
        self.assertEqual(result.returncode, 0, result.stderr)
        subprocess.run(["codesign", "--verify", "--strict", str(cocoa)], check=True, capture_output=True)
        stripped = self.root / "stripped.dylib"
        shutil.copy2(cocoa, stripped)
        subprocess.run(["codesign", "--remove-signature", str(stripped)], check=True, capture_output=True)
        self.assertEqual(stripped.read_bytes(), before)
        receipt = json.loads((app / "Contents/Resources/ThirdPartyNotices/EiskaltUpdaterPackaging.json").read_text())
        self.assertEqual(receipt["cocoa_code_sha256"], hashlib.sha256(before).hexdigest())

    def test_packaging_recipe_hash_distinguishes_field_separator_aliases(self):
        source = self.root / "source"
        source.mkdir()
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.23)
project(RecipeFixture LANGUAGES NONE)
set(EISKALT_MAC_UPDATER_PACKAGE ON)
set(EISKALT_MAC_UPDATER_METADATA ON)
set(CMAKE_OSX_ARCHITECTURES arm64)
include("${REPO}/cmake/MacUpdaterPackaging.cmake")
file(WRITE "${PROJECT_BINARY_DIR}/recipe-hash" "${EISKALT_MAC_BUNDLE_PACKAGING_HASH}")
''')
        stages = [self.root / "stage|second", self.root / "stage"]
        for stage in stages:
            shutil.copytree(self.stages["apple-silicon"], stage, symlinks=True)
        hashes = []
        for index, (stage, identity) in enumerate(zip(stages, ("third", "second|third"))):
            build = self.root / ("build" + str(index))
            result = subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", "Unix Makefiles",
                                     "-DREPO=" + str(ROOT), "-DEISKALT_MAC_UPDATER_STAGE=" + str(stage),
                                     "-DEISKALT_MAC_UPDATER_STAGE_SHA256=" + digest_tree(stage),
                                     "-DEISKALT_MAC_UPDATER_CODESIGN_IDENTITY=" + identity],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            hashes.append((build / "recipe-hash").read_text())
        self.assertNotEqual(hashes[0], hashes[1])

    def test_closure_audit_change_invalidates_recipe_even_with_equal_timestamp(self):
        source = self.root / "copied-recipe"
        for folder in ("cmake", "macos/updater"):
            (source / folder).mkdir(parents=True)
        for name in ("cmake/MacUpdaterPackaging.cmake", "macos/release.py",
                     "macos/check-bundle-external-links.sh", "macos/audit-bundle-closure.py",
                     "macos/updater/package-framework.py", "macos/updater/release_feeds.py",
                     "macos/updater/sparkle.lock.json"):
            shutil.copyfile(ROOT / name, source / name)
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.23)
project(ClosureRecipe NONE)
set(EISKALT_MAC_UPDATER_PACKAGE ON)
set(EISKALT_MAC_UPDATER_METADATA ON)
set(CMAKE_OSX_ARCHITECTURES arm64)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/MacUpdaterPackaging.cmake")
file(WRITE "${PROJECT_BINARY_DIR}/recipe" "${EISKALT_MAC_BUNDLE_PACKAGING_HASH}")
''')
        build = self.root / "recipe-build"
        stage = self.stages["apple-silicon"]

        def configure():
            result = subprocess.run(["cmake", "-S", str(source), "-B", str(build),
                                     "-DEISKALT_MAC_UPDATER_STAGE=" + str(stage),
                                     "-DEISKALT_MAC_UPDATER_STAGE_SHA256=" + digest_tree(stage)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return (build / "recipe").read_text()

        before = configure()
        audit = source / "macos/audit-bundle-closure.py"
        old = audit.stat()
        audit.write_text(audit.read_text() + "\n# changed audit recipe\n")
        os.utime(audit, ns=(old.st_atime_ns, old.st_mtime_ns))
        self.assertNotEqual(configure(), before)

    def test_invalid_stage_is_rejected_before_application_mutation(self):
        app = self.fixture()
        original = digest_tree(app)
        source = self.stages["apple-silicon"]
        for change in ("digest", "binary", "signature", "notice", "helper", "provenance", "link", "extra"):
            with self.subTest(change=change):
                stage = self.root / change
                shutil.copytree(source, stage, symlinks=True)
                wanted = digest_tree(stage)
                framework = stage / "Contents/Frameworks/Sparkle.framework"
                if change == "binary":
                    with (framework / "Versions/B/Sparkle").open("ab") as stream:
                        stream.write(b"changed")
                elif change == "signature":
                    binary = framework / "Versions/B/Sparkle"
                    subprocess.run(["codesign", "--remove-signature", str(binary)], check=True, capture_output=True)
                    receipt = stage / "Contents/Resources/ThirdPartyNotices/Sparkle/provenance.json"
                    data = json.loads(receipt.read_text())
                    data["binaries"]["Versions/B/Sparkle"] = hashlib.sha256(binary.read_bytes()).hexdigest()
                    receipt.write_text(json.dumps(data))
                elif change == "notice":
                    (stage / "Contents/Resources/ThirdPartyNotices/Sparkle/LICENSE").unlink()
                elif change == "helper":
                    (framework / "Versions/B/Autoupdate").unlink()
                elif change == "provenance":
                    receipt = stage / "Contents/Resources/ThirdPartyNotices/Sparkle/provenance.json"
                    data = json.loads(receipt.read_text())
                    data["source_sha256"] = "0" * 64
                    receipt.write_text(json.dumps(data))
                elif change == "link":
                    (framework / "escape").symlink_to(app)
                elif change == "extra":
                    (stage / "unexpected").write_text("unapproved content")
                else:
                    wanted = "0" * 64
                # Re-pinning an invalid notice/provenance/tree must not waive validation.
                if change in ("signature", "notice", "helper", "provenance", "link", "extra"):
                    wanted = digest_tree(stage)
                result = self.cli(app, stage, digest=wanted)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("refused", result.stderr.lower())
                self.assertEqual(digest_tree(app), original)

    def test_wrong_cpu_and_unsafe_metadata_preserve_application(self):
        app = self.fixture()
        original = digest_tree(app)
        result = self.cli(app, stage=self.stages["intel"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("architecture", result.stderr.lower())
        self.assertEqual(digest_tree(app), original)
        info_path = app / "Contents/Info.plist"
        info = plistlib.loads(info_path.read_bytes())
        info["SURequireSignedFeed"] = False
        info_path.write_bytes(plistlib.dumps(info))
        original = digest_tree(app)
        result = self.cli(app)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("metadata", result.stderr.lower())
        self.assertEqual(digest_tree(app), original)

    def test_outside_or_symlinked_application_and_conflicting_destination_are_refused(self):
        app = self.fixture()
        original = digest_tree(app)
        outside_root = self.root / "owned"
        outside_root.mkdir()
        result = self.cli(app, None, "apple-silicon", None, "--build-root", str(outside_root))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("build", result.stderr.lower())
        link = self.root / "Link.app"
        link.symlink_to(app)
        result = self.cli(link)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(digest_tree(app), original)
        target = app / "Contents/Frameworks/Sparkle.framework"
        target.mkdir(parents=True)
        (target / "preserve").write_text("not ours")
        original = digest_tree(app)
        result = self.cli(app)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("existing", result.stderr.lower())
        self.assertEqual(digest_tree(app), original)

    def test_cmake_default_off_and_enabled_make_xcode_packaging(self):
        source = self.root / "source"
        source.mkdir()
        (source / "main.c").write_text("int main(void) { return 0; }\n")
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.10)
project(PackageFixture LANGUAGES C CXX)
cmake_policy(SET CMP0112 NEW)
add_executable(${PROJECT_NAME} MACOSX_BUNDLE main.c)
add_library(CocoaFixture MODULE main.c)
set_target_properties(CocoaFixture PROPERTIES PREFIX "lib" OUTPUT_NAME qcocoa SUFFIX ".dylib")
add_dependencies(${PROJECT_NAME} CocoaFixture)
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${REPO}/eiskaltdcpp-qt/Info.plist.in"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
set(APP_VERSION "3.0.0-pre.1")
set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.eiskaltdcpp")
set(MACOSX_BUNDLE_BUNDLE_NAME "PackageFixture")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}")
include("${REPO}/cmake/MacBundleVersion.cmake")
include("${REPO}/cmake/MacOpenHandlers.cmake")
include("${REPO}/cmake/MacUpdaterMetadata.cmake")
include("${REPO}/cmake/MacUpdaterPackaging.cmake")
include("${REPO}/cmake/MacBundleMetadata.cmake")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/PlugIns/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:CocoaFixture>"
        "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/PlugIns/platforms/libqcocoa.dylib"
    COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/PlugIns/platforms/libqcocoa.dylib"
    VERBATIM)
if(EISKALT_MAC_UPDATER_PACKAGE)
    eiskalt_package_updater(${PROJECT_NAME})
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>" VERBATIM)
endif()
''')
        stage = self.root / "stage"
        shutil.copytree(self.stages["apple-silicon"], stage, symlinks=True)

        def configure(build, generator, enabled="ON", metadata="ON", digest=None, stage_path=None):
            return subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", generator,
                                   "-DREPO=" + str(ROOT), "-DCMAKE_BUILD_TYPE=Release",
                                   "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0",
                                   "-DEISKALT_MAC_UPDATER_PACKAGE=" + enabled,
                                   "-DEISKALT_MAC_UPDATER_METADATA=" + metadata,
                                   "-DEISKALT_MAC_UPDATER_PUBLIC_KEY=" + KEY,
                                   "-DEISKALT_MAC_UPDATER_FEED_URL=" + PREFIX + "apple-silicon.xml",
                                   "-DEISKALT_MAC_UPDATER_STAGE=" + str(stage_path or stage),
                                   "-DEISKALT_MAC_UPDATER_STAGE_SHA256=" + (digest or digest_tree(stage))],
                                  text=True, capture_output=True)

        def build_it(build):
            result = subprocess.run(["cmake", "--build", str(build), "--config", "Release", "-j2"],
                                    text=True, capture_output=True)
            with (build / "history.log").open("a") as log:
                log.write(result.stdout + result.stderr)
            return result

        off = self.root / "off"
        result = configure(off, "Unix Makefiles", enabled="OFF", metadata="OFF", digest="bad")
        self.assertEqual(result.returncode, 0, result.stderr)
        result = build_it(off)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(any(off.rglob("Sparkle.framework")))
        for generator in ("Unix Makefiles", "Xcode"):
            build = self.root / generator.replace(" ", "-")
            result = configure(build, generator, metadata="OFF")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("METADATA", result.stderr)
            result = configure(build, generator, digest="0" * 64)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("stage", result.stderr.lower())
            result = configure(build, generator)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = build_it(build)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            app = next(build.rglob("PackageFixture.app"))
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)],
                           check=True, capture_output=True)
            original = digest_tree(app)
            executable = app / "Contents/MacOS/PackageFixture"
            before = executable.stat().st_mtime_ns
            result = build_it(build)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(digest_tree(app), original)
            if generator == "Unix Makefiles":
                self.assertEqual(executable.stat().st_mtime_ns, before)
            # A new explicitly selected input path with the same audited bytes
            # still changes the packaging recipe generation and must reseal.
            alternate = self.root / (generator.replace(" ", "-") + "-stage")
            shutil.copytree(stage, alternate, symlinks=True)
            generation = (build / "MacBundleMetadata.cpp").read_bytes()
            result = configure(build, generator, stage_path=alternate)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotEqual((build / "MacBundleMetadata.cpp").read_bytes(), generation)
            result = build_it(build)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)],
                           check=True, capture_output=True)
            original = digest_tree(app)
            # A changed recipe leaves a pending metadata invalidation. A bad
            # stage must stop that gate too, not erase the previous executable.
            result = configure(build, generator)
            self.assertEqual(result.returncode, 0, result.stderr)
            notice = stage / "Contents/Resources/ThirdPartyNotices/Sparkle/LICENSE"
            saved = notice.read_bytes()
            notice.write_bytes(b"changed before pending relink")
            result = build_it(build)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(digest_tree(app), original)
            notice.write_bytes(saved)
            result = build_it(build)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            original = digest_tree(app)
            # The build-time audit catches tampering even when no relink is due.
            notice = stage / "Contents/Resources/ThirdPartyNotices/Sparkle/LICENSE"
            saved = notice.read_bytes()
            notice.write_bytes(b"changed")
            result = build_it(build)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(digest_tree(app), original)
            notice.write_bytes(saved)
            result = configure(build, generator, enabled="OFF")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("fresh", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()
