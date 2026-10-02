#!/usr/bin/env python3
"""Offline public-CLI tests; use a verified Sparkle signer and ephemeral keys."""
import base64
import copy
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[3]
TOOLS = ROOT / "macos/updater"
SPARKLE = "http://www.andymatuschak.org/xml-namespaces/sparkle"


class ReleaseFeeds(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.signer = os.environ.get("EISKALT_SPARKLE_SIGNER")
        cls.openssl = os.environ.get("OPENSSL", shutil.which("openssl"))
        if not cls.signer or not cls.openssl:
            raise unittest.SkipTest("Set EISKALT_SPARKLE_SIGNER to the verified pinned signer and OPENSSL to OpenSSL 3")
        cls.fixture = tempfile.TemporaryDirectory(prefix="eiskalt-feed-keys-")
        cls.addClassCleanup(cls.fixture.cleanup)
        cls.keys = Path(cls.fixture.name)
        cls.keys.chmod(0o700)
        private = cls.keys / "key.pem"
        subprocess.run([cls.openssl, "genpkey", "-algorithm", "ED25519", "-out", str(private)], check=True, capture_output=True)
        private.chmod(0o600)
        der = subprocess.check_output([cls.openssl, "pkey", "-in", str(private), "-outform", "DER"])
        public = subprocess.check_output([cls.openssl, "pkey", "-in", str(private), "-pubout", "-outform", "DER"])
        cls.key = cls.keys / "seed.b64"
        cls.key.write_bytes(base64.b64encode(der[-32:])); cls.key.chmod(0o600)
        cls.public = base64.b64encode(public[-32:]).decode("ascii")
        # Only fixture keys are passed to the real pinned SDK. Production tools
        # receive a trusted executable path and use its Keychain defaults.
        cls.wrapper = cls.keys / "fixture-signer"
        cls.wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cls.signer) + " --ed-key-file " + shlex.quote(str(cls.key)) + ' "$@"\n')
        cls.wrapper.chmod(0o700)
        cls.assets = []
        for flavor, arch, minimum, suffix in (
            ("intel", "x86_64", "14.0", "macOS14-plus-Intel-x86_64"),
            ("apple-silicon", "arm64", "26.0", "macOS26-plus-AppleSilicon-arm64"),
        ):
            name = "EiskaltDCpp-3.0.0-pre.1-" + suffix + ".zip"
            path = cls.keys / name
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("EiskaltDC++.app/Contents/fixture.txt", flavor + " crypto fixture, not a release bundle")
            signature = subprocess.check_output([str(cls.wrapper), "-p", str(path)], text=True).strip()
            cls.assets.append((path.read_bytes(), {
                "flavor": flavor, "arch": arch, "minimum_os": minimum,
                "filename": name, "url": "https://github.com/Delitants/eiskaltdcpp/releases/download/v3.0.0-pre.1/" + name,
                "size": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "ed_signature": signature,
            }))

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eiskalt-release-feeds-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.root.chmod(0o700)
        self.manifest = {
            "schema_version": 1, "version": "3.0.0-pre.1", "build_number": 30000001,
            "channel": "preview", "bundle_id": "com.github.eiskaltdcpp",
            "notes": '<img src="https://invalid.test/notes"> & plain notes\r\nNext\rLine\t' + chr(0x1f642),
            "assets": [copy.deepcopy(metadata) for _, metadata in self.assets],
        }
        for data, metadata in self.assets:
            (self.root / metadata["filename"]).write_bytes(data)
        self.path = self.root / "manifest.json"
        self.output = self.root / "feeds"
        self.write_manifest()

    def write_manifest(self):
        self.path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def cli(self, name, *arguments):
        script = TOOLS / name
        self.assertTrue(script.is_file(), "Offline release-feed tool has not been implemented: " + name)
        return subprocess.run([sys.executable, str(script), *map(str, arguments)], text=True, capture_output=True, timeout=30)

    def make(self, *extra):
        return self.cli("make-feeds.py", "--manifest", self.path, "--output", self.output,
                        "--signer", self.wrapper, *extra)

    def verify(self, *extra, public=None):
        return self.cli("verify-feeds.py", "--manifest", self.path, "--feeds", self.output,
                        "--public-key", self.public if public is None else public, "--openssl", self.openssl, *extra)

    def generated(self):
        result = self.make()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_pair_round_trips_with_actual_sparkle_and_public_key_only_verifier(self):
        self.generated()
        self.assertEqual(sorted(p.name for p in self.output.iterdir()), ["apple-silicon.xml", "intel.xml"])
        result = self.verify(); self.assertEqual(result.returncode, 0, result.stderr)
        for name in ("intel.xml", "apple-silicon.xml"):
            path = self.output / name
            result = subprocess.run([str(self.wrapper), "--verify", str(path)], capture_output=True)
            self.assertEqual(result.returncode, 0)
            root = ET.parse(path).getroot()
            self.assertEqual(root.findtext("channel/item/{" + SPARKLE + "}version"), "30000001")
            self.assertEqual(root.findtext("channel/item/{" + SPARKLE + "}shortVersionString"), "3.0.0-pre.1")
            self.assertEqual(root.findtext("channel/item/description"), self.manifest["notes"])
        self.export_public_fixture("preview")

    def export_public_fixture(self, channel):
        destination = os.environ.get("EISKALT_PUBLICATION_QA")
        if not destination:
            return
        target = Path(destination) / channel
        target.mkdir(mode=0o700)
        shutil.copytree(self.output, target / "feeds")
        for source in (self.path, *(self.root / a["filename"] for a in self.manifest["assets"])):
            shutil.copyfile(source, target / source.name)
            (target / source.name).chmod(0o600)
        (target / "public-key.txt").write_text(self.public, encoding="ascii")
        (target / "public-key.txt").chmod(0o600)

    def test_stable_channel_omits_preview_marker(self):
        self.manifest["channel"] = "stable"
        self.write_manifest()
        self.generated()
        result = self.verify(); self.assertEqual(result.returncode, 0, result.stderr)
        for path in self.output.iterdir():
            self.assertIsNone(ET.parse(path).getroot().find("channel/item/{" + SPARKLE + "}channel"))
        self.export_public_fixture("stable")

    def test_plain_notes_preserve_carriage_returns_and_unicode(self):
        self.manifest["notes"] = "First\r\nSecond\rThird\t<&> " + chr(0x1f642)
        self.write_manifest()
        self.generated()
        result = self.verify(); self.assertEqual(result.returncode, 0, result.stderr)
        for path in self.output.iterdir():
            self.assertEqual(ET.parse(path).getroot().findtext("channel/item/description"), self.manifest["notes"])

    def test_repeated_generation_is_deterministic_and_never_overwrites(self):
        self.generated()
        before = {p.name: p.read_bytes() for p in self.output.iterdir()}
        self.assertNotEqual(self.make().returncode, 0)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.output.iterdir()})
        second = self.root / "other"
        result = self.cli("make-feeds.py", "--manifest", self.path, "--output", second, "--signer", self.wrapper)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(before, {p.name: p.read_bytes() for p in second.iterdir()})

    @unittest.skipUnless(sys.platform == "darwin", "macOS exclusive directory promotion")
    def test_exclusive_pair_promotion_preserves_an_existing_empty_directory(self):
        sys.path.insert(0, str(TOOLS))
        import release_feeds
        promote = getattr(release_feeds, "publish_directory", None)
        self.assertTrue(callable(promote), "Exclusive pair promotion has not been implemented")
        staged = self.root / "stage"; staged.mkdir()
        (staged / "intel.xml").write_bytes(b"new feed")
        self.output.mkdir()
        inode = self.output.stat().st_ino
        with self.assertRaises(OSError):
            promote(staged, self.output)
        self.assertEqual(self.output.stat().st_ino, inode)
        self.assertEqual(list(self.output.iterdir()), [])
        self.assertEqual((staged / "intel.xml").read_bytes(), b"new feed")

    def test_second_feed_signing_failure_leaves_no_partial_pair_and_redacts_errors(self):
        failing = self.root / "failing-signer"
        failing.write_text('#!/bin/sh\ncase "$1" in */intel.xml) echo "private-key=SECRET" >&2; exit 1;; esac\nexec '
                           + shlex.quote(str(self.wrapper)) + ' "$@"\n')
        failing.chmod(0o700)
        result = self.cli("make-feeds.py", "--manifest", self.path, "--output", self.output, "--signer", failing)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("SECRET", result.stdout + result.stderr)
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob(".eiskalt-feeds-*")), [])

    def test_invalid_manifest_is_rejected_before_signer_runs(self):
        base = copy.deepcopy(self.manifest)
        mutations = [
            lambda m: m.update(schema_version=2), lambda m: m.update(build_number=True),
            lambda m: m.update(build_number=0), lambda m: m.update(build_number=9223372036854775808),
            lambda m: m.update(version="../3"), lambda m: m.update(channel="nightly"),
            lambda m: m.update(bundle_id="org.other.App"), lambda m: m.update(signer="touch sentinel"),
            lambda m: m.update(notes="x" * 16385), lambda m: m.update(notes="\x00"),
            lambda m: m.update(notes=chr(0xfffe)), lambda m: m.update(notes=chr(0xffff)),
            lambda m: m["assets"].pop(), lambda m: m["assets"].append(copy.deepcopy(m["assets"][0])),
            lambda m: m["assets"][0].update(arch="arm64"),
            lambda m: m["assets"][1].update(flavor="intel"),
            lambda m: m["assets"][0].update(minimum_os="14.1"),
            lambda m: m["assets"][0].update(filename="../escape.zip"),
            lambda m: m["assets"][0].update(filename="wrong.zip"),
            lambda m: m["assets"][0].update(size=536870913),
            lambda m: m["assets"][0].update(ed_signature=""),
            lambda m: m["assets"][0].update(url=m["assets"][0]["url"].replace("https:", "http:")),
            lambda m: m["assets"][0].update(url=m["assets"][0]["url"] + "?token=secret"),
            lambda m: m["assets"][0].update(url=m["assets"][0]["url"].replace("github.com", "github.com.evil.test")),
            lambda m: m["assets"][0].update(url=m["assets"][0]["url"].replace("/v3.0.0-pre.1/", "/../")),
        ]
        sentinel = self.root / "sentinel"
        signer = self.root / "should-not-run"
        signer.write_text("#!/bin/sh\ntouch " + shlex.quote(str(sentinel)) + "\nexit 1\n"); signer.chmod(0o700)
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index):
                sentinel.unlink(missing_ok=True)
                self.manifest = copy.deepcopy(base); mutate(self.manifest); self.write_manifest()
                result = self.cli("make-feeds.py", "--manifest", self.path, "--output", self.output, "--signer", signer)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(sentinel.exists()); self.assertFalse(self.output.exists())

    def test_changed_or_missing_assets_and_signatures_are_refused(self):
        for field, replacement in (("size", 1), ("sha256", "0" * 64),
                                   ("ed_signature", base64.b64encode(b"x" * 64).decode())):
            with self.subTest(field=field):
                original = self.manifest["assets"][0][field]
                self.manifest["assets"][0][field] = replacement; self.write_manifest()
                self.assertNotEqual(self.make().returncode, 0); self.assertFalse(self.output.exists())
                self.manifest["assets"][0][field] = original
        self.write_manifest()
        asset = self.root / self.manifest["assets"][0]["filename"]
        asset.unlink(); self.assertNotEqual(self.make().returncode, 0)
        asset.symlink_to(self.keys / self.manifest["assets"][0]["filename"])
        self.assertNotEqual(self.make().returncode, 0); self.assertFalse(self.output.exists())

    def test_unsigned_tampered_and_resigned_conflicting_feeds_are_refused(self):
        self.generated()
        feed = self.output / "intel.xml"; original = feed.read_bytes()
        for data in (original[:-10], original.split(b"<!-- sparkle-signatures:")[0],
                     original.replace(b"30000001", b"30000002")):
            with self.subTest(length=len(data)):
                feed.write_bytes(data); self.assertNotEqual(self.verify().returncode, 0)
        feed.write_bytes(original.replace(b"3.0.0-pre.1", b"3.0.0-pre.2"))
        subprocess.run([str(self.wrapper), str(feed)], check=True, capture_output=True)
        self.assertNotEqual(self.verify().returncode, 0)
        feed.write_bytes(original)
        self.assertNotEqual(self.verify(public=base64.b64encode(b"x" * 32).decode()).returncode, 0)
        self.assertNotEqual(self.verify(public=self.public + "\n").returncode, 0)

    def test_verifier_rechecks_archive_bytes_not_just_feed_signature(self):
        self.generated()
        asset = self.root / self.manifest["assets"][1]["filename"]
        asset.write_bytes(asset.read_bytes()[:-1] + b"x")
        self.assertNotEqual(self.verify().returncode, 0)

    def test_symlinked_manifest_or_feed_is_refused_without_following_it(self):
        real_manifest = self.root / "real-manifest.json"
        self.path.rename(real_manifest); self.path.symlink_to(real_manifest)
        self.assertNotEqual(self.make().returncode, 0)
        self.path.unlink(); real_manifest.rename(self.path); self.generated()
        feed = self.output / "intel.xml"; real_feed = self.root / "real-feed.xml"
        feed.rename(real_feed); feed.symlink_to(real_feed)
        self.assertNotEqual(self.verify().returncode, 0)

    def test_numeric_build_must_increase_against_previous_manifest(self):
        previous = self.root / "previous.json"
        previous.write_text(json.dumps(self.manifest))
        self.assertNotEqual(self.make("--previous-manifest", previous).returncode, 0)
        old = copy.deepcopy(self.manifest); old["build_number"] = 30000000
        previous.write_text(json.dumps(old))
        result = self.make("--previous-manifest", previous)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.verify("--previous-manifest", previous).returncode, 0)

    def test_duplicate_json_fields_and_missing_feed_are_refused(self):
        self.path.write_text('{"schema_version": 1, "schema_version": 2}')
        self.assertNotEqual(self.make().returncode, 0)
        self.write_manifest(); self.generated()
        (self.output / "intel.xml").unlink()
        self.assertNotEqual(self.verify().returncode, 0)


if __name__ == "__main__":
    unittest.main()
