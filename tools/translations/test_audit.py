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
    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_connection_catalog_survives_compilation(self):
        self._assert_connection_catalog_survives_compilation("ka")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_tls_choices_survive_compilation(self):
        self._assert_tls_choices_survive_compilation("ka", {
            "TLS settings": "TLS პარამეტრები",
            "Disable TLS": "TLS-ის გამორთვა",
            "Allow TLS": "TLS-ის დაშვება",
            "Require TLS": "TLS-ის აუცილებლად მოთხოვნა",
            "Allow untrusted certificate for hubs": "ჰაბებისთვის არასანდო სერტიფიკატის დაშვება",
            "Allow untrusted certificate for clients": "კლიენტებისთვის არასანდო სერტიფიკატის დაშვება",
        })

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("pl")

    def test_polish_rich_help_and_commands(self):
        self._assert_rich_help_and_commands("pl", "lub")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_mainwindow_translations(self):
        required = {
            "Kill all humans?", "Action confirm", "Match all listings", "Debug Console", "Secretary",
            "Torrent files", "Open magnet or Torrent file", "Enter a magnet link or local .torrent path:",
            "Torrent support is not ready.", "Exit", "Source code (git)", " | DHT nodes: %1",
            "DC++ core version: %1 (modified)", "Home page: ", "Ratio: <b>%1</b>",
            'Please use <a href="https://github.com/Delitants/eiskaltdcpp/issues">https://github.com/Delitants/eiskaltdcpp/issues</a> to report bugs.<br/>',
            "<b>Developers</b><br/>", '2026 <a href="mailto:admin@nlight.org.ua">Neolo</a><br/>',
            "<b>Graphic files</b><br/>", "Setup speed limits", "Torrent: %1", "Nickname required",
            "Enter a nickname in Preferences before connecting to hubs.", "Download program",
            "lead developer", "release manager", "Arch Linux maintainer", "developer", "Debian/Ubuntu maintainer",
            "translations coordinator", "macOS maintainer", "MS Windows maintainer", "creator of the logo of the project",
            "tiny updates of the logo", "Participate in the translation. It is easy:", "[No name]",
        }
        required.update(language + " translation<br/>" for language in (
            "Russian", "Belarusian", "Hungarian", "French", "Polish", "Ukrainian", "Serbian (Cyrillic)",
            "Serbian (Latin)", "Spanish", "Basque", "Bulgarian", "Slovak", "Czech", "German", "Greek",
            "Italian", "Portuguese (Brazil)", "Vietnamese", "Chinese (China)", "Swedish (Sweden)",
            "Turkish", "Danish", "Georgian"))
        self.assertEqual(59, len(required))
        path = ROOT / "eiskaltdcpp-qt/translations/pl.ts"
        def entries(catalog):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") == "MainWindow" for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-pl-mainwindow-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            original, compiled = entries(path), entries(output)
            for source in sorted(required):
                with self.subTest(source=source):
                    self.assertIn(source, original)
                    value = original[source]
                    self.assertNotIn(value.get("type"), {"unfinished", "obsolete", "vanished"})
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    self.assertEqual(Counter(re.findall(r"%\d+|%n", source)), Counter(re.findall(r"%\d+|%n", text)))
                    self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                    self.assertEqual(source[len(source.rstrip()):], text[len(text.rstrip()):])
                    for token in (".torrent", "DHT", "DC++", "Arch Linux", "Debian/Ubuntu", "macOS", "MS Windows"):
                        if token in source:
                            self.assertIn(token, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_hubs_notes_and_magnets(self):
        required = {
            "CmdDebug": {"Debug Console"}, "HashProgress": {"%1% %2 left"},
            "PublicHubs": {"Refreshing public hub list...", "Hub list downloaded... (%1)",
                           "Locally cached (as of %1) version of the hub list loaded (%2)"},
            "PublicHubsList": {"Public hub list URLs"},
            "Secretary": {"Secretary", "Clear notes", "Find in notes"},
            "UICmdDebug": {"Use comma as separator", "Filter by IP", "Lines:", "Clear now"},
            "UIFileHasher": {"Copy magnet link to clipboard", "Copy search link to clipboard"},
            "UIMagnet": {"Magnet link viewer", "Copy magnet link to clipboard", "Copy search link to clipboard"},
            "UIHubFrame": {"Press Shift+Enter for multiline input"},
            "UIPrivateMessage": {"Press Shift+Enter for multiline input"},
            "UIPublicHubs": {"Refresh"},
            "UISecretary": {"Magnet links", "Search links", "Public chat", "BitTorrent links",
                            "Filter duplicates", "Use comma as separator", "Additional keywords",
                            "Filter hubs by IP", "Lines:", "Clear now"},
        }
        exact = {"Clear notes": "Wyczyść notatki", "Find in notes": "Szukaj w notatkach",
                 "Clear now": "Wyczyść teraz", "Refresh": "Odśwież",
                 "Copy magnet link to clipboard": "Kopiuj link magnet do schowka",
                 "Copy search link to clipboard": "Kopiuj link wyszukiwania do schowka"}
        path = ROOT / "eiskaltdcpp-qt/translations/pl.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context") for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-pl-hubs-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            original, compiled = entries(path), entries(output)
            for context, sources in required.items():
                for source in sorted(sources):
                    with self.subTest(context=context, source=source):
                        self.assertIn((context, source), original)
                        value = original[context, source]
                        self.assertNotIn(value.get("type"), {"unfinished", "obsolete", "vanished"})
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(Counter(re.findall(r"%\d+|%n", source)), Counter(re.findall(r"%\d+|%n", text)))
                        self.assertEqual(source.count("%"), text.count("%"))
                        for token in ("Shift+Enter", "BitTorrent", "IP", "URL"):
                            if token in source:
                                self.assertIn(token, text)
                        if source in exact:
                            self.assertEqual(exact[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_browser_and_search_translations(self):
        required = {
            "ChatEdit": {"Address", "Select image"},
            "FileBrowserModel": {"<b>Media Info:</b><br/>", "&nbsp;&nbsp;<b>Video:</b> %1<br/>",
                                 "&nbsp;&nbsp;<b>Audio:</b> %1<br/>", "&nbsp;&nbsp;<b>Bitrate:</b> %1<br/>",
                                 "&nbsp;&nbsp;<b>Resolution:</b> %1<br/><br/>", "Bitrate", "Resolution", "Video", "Audio"},
            "SearchFrame": {"Searching for %1 ...", 'Search progress of "%1" is %2%'},
            "ShareBrowser": {"Restrictions", "Add restriction", "Remove restriction", "Share browser error: %1",
                             "Selected %1 from %2 items; ", "Enter restriction size (in GB)", "Share browser", "Matched %1 files"},
            "ShareBrowserSearch": {"Found %1 items"},
            "UIShareBrowserSearch": {"Search only files", "Search only directories", "Search all"},
            "UserListModel": {"Exact share size", "IPv6"},
            "WulforSettings": {"GUI setting %1: %2", "Change GUI setting %1 to %2"},
        }
        exact = {"Add restriction": "Dodaj ograniczenie", "Remove restriction": "Usuń ograniczenie",
                 "Search only files": "Szukaj tylko plików", "Search only directories": "Szukaj tylko katalogów",
                 "Search all": "Szukaj wszystkiego", "Enter restriction size (in GB)": "Podaj rozmiar ograniczenia (w GB)"}
        path = ROOT / "eiskaltdcpp-qt/translations/pl.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context") for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-pl-browser-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            original, compiled = entries(path), entries(output)
            for context, sources in required.items():
                for source in sorted(sources):
                    with self.subTest(context=context, source=source):
                        self.assertIn((context, source), original)
                        value = original[context, source]
                        self.assertNotIn(value.get("type"), {"unfinished", "obsolete", "vanished"})
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(Counter(re.findall(r"%\d+|%n", source)), Counter(re.findall(r"%\d+|%n", text)))
                        self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                        self.assertEqual(source.count("&nbsp;"), text.count("&nbsp;"))
                        self.assertEqual(source[len(source.rstrip()):], text[len(text.rstrip()):])
                        if source in exact:
                            self.assertEqual(exact[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_migration_and_log_translations(self):
        path = ROOT / "eiskaltdcpp-qt/translations/pl.ts"
        required = {
            "QObject": ("Old or incompatible EiskaltDC++ GUI settings were detected.",
                        "A backup of the previous GUI config was saved here:",
                        "The previous GUI config could not be backed up,",
                        "EiskaltDC++ settings updated", "Images (%1);;All files (*)",
                        "Drag to resize input area", "%1 (%2)", "Click to expand or collapse",
                        "Spoiler: %1", "Open Externally", "Close", "Default (EiskaltDC++)"),
            "SettingsLog": ("Help",),
            "UISettingsLog": ("Debug Console", "<!DOCTYPE HTML PUBLIC", "Diagnostic crash log",
                              "Keeps a persistent low-volume diagnostic log",
                              "Writes to the log directory above."),
        }
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") in {"QObject", "SettingsLog", "UISettingsLog"}
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        with tempfile.TemporaryDirectory(prefix="eiskalt-pl-migration-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            original, compiled = entries(path), entries(output)
            for context, prefixes in required.items():
                for prefix in prefixes:
                    with self.subTest(required_context=context, required_source=prefix):
                        self.assertEqual(1, sum(key[0] == context and key[1].startswith(prefix)
                                                for key in original))
            for key, value in original.items():
                with self.subTest(context=key[0], source=key[1][:80]):
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(key, compiled)
                    self.assertEqual(text, "".join(compiled[key].itertext()))
                    source = key[1]
                    self.assertEqual(Counter(re.findall(r"%\d+|%n", source)), Counter(re.findall(r"%\d+|%n", text)))
                    if source.startswith("Old or incompatible"):
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        self.assertIn("zostały zachowane", text)
                        self.assertIn("zostały zresetowane", text)
                    if source.startswith("A backup of the previous"):
                        self.assertIn("zapisana tutaj:\n%1", text)
                    if source.startswith("The previous GUI config could not"):
                        self.assertIn("Nie udało się utworzyć kopii zapasowej", text)
                        self.assertIn("mimo to odrzucone", text)
                    if source.startswith("<!DOCTYPE"):
                        self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                        self.assertEqual(source.count("\n"), text.count("\n"))
                    if source == "Images (%1);;All files (*)":
                        self.assertEqual("Obrazy (%1);;Wszystkie pliki (*)", text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_download_and_sharing_settings(self):
        path = ROOT / "eiskaltdcpp-qt/translations/pl.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") in {"UISettingsDownloads", "UISettingsSharing"}
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        with tempfile.TemporaryDirectory(prefix="eiskalt-pl-download-policy-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            original, compiled = entries(path), entries(output)
            self.assertTrue(original)
            retention = 0
            for key, value in original.items():
                with self.subTest(source=key[1]):
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(key, compiled)
                    self.assertEqual(text, "".join(compiled[key].itertext()))
                    source = key[1]
                    if source.startswith("Enable this option if you want to keep downloaded"):
                        retention += 1
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        self.assertIn("nie spowoduje automatycznego usunięcia", text)
                        self.assertIn("należy zrobić to ręcznie", text)
                    if source.startswith("If the average upload speed"):
                        self.assertIn("30 sekund", text)
                        self.assertIn("nie zostanie zamknięty", text)
                    if source.startswith("This is the maximal time"):
                        self.assertIn("10 s", text)
                    if source.startswith("Hashing delay on startup"):
                        self.assertIn("-1 =", text)
                    if source.startswith("Max hash speed"):
                        self.assertIn("MB/s", text)
                        self.assertIn("0 =", text)
            self.assertEqual(2, retention)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_transfer_and_command_translations(self):
        required = {
            "HubFrame": {"IPv6", "search", "Filter",
                         "/rebuild - rebuild hash\n", "/refresh - update own file list\n",
                         "/ws param value - set gui option param in value (without value return current value of option)\n",
                         "/dcpps param value - set core option param in value (without value return current value of option)\n"},
            "Settings": {"Torrent preferences", "Could not save Torrent settings: %1"},
            "TransferView": {"Flags", "Protocol", "Cancel download", "Show only transferred files",
                             "Torrent details", "Pause Torrent", "Resume Torrent", "Uploaded %1 (%2%) "},
            "TransferViewModel": {"Flags", "Paused", "Seeding", "Waiting", "Download (%1 peers)",
                                  "Upload (%1 peers)", "%1 | Downloaded %2 (%3%)", "%1 | Uploaded %2"},
        }
        exact = {"Pause Torrent": "Wstrzymaj torrent", "Resume Torrent": "Wznów torrent",
                 "Paused": "Wstrzymano", "Seeding": "Udostępnianie", "Waiting": "Oczekiwanie",
                 "Cancel download": "Anuluj pobieranie"}
        path = ROOT / "eiskaltdcpp-qt/translations/pl.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context") for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-pl-transfers-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            original, compiled = entries(path), entries(output)
            for context, sources in required.items():
                for source in sorted(sources):
                    with self.subTest(context=context, source=source):
                        value = original[context, source]
                        self.assertNotIn(value.get("type"), {"unfinished", "obsolete", "vanished"})
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(Counter(re.findall(r"%\d+|%n", source)), Counter(re.findall(r"%\d+|%n", text)))
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        self.assertEqual(source[len(source.rstrip()):], text[len(text.rstrip()):])
                        if source.startswith("/"):
                            self.assertEqual(source.split(" - ", 1)[0], text.split(" - ", 1)[0])
                        if source in exact:
                            self.assertEqual(exact[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_connection_policy_help_survives_compilation(self):
        expected = {
            "Don't allow Hub/UPnP/DHT to override": "Nie zezwalaj hubowi/UPnP/DHT na nadpisanie",
            "Type-of-Service (ToS)": "Typ usługi (ToS)",
            "No": "Nie",
            "Low delay": "Małe opóźnienie",
            "Throughput": "Przepustowość",
            "Minimal cost": "Minimalny koszt",
            "DHT port": "Port DHT",
        }
        path = ROOT / "eiskaltdcpp-qt/translations/pl.ts"
        def entries(catalog):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") == "UISettingsConnection"
                    for m in c.findall("message")}
        original = entries(path)
        help_sources = [s for s in original if s.startswith("<!DOCTYPE") and "Type-Of-Service" in s]
        self.assertEqual(1, len(help_sources))
        source = help_sources[0]
        with tempfile.TemporaryDirectory(prefix="eiskalt-pl-policy-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            compiled = entries(output)
            for key in (*expected, source):
                with self.subTest(source=key[:80]):
                    value = original[key]
                    self.assertNotIn(value.get("type"), {"unfinished", "obsolete", "vanished"})
                    text = "".join(value.itertext())
                    self.assertIn(key, compiled)
                    self.assertEqual(text, "".join(compiled[key].itertext()))
                    if key in expected:
                        self.assertEqual(expected[key], text)
                    else:
                        self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        for phrase in ("Uwaga! Ta opcja wymaga uprawnień administratora!",
                                       "Można określić co najwyżej jedną z tych wartości TOS.",
                                       "Małe opóźnienie", "Przepustowość", "Niezawodność", "Minimalny koszt"):
                            self.assertIn(phrase, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_connection_catalog_survives_compilation(self):
        self._assert_connection_catalog_survives_compilation("pl")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_polish_tls_choices_survive_compilation(self):
        self._assert_tls_choices_survive_compilation("pl", {
            "TLS settings": "Ustawienia TLS",
            "Disable TLS": "Wyłącz TLS",
            "Allow TLS": "Zezwalaj na TLS",
            "Require TLS": "Wymagaj TLS",
            "Allow untrusted certificate for hubs": "Zezwalaj na niezaufane certyfikaty hubów",
            "Allow untrusted certificate for clients": "Zezwalaj na niezaufane certyfikaty klientów",
        })

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_bulgarian_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("bg")

    def test_bulgarian_rich_help_and_commands(self):
        self._assert_rich_help_and_commands("bg", "или")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_bulgarian_connection_catalog_survives_compilation(self):
        self._assert_connection_catalog_survives_compilation("bg")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_bulgarian_tls_choices_survive_compilation(self):
        self._assert_tls_choices_survive_compilation("bg", {
            "TLS settings": "Настройки на TLS",
            "Disable TLS": "Забраняване на TLS",
            "Allow TLS": "Разрешаване на TLS",
            "Require TLS": "Изискване на TLS",
            "Allow untrusted certificate for hubs": "Разрешаване на недоверен сертификат за хъбове",
            "Allow untrusted certificate for clients": "Разрешаване на недоверен сертификат за клиенти",
        })

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_czech_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("cs")

    def test_czech_rich_help_and_commands(self):
        self._assert_rich_help_and_commands("cs", "nebo")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_hungarian_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("hu")

    def test_hungarian_rich_help_and_commands(self):
        self._assert_rich_help_and_commands("hu", "vagy")

    def _assert_rich_help_and_commands(self, language, conjunction):
        self._assert_rich_help_preserves_markup_and_links(language)
        commands = time_help = 0
        for c in ET.parse(ROOT / "eiskaltdcpp-qt/translations" / (language + ".ts")).getroot().findall("context"):
            for m in c.findall("message"):
                source = m.findtext("source", "")
                value = m.find("translation")
                if c.findtext("name") == "HubFrame" and source.startswith(("/rebuild -", "/refresh -", "/ws param value -", "/dcpps param value -")):
                    commands += 1
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertEqual(source.split(" - ", 1)[0], text.split(" - ", 1)[0])
                    self.assertEqual(source.count("\n"), text.count("\n"))
                if c.findtext("name") == "UISettingsGUI" and source.startswith("<html><head/><body><p>These expressions"):
                    time_help += 1
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                    for token in ("h", "hh", "H", "HH", "m", "mm", "s", "ss", "z", "zzz", "AP " + conjunction + " A", "ap " + conjunction + " a", "t"):
                        self.assertIn(">" + token + "<", text)
                    for token in ("A/AP", "a/ap", "&quot;AM&quot;", "&quot;PM&quot;", "&quot;am&quot;", "&quot;pm&quot;", "&quot;CEST&quot;"):
                        self.assertIn(token, text)
        self.assertEqual(4, commands)
        self.assertEqual(1, time_help)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_hungarian_connection_catalog_survives_compilation(self):
        self._assert_connection_catalog_survives_compilation("hu")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_hungarian_tls_choices_survive_compilation(self):
        expected = {
            "TLS settings": "TLS-beállítások",
            "Disable TLS": "TLS letiltása",
            "Allow TLS": "TLS engedélyezése",
            "Require TLS": "TLS megkövetelése",
            "Allow untrusted certificate for hubs": "Nem megbízható tanúsítvány engedélyezése hubokhoz",
            "Allow untrusted certificate for clients": "Nem megbízható tanúsítvány engedélyezése kliensekhez",
        }
        self._assert_tls_choices_survive_compilation("hu", expected)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_czech_connection_catalog_survives_compilation(self):
        self._assert_connection_catalog_survives_compilation("cs")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_czech_tls_choices_survive_compilation(self):
        self._assert_tls_choices_survive_compilation("cs", {
            "TLS settings": "Nastavení TLS",
            "Disable TLS": "Zakázat TLS",
            "Allow TLS": "Povolit TLS",
            "Require TLS": "Vyžadovat TLS",
            "Allow untrusted certificate for hubs": "Povolit nedůvěryhodný certifikát pro huby",
            "Allow untrusted certificate for clients": "Povolit nedůvěryhodný certifikát pro klienty",
        })

    def _assert_tls_choices_survive_compilation(self, language, expected):
        path = ROOT / "eiskaltdcpp-qt/translations" / (language + ".ts")
        with tempfile.TemporaryDirectory(prefix="eiskalt-tls-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            for catalog in (path, output):
                entries = {m.findtext("source"): m.find("translation")
                           for c in ET.parse(catalog).getroot().findall("context")
                           if c.findtext("name") == "UISettingsConnection"
                           for m in c.findall("message")}
                for source, text in expected.items():
                    with self.subTest(catalog=catalog.name, source=source):
                        self.assertIn(source, entries)
                        self.assertNotIn(entries[source].get("type"), {"unfinished", "obsolete", "vanished"})
                        self.assertEqual(text, "".join(entries[source].itertext()))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_belarusian_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("be")

    def test_belarusian_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("be")
        entries = [m for c in ET.parse(ROOT / "eiskaltdcpp-qt/translations/be.ts").getroot().findall("context")
                   if c.findtext("name") == "UISettingsGUI" for m in c.findall("message")
                   if m.findtext("source", "").startswith("<html><head/><body><p>These expressions")]
        self.assertEqual(1, len(entries))
        source = entries[0].findtext("source")
        value = entries[0].find("translation")
        self.assertNotEqual("unfinished", value.get("type"))
        text = "".join(value.itertext())
        self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
        for token in ("h", "hh", "H", "HH", "m", "mm", "s", "ss", "z", "zzz", "AP або A", "ap або a", "t"):
            self.assertIn(">" + token + "<", text)
        for token in ("A/AP", "a/ap", "&quot;AM&quot;", "&quot;PM&quot;", "&quot;am&quot;", "&quot;pm&quot;", "&quot;CEST&quot;"):
            self.assertIn(token, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_serbian_latin_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("sr@latin")

    def test_serbian_latin_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("sr@latin")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_serbian_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("sr")

    def test_serbian_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("sr")

    def test_italian_tls_commands_and_time_help(self):
        entries = {(c.findtext("name"), m.findtext("source")): m.findtext("translation", "")
                   for c in ET.parse(ROOT / "eiskaltdcpp-qt/translations/it.ts").getroot().findall("context")
                   for m in c.findall("message")}
        for source, expected in {
            "Disable TLS": "Disabilita TLS", "Allow TLS": "Consenti TLS", "Require TLS": "Richiedi TLS",
            "Allow untrusted certificate for hubs": "Consenti certificati non attendibili per gli hub",
            "Allow untrusted certificate for clients": "Consenti certificati non attendibili per i client",
        }.items():
            self.assertEqual(expected, entries["UISettingsConnection", source])
        commands = 0
        time_help = 0
        for (context, source), text in entries.items():
            if context == "HubFrame" and source.startswith(("/rebuild -", "/refresh -", "/ws param value -", "/dcpps param value -")):
                commands += 1
                self.assertEqual(source.split(" - ", 1)[0], text.split(" - ", 1)[0])
                self.assertEqual(source.count("\n"), text.count("\n"))
            if context == "UISettingsGUI" and source.startswith("<html><head/><body><p>These expressions"):
                time_help += 1
                self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                for token in ("h", "hh", "H", "HH", "m", "mm", "s", "ss", "z", "zzz", "AP o A", "ap o a", "t"):
                    self.assertIn(">" + token + "<", text)
                for token in ("A/AP", "a/ap", "&quot;AM&quot;", "&quot;PM&quot;", "&quot;am&quot;", "&quot;pm&quot;", "&quot;CEST&quot;"):
                    self.assertIn(token, text)
        self.assertEqual(4, commands)
        self.assertEqual(1, time_help)

    def test_french_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("fr")

    def test_spanish_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("es")

    def test_german_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("de")

    def test_swedish_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("sv_SE")

    def test_turkish_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("tr")

    def test_russian_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("ru")

    def test_italian_rich_help_preserves_markup_and_links(self):
        self._assert_rich_help_preserves_markup_and_links("it")

    def _assert_rich_help_preserves_markup_and_links(self, language):
        checked = 0
        path = ROOT / "eiskaltdcpp-qt/translations" / (language + ".ts")
        for context in ET.parse(path).getroot().findall("context"):
            if context.findtext("name") not in {"MainWindow", "UISettingsLog"}:
                continue
            for message in context.findall("message"):
                source = message.findtext("source", "")
                value = message.find("translation")
                if value is None or value.get("type") in {"obsolete", "vanished"}:
                    continue
                if not (source.startswith("<!DOCTYPE HTML PUBLIC") or
                        "github.com/Delitants/eiskaltdcpp/issues" in source or
                        source.startswith('2026 <a href="mailto:')):
                    continue
                checked += 1
                text = "".join(value.itertext())
                with self.subTest(context=context.findtext("name"), source=source[:80]):
                    self.assertNotEqual("unfinished", value.get("type"))
                    self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                    self.assertEqual(source.count("\n"), text.count("\n"))
        self.assertEqual(3, checked)

    def test_ukrainian_rich_help_preserves_markup_and_links(self):
        checked = 0
        for context in ET.parse(ROOT / "eiskaltdcpp-qt/translations/uk.ts").getroot().findall("context"):
            if context.findtext("name") not in {"MainWindow", "UISettingsGUI", "UISettingsLog"}:
                continue
            for message in context.findall("message"):
                source = message.findtext("source", "")
                value = message.find("translation")
                if value is None or value.get("type") in {"obsolete", "vanished"}:
                    continue
                if not (source.startswith("<html><head/><body><p>These expressions") or
                        source.startswith("<!DOCTYPE HTML PUBLIC") or
                        "github.com/Delitants/eiskaltdcpp/issues" in source or
                        source.startswith('2026 <a href="mailto:')):
                    continue
                checked += 1
                text = "".join(value.itertext())
                with self.subTest(context=context.findtext("name"), source=source[:80]):
                    self.assertNotEqual("unfinished", value.get("type"))
                    self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                    self.assertEqual(source.count("\n"), text.count("\n"))
                    if source.startswith("<html><head/><body><p>These expressions"):
                        for token in ("h", "hh", "H", "HH", "m", "mm", "s", "ss", "z", "zzz",
                                      "AP or A", "ap or a", "t"):
                            self.assertIn(">" + token + "<", text)
                        for token in ("A/AP", "a/ap", "&quot;AM&quot;", "&quot;PM&quot;",
                                      "&quot;am&quot;", "&quot;pm&quot;", "&quot;CEST&quot;"):
                            self.assertIn(token, text)
        self.assertEqual(4, checked)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_ukrainian_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("uk")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_french_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("fr")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_spanish_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("es")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_german_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("de")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_swedish_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("sv_SE")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_turkish_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("tr")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_russian_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("ru")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_italian_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("it")

    def _assert_active_catalog_is_complete_and_compiles(self, language):
        path = ROOT / "eiskaltdcpp-qt/translations" / (language + ".ts")
        with tempfile.TemporaryDirectory(prefix="eiskalt-catalog-complete-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            def entries(catalog):
                return {(c.findtext("name"), m.findtext("source"), m.findtext("comment", "")):
                        m.find("translation")
                        for c in ET.parse(catalog).getroot().findall("context")
                        for m in c.findall("message")}
            original, compiled = entries(path), entries(output)
            active = 0
            for key, value in original.items():
                if value is not None and value.get("type") in {"obsolete", "vanished"}:
                    continue
                active += 1
                with self.subTest(context=key[0], source=key[1]):
                    self.assertIsNotNone(value)
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(key, compiled)
                    self.assertEqual(text, "".join(compiled[key].itertext()))
            self.assertGreaterEqual(active, 2086)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_ukrainian_connection_catalogs_survive_compilation(self):
        self._assert_connection_catalog_survives_compilation("uk")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_belarusian_connection_catalog_survives_compilation(self):
        self._assert_connection_catalog_survives_compilation("be")

    def _assert_connection_catalog_survives_compilation(self, language):
        required = {
            "DHTBootstrapList": {"DHT bootstrap URLs", "DHT bootstrap URL", "URL"},
            "SettingsConnection": {
                "Enable IPv6", "External/WAN IPv6:", "e.g. 2001:db8::1234",
                "Bind IPv6 address", "Country MMDB file",
                "Path to GeoLite2/MaxMind country .mmdb", "Configure DHT bootstrap URLs",
                "Public hub list URLs", "Configure public hub list URLs",
                "No valid external IPv6 address found!", "No valid bind IPv6 address found!",
                "Select MaxMind country database", "MaxMind DB (*.mmdb);;All files (*.*)",
            },
            "UISettingsConnection": {
                "DHT bootstrap URLs", "Separate multiple URLs with semicolons",
            },
        }
        path = ROOT / "eiskaltdcpp-qt/translations" / (language + ".ts")
        with tempfile.TemporaryDirectory(prefix="eiskalt-connection-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            def entries(catalog):
                return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                        for c in ET.parse(catalog).getroot().findall("context")
                        for m in c.findall("message")}
            original, compiled = entries(path), entries(output)
            for context, sources in required.items():
                for source in sorted(sources):
                    with self.subTest(context=context, source=source):
                        value = original[(context, source)]
                        self.assertNotIn(value.get("type"), {"unfinished", "obsolete", "vanished"})
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        if source != "URL":
                            self.assertNotEqual(source, text)
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[(context, source)].itertext()))
                        for token in ("IPv6", "WAN", "DHT", "URL", "MaxMind", "GeoLite2/MaxMind", "MMDB",
                                      ".mmdb", "2001:db8::1234", "(*.mmdb)", ";;", "(*.*)"):
                            if token in source:
                                self.assertIn(token, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_shutdown_refusal_diagnostic_survives_compilation(self):
        self._assert_compiled_runtime_messages("eiskalt::torrent::TorrentEngine", {
            "Torrent shutdown refused. Stages: %1; last operation: %2; elapsed: %3 us; "
            "active checks: %4; journals: %5; pending saves: %6.",
        })

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_italian_away_help_is_not_blank(self):
        source = "/away <message> - set away-mode on/off\n"
        path = ROOT / "eiskaltdcpp-qt/translations/it.ts"
        entries = [m.find("translation")
                   for c in ET.parse(path).getroot().findall("context")
                   if c.findtext("name") == "HubFrame"
                   for m in c.findall("message") if m.findtext("source") == source]
        self.assertEqual(1, len(entries))
        self.assertIsNotNone(entries[0])
        text = "".join(entries[0].itertext())
        self.assertTrue(text.strip())
        self.assertNotIn(entries[0].get("type"), {"unfinished", "obsolete", "vanished"})
        self.assertTrue(text.startswith("/away <"))
        self.assertEqual(1, text.count("\n"))
        self.assertTrue(text.endswith("\n"))
        with tempfile.TemporaryDirectory(prefix="eiskalt-italian-away-") as work:
            binary, output = Path(work) / "it.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            compiled = [m.findtext("translation")
                        for c in ET.parse(output).getroot().findall("context")
                        if c.findtext("name") == "HubFrame"
                        for m in c.findall("message") if m.findtext("source") == source]
            self.assertEqual([text], compiled)

    def _assert_compiled_runtime_messages(self, context_name, sources):
        with tempfile.TemporaryDirectory(prefix="eiskalt-runtime-catalogs-") as work:
            for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
                binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
                subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                               check=True, capture_output=True, text=True)
                subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                               check=True, capture_output=True, text=True)
                def entries(catalog):
                    return {m.findtext("source"): m.find("translation")
                            for c in ET.parse(catalog).getroot().findall("context")
                            if c.findtext("name") == context_name
                            for m in c.findall("message")}
                original, compiled = entries(path), entries(output)
                for source in sorted(sources):
                    with self.subTest(language=path.stem, context=context_name, source=source):
                        self.assertTrue(source in original, f"Missing runtime key: {source!r}")
                        value = original[source]
                        self.assertIsNotNone(value)
                        self.assertNotIn(value.get("type"), {"unfinished", "obsolete", "vanished"})
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertTrue(source in compiled, f"Missing compiled key: {source!r}")
                        self.assertEqual(text, "".join(compiled[source].itertext()))
                        self.assertEqual(Counter(re.findall(r"%[1-9][0-9]*", source)),
                                         Counter(re.findall(r"%[1-9][0-9]*", text)))
                        self.assertEqual(source.count("%"), text.count("%"))
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        if path.stem != "en" and len(source) > 40:
                            self.assertNotEqual(source, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_listener_and_proxy_retirement_messages_survive_compilation(self):
        listeners = {
            "Torrent could not accept an incoming connection.",
            "Torrent could not bind a configured listening endpoint.",
        }
        self._assert_compiled_runtime_messages("TorrentEngine", listeners)
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            for source in sorted(listeners):
                current = entries.get(("TorrentEngine", source))
                # Missing runtime keys are already reported by the compiled check.
                if current is None:
                    continue
                with self.subTest(language=path.stem, reused_source=source):
                    previous = entries.get(("eiskalt::torrent::TorrentEngine", source))
                    self.assertIsNotNone(previous)
                    self.assertEqual("".join(current.itertext()), "".join(previous.itertext()))
        self._assert_compiled_runtime_messages("TorrentProxyAdapter", {
            "The proxy adapter is retiring for application quit.",
        })

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_mainwindow_magnet_and_recovery_runtime_messages(self):
        self._assert_compiled_runtime_messages("MainWindow", {
            "Quit preparation failed: %1. Application retained.", "Share magnets",
            "Open a hub chat or private message first.", "Paste into chat draft:", "DC++ magnets",
            "No completed, selected files are ready in the DC++ share. Wait for DC hashing to finish. "
            "Private Torrents and disabled DC sharing are excluded.",
            "Choose files for DC++ magnets",
            "DC++ magnets identify individual files, not folders. Select up to 100 files. "
            "Only completed, selected files already shared in DC++ are listed.",
            "File", "Size", "Paste selected magnets",
            "The share changed while choosing files. Please select the files again.", "Torrent magnets",
            "A selected Torrent is no longer available or has no shareable hash yet.",
        })

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_transfer_status_and_specification_runtime_messages(self):
        self._assert_compiled_runtime_messages("TransferViewModel", {
            "D: %1/s | U: %2/s", "Downloading peers: %1; uploading peers: %2",
            "Downloading peers: %1; uploading peers: %2\nConnected peers: %3\n"
            "Active payload rates sampled once per second; a peer may count in both directions.\n"
            "Sorted by downloading peers, then uploading peers.",
            "Download and upload rates; sorted by their combined bytes per second.",
            "v1: SHA-1 piece hashes (BEP 3).",
            "v2: SHA-256 Merkle trees (BEP 52). Both v1 and v2 indicate a hybrid torrent.",
            "Private torrent (BEP 27): tracker-managed peer discovery; DHT and PEX are disabled.",
            "D: %1 | U: %2", "%1 | Downloaded %2 (%3%) | Uploaded %4",
        })

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_proxy_diagnostics_and_torrent_menu_survive_compilation(self):
        required = {
            "SettingsConnection": {
                "TCP port:", "UDP DNS port:",
                "Proxy settings changed; test cancelled. Test again with the new values.",
                "Enter a valid proxy server and port before testing.",
                "Testing TCP and UDP through %1:%2...",
            },
            "TransferView": {
                "Multiple Torrents selected", "Current state: %1", "Stop Torrent",
                "Stop all Torrent activity until Resume; keep the job and downloaded files.",
            },
        }
        with tempfile.TemporaryDirectory(prefix="eiskalt-proxy-menu-catalogs-") as work:
            for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
                binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
                subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                               check=True, capture_output=True, text=True)
                subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                               check=True, capture_output=True, text=True)
                def entries(catalog):
                    return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                            for c in ET.parse(catalog).getroot().findall("context")
                            for m in c.findall("message")}
                original, compiled = entries(path), entries(output)
                for context, sources in required.items():
                    for source in sorted(sources):
                        with self.subTest(language=path.stem, context=context, source=source):
                            key = context, source
                            self.assertIn(key, original)
                            value = original[key]
                            self.assertNotIn(value.get("type"), {"unfinished", "obsolete", "vanished"})
                            text = "".join(value.itertext())
                            self.assertTrue(text.strip())
                            self.assertIn(key, compiled)
                            self.assertEqual(text, "".join(compiled[key].itertext()))
                            self.assertEqual(Counter(re.findall(r"%[1-9][0-9]*", source)),
                                             Counter(re.findall(r"%[1-9][0-9]*", text)))
                            if path.stem != "en" and len(source) > 40:
                                self.assertNotEqual(source, text)

    def test_application_update_entry_points_are_translated(self):
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            for key in (("Settings", "Updates"), ("MainWindow", "Check for Updates"),
                        ("updates::SettingsUpdates", "No newer compatible version is available.")):
                with self.subTest(language=path.stem, caption=key):
                    translation = entries.get(key)
                    self.assertIsNotNone(translation)
                    self.assertNotIn(translation.get("type"), {"unfinished", "obsolete", "vanished"})
                    self.assertTrue("".join(translation.itertext()).strip())

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

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_quit_recovery_messages_survive_catalog_compilation(self):
        context_name = "updates::ApplicationQuitSession"
        sources = {"Quit preparation failed", "Retry Save and Quit", "Keep Open",
                   "Could not save %1. Application data and stopped sessions remain retained. "
                   "Fix the filesystem problem, then retry saving and quitting. "
                   "Updates will not be installed during recovery."}
        with tempfile.TemporaryDirectory(prefix="eiskalt-recovery-catalogs-") as work:
            for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
                binary = Path(work) / f"{path.stem}.qm"
                compiled = Path(work) / f"{path.stem}.ts"
                subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                               check=True, capture_output=True, text=True)
                subprocess.run(["lconvert", "-i", str(binary), "-o", str(compiled)],
                               check=True, capture_output=True, text=True)
                def recovery_entries(catalog):
                    return {m.findtext("source"): m.findtext("translation")
                            for c in ET.parse(catalog).getroot().findall("context")
                            if c.findtext("name") == context_name
                            for m in c.findall("message")}
                original = recovery_entries(path)
                actual = recovery_entries(compiled)
                for source in sorted(sources):
                    with self.subTest(language=path.stem, source=source):
                        self.assertIn(source, actual)
                        if source not in actual:
                            continue
                        self.assertTrue(actual[source].strip())
                        self.assertEqual(original[source], actual[source])
                        self.assertEqual(source.count("%1"), actual[source].count("%1"))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_shared_proxy_and_magnet_messages_cover_runtime_contexts(self):
        required = {
            "SettingsConnection": {
                "Cancel test", "TCP host / IP:", "TCP: %1\nUDP: %2",
                "UDP DNS query name:", "UDP DNS resolver (numeric IP):",
                "What to test (diagnostics only)",
                "Shadowsocks tests plain HTTP HEAD at the TCP host and port, not an arbitrary "
                "TLS service. UDP testing is unsupported. No direct fallback.",
                "TCP tests SOCKS CONNECT, not a download or TLS handshake. UDP sends a "
                "separate DNS A query through the proxy relay; no direct fallback.",
            },
            "TransferView": {"Paste DC++ magnet(s) in chat", "Paste Torrent Magnet(s) in chat"},
        }
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            entries = {(c.findtext("name"), m.findtext("source")): m.find("translation")
                       for c in ET.parse(path).getroot().findall("context")
                       for m in c.findall("message")}
            with tempfile.TemporaryDirectory(prefix="eiskalt-shared-catalog-") as work:
                binary = Path(work) / "catalog.qm"
                output = Path(work) / "compiled.ts"
                subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                               check=True, capture_output=True, text=True)
                subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                               check=True, capture_output=True, text=True)
                compiled = {(c.findtext("name"), m.findtext("source")): m.findtext("translation")
                            for c in ET.parse(output).getroot().findall("context")
                            for m in c.findall("message")}
            for context, sources in required.items():
                for source in sorted(sources):
                    with self.subTest(language=path.stem, context=context, source=source):
                        actual = entries.get((context, source))
                        self.assertIsNotNone(actual)
                        if actual is None:
                            continue
                        self.assertNotIn(actual.get("type"), {"unfinished", "obsolete", "vanished"})
                        text = "".join(actual.itertext())
                        self.assertTrue(text.strip())
                        self.assertEqual(text, compiled.get((context, source)))
                        peers = {"".join(value.itertext()) for (other, caption), value in entries.items()
                                 if caption == source and other != context and value is not None
                                 and value.get("type") not in {"unfinished", "obsolete", "vanished"}
                                 and "".join(value.itertext()).strip()}
                        self.assertEqual({text}, peers)

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
