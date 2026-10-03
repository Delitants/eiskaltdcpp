"""Run with PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/translations."""

import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from collections import Counter


ROOT = Path(__file__).resolve().parents[2]


class CatalogRegressionTests(unittest.TestCase):
    def test_updater_reviewed_status_wording_is_unambiguous(self):
        reviewed = {
            "hu": ("updates::SettingsUpdates", "Update operation canceled.",
                   "A frissítési művelet megszakítva."),
            "es": ("updates::UnsupportedUpdateBackend", "Self-updating is not supported on this platform or build.",
                   "La instalación de actualizaciones desde la aplicación no es compatible con esta plataforma o compilación."),
            "fr": ("updates::UnsupportedUpdateBackend", "Self-updating is not supported on this platform or build.",
                   "L'installation de mises à jour par l'application n'est pas prise en charge sur cette plateforme ou dans cette version du programme."),
            "it": ("updates::UnsupportedUpdateBackend", "Self-updating is not supported on this platform or build.",
                   "L'installazione degli aggiornamenti dall'applicazione non è supportata su questa piattaforma o in questa build."),
        }
        for language, (context, source, expected) in reviewed.items():
            path = ROOT / "eiskaltdcpp-qt/translations" / f"{language}.ts"
            entries = {(c.findtext("name"), m.findtext("source")): m.findtext("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            with self.subTest(language=language):
                self.assertEqual(expected, entries[(context, source)])

    def test_public_hub_filter_and_counter_are_translated(self):
        captions = {("PublicHubs", "Hubs: %1 / %2"),
                    ("UIPublicHubs", "Filter hubs"),
                    ("UIPublicHubs", "Clear filter")}
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            for key in captions:
                with self.subTest(language=path.stem, caption=key[1]):
                    translation = entries.get(key)
                    self.assertIsNotNone(translation)
                    self.assertNotIn(translation.get("type"), {"unfinished", "obsolete", "vanished"})
                    self.assertTrue("".join(translation.itertext()).strip())
                    self.assertEqual(Counter(re.findall(r"%[12]", key[1])),
                                     Counter(re.findall(r"%[12]", "".join(translation.itertext()))))

    def test_selected_file_verification_controls_are_translated(self):
        captions = {"Recheck selected files...", "Cancel file check", "Verification",
                    "Rechecking selected files... %1%", "Canceling selected-file check..."}
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            for caption in captions:
                with self.subTest(language=path.stem, caption=caption):
                    translation = entries.get(("TorrentWindow", caption))
                    self.assertIsNotNone(translation)
                    self.assertNotIn(translation.get("type"), {"unfinished", "obsolete", "vanished"})
                    self.assertTrue("".join(translation.itertext()).strip())

    def test_dht_advertisement_controls_are_translated(self):
        captions = {"Enable DHT", "Do not advertise DHT to hubs",
                    "Applies when reconnecting to ADC/NMDC hubs. The DHT enabled setting is unchanged; this does not guarantee privacy."}
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            for caption in captions:
                with self.subTest(language=path.stem, caption=caption):
                    translation = entries.get(("UISettingsConnection", caption))
                    self.assertIsNotNone(translation)
                    self.assertNotIn(translation.get("type"), {"unfinished", "obsolete", "vanished"})
                    self.assertTrue((translation.text or "").strip())

    def test_torrent_shared_state_captions_are_complete(self):
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            for caption in ("Pause", "Resume", "Stop"):
                with self.subTest(language=path.stem, caption=caption):
                    translation = entries.get(("TorrentWindow", caption))
                    self.assertIsNotNone(translation)
                    self.assertNotIn(translation.get("type"), {"unfinished", "obsolete", "vanished"})
                    self.assertTrue((translation.text or "").strip())
            if path.stem == "zh_CN":
                self.assertEqual("对等节点", entries["TorrentWindow", "Peers"].text)

    @unittest.skipUnless(shutil.which("lupdate"), "lupdate is required")
    def test_torrent_interface_is_translated_in_every_language(self):
        sources = [ROOT / "torrent", ROOT / "eiskaltdcpp-qt/src"]
        with tempfile.TemporaryDirectory(prefix="eiskalt-torrent-catalogs-") as work:
            output = Path(work) / "torrent.ts"
            subprocess.run(["lupdate", *(str(p) for p in sources), "-no-obsolete", "-ts", str(output)],
                           check=True, capture_output=True, text=True)
            required = set()
            for context in ET.parse(output).getroot().findall("context"):
                name = context.findtext("name")
                for message in context.findall("message"):
                    source = message.findtext("source")
                    if ("Torrent" in name or source in {"Torrent", "Torrents"} or
                        any("Torrent" in loc.get("filename", "") or "/torrent/" in loc.get("filename", "")
                            for loc in message.findall("location"))):
                        required.add((name, source))
        self.assertGreater(len(required), 200)
        catalogs = sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts"))
        self.assertEqual(25, len(catalogs))
        for path in catalogs:
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context") for m in c.findall("message")}
            for key in sorted(required):
                with self.subTest(language=path.stem, context=key[0], source=key[1]):
                    translation = entries.get(key)
                    self.assertIsNotNone(translation)
                    self.assertNotIn(translation.get("type"), {"unfinished", "obsolete", "vanished"})
                    translated = "".join(translation.itertext())
                    self.assertTrue(translated.strip())
                    for literal in ("*.torrent", "%p%"):
                        if literal in key[1]:
                            self.assertIn(literal, translated)

    def test_transfer_and_tab_navigation_controls_are_translated(self):
        expected = {
            "TransferView": {"Fit to content"},
            "TransferViewModel": {"Protocol"},
            "ChatFormatBar": {"More formatting"},
            "tab_navigation::AllTabsMenu": {"All Tabs", "Search tabs by title or hostname", "No matching tabs"},
            "UIHubFrame": {"Bold", "Italic", "Underline", "Strikethrough", "Text color", "Insert link", "Color", "Link", "Code", "Emoji"},
            "UIPrivateMessage": {"Bold", "Italic", "Underline", "Strikethrough", "Text color", "Insert link", "Color", "Link", "Code", "Emoji"},
            "HubFrame": {"Image", "Emoji"},
            "PMWindow": {"Image", "Emoji"},
            "ChatEdit": {"Color", "Link", "Code", "Image"},
            "TabFrame": {"Previous tab row", "Next tab row"},
            "TabButton": {"Close"},
        }
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            for context, sources in expected.items():
                for source in sources:
                    with self.subTest(language=path.stem, context=context, source=source):
                        translation = entries.get((context, source))
                        self.assertIsNotNone(translation)
                        self.assertNotIn(translation.get("type"), {"unfinished", "obsolete", "vanished"})
                        self.assertTrue((translation.text or "").strip())

    def test_toolbar_modes_and_captions_are_translated_in_every_language(self):
        expected = {
            "MainWindow": {"Files", "Reconnect", "Connect", "Queue", "Downloaded", "Uploaded",
                           "Speed limit", "Chat", "AntiSpam", "IP filter", "Button style", "Icons only",
                           "Text only", "Text beside icons", "Text under icons", "Customize"},
            "ActionToolBar": {"More actions"},
        }
        catalogs = sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts"))
        self.assertEqual(25, len(catalogs))
        for path in catalogs:
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            for context, sources in expected.items():
                for source in sources:
                    with self.subTest(language=path.stem, context=context, source=source):
                        text = entries.get((context, source))
                        self.assertIsNotNone(text)
                        self.assertNotIn(text.get("type"), {"unfinished", "obsolete", "vanished"})
                        self.assertTrue((text.text or "").strip())

    def test_qt_format_arguments_are_preserved(self):
        token = re.compile(r"%L?(?:[1-9][0-9]?|n)")
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            for message in ET.parse(path).iter("message"):
                source = message.findtext("source") or ""
                expected = Counter(token.findall(source))
                # A percent before a number can be literal text in some languages.
                if not expected:
                    continue
                translation = message.find("translation")
                if translation is None or translation.get("type") in {"obsolete", "vanished"}:
                    continue
                forms = translation.findall("numerusform") or [translation]
                for form in forms:
                    text = "".join(form.itertext())
                    if text.strip():
                        with self.subTest(language=path.stem, source=source):
                            self.assertEqual(expected, Counter(token.findall(text)))

    @unittest.skipUnless(shutil.which("msgfmt"), "msgfmt is required")
    def test_core_catalog_formats(self):
        for path in sorted((ROOT / "dcpp/po").glob("*.po")):
            with self.subTest(language=path.stem):
                result = subprocess.run(["msgfmt", "--check", "--check-format", "-o", "/dev/null", str(path)],
                                        capture_output=True, text=True)
                self.assertEqual(0, result.returncode, result.stderr)


class ExtractionRegressionTests(unittest.TestCase):
    def test_manifest_includes_updater_sources_and_headers(self):
        manifest = ROOT / "eiskaltdcpp-qt/translations.pro"
        text = manifest.read_text().replace("\\\n", " ")
        files = set()
        for line in text.splitlines():
            match = re.match(r"(?:HEADERS|SOURCES)\s*\+?=\s*(.*)", line)
            if match:
                for pattern in match[1].split():
                    files.update(p.resolve() for p in manifest.parent.glob(pattern))
        expected = {p.resolve() for p in (ROOT / "eiskaltdcpp-qt/src/updates").glob("*")
                    if p.suffix in {".cpp", ".h", ".mm"}}
        self.assertTrue(expected)
        self.assertEqual([], sorted(str(p.relative_to(ROOT)) for p in expected - files))

    @unittest.skipUnless(shutil.which("lupdate"), "lupdate is required")
    def test_updater_settings_captions_are_extractable_in_runtime_context(self):
        with tempfile.TemporaryDirectory(prefix="eiskalt-i18n-updates-") as work:
            output = Path(work) / "updates.ts"
            subprocess.run(["lupdate", str(ROOT / "eiskaltdcpp-qt/src/updates/SettingsUpdates.cpp"),
                            str(ROOT / "eiskaltdcpp-qt/src/updates/SettingsUpdates.h"),
                            "-no-obsolete", "-ts", str(output)],
                           check=True, capture_output=True, text=True)
            keys = {(c.findtext("name"), m.findtext("source"))
                    for c in ET.parse(output).getroot().findall("context")
                    for m in c.findall("message")}
        self.assertIn(("updates::SettingsUpdates", "Last successful check: %1"), keys)
        self.assertIn(("updates::SettingsUpdates", "Available version: %1"), keys)
        self.assertIn(("updates::SettingsUpdates", "Install on Next &Quit"), keys)
        self.assertNotIn("updates", {context for context, _ in keys})

    @unittest.skipUnless(shutil.which("lupdate"), "lupdate is required")
    def test_updater_catalogs_cover_extracted_runtime_messages(self):
        sources = sorted(str(p) for p in (ROOT / "eiskaltdcpp-qt/src/updates").glob("*")
                         if p.suffix in {".cpp", ".h", ".mm"})
        with tempfile.TemporaryDirectory(prefix="eiskalt-updater-catalogs-") as work:
            output = Path(work) / "updates.ts"
            subprocess.run(["lupdate", *sources, "-no-obsolete", "-locations", "none",
                            "-ts", str(output)], check=True, capture_output=True, text=True)
            required = {(c.findtext("name"), m.findtext("source"))
                        for c in ET.parse(output).getroot().findall("context")
                        for m in c.findall("message")}
        self.assertGreaterEqual(len(required), 40)
        self.assertIn(("updates::MacUpdateBackend", "Unable to create private update staging."), required)
        tokens = re.compile(r"%(?:L?[1-9]\d*|n)")
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            entries = {}
            for context in ET.parse(path).getroot().findall("context"):
                for message in context.findall("message"):
                    translation = message.find("translation")
                    if translation is not None and translation.get("type") not in {"obsolete", "vanished"}:
                        key = (context.findtext("name"), message.findtext("source"))
                        entries.setdefault(key, []).append(translation)
            for key in sorted(required):
                with self.subTest(language=path.stem, context=key[0], source=key[1]):
                    translations = entries.get(key, [])
                    self.assertEqual(1, len(translations), "Missing or ambiguous runtime message")
                    if not translations:
                        continue
                    translation = translations[0]
                    self.assertNotEqual("unfinished", translation.get("type"))
                    text = "".join(translation.itertext())
                    self.assertTrue(text.strip())
                    self.assertEqual(Counter(tokens.findall(key[1])), Counter(tokens.findall(text)))
                    self.assertEqual(key[1].count("&"), text.count("&"))

    @unittest.skipUnless(shutil.which("lupdate"), "lupdate is required")
    def test_shared_torrent_actions_are_extractable(self):
        with tempfile.TemporaryDirectory(prefix="eiskalt-i18n-actions-") as work:
            output = Path(work) / "actions.ts"
            subprocess.run(["lupdate", str(ROOT / "eiskaltdcpp-qt/src/TorrentActionMenu.h"),
                            "-no-obsolete", "-ts", str(output)],
                           check=True, capture_output=True, text=True)
            keys = {(c.findtext("name"), m.findtext("source"))
                    for c in ET.parse(output).getroot().findall("context")
                    for m in c.findall("message")}
        self.assertEqual({("TorrentWindow", text) for text in (
            "Pause", "Resume", "Stop", "Open Download Folder",
            "Open Download Location in Finder", "Could not open the download folder.")}, keys)

    def test_manifest_includes_root_torrent_sources_and_headers(self):
        manifest = ROOT / "eiskaltdcpp-qt/translations.pro"
        text = manifest.read_text().replace("\\\n", " ")
        files = set()
        for line in text.splitlines():
            match = re.match(r"(?:HEADERS|SOURCES|FORMS)\s*\+?=\s*(.*)", line)
            if match:
                for pattern in match[1].split():
                    files.update(p.resolve() for p in manifest.parent.glob(pattern))
        expected = {p.resolve() for p in (ROOT / "torrent").glob("*")
                    if p.suffix in {".cpp", ".h"}}
        self.assertTrue(expected)
        self.assertEqual([], sorted(str(p.relative_to(ROOT)) for p in expected - files))

    @unittest.skipUnless(shutil.which("lupdate"), "lupdate is required")
    def test_torrent_extracted_contexts_match_runtime_contexts(self):
        with tempfile.TemporaryDirectory(prefix="eiskalt-i18n-test-") as work:
            output = Path(work) / "torrent.ts"
            sources = sorted(str(p) for p in (ROOT / "torrent").glob("*")
                             if p.suffix in {".cpp", ".h"})
            subprocess.run(["lupdate", *sources, "-no-obsolete", "-ts", str(output)],
                           check=True, capture_output=True, text=True)
            keys = {(c.findtext("name"), m.findtext("source"))
                    for c in ET.parse(output).getroot().findall("context")
                    for m in c.findall("message")}
        self.assertIn(("TorrentEngine", "Torrent support is disabled."), keys)
        self.assertIn(("TorrentSettings", "Invalid Torrent proxy mode."), keys)
        self.assertIn(("eiskalt::torrent::TorrentEngine",
                       "Torrent routing has not been configured."), keys)
        self.assertNotIn("eiskalt::torrent", {context for context, _ in keys})

    @unittest.skipUnless(shutil.which("xgettext"), "xgettext is required")
    def test_cmake_extracts_both_fn_plural_arguments(self):
        cmake = (ROOT / "cmake/CMakeLists.txt").read_text()
        block = cmake.split("COMMAND ${GETTEXT_XGETTEXT_EXECUTABLE}", 1)[1]
        block = block.split("--output=", 1)[0]
        keywords = re.findall(r"--keyword=[^\s]+", block)
        with tempfile.TemporaryDirectory(prefix="eiskalt-i18n-test-") as work:
            source = Path(work) / "plural.cpp"
            source.write_text('auto value = FN_("One item", "Many items", count);\n')
            result = subprocess.run(["xgettext", "--language=C++", "--from-code=UTF-8",
                                     *keywords, "--output=-", str(source)],
                                    check=True, capture_output=True, text=True)
        self.assertIn('msgid_plural "Many items"', result.stdout)


if __name__ == "__main__":
    unittest.main()
