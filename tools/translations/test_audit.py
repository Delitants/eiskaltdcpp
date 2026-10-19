"""Run with PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/translations."""

import ast
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
    def test_all_active_gui_messages_survive_compilation(self):
        def entries(path):
            return {(context.findtext("name"), message.findtext("source"),
                     message.findtext("comment", "")): "".join(message.find("translation").itertext())
                    for context in ET.parse(path).getroot().findall("context")
                    for message in context.findall("message")
                    if message.find("translation").get("type") not in {"obsolete", "vanished"}}
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            with self.subTest(language=path.stem), tempfile.TemporaryDirectory(prefix="eiskalt-all-catalog-") as work:
                binary, compiled = Path(work) / "catalog.qm", Path(work) / "catalog.ts"
                subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                               check=True, capture_output=True)
                subprocess.run(["lconvert", "-i", str(binary), "-o", str(compiled)],
                               check=True, capture_output=True)
                self.assertEqual(entries(path), entries(compiled))

    def test_all_current_gui_catalog_entries_are_complete(self):
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            incomplete = []
            for context in ET.parse(path).getroot().findall("context"):
                for message in context.findall("message"):
                    translation = message.find("translation")
                    if translation is not None and translation.get("type") in {"obsolete", "vanished"}:
                        continue
                    forms = [] if translation is None else translation.findall("numerusform") or [translation]
                    if (translation is None or translation.get("type") == "unfinished"
                            or not forms or any(not "".join(form.itertext()).strip() for form in forms)):
                        incomplete.append((context.findtext("name"), message.findtext("source")))
            with self.subTest(language=path.stem):
                self.assertEqual([], incomplete, f"{path.name}: {len(incomplete)} incomplete entries")

    @unittest.skipUnless(shutil.which("msgcat"), "msgcat is required")
    def test_all_gettext_catalog_entries_are_complete(self):
        catalogs = sorted((ROOT / "dcpp/po").glob("*.po"))
        catalogs += sorted((ROOT / "eiskaltdcpp-gtk/po").glob("*.po"))
        for path in catalogs:
            # Let gettext parse legal record layouts before inspecting their forms.
            canonical = subprocess.run(["msgcat", "--no-wrap", "--force-po", str(path)],
                                       check=True, capture_output=True, text=True).stdout
            messages = []
            for block in re.split(r"\n\s*\n", canonical):
                fields = {match[1]: "".join(ast.literal_eval(line) for line in match[2].splitlines())
                          for match in re.finditer(
                              r'^(msgid(?:_plural)?|msgstr(?:\[\d+\])?) ("[^\n]*"(?:\n"[^\n]*")*)',
                              block, re.M)}
                if "msgid" in fields:
                    messages.append((fields, block))
            header = next((fields.get("msgstr", "") for fields, _ in messages
                           if not fields["msgid"]), "")
            plural_count = re.search(r"nplurals\s*=\s*(\d+)", header)
            plural_count = int(plural_count[1]) if plural_count else 2
            incomplete = []
            for fields, block in messages:
                if not fields["msgid"]:
                    continue
                fuzzy = any("fuzzy" in [flag.strip() for flag in line[2:].split(",")]
                            for line in block.splitlines() if line.startswith("#,"))
                required = ({f"msgstr[{i}]" for i in range(plural_count)}
                            if "msgid_plural" in fields else {"msgstr"})
                actual = {key for key in fields if key.startswith("msgstr")}
                if (fuzzy or required != actual
                        or any(not fields[key].strip() for key in actual)):
                    incomplete.append(fields["msgid"])
            with self.subTest(catalog=str(path.relative_to(ROOT))):
                self.assertEqual([], incomplete, f"{path.name}: {len(incomplete)} incomplete entries")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_appearance_and_languages(self):
        expected = {
            'SettingsGUI': {
                'Portuguese (Brazil)': 'Πορτογαλικά (Βραζιλία)',
                'Serbian (Cyrillic)': 'Σερβικά (Κυριλλικά)',
                'Basque': 'Βασκικά',
                'Greek': 'Ελληνικά',
                'Italian': 'Ιταλικά',
                'Vietnamese': 'Βιετναμικά',
                'Chinese (China)': 'Κινεζικά (Κίνα)',
                'Swedish (Sweden)': 'Σουηδικά (Σουηδία)',
                'Turkish': 'Τουρκικά',
                'Danish': 'Δανικά',
                'Georgian': 'Γεωργιανά',
                'Toolbar icon theme': 'Θέμα εικονιδίων γραμμής εργαλείων',
                'Theme': 'Θέμα',
                'Default': 'Προεπιλογή',
                'Monochrome': 'Μονόχρωμο',
                'User list icon theme': 'Θέμα εικονιδίων λίστας χρηστών',
                'Original': 'Αρχικό',
                'Chat pictures': 'Εικόνες συνομιλίας',
                'Folder': 'Φάκελος',
                'Auto-clean files older than': 'Αυτόματη εκκαθάριση αρχείων παλαιότερων από',
                ' days': ' ημέρες',
                'Select chat pictures folder': 'Επιλογή φακέλου εικόνων συνομιλίας',
            },
        }
        counts = {"SettingsGUI": 48}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-appearance-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(Counter(re.findall(r"%[1-9]\d*", source)),
                                             Counter(re.findall(r"%[1-9]\d*", text)))
                            self.assertEqual(re.findall(r"<[^>]+>|&nbsp;", source),
                                             re.findall(r"<[^>]+>|&nbsp;", text))
                            self.assertEqual(source.endswith(" "), text.endswith(" "))
                            self.assertEqual(source.count("\n"), text.count("\n"))
                            self.assertEqual(source.startswith(" "), text.startswith(" "))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_hub_users_sidebar_and_commands(self):
        expected = {
            'HubFrame': {
                'Comment': 'Σχόλιο',
                'IPv6': 'IPv6',
                'E-mail': 'Ηλεκτρονικό ταχυδρομείο',
                'search': 'αναζήτηση',
                'Filter': 'Φίλτρο',
                '/rebuild - rebuild hash\n': '/rebuild - αναδόμηση της βάσης δεδομένων κατακερματισμών\n',
                '/refresh - update own file list\n': '/refresh - ενημέρωση της δικής σας λίστας αρχείων\n',
                '/ws param value - set gui option param in value (without value return current value of option)\n': '/ws param value - ορισμός της επιλογής γραφικού περιβάλλοντος param στην τιμή value (χωρίς value επιστρέφεται η τρέχουσα τιμή της επιλογής)\n',
                '/dcpps param value - set core option param in value (without value return current value of option)\n': '/dcpps param value - ορισμός της επιλογής πυρήνα param στην τιμή value (χωρίς value επιστρέφεται η τρέχουσα τιμή της επιλογής)\n',
            },
            'SideBarModel': {
                'Secretary': 'Σημειωματάριο',
                'Debug Console': 'Κονσόλα αποσφαλμάτωσης',
            },
            'ToolBar': {
                'Close': 'Κλείσιμο',
            },
            'UserListModel': {
                '<b>Hub role</b>: Operator': '<b>Ρόλος στον κόμβο</b>: Διαχειριστής',
                '<b>Hub role</b>: User': '<b>Ρόλος στον κόμβο</b>: Χρήστης',
                '<br/><b>Favorite user</b>': '<br/><b>Αγαπημένος χρήστης</b>',
                'Comment': 'Σχόλιο',
                'E-mail': 'Ηλεκτρονικό ταχυδρομείο',
                'Exact share size': 'Ακριβές μέγεθος κοινόχρηστων αρχείων',
                'IPv6': 'IPv6',
            },
        }
        counts = {"HubFrame": 118, "SideBarModel": 19, "ToolBar": 2, "UserListModel": 12}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-users-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(Counter(re.findall(r"%[1-9]\d*", source)),
                                             Counter(re.findall(r"%[1-9]\d*", text)))
                            self.assertEqual(re.findall(r"<[^>]+>|&nbsp;", source),
                                             re.findall(r"<[^>]+>|&nbsp;", text))
                            self.assertEqual(source.endswith(" "), text.endswith(" "))
                            self.assertEqual(source.count("\n"), text.count("\n"))
                            if source.startswith("/"):
                                self.assertEqual(source.split(" - ", 1)[0], text.split(" - ", 1)[0])
                                self.assertTrue(text.endswith("\n"))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_transfers_and_torrent_settings(self):
        expected = {
            'TransferView': {
                'Flags': 'Σημαίες',
                'Protocol': 'Πρωτόκολλο',
                'Cancel download': 'Ακύρωση λήψης',
                'Show only transferred files': 'Εμφάνιση μόνο μεταφερμένων αρχείων',
                'Torrent details': 'Λεπτομέρειες torrent',
                'Pause Torrent': 'Παύση torrent',
                'Resume Torrent': 'Συνέχιση torrent',
                'Uploaded %1 (%2%) ': 'Απεστάλησαν %1 (%2%) ',
            },
            'TransferViewModel': {
                'Flags': 'Σημαίες',
                'Paused': 'Σε παύση',
                'Seeding': 'Διαμοιρασμός',
                'Waiting': 'Σε αναμονή',
                'Download (%1 peers)': 'Λήψη (%1 ομότιμοι)',
                'Upload (%1 peers)': 'Αποστολή (%1 ομότιμοι)',
                '%1 | Downloaded %2 (%3%)': '%1 | Λήφθηκαν %2 (%3%)',
                '%1 | Uploaded %2': '%1 | Απεστάλησαν %2',
            },
            'Settings': {
                'Torrent preferences': 'Προτιμήσεις torrent',
                'Could not save Torrent settings: %1': 'Αδυναμία αποθήκευσης ρυθμίσεων torrent: %1',
            },
            'Notification': {
                'Setup speed limits': 'Ρύθμιση ορίων ταχύτητας',
            },
        }
        counts = {"TransferView": 48, "TransferViewModel": 33, "Settings": 16, "Notification": 7}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-transfers-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(Counter(re.findall(r"%[1-9]\d*", source)),
                                             Counter(re.findall(r"%[1-9]\d*", text)))
                            self.assertEqual(re.findall(r"<[^>]+>|&nbsp;", source),
                                             re.findall(r"<[^>]+>|&nbsp;", text))
                            self.assertEqual(source.endswith(" "), text.endswith(" "))
                            self.assertEqual(re.findall(r"%[1-9]\d*%", source),
                                             re.findall(r"%[1-9]\d*%", text))
                            self.assertEqual(source.count(" | "), text.count(" | "))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_hubs_magnets_and_chat(self):
        expected = {
            'ChatEdit': {
                'Address': 'Διεύθυνση',
                'Select image': 'Επιλογή εικόνας',
            },
            'DHTBootstrapList': {
                'DHT bootstrap URLs': 'Διευθύνσεις URL αρχικής σύνδεσης DHT',
                'DHT bootstrap URL': 'Διεύθυνση URL αρχικής σύνδεσης DHT',
                'URL': 'URL',
            },
            'PublicHubs': {
                'Refreshing public hub list...': 'Ανανέωση λίστας δημόσιων κόμβων...',
                'Hub list downloaded... (%1)': 'Η λίστα κόμβων λήφθηκε... (%1)',
                'Locally cached (as of %1) version of the hub list loaded (%2)': 'Φορτώθηκε η τοπικά αποθηκευμένη έκδοση της λίστας κόμβων (από %1) (%2)',
            },
            'PublicHubsList': {
                'Public hub list URLs': 'Διευθύνσεις URL λιστών δημόσιων κόμβων',
            },
            'SearchFrame': {
                'Searching for %1 ...': 'Αναζήτηση για %1 ...',
                'Search progress of "%1" is %2%': 'Η πρόοδος αναζήτησης για "%1" είναι %2%',
            },
            'UIHubFrame': {
                'Press Shift+Enter for multiline input': 'Πατήστε Shift+Enter για εισαγωγή πολλών γραμμών',
            },
            'UIMagnet': {
                'Magnet link viewer': 'Προβολή συνδέσμου magnet',
                'Copy magnet link to clipboard': 'Αντιγραφή συνδέσμου magnet στο πρόχειρο',
                'Copy search link to clipboard': 'Αντιγραφή συνδέσμου αναζήτησης στο πρόχειρο',
            },
            'UIPrivateMessage': {
                'Press Shift+Enter for multiline input': 'Πατήστε Shift+Enter για εισαγωγή πολλών γραμμών',
            },
            'UIPublicHubs': {
                'Refresh': 'Ανανέωση',
            },
        }
        counts = {"ChatEdit": 6, "DHTBootstrapList": 3, "PublicHubs": 13, "PublicHubsList": 3, "SearchFrame": 47, "UIHubFrame": 19, "UIMagnet": 12, "UIPrivateMessage": 20, "UIPublicHubs": 3}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-hubs-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(Counter(re.findall(r"%[1-9]\d*", source)),
                                             Counter(re.findall(r"%[1-9]\d*", text)))
                            self.assertEqual(re.findall(r"<[^>]+>|&nbsp;", source),
                                             re.findall(r"<[^>]+>|&nbsp;", text))
                            self.assertEqual(source.endswith(" "), text.endswith(" "))
                            if source == 'Search progress of "%1" is %2%':
                                self.assertIn("%2%", text)
                            if "Shift+Enter" in source:
                                self.assertIn("Shift+Enter", text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_file_browsing_and_hashing(self):
        expected = {
            'FileBrowserModel': {
                '<b>Media Info:</b><br/>': '<b>Πληροφορίες πολυμέσων:</b><br/>',
                '&nbsp;&nbsp;<b>Video:</b> %1<br/>': '&nbsp;&nbsp;<b>Βίντεο:</b> %1<br/>',
                '&nbsp;&nbsp;<b>Audio:</b> %1<br/>': '&nbsp;&nbsp;<b>Ήχος:</b> %1<br/>',
                '&nbsp;&nbsp;<b>Bitrate:</b> %1<br/>': '&nbsp;&nbsp;<b>Ρυθμός bit:</b> %1<br/>',
                '&nbsp;&nbsp;<b>Resolution:</b> %1<br/><br/>': '&nbsp;&nbsp;<b>Ανάλυση:</b> %1<br/><br/>',
                'Bitrate': 'Ρυθμός bit',
                'Resolution': 'Ανάλυση',
            },
            'HashProgress': {
                '%1% %2 left': '%1% απομένουν %2',
            },
            'ShareBrowser': {
                'Share browser error: %1': 'Σφάλμα περιηγητή κοινόχρηστων αρχείων: %1',
                'Selected %1 from %2 items; ': 'Επιλέχθηκαν %1 από %2 στοιχεία· ',
                'Share browser': 'Περιηγητής κοινόχρηστων αρχείων',
                'Matched %1 files': 'Βρέθηκαν %1 αρχεία που ταιριάζουν',
            },
            'ShareBrowserSearch': {
                'Found %1 items': 'Βρέθηκαν %1 στοιχεία',
            },
            'UIFileHasher': {
                'Copy magnet link to clipboard': 'Αντιγραφή συνδέσμου magnet στο πρόχειρο',
                'Copy search link to clipboard': 'Αντιγραφή συνδέσμου αναζήτησης στο πρόχειρο',
            },
            'UIShareBrowserSearch': {
                'Search only files': 'Αναζήτηση μόνο αρχείων',
                'Search only directories': 'Αναζήτηση μόνο καταλόγων',
                'Search all': 'Αναζήτηση όλων',
            },
        }
        counts = {"FileBrowserModel": 18, "HashProgress": 12, "ShareBrowser": 26, "ShareBrowserSearch": 2, "UIFileHasher": 10, "UIShareBrowserSearch": 6}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-files-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(Counter(re.findall(r"%[1-9]\d*", source)),
                                             Counter(re.findall(r"%[1-9]\d*", text)))
                            self.assertEqual(re.findall(r"<[^>]+>|&nbsp;", source),
                                             re.findall(r"<[^>]+>|&nbsp;", text))
                            self.assertEqual(source.endswith(" "), text.endswith(" "))
                            if source == "%1% %2 left":
                                self.assertIn("%1%", text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_commands_favorites_and_ipfilter(self):
        expected = {
            'UCModel': {
                'Separator': 'Διαχωριστικό',
            },
            'UIUserCommands': {
                'New command': 'Νέα εντολή',
                'Separator': 'Διαχωριστικό',
                'Chat': 'Συνομιλία',
                'Raw': 'Ακατέργαστη εντολή',
                'PM': 'Προσωπικό μήνυμα',
                'Context': 'Πλαίσιο χρήσης',
                'Parameters': 'Παράμετροι',
                'Hub address': 'Διεύθυνση κόμβου',
                'To': 'Προς',
                'Send once per nick': 'Αποστολή μία φορά ανά ψευδώνυμο',
                'Result': 'Αποτέλεσμα',
            },
            'FavoriteUsers': {
                'Browse Files': 'Περιήγηση αρχείων',
            },
            'UIFavoriteHubEditor': {
                'This description will be shown in the list of favorite hubs.': 'Αυτή η περιγραφή θα εμφανίζεται στη λίστα αγαπημένων κόμβων.',
                'This description will be shown in the user list.': 'Αυτή η περιγραφή θα εμφανίζεται στη λίστα χρηστών.',
            },
            'UIIPFilter': {
                'Import': 'Εισαγωγή',
            },
        }
        counts = {"UCModel": 4, "UIUserCommands": 18, "FavoriteUsers": 6, "UIFavoriteHubEditor": 27, "UIIPFilter": 12}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-commands-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_logging_scripts_and_spy(self):
        expected = {
            'SettingsLog': {
                'Help':
                    'Βοήθεια',
            },
            'UIScriptManager': {
                'Ask user':
                    'Ερώτηση στον χρήστη',
                'Reload it':
                    'Επαναφόρτωση',
                'When script changed':
                    'Όταν αλλάζει το σενάριο',
            },
            'UISpy': {
                'Ignore TTH search':
                    'Παράβλεψη αναζητήσεων TTH',
                'Autoscrolling':
                    'Αυτόματη κύλιση',
            },
            'UISettingsLog': {
                'Log directory':
                    'Κατάλογος αρχείων καταγραφής',
                'All logs generated by EiskaltDC++ will be put in this subdirectory.':
                    'Όλα τα αρχεία καταγραφής που δημιουργούνται από το EiskaltDC++ θα τοποθετούνται σε αυτόν τον υποκατάλογο.',
                'Private messages':
                    'Προσωπικά μηνύματα',
                'Text':
                    'Κείμενο',
                'Main chat':
                    'Κύρια συνομιλία',
                'Uploads':
                    'Αποστολές',
                'Debug Console':
                    'Κονσόλα αποσφαλμάτωσης',
                'System messages':
                    'Μηνύματα συστήματος',
                'Status messages':
                    'Μηνύματα κατάστασης',
                '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "https://www.w3.org/TR/REC-html40/strict.dtd">\n<html><head><meta name="qrichtext" content="1" /><style type="text/css">\np, li { white-space: pre-wrap; }\n</style></head><body style=" font-size:9pt; font-weight:400; font-style:normal;">\n<p style=" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;"><a href="https://dcplusplus.sourceforge.io/webhelp/settings_logs.html"><span style=" text-decoration: underline; color:#296fbe;">Help</span></a></p></body></html>':
                    '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "https://www.w3.org/TR/REC-html40/strict.dtd">\n<html><head><meta name="qrichtext" content="1" /><style type="text/css">\np, li { white-space: pre-wrap; }\n</style></head><body style=" font-size:9pt; font-weight:400; font-style:normal;">\n<p style=" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;"><a href="https://dcplusplus.sourceforge.io/webhelp/settings_logs.html"><span style=" text-decoration: underline; color:#296fbe;">Βοήθεια</span></a></p></body></html>',
                'Auto search alternates':
                    'Αυτόματη αναζήτηση εναλλακτικών πηγών',
                'Diagnostic crash log':
                    'Διαγνωστικό αρχείο καταγραφής κατάρρευσης',
                'Keeps a persistent low-volume diagnostic log with Qt warnings, heartbeat, shutdown and fatal signal markers. Useful when the application exits after running for a long time.':
                    'Διατηρεί μόνιμο διαγνωστικό αρχείο καταγραφής μικρού όγκου με προειδοποιήσεις Qt, περιοδικές ενδείξεις λειτουργίας και σημάνσεις τερματισμού και μοιραίων σημάτων. Χρήσιμο όταν η εφαρμογή τερματίζεται μετά από πολύωρη λειτουργία.',
                'Writes to the log directory above. Keeps the last heartbeat before crashes or forced termination.':
                    'Γράφει στον παραπάνω κατάλογο αρχείων καταγραφής. Διατηρεί την τελευταία περιοδική ένδειξη λειτουργίας πριν από κατάρρευση ή αναγκαστικό τερματισμό.',
                'Own filelist uploads':
                    'Αποστολές της δικής σας λίστας αρχείων',
            },
        }
        counts = {"SettingsLog": 2, "UIScriptManager": 4, "UISpy": 4, "UISettingsLog": 19}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-logs-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            if source.startswith("<!DOCTYPE"):
                                self.assertEqual(source.replace(">Help<", ">Βοήθεια<"), text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_console_and_notes(self):
        expected = {
            "CmdDebug": {"Debug Console": "Κονσόλα αποσφαλμάτωσης"},
            "Secretary": {
                "Secretary": "Σημειωματάριο",
                "Clear notes": "Εκκαθάριση σημειώσεων",
                "Find in notes": "Εύρεση στις σημειώσεις",
            },
            "UICmdDebug": {
                "Use comma as separator": "Χρήση κόμματος ως διαχωριστικού",
                "Filter by IP": "Φιλτράρισμα κατά IP",
                "Lines:": "Γραμμές:", "Clear now": "Εκκαθάριση τώρα",
            },
            "UISecretary": {
                "Magnet links": "Σύνδεσμοι magnet", "Search links": "Σύνδεσμοι αναζήτησης",
                "Public chat": "Δημόσια συνομιλία", "Status messages": "Μηνύματα κατάστασης",
                "BitTorrent links": "Σύνδεσμοι BitTorrent", "Private messages": "Προσωπικά μηνύματα",
                "Filter duplicates": "Φιλτράρισμα διπλοτύπων",
                "Use comma as separator": "Χρήση κόμματος ως διαχωριστικού",
                "Additional keywords": "Πρόσθετες λέξεις-κλειδιά",
                "Filter hubs by IP": "Φιλτράρισμα κόμβων κατά IP",
                "Lines:": "Γραμμές:", "Clear now": "Εκκαθάριση τώρα",
            },
        }
        counts = {"CmdDebug": 1, "Secretary": 9, "UICmdDebug": 11, "UISecretary": 19}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-console-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_history_and_personal_settings(self):
        expected = {
            "UISettingsHistory": {
                "Clear search history on program exit": "Εκκαθάριση ιστορικού αναζητήσεων κατά την έξοδο από το πρόγραμμα",
                "Clear download directories history on program exit": "Εκκαθάριση ιστορικού φακέλων λήψης κατά την έξοδο από το πρόγραμμα",
                "Clear now": "Εκκαθάριση τώρα",
                "Number of search history elements to store:": "Αριθμός εγγραφών ιστορικού αναζητήσεων προς αποθήκευση:",
                "Number of download directories history elements to store:": "Αριθμός εγγραφών ιστορικού φακέλων λήψης προς αποθήκευση:",
                "Memorize TTH search phrases": "Απομνημόνευση φράσεων αναζήτησης TTH",
            },
            "UISettingsPersonal": {
                "This description will show in the user list.\nIt can be overridden on a per-hub basis in the Favorite Hub Properties.":
                    "Αυτή η περιγραφή θα εμφανίζεται στη λίστα χρηστών.\nΜπορεί να αντικατασταθεί ξεχωριστά για κάθε κόμβο στις ιδιότητες αγαπημένου κόμβου.",
                "If you are in away mode, this message will be sent to everybody\nwho sends private message to you.":
                    "Αν βρίσκεστε σε κατάσταση απουσίας, αυτό το μήνυμα θα αποστέλλεται σε όποιον\nσας στέλνει προσωπικό μήνυμα.",
                "This nickname, or handle, will identify you when you join a hub.\nIt can be overridden on a per-hub basis in the Favorite Hub Properties.":
                    "Αυτό το ψευδώνυμο θα σας προσδιορίζει όταν συνδέεστε σε έναν κόμβο.\nΜπορεί να αντικατασταθεί ξεχωριστά για κάθε κόμβο στις ιδιότητες αγαπημένου κόμβου.",
                "E-Mail": "Ηλεκτρονικό ταχυδρομείο", "Auto away interval (min)": "Διάστημα αυτόματης απουσίας (λεπτά)",
                "This field is for sharing your email address, if you wish it.": "Αυτό το πεδίο είναι για την κοινοποίηση της διεύθυνσης ηλεκτρονικού ταχυδρομείου σας, αν το επιθυμείτε.",
                "This drop-down contains a number of common upload speeds (in Mbit/s),\nplease choose the one closest to your own.\nDo not put in your download speed; this setting is a measure\nof how fast a source you are likely to be for other users.":
                    "Αυτή η αναπτυσσόμενη λίστα περιέχει συνήθεις ταχύτητες αποστολής (σε Mbit/s),\nεπιλέξτε την πλησιέστερη στη δική σας.\nΜην εισάγετε την ταχύτητα λήψης σας· αυτή η ρύθμιση δείχνει\nπόσο γρήγορη πηγή είναι πιθανό να είστε για τους άλλους χρήστες.",
                "Mbit/s": "Mbit/s", "Default hub encoding": "Προεπιλεγμένη κωδικοποίηση κόμβου",
            },
        }
        counts = {"UISettingsHistory": 6, "UISettingsPersonal": 15}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-personal-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(source.count("\n"), text.count("\n"))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_greek_notification_and_sound_settings(self):
        expected = {"UISettingsNotification": {
            "Text": "Κείμενο", "Ask confirm before exit": "Ερώτηση επιβεβαίωσης πριν από την έξοδο",
            "Enable notifications": "Ενεργοποίηση ειδοποιήσεων",
            "Your nick is mentioned in main chat": "Το ψευδώνυμό σας αναφέρεται στην κύρια συνομιλία",
            "Transfer done": "Η μεταφορά ολοκληρώθηκε",
            "Favorite user join/part": "Σύνδεση/αποχώρηση αγαπημένου χρήστη",
            "Allow other types of notifications": "Να επιτρέπονται άλλοι τύποι ειδοποιήσεων",
            "Enable notifications with not active main window": "Ενεργοποίηση ειδοποιήσεων όταν το κύριο παράθυρο δεν είναι ενεργό",
            "Enable notifications with active main window": "Ενεργοποίηση ειδοποιήσεων όταν το κύριο παράθυρο είναι ενεργό",
            "Change icon when main window is visible": "Αλλαγή εικονιδίου όταν το κύριο παράθυρο είναι ορατό",
            "Notification module": "Μονάδα ειδοποιήσεων", "Default (Qt)": "Προεπιλογή (Qt)", "D-Bus": "D-Bus",
            "Sound": "Ήχος", "Enable sounds": "Ενεργοποίηση ήχων", "Test": "Δοκιμή",
            "Play sound on active PM window": "Αναπαραγωγή ήχου όταν το παράθυρο ιδιωτικών μηνυμάτων είναι ενεργό",
            "Favorite user event": "Συμβάν αγαπημένου χρήστη", "Use external command": "Χρήση εξωτερικής εντολής",
            "If your sound files' format is unsupported by Qt, you can define\ncustom media player here to play them.\nFor example: /usr/bin/mplayer":
                "Αν η μορφή των αρχείων ήχου σας δεν υποστηρίζεται από το Qt, μπορείτε να ορίσετε\nεδώ ένα προσαρμοσμένο πρόγραμμα αναπαραγωγής πολυμέσων για την αναπαραγωγή τους.\nΓια παράδειγμα: /usr/bin/mplayer",
        }}
        def entries(path):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") == "UISettingsNotification" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        path = ROOT / "eiskaltdcpp-qt/translations/el.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-el-notifications-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(21, len(original))
            self.assertEqual(20, len(expected["UISettingsNotification"]))
            self.assertTrue(expected["UISettingsNotification"].keys() <= original.keys())
            for source, value in original.items():
                with self.subTest(source=source):
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    if source in expected["UISettingsNotification"]:
                        self.assertEqual(expected["UISettingsNotification"][source], text)
                        self.assertEqual(source.count("\n"), text.count("\n"))

    def test_slovak_final_html_help(self):
        replacements = {
            "UISettingsConnection": {
                "Set the Type-Of-Service (TOS) field that is sent with every ": "Nastaví pole typu služby (TOS), ktoré sa odosiela s každým ",
                "IP packet": "IP paketom",
                " originating from this socket. It is used to prioritize packets on the network. TOS is a byte. There are some standard TOS flags defined: ":
                    " pochádzajúcim z tohto soketu. Používa sa na určovanie priority paketov v sieti. TOS je bajt. Sú definované niektoré štandardné príznaky TOS: ",
                "Low delay": "Nízke oneskorenie", "Throughput": "Priepustnosť", "Reliability": "Spoľahlivosť", "Minimal cost": "Minimálne náklady",
                " to minimize delays for interactive traffic": " na minimalizáciu oneskorenia interaktívnej komunikácie",
                " to optimize throughput": " na optimalizáciu priepustnosti",
                " to optimize for reliability": " na optimalizáciu spoľahlivosti",
                " should be used for &quot;filler data&quot; where slow transmission doesn't matter":
                    " sa má používať pre &quot;výplňové dáta&quot;, pri ktorých pomalý prenos neprekáža",
                "At most one of these TOS values can be specified. Linux sends ": "Možno zadať najviac jednu z týchto hodnôt TOS. Linux predvolene odosiela ",
                " datagrams first by default, but the exact behaviour depends on the configured queueing discipline.":
                    " datagramy ako prvé, ale presné správanie závisí od nastaveného spôsobu radenia do frontu.",
                " Warning! This option needs superuser privileges!": " Upozornenie! Táto možnosť vyžaduje oprávnenia superpoužívateľa!",
            },
            "UISettingsGUI": {
                "These expressions may be used for the time:": "Pre čas možno použiť tieto výrazy:",
                "Expression": "Výraz", "Output": "Výstup",
                "the hour without a leading zero (0 to 23 or 1 to 12 if AM/PM display)": "hodina bez úvodnej nuly (0 až 23 alebo 1 až 12 pri zobrazení AM/PM)",
                "the hour with a leading zero (00 to 23 or 01 to 12 if AM/PM display)": "hodina s úvodnou nulou (00 až 23 alebo 01 až 12 pri zobrazení AM/PM)",
                "the hour without a leading zero (0 to 23, even with AM/PM display)": "hodina bez úvodnej nuly (0 až 23 aj pri zobrazení AM/PM)",
                "the hour with a leading zero (00 to 23, even with AM/PM display)": "hodina s úvodnou nulou (00 až 23 aj pri zobrazení AM/PM)",
                "the minute without a leading zero (0 to 59)": "minúta bez úvodnej nuly (0 až 59)",
                "the minute with a leading zero (00 to 59)": "minúta s úvodnou nulou (00 až 59)",
                "the second without a leading zero (0 to 59)": "sekunda bez úvodnej nuly (0 až 59)",
                "the second with a leading zero (00 to 59)": "sekunda s úvodnou nulou (00 až 59)",
                "the milliseconds without leading zeroes (0 to 999)": "milisekundy bez úvodných núl (0 až 999)",
                "the milliseconds with leading zeroes (000 to 999)": "milisekundy s úvodnými nulami (000 až 999)",
                "AP or A": "AP alebo A", "ap or a": "ap alebo a",
                "use AM/PM display. ": "použiť zobrazenie AM/PM. ", "use am/pm display. ": "použiť zobrazenie am/pm. ",
                " will be replaced by either &quot;AM&quot; or &quot;PM&quot;.": " sa nahradí buď &quot;AM&quot;, alebo &quot;PM&quot;.",
                " will be replaced by either &quot;am&quot; or &quot;pm&quot;.": " sa nahradí buď &quot;am&quot;, alebo &quot;pm&quot;.",
                "the timezone (for example &quot;CEST&quot;)": "časové pásmo (napríklad &quot;CEST&quot;)",
            },
        }
        markers = {"UISettingsConnection": "Set the Type-Of-Service", "UISettingsGUI": "These expressions may be used for the time:"}
        root = ET.parse(ROOT / "eiskaltdcpp-qt/translations/sk.ts").getroot()
        for context, mapping in replacements.items():
            messages = [m for c in root.findall("context") if c.findtext("name") == context
                        for m in c.findall("message") if markers[context] in m.findtext("source", "")
                        and m.find("translation").get("type") not in {"obsolete", "vanished"}]
            self.assertEqual(1, len(messages), context)
            message = messages[0]
            source = message.findtext("source")
            expected = source
            for old, new in mapping.items():
                self.assertIn(old, expected)
                expected = expected.replace(old, new)
            with self.subTest(context=context):
                value = message.find("translation")
                self.assertNotEqual("unfinished", value.get("type"))
                text = "".join(value.itertext())
                self.assertEqual(expected, text)
                self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                self.assertEqual(Counter(re.findall(r"\d+", source)), Counter(re.findall(r"\d+", text)))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("sk")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_connection_labels_and_warnings(self):
        expected = {"UISettingsConnection": {
            "Don't allow Hub/UPnP/DHT to override": "Nepovoliť prepísanie hubom/UPnP/DHT",
            "TCP port": "Port TCP", "UDP port": "Port UDP", "TLS port": "Port TLS",
            "Bind to address": "Viazať na adresu", "Bind to interface": "Viazať na rozhranie",
            "SOCKS5": "SOCKS5", "Port": "Port",
            "When enabled can hang program on exit.": "Po zapnutí môže program pri ukončovaní zamrznúť.",
            "KiB/s": "KiB/s", "Reconnect to hub (seconds)": "Znovu sa pripojiť k hubu (sekundy)",
            "Type-of-Service (ToS)": "Typ služby (ToS)", "Low delay": "Nízke oneskorenie",
            "Throughput": "Priepustnosť", "Minimal cost": "Minimálne náklady", "DHT port": "Port DHT",
        }}
        def entries(path):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") == "UISettingsConnection" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-connection-labels-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(16, len(expected["UISettingsConnection"]))
            self.assertTrue(expected["UISettingsConnection"].keys() <= original.keys())
            for source, translation in expected["UISettingsConnection"].items():
                with self.subTest(source=source):
                    value = original[source]
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertEqual(translation, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_antispam_and_logging_settings(self):
        expected = {
            "AntiSpam": {"%1: Moving user to GRAY.": "%1: Presúvam používateľa do sivého zoznamu.",
                         "%1: Moving user to BLACK.": "%1: Presúvam používateľa do čierneho zoznamu."},
            "UIAntiSpam": {"OK": "OK", "C": "C"},
            "UISettingsLog": {
                "Text": "Text", "Debug Console": "Ladiaca konzola",
                '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "https://www.w3.org/TR/REC-html40/strict.dtd">\n<html><head><meta name="qrichtext" content="1" /><style type="text/css">\np, li { white-space: pre-wrap; }\n</style></head><body style=" font-size:9pt; font-weight:400; font-style:normal;">\n<p style=" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;"><a href="https://dcplusplus.sourceforge.io/webhelp/settings_logs.html"><span style=" text-decoration: underline; color:#296fbe;">Help</span></a></p></body></html>':
                    '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "https://www.w3.org/TR/REC-html40/strict.dtd">\n<html><head><meta name="qrichtext" content="1" /><style type="text/css">\np, li { white-space: pre-wrap; }\n</style></head><body style=" font-size:9pt; font-weight:400; font-style:normal;">\n<p style=" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;"><a href="https://dcplusplus.sourceforge.io/webhelp/settings_logs.html"><span style=" text-decoration: underline; color:#296fbe;">Pomocník</span></a></p></body></html>',
                "Diagnostic crash log": "Diagnostický protokol pádov",
                "Keeps a persistent low-volume diagnostic log with Qt warnings, heartbeat, shutdown and fatal signal markers. Useful when the application exits after running for a long time.":
                    "Uchováva trvalý diagnostický protokol s malým objemom záznamov obsahujúci upozornenia Qt, pravidelné signály aktivity a značky ukončenia a fatálnych signálov. Užitočné pri ukončení aplikácie po dlhom čase behu.",
                "Writes to the log directory above. Keeps the last heartbeat before crashes or forced termination.":
                    "Zapisuje do vyššie uvedeného priečinka protokolov. Uchováva posledný signál aktivity pred pádom alebo vynúteným ukončením.",
            },
        }
        counts = {"AntiSpam": 5, "UIAntiSpam": 17, "UISettingsLog": 19}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-logs-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(source.count("\n"), text.count("\n"))
                            self.assertEqual(Counter(re.findall(r"%\d+", source)), Counter(re.findall(r"%\d+", text)))
                            self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_appearance_and_chat_settings(self):
        expected = {"UISettingsGUI": {
            "Don't show icons in menus (need restart)": "Nezobrazovať ikony v ponukách (vyžaduje reštart)",
            "Redirect new personal messages to the main chat": "Presmerovať nové súkromné správy do hlavného chatu",
            "Use separator for unread messages in the chat": "Použiť oddeľovač neprečítaných správ v chate",
            "Application unit base": "Základ jednotiek aplikácie",
            "Show kibibyte (KiB) or kilobyte (KB), etc.": "Zobrazovať kibibajty (KiB) alebo kilobajty (KB) atď.",
            "Window behavior": "Správanie okna",
            "Minimize to status icon when clicking the close button": "Po kliknutí na tlačidlo zatvorenia minimalizovať do stavovej ikony",
            "Status icon": "Stavová ikona", "Use colored status icon": "Použiť farebnú stavovú ikonu",
            "Use monochrome status icon": "Použiť jednofarebnú stavovú ikonu", "Metric": "Metrické",
            "Default action with magnet": "Predvolená akcia pre magnetový odkaz", "Parse BBCode": "Spracovať BBCode",
            "Transfer colors": "Farby prenosov", "Reset": "Obnoviť predvolené nastavenia",
            "Change background color in the chat": "Zmeniť farbu pozadia v chate",
        }}
        def entries(path):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") == "UISettingsGUI" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-appearance-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(16, len(expected["UISettingsGUI"]))
            self.assertTrue(expected["UISettingsGUI"].keys() <= original.keys())
            for source, translation in expected["UISettingsGUI"].items():
                with self.subTest(source=source):
                    value = original[source]
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertEqual(translation, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_download_retention_and_sources(self):
        expected = {"UISettingsDownloads": {
            "Auto-drop slow sources for all queue items (except filelists)":
                "Automaticky vyradiť pomalé zdroje pre všetky položky frontu (okrem zoznamov súborov)",
            'This option removes the slow sources for all queue items (except file lists).\nTo enable autodrop, turn on either "Autodrop slow sources for all queue items"\nor "Remove slow file lists" (or both of them).':
                'Táto možnosť odstráni pomalé zdroje pre všetky položky frontu (okrem zoznamov súborov).\nNa zapnutie automatického vyraďovania zapnite buď "Automaticky vyradiť pomalé zdroje pre všetky položky frontu"\nalebo "Odstrániť pomalé zoznamy súborov" (alebo obe možnosti).',
            'With this option enabled, EiskaltDC++ will delete a file list whose speed\nwas recognized as too slow.\nTo enable autodrop, turn on either "Autodrop slow sources for all queue items"\nor "Remove slow file lists" (or both of them).':
                'Ak je táto možnosť zapnutá, EiskaltDC++ odstráni zoznam súborov, ktorého rýchlosť\nbola vyhodnotená ako príliš nízka.\nNa zapnutie automatického vyraďovania zapnite buď "Automaticky vyradiť pomalé zdroje pre všetky položky frontu"\nalebo "Odstrániť pomalé zoznamy súborov" (alebo obe možnosti).',
            "Don't remove the source when auto-dropping, only disconnect":
                "Pri automatickom vyraďovaní zdroj neodstraňovať, iba odpojiť",
            "With this option enabled, sources will only be disconnected\nbut not removed from the queue.":
                "Ak je táto možnosť zapnutá, zdroje sa iba odpoja,\nale neodstránia sa z frontu.",
            "Every few minutes, the client will search for files in your\nDownload Queue to find more download locations (more users/sources).\nThe autosearch will find exact matches by searching via hash.\nThe drawback is that it may won't find much more sources\nand will prevent manual searches on hubs where\na predefined search interval rule is in effect.":
                "Každých pár minút klient vyhľadá súbory vo vašom\nfronte sťahovania, aby našiel ďalšie miesta na stiahnutie (používateľov/zdroje).\nAutomatické vyhľadávanie nájde presné zhody pomocou hašu.\nNevýhodou je, že nemusí nájsť veľa ďalších zdrojov\na zabráni ručnému vyhľadávaniu na huboch, kde\nplatí pravidlo predpísaného intervalu vyhľadávania.",
            "Automatically match queue for auto search hits": "Automaticky porovnať výsledky automatického vyhľadávania s frontom",
            "Enable to automatically download a user's list and match them\nas sources for files in your Download Queue when they've been\ndiscovered as a source for one of the files you're downloading.\nIf you download files in sets, that user is probably a source\nfor many more files you're downloading. Enable if unsure.\nThe drawbacks are that it might add slower sources, possibly\nincreasing the time needed to complete a download,\nand that you'll download a lot of file lists.":
                "Zapnutím automaticky stiahnete zoznam používateľa a priradíte ho\nako zdroj pre súbory vo fronte sťahovania, keď bol\nnájdený ako zdroj jedného zo sťahovaných súborov.\nAk sťahujete súbory v súpravách, tento používateľ je pravdepodobne zdrojom\naj mnohých ďalších sťahovaných súborov. Ak si nie ste istí, zapnite túto možnosť.\nNevýhodou je možné pridanie pomalších zdrojov, čo môže\npredĺžiť čas potrebný na dokončenie sťahovania,\na sťahovanie veľkého množstva zoznamov súborov.",
            "When this option is enabled, any zero byte files you\ntry to download will be silently skipped.\nSometimes empty files contain information through their naming.\nHowever every file, even an empty one, will consume some drive space.":
                "Ak je táto možnosť zapnutá, všetky súbory s nulovou veľkosťou, ktoré\nsa pokúsite stiahnuť, sa bez upozornenia preskočia.\nPrázdne súbory niekedy obsahujú informácie vo svojich názvoch.\nKaždý súbor, aj prázdny, však zaberá určité miesto na disku.",
            "This option will stop you from downloading files\nwhich already exist in one of your shared directories.":
                "Táto možnosť zabráni sťahovaniu súborov,\nktoré už existujú v niektorom z vašich zdieľaných priečinkov.",
            "This option will stop you from downloading files\nwhich already exist in your queue.":
                "Táto možnosť zabráni sťahovaniu súborov,\nktoré už existujú vo vašom fronte.",
            "Enable this option if you want to keep downloaded filelists\nin the download queue after they have been finished.\nAfter downloaded the queue items will have 'Finished' status\nand you can delete these finished entries anytime later.\nNote that if you physically move/delete your downloaded files from\ntheir target location, it won't result automatic deletion of\nthe corresponding finished entries from the queue.\nIf you disable this function, it won't remove existing finished items\nfrom the queue either, you must do that manually.":
                "Zapnite túto možnosť, ak chcete ponechať stiahnuté zoznamy súborov\nvo fronte sťahovania aj po dokončení.\nPo stiahnutí budú mať položky frontu stav 'Dokončené'\na tieto dokončené záznamy môžete neskôr kedykoľvek odstrániť.\nAk fyzicky presuniete alebo odstránite stiahnuté súbory\nz cieľového umiestnenia, nespôsobí to automatické odstránenie\nzodpovedajúcich dokončených záznamov z frontu.\nAni vypnutie tejto funkcie neodstráni existujúce dokončené položky\nz frontu; musíte to urobiť ručne.",
            "Keep downloaded files": "Ponechať stiahnuté súbory",
            "Enable this option if you want to keep downloaded files and folders\nin the download queue after they have been finished.\nAfter downloaded the queue items will have 'Finished' status\nand you can delete these finished entries anytime later.\nUsing this function you can keep track of what files are already\ndownloaded right there in the download queue without the need\nof checking the Finished Downloads window.\nNote that if you physically move/delete your downloaded files from\ntheir target location, it won't result automatic deletion of\nthe corresponding finished entries from the queue.\nIf you disable this function, it won't remove existing finished items\nfrom the queue either, you must do that manually.":
                "Zapnite túto možnosť, ak chcete ponechať stiahnuté súbory a priečinky\nvo fronte sťahovania aj po dokončení.\nPo stiahnutí budú mať položky frontu stav 'Dokončené'\na tieto dokončené záznamy môžete neskôr kedykoľvek odstrániť.\nPomocou tejto funkcie môžete sledovať, ktoré súbory sú už\nstiahnuté, priamo vo fronte sťahovania bez potreby\nkontrolovať okno dokončených sťahovaní.\nAk fyzicky presuniete alebo odstránite stiahnuté súbory\nz cieľového umiestnenia, nespôsobí to automatické odstránenie\nzodpovedajúcich dokončených záznamov z frontu.\nAni vypnutie tejto funkcie neodstráni existujúce dokončené položky\nz frontu; musíte to urobiť ručne.",
            "Segment size (in MB, 0 = auto segment size)": "Veľkosť segmentu (v MB, 0 = automatická veľkosť segmentu)",
            "Allow simultaneous uploads by one user from few NMDC hubs": "Povoliť súčasné odosielanie jednému používateľovi z viacerých hubov NMDC",
            "Allow simultaneous uploads by one IP": "Povoliť súčasné odosielanie na jednu IP adresu",
        }}
        def entries(path):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") == "UISettingsDownloads" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-retention-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)], check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)], check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(78, len(original))
            self.assertEqual(17, len(expected["UISettingsDownloads"]))
            self.assertTrue(expected["UISettingsDownloads"].keys() <= original.keys())
            for source, value in original.items():
                with self.subTest(source=source):
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    if source in expected["UISettingsDownloads"]:
                        self.assertEqual(expected["UISettingsDownloads"][source], text)
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        self.assertEqual(Counter(re.findall(r"\d+", source)), Counter(re.findall(r"\d+", text)))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_download_priority_and_drop_thresholds(self):
        expected = {"UISettingsDownloads": {
            "This HTTP proxy will only be used to request\nthe Public Hub List specified above.\nIt's not used for uploads, downloads, or connecting to a hub.":
                "Tento HTTP proxy sa použije iba na získanie\nzoznamu verejných hubov uvedeného vyššie.\nNepoužíva sa na odosielanie, sťahovanie ani pripájanie k hubu.",
            "Files with the same or under the entered size\nwill be set to Highest as priority. (default: 64 KiB)":
                "Súbory s veľkosťou rovnou alebo menšou než zadaná hodnota\nbudú mať najvyššiu prioritu. (predvolené: 64 KiB)",
            "Files with the same or under the entered size, but above Highest\nprio max size, will be set to High as priority. (default: 0 KiB)":
                "Súbory s veľkosťou rovnou alebo menšou než zadaná hodnota, ale nad maximom najvyššej\npriority, budú mať vysokú prioritu. (predvolené: 0 KiB)",
            "Files with the same or under the entered size, but above Highest and\nHigh prio max size, will be set to Normal as priority. (default: 0 KiB)":
                "Súbory s veľkosťou rovnou alebo menšou než zadaná hodnota, ale nad maximami najvyššej a\nvysokej priority, budú mať normálnu prioritu. (predvolené: 0 KiB)",
            "Files with the same or under the entered size, but above Highest, High\nand Normal prio max size, will be set to Low as priority. (default: 0 KiB)":
                "Súbory s veľkosťou rovnou alebo menšou než zadaná hodnota, ale nad maximami najvyššej, vysokej\na normálnej priority, budú mať nízku prioritu. (predvolené: 0 KiB)",
            "Auto-drop settings": "Nastavenia automatického vyraďovania",
            "If the download speed of a file is below the entered value,\nthe source will be dropped. (default: 1024 B/s)":
                "Ak je rýchlosť sťahovania súboru nižšia než zadaná hodnota,\nzdroj sa vyradí. (predvolené: 1024 B/s)",
            "Amount of time (in seconds) that has to pass since the download\nstarted before a source can be dropped. (default: 15 s)":
                "Čas (v sekundách), ktorý musí uplynúť od začiatku sťahovania,\nkým sa zdroj môže vyradiť. (predvolené: 15 s)",
            "This is the maximal time since the last chunk of data for\nthe file was received. It is meant to improve speed measurement\naccuracy and to prevent dropping sources that are going to be\ndisconnected only because of their current inactivity. (default: 10 s)":
                "Toto je maximálny čas od prijatia posledného bloku dát\nsúboru. Slúži na zvýšenie presnosti merania rýchlosti\na zabránenie vyraďovaniu zdrojov, ktoré by sa odpojili\niba pre svoju aktuálnu nečinnosť. (predvolené: 10 s)",
            "Minimum amount of sources before any sources\nwill be dropped. (default: 1)":
                "Minimálny počet zdrojov potrebný na to, aby sa niektoré\nzdroje mohli vyradiť. (predvolené: 1)",
            "Minimum filesize before sources will be dropped (in KiB). (default: 0 KiB)":
                "Minimálna veľkosť súboru na vyraďovanie zdrojov (v KiB). (predvolené: 0 KiB)",
            "Drop sources below (B/s)": "Vyradiť zdroje pod (B/s)",
            "Min. elapsed (s)": "Min. uplynutý čas (s)", "Min sources online": "Min. počet dostupných zdrojov",
            "With this enabled, files that are larger than Low prio max size\nare set to Lowest as priority.\nIf disabled, the files will be set to Normal as priority.":
                "Ak je táto možnosť zapnutá, súbory väčšie než maximum nízkej priority\nbudú mať najnižšiu prioritu.\nAk je vypnutá, súbory budú mať normálnu prioritu.",
        }}
        def entries(path):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") == "UISettingsDownloads" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-priorities-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(15, len(expected["UISettingsDownloads"]))
            self.assertTrue(expected["UISettingsDownloads"].keys() <= original.keys())
            for source, translation in expected["UISettingsDownloads"].items():
                with self.subTest(source=source):
                    value = original[source]
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertEqual(translation, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    self.assertEqual(source.count("\n"), text.count("\n"))
                    self.assertEqual(Counter(re.findall(r"\d+", source)), Counter(re.findall(r"\d+", text)))
                    units = r"(?<![A-Za-z/])(?:KiB|B/s)(?![A-Za-z/])|(?<=\d )s\b|(?<=\()s(?=\))"
                    self.assertEqual(Counter(re.findall(units, source)), Counter(re.findall(units, text)))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_main_window_about_and_credits(self):
        expected = {"MainWindow": {
            "EiskaltDC++ is a graphical client for Direct Connect and ADC protocols.":
                "EiskaltDC++ je grafický klient pre protokoly Direct Connect a ADC.",
            "DC++ core version: %1 (modified)": "Verzia jadra DC++: %1 (upravená)",
            "Home page: ": "Domovská stránka: ", "Total up: <b>%1</b>": "Celkovo odoslané: <b>%1</b>",
            "Total down: <b>%1</b>": "Celkovo stiahnuté: <b>%1</b>", "Ratio: <b>%1</b>": "Pomer: <b>%1</b>",
            'Please use <a href="https://github.com/Delitants/eiskaltdcpp/issues">https://github.com/Delitants/eiskaltdcpp/issues</a> to report bugs.<br/>':
                'Na hlásenie chýb použite <a href="https://github.com/Delitants/eiskaltdcpp/issues">https://github.com/Delitants/eiskaltdcpp/issues</a>.<br/>',
            "<b>Developers</b><br/>": "<b>Vývojári</b><br/>",
            '2026 <a href="mailto:admin@nlight.org.ua">Neolo</a><br/>':
                '2026 <a href="mailto:admin@nlight.org.ua">Neolo</a><br/>',
            "<b>Graphic files</b><br/>": "<b>Grafické súbory</b><br/>",
            "Russian translation<br/>": "Ruský preklad<br/>",
            "Belarusian translation<br/>": "Bieloruský preklad<br/>",
            "Hungarian translation<br/>": "Maďarský preklad<br/>",
            "French translation<br/>": "Francúzsky preklad<br/>",
            "Polish translation<br/>": "Poľský preklad<br/>",
            "Ukrainian translation<br/>": "Ukrajinský preklad<br/>",
            "Serbian (Cyrillic) translation<br/>": "Srbský preklad (cyrilika)<br/>",
            "Serbian (Latin) translation<br/>": "Srbský preklad (latinka)<br/>",
            "Spanish translation<br/>": "Španielsky preklad<br/>",
            "Basque translation<br/>": "Baskický preklad<br/>",
            "Bulgarian translation<br/>": "Bulharský preklad<br/>",
            "Slovak translation<br/>": "Slovenský preklad<br/>",
            "Czech translation<br/>": "Český preklad<br/>",
            "German translation<br/>": "Nemecký preklad<br/>",
            "Greek translation<br/>": "Grécky preklad<br/>",
            "Italian translation<br/>": "Taliansky preklad<br/>",
            "Portuguese (Brazil) translation<br/>": "Portugalský preklad (Brazília)<br/>",
            "Vietnamese translation<br/>": "Vietnamský preklad<br/>",
            "Chinese (China) translation<br/>": "Čínsky preklad (Čína)<br/>",
            "Swedish (Sweden) translation<br/>": "Švédsky preklad (Švédsko)<br/>",
            "Turkish translation<br/>": "Turecký preklad<br/>",
            "Danish translation<br/>": "Dánsky preklad<br/>",
            "Georgian translation<br/>": "Gruzínsky preklad<br/>",
            "lead developer": "hlavný vývojár", "release manager": "správca vydaní",
            "Arch Linux maintainer": "správca pre Arch Linux", "developer": "vývojár",
            "Debian/Ubuntu maintainer": "správca pre Debian/Ubuntu",
            "translations coordinator": "koordinátor prekladov", "macOS maintainer": "správca pre macOS",
            "MS Windows maintainer": "správca pre MS Windows",
            "creator of the logo of the project": "tvorca loga projektu",
            "tiny updates of the logo": "drobné úpravy loga",
            "Participate in the translation. It is easy:": "Zapojte sa do prekladu. Je to jednoduché:",
        }}
        def entries(path):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") == "MainWindow" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-about-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(198, len(original))
            self.assertEqual(44, len(expected["MainWindow"]))
            self.assertTrue(expected["MainWindow"].keys() <= original.keys())
            for source, value in original.items():
                with self.subTest(source=source):
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    if source in expected["MainWindow"]:
                        self.assertEqual(expected["MainWindow"][source], text)
                        self.assertEqual(Counter(re.findall(r"%\d+", source)),
                                         Counter(re.findall(r"%\d+", text)))
                        self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_main_window_actions_and_prompts(self):
        expected = {"MainWindow": {
            "Kill all humans?": "Zabiť všetkých ľudí?", "Action confirm": "Potvrdenie akcie",
            "Open magnet link": "Otvoriť magnetový odkaz", "Match all listings": "Porovnať všetky zoznamy",
            "Queued Users": "Používatelia vo fronte", "Debug Console": "Ladiaca konzola",
            "Secretary": "Zápisník", "Speed limit On/Off": "Zapnúť/vypnúť obmedzenie rýchlosti",
            "Torrent files": "Súbory torrent", "Open magnet or Torrent file": "Otvoriť magnet alebo súbor torrent",
            "Enter a magnet link or local .torrent path:": "Zadajte magnetový odkaz alebo cestu k miestnemu súboru .torrent:",
            "Torrent support is not ready.": "Podpora torrentov nie je pripravená.",
            "Exit": "Ukončiť", "Source code (git)": "Zdrojový kód (git)",
            " | DHT nodes: %1": " | Uzly DHT: %1", "Delayed": "Odložené",
            "Enter magnet link:": "Zadajte magnetový odkaz:", "Script Engine": "Skriptovací modul",
            "Setup speed limits": "Nastaviť obmedzenia rýchlosti", "Torrent: %1": "Torrent: %1",
            "Nickname required": "Vyžaduje sa prezývka",
            "Enter a nickname in Preferences before connecting to hubs.":
                "Pred pripojením k hubom zadajte prezývku v nastaveniach.",
            "Download program": "Stiahnuť program", "[No name]": "[Bez názvu]",
        }}
        def entries(path):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") == "MainWindow" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-main-actions-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(24, len(expected["MainWindow"]))
            self.assertTrue(expected["MainWindow"].keys() <= original.keys())
            for source, translation in expected["MainWindow"].items():
                with self.subTest(source=source):
                    value = original[source]
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertEqual(translation, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    self.assertEqual(Counter(re.findall(r"%\d+", source)),
                                     Counter(re.findall(r"%\d+", text)))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_migration_and_shared_ui_messages(self):
        expected = {"QObject": {
            "Old or incompatible EiskaltDC++ GUI settings were detected.\n\n"
            "Safe settings such as hubs, account details, sharing, downloads and history were kept. Theme, chat color, window layout and table-column state from the older config were reset because they can break live light/dark switching on current macOS.\n\n":
                "Boli zistené staré alebo nekompatibilné nastavenia rozhrania EiskaltDC++.\n\n"
                "Bezpečné nastavenia, ako sú huby, údaje účtu, zdieľanie, sťahovanie a história, boli zachované. Téma, farba chatu, rozloženie okien a stav stĺpcov tabuliek zo staršej konfigurácie boli obnovené na predvolené hodnoty, pretože môžu narušiť prepínanie svetlého a tmavého režimu za behu v aktuálnom macOS.\n\n",
            "A backup of the previous GUI config was saved here:\n%1":
                "Záloha predchádzajúcej konfigurácie rozhrania bola uložená sem:\n%1",
            "The previous GUI config could not be backed up, but incompatible visual settings were still discarded.":
                "Predchádzajúcu konfiguráciu rozhrania sa nepodarilo zálohovať, ale nekompatibilné vizuálne nastavenia boli napriek tomu odstránené.",
            "EiskaltDC++ settings updated": "Nastavenia EiskaltDC++ boli aktualizované",
            "Images (%1);;All files (*)": "Obrázky (%1);;Všetky súbory (*)",
            "Drag to resize input area": "Ťahaním zmeníte veľkosť vstupnej oblasti",
            "%1 (%2)": "%1 (%2)", "Click to expand or collapse": "Kliknutím rozbalíte alebo zbalíte",
            "Spoiler: %1": "Spoiler: %1", "Open Externally": "Otvoriť v externej aplikácii",
            "Close": "Zavrieť", "Default (EiskaltDC++)": "Predvolené (EiskaltDC++)",
        }}
        def entries(path):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") == "QObject" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-migration-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(15, len(original))
            self.assertTrue(expected["QObject"].keys() <= original.keys())
            for source, value in original.items():
                with self.subTest(source=source):
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    if source in expected["QObject"]:
                        self.assertEqual(expected["QObject"][source], text)
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        self.assertEqual(Counter(re.findall(r"%\d+", source)),
                                         Counter(re.findall(r"%\d+", text)))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_navigation_and_settings_messages(self):
        expected = {
            "FavoriteUsers": {"Browse Files": "Prehliadať súbory"},
            "Notification": {"Setup speed limits": "Nastaviť obmedzenia rýchlosti"},
            "QueuedUsers": {"Queued Users": "Používatelia vo fronte"},
            "Settings": {"Torrent preferences": "Nastavenia torrentov",
                         "Could not save Torrent settings: %1": "Nepodarilo sa uložiť nastavenia torrentov: %1",
                         "History": "História"},
            "SettingsLog": {"Help": "Pomocník"},
            "SideBarModel": {"Share Browsers": "Prehliadače zdieľaných súborov",
                             "Queued Users": "Používatelia vo fronte", "Secretary": "Zápisník",
                             "Debug Console": "Ladiaca konzola", "Widgets": "Widgety"},
            "ToolBar": {"Close": "Zavrieť"},
            "UIUserCommands": {"PM": "Súkromná správa"},
            "WulforSettings": {"GUI setting %1: %2": "Nastavenie rozhrania %1: %2",
                               "Change GUI setting %1 to %2": "Zmeniť nastavenie rozhrania %1 na %2"},
        }
        counts = {"FavoriteUsers": 6, "Notification": 7, "QueuedUsers": 2, "Settings": 16,
                  "SettingsLog": 2, "SideBarModel": 19, "ToolBar": 2, "UIUserCommands": 18,
                  "WulforSettings": 2}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-navigation-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(Counter(re.findall(r"%\d+", source)),
                                             Counter(re.findall(r"%\d+", text)))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_chat_and_magnet_dialogs(self):
        expected = {
            "HubFrame": {
                "Search text": "Hľadať text", "IPv6": "IPv6", "E-mail": "E-mail",
                "[User went offline]": "[Používateľ sa odpojil]",
                "Invalid keywords": "Neplatné kľúčové slová", "search": "hľadať",
                "%1 (%2)": "%1 (%2)", "Filter": "Filter",
                "Invalid command syntax.": "Neplatná syntax príkazu.",
                "List of keywords:\n": "Zoznam kľúčových slov:\n",
                "/kword add <keyword> - add user-defined keyword which will be highlighted in the chat\n":
                    "/kword add <keyword> - pridať vlastné kľúčové slovo, ktoré bude zvýraznené v chate\n",
                "/kword purge <keyword> - remove user-defined keyword\n":
                    "/kword purge <keyword> - odstrániť vlastné kľúčové slovo\n",
                "/kword list - full list of keywords which will be highlighted in the chat\n":
                    "/kword list - úplný zoznam kľúčových slov, ktoré budú zvýraznené v chate\n",
                "/rebuild - rebuild hash\n": "/rebuild - znova vytvoriť haš\n",
                "/refresh - update own file list\n": "/refresh - aktualizovať vlastný zoznam súborov\n",
                "/ws param value - set gui option param in value (without value return current value of option)\n":
                    "/ws param value - nastaviť voľbu rozhrania param na value (bez value vráti aktuálnu hodnotu voľby)\n",
                "/dcpps param value - set core option param in value (without value return current value of option)\n":
                    "/dcpps param value - nastaviť voľbu jadra param na value (bez value vráti aktuálnu hodnotu voľby)\n",
            },
            "UIHubFrame": {"Shift+F3": "Shift+F3", "F3": "F3",
                           "Press Shift+Enter for multiline input": "Stlačte Shift+Enter pre viacriadkový vstup"},
            "UIPrivateMessage": {"Find:": "Hľadať:", "Shift+F3": "Shift+F3", "F3": "F3",
                                 "Press Shift+Enter for multiline input": "Stlačte Shift+Enter pre viacriadkový vstup"},
            "UIMagnet": {
                "Magnet link viewer": "Prehliadač magnetových odkazov",
                "Copy magnet link to clipboard": "Kopírovať magnetový odkaz do schránky",
                "Copy search link to clipboard": "Kopírovať odkaz na vyhľadávanie do schránky",
                "Adds magnet to download queue. If autosearch for alternatives is enabled\nin options, file will be downloaded without opening search dialog.":
                    "Pridá magnet do frontu sťahovania. Ak je v nastaveniach zapnuté automatické\nvyhľadávanie alternatív, súbor sa stiahne bez otvorenia dialógu vyhľadávania.",
            },
        }
        counts = {"HubFrame": 118, "UIHubFrame": 19, "UIPrivateMessage": 20, "UIMagnet": 12}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-chat-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(source.count("\n"), text.count("\n"))
                            self.assertEqual(Counter(re.findall(r"%\d+", source)),
                                             Counter(re.findall(r"%\d+", text)))
                            if source.startswith("/"):
                                self.assertEqual(source.split(" - ")[0], text.split(" - ")[0])

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_gui_choices_and_picture_storage(self):
        expected = {"SettingsGUI": {
            "Portuguese (Brazil)": "Portugalčina (Brazília)",
            "Serbian (Cyrillic)": "Srbčina (cyrilika)", "Serbian (Latin)": "Srbčina (latinka)",
            "Basque": "Baskičtina", "Czech": "Čeština", "German": "Nemčina",
            "Greek": "Gréčtina", "Italian": "Taliančina", "Vietnamese": "Vietnamčina",
            "Chinese (China)": "Čínština (Čína)", "Swedish (Sweden)": "Švédčina (Švédsko)",
            "Turkish": "Turečtina", "Danish": "Dánčina", "Georgian": "Gruzínčina",
            "Toolbar icon theme": "Téma ikon panela nástrojov", "Theme": "Téma",
            "Default": "Predvolená", "Monochrome": "Jednofarebná",
            "User list icon theme": "Téma ikon zoznamu používateľov", "Original": "Pôvodná",
            "Chat pictures": "Obrázky v chate", "Folder": "Priečinok",
            "Auto-clean files older than": "Automaticky odstrániť súbory staršie ako",
            " days": " dní", "Select chat pictures folder": "Vybrať priečinok obrázkov v chate",
        }}
        def entries(path):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") == "SettingsGUI" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-gui-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(48, len(original))
            self.assertTrue(expected["SettingsGUI"].keys() <= original.keys())
            for source, value in original.items():
                with self.subTest(source=source):
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    if source in expected["SettingsGUI"]:
                        self.assertEqual(expected["SettingsGUI"][source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_hub_filter_and_shortcut_dialogs(self):
        expected = {
            "SearchBlackListModel": {"Set text...": "Zadať text..."},
            "ShortcutGetter": {"Modify shortcut": "Upraviť klávesovú skratku",
                               "Press the key combination you want to assign":
                                   "Stlačte kombináciu klávesov, ktorú chcete priradiť",
                               "Capture": "Zachytiť", "Capture keystrokes": "Zachytiť stlačenia klávesov"},
            "UIFavoriteHubEditor": {
                "This description will be shown in the list of favorite hubs.":
                    "Tento popis sa zobrazí v zozname obľúbených hubov.",
                "Mode": "Režim", "Active": "Aktívny", "Passive": "Pasívny", "Client ID": "ID klienta",
                "Minimum search interval (seconds)": "Minimálny interval vyhľadávania (sekundy)",
                "This description will be shown in the user list.":
                    "Tento popis sa zobrazí v zozname používateľov."},
            "UIIPFilter": {"IP Filter": "Filter IP", "IN": "Prichádzajúce",
                           "OUT": "Odchádzajúce", "OK": "OK"},
            "UIQuickConnect": {"IP address of DNS name of hub you want to connect.":
                                   "IP adresa alebo DNS názov hubu, ku ktorému sa chcete pripojiť."},
            "UIScriptManager": {"Ask user": "Opýtať sa používateľa", "Reload it": "Znova načítať",
                                "When script changed": "Pri zmene skriptu"},
            "UISearchBlacklistDialog": {"Blacklist": "Čierna listina", "OK": "OK"},
        }
        counts = {"SearchBlackListModel": 4, "ShortcutGetter": 5, "UIFavoriteHubEditor": 27,
                  "UIIPFilter": 12, "UIQuickConnect": 2, "UIScriptManager": 4,
                  "UISearchBlacklistDialog": 3}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-dialogs-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_file_queue_and_search_sections(self):
        expected = {
            "DownloadQueue": {"Magnet": "Magnet", "Copy web-magnet": "Kopírovať webový magnet",
                              "Properties of magnet": "Vlastnosti magnetu",
                              "Total files: <b>%1</b> Total size: <b>%2</b>":
                                  "Celkový počet súborov: <b>%1</b> Celková veľkosť: <b>%2</b>"},
            "FileBrowserModel": {
                "%1 [%2 Gb]": "%1 [%2 Gb]",
                "File marked as a duplicate of another file: %1":
                    "Súbor označený ako duplikát iného súboru: %1",
                "<b>Media Info:</b><br/>": "<b>Informácie o médiu:</b><br/>",
                "&nbsp;&nbsp;<b>Video:</b> %1<br/>": "&nbsp;&nbsp;<b>Video:</b> %1<br/>",
                "&nbsp;&nbsp;<b>Audio:</b> %1<br/>": "&nbsp;&nbsp;<b>Zvuk:</b> %1<br/>",
                "&nbsp;&nbsp;<b>Bitrate:</b> %1<br/>": "&nbsp;&nbsp;<b>Dátový tok:</b> %1<br/>",
                "&nbsp;&nbsp;<b>Resolution:</b> %1<br/><br/>":
                    "&nbsp;&nbsp;<b>Rozlíšenie:</b> %1<br/><br/>",
                "File already exists: %1": "Súbor už existuje: %1",
                "Bitrate": "Dátový tok", "Resolution": "Rozlíšenie",
                "Video": "Video", "Audio": "Zvuk"},
            "FinishedTransfers": {"Open directory": "Otvoriť priečinok"},
            "FinishedTransfersModel": {"Full": "Úplný", "%1/s": "%1/s"},
            "HashProgress": {"%1% %2 left": "%1% zostáva %2"},
            "SearchModel": {"Host": "Hostiteľ", "File already exists: %1": "Súbor už existuje: %1"},
            "ShareBrowserSearch": {"Found %1 items": "Počet nájdených položiek: %1"},
            "UIShareBrowserSearch": {"Search only files": "Hľadať iba súbory",
                                     "Search only directories": "Hľadať iba priečinky",
                                     "Search all": "Hľadať všetko"},
        }
        counts = {"DownloadQueue": 32, "FileBrowserModel": 18, "FinishedTransfers": 2,
                  "FinishedTransfersModel": 12, "HashProgress": 12, "SearchModel": 13,
                  "ShareBrowserSearch": 2, "UIShareBrowserSearch": 6}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-file-search-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)
                            self.assertEqual(Counter(re.findall(r"%\d+", source)),
                                             Counter(re.findall(r"%\d+", text)))
                            self.assertEqual(re.findall(r"<[^>]+>|&nbsp;", source),
                                             re.findall(r"<[^>]+>|&nbsp;", text))

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_console_and_notes_sections(self):
        expected = {
            "CmdDebug": {"Debug Console": "Ladiaca konzola"},
            "DialogScriptConsole": {"Output": "Výstup", "Code": "Kód"},
            "Secretary": {"Secretary": "Zápisník", "Search text": "Hľadať text",
                          "Clear notes": "Vymazať poznámky", "Find in notes": "Hľadať v poznámkach"},
            "UICmdDebug": {"Shift+F3": "Shift+F3", "F3": "F3",
                           "Use comma as separator": "Použiť čiarku ako oddeľovač",
                           "Filter by IP": "Filtrovať podľa IP",
                           "Lines:": "Riadky:", "Clear now": "Vymazať teraz"},
            "UISecretary": {"Shift+F3": "Shift+F3", "F3": "F3",
                            "Magnet links": "Magnetové odkazy", "Search links": "Odkazy na vyhľadávanie",
                            "Public chat": "Verejný chat", "BitTorrent links": "Odkazy BitTorrent",
                            "Filter duplicates": "Filtrovať duplicity",
                            "Use comma as separator": "Použiť čiarku ako oddeľovač",
                            "Additional keywords": "Ďalšie kľúčové slová",
                            "Filter hubs by IP": "Filtrovať huby podľa IP",
                            "Lines:": "Riadky:", "Clear now": "Vymazať teraz"},
        }
        counts = {"CmdDebug": 1, "DialogScriptConsole": 5, "Secretary": 9,
                  "UICmdDebug": 11, "UISecretary": 19}
        def entries(path):
            return {c.findtext("name"): {m.findtext("source"): m.find("translation")
                    for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
                    for c in ET.parse(path).getroot().findall("context")
                    if c.findtext("name") in expected}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-notes-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(set(expected), set(original))
            for context, required in expected.items():
                self.assertEqual(counts[context], len(original[context]))
                self.assertTrue(required.keys() <= original[context].keys())
                for source, value in original[context].items():
                    with self.subTest(context=context, source=source):
                        self.assertNotEqual("unfinished", value.get("type"))
                        text = "".join(value.itertext())
                        self.assertTrue(text.strip())
                        self.assertIn(source, compiled.get(context, {}))
                        self.assertEqual(text, "".join(compiled[context][source].itertext()))
                        if source in required:
                            self.assertEqual(required[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_sharing_section_is_complete(self):
        required = {
            "Add", "Remove", "Skip zero sized files", "8",
            "Use ADL search only in own file list",
            "Hashing delay on startup (seconds, -1 = infinity)",
            "Share files with the Hidden attribute set and files inside directory with\n"
            "the Hidden attribute set.\n"
            "With this option off, files starting with a . (full stop) will not be shared.\n"
            "Note: this option requires a refresh of the file list,\n"
            "and may cause the client to appear to freeze.",
        }
        exact = {"Add": "Pridať", "Remove": "Odstrániť", "8": "8",
                 "Skip zero sized files": "Vynechať súbory s nulovou veľkosťou",
                 "Use ADL search only in own file list": "Používať vyhľadávanie ADL iba vo vlastnom zozname súborov",
                 "Hashing delay on startup (seconds, -1 = infinity)":
                     "Oneskorenie hašovania pri spustení (sekundy, -1 = nekonečno)"}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        def entries(catalog):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") == "UISettingsSharing" for m in c.findall("message")
                    if m.find("translation").get("type") not in {"obsolete", "vanished"}}
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-sharing-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            original, compiled = entries(path), entries(output)
            self.assertEqual(39, len(original))
            self.assertTrue(required.issubset(original))
            for source, value in original.items():
                with self.subTest(source=source):
                    self.assertNotEqual("unfinished", value.get("type"))
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    if source in required:
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        self.assertEqual(Counter(re.findall(r"-?\d+", source)),
                                         Counter(re.findall(r"-?\d+", text)))
                        if source in exact:
                            self.assertEqual(exact[source], text)
                        else:
                            self.assertEqual(
                                "Zdieľať súbory s nastaveným atribútom Skrytý a súbory v priečinku\n"
                                "s nastaveným atribútom Skrytý.\n"
                                "Ak je táto možnosť vypnutá, súbory začínajúce bodkou (.) sa nebudú zdieľať.\n"
                                "Poznámka: táto možnosť vyžaduje obnovenie zoznamu súborov\n"
                                "a môže spôsobiť, že klient bude pôsobiť ako zamrznutý.", text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_personal_history_and_notifications(self):
        required = {
            "UISettingsHistory": {"Clear search history on program exit",
                                  "Clear download directories history on program exit", "Clear now",
                                  "Number of search history elements to store:",
                                  "Number of download directories history elements to store:",
                                  "Memorize TTH search phrases"},
            "UISettingsNotification": {"Text", "Favorite user join/part", "D-Bus", "Test",
                                       "Play sound on active PM window",
                                       "Enable notifications with not active main window",
                                       "Enable notifications with active main window"},
            "UISettingsPersonal": {
                "This nickname, or handle, will identify you when you join a hub.\n"
                "It can be overridden on a per-hub basis in the Favorite Hub Properties.",
                "E-Mail", "Mbit/s",
                "This drop-down contains a number of common upload speeds (in Mbit/s),\n"
                "please choose the one closest to your own.\n"
                "Do not put in your download speed; this setting is a measure\n"
                "of how fast a source you are likely to be for other users.",
            },
        }
        exact = {
            "Enable notifications with not active main window": "Zapnúť upozornenia ak nie je aktívne hlavné okno",
            "Enable notifications with active main window": "Zapnúť upozornenia ak je aktívne hlavné okno",
            "Play sound on active PM window": "Prehrať zvuk aj pri aktívnom okne súkromnej správy",
            "Clear now": "Vymazať teraz",
        }
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-personal-") as work:
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
                        if source not in {"Text", "Test", "D-Bus", "E-Mail", "Mbit/s"}:
                            self.assertNotEqual(source, text)
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        for token in ("TTH", "D-Bus", "Mbit/s"):
                            if token in source:
                                self.assertRegex(text, r"(?<![A-Za-z/])" + re.escape(token) + r"(?![A-Za-z/])")
                        if source in exact:
                            self.assertEqual(exact[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_hubs_search_and_browser(self):
        required = {
            "ChatEdit": {"Address", "Select image"},
            "PublicHubs": {"Refreshing public hub list...", "Hub list downloaded... (%1)",
                           "Locally cached (as of %1) version of the hub list loaded (%2)",
                           "Cached hub list is corrupted or unsupported",
                           "Downloaded hub list is corrupted or unsupported (%1)"},
            "PublicHubsList": {"Public hub list URLs"},
            "SearchFrame": {"Magnet", "Copy web-magnet", "Properties of magnet", "Blacklist...",
                            "Blacklist", "Add to Blacklist", "Searching for %1 ...",
                            'Search progress of "%1" is %2%'},
            "ShareBrowser": {"Restrictions", "Magnet", "Copy web-magnet", "Properties of magnet",
                             "Add restriction", "Remove restriction", "Open directory",
                             "Share browser error: %1", "Selected %1 from %2 items; ",
                             "Enter restriction size (in GB)", "Share browser", "Matched %1 files"},
            "UIPublicHubs": {"Refresh"},
            "UserListModel": {"<b>Hub role</b>: Operator", "<b>Hub role</b>: User",
                              "<br/><b>Favorite user</b>", "E-mail", "Exact share size", "IPv6"},
        }
        exact = {"Add restriction": "Pridať obmedzenie", "Remove restriction": "Odstrániť obmedzenie",
                 "<b>Hub role</b>: Operator": "<b>Rola v hube</b>: Operátor",
                 "<b>Hub role</b>: User": "<b>Rola v hube</b>: Používateľ"}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-browser-") as work:
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
                        if source not in {"Magnet", "E-mail", "IPv6"}:
                            self.assertNotEqual(source, text)
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(Counter(re.findall(r"%\d+|%n", source)),
                                         Counter(re.findall(r"%\d+|%n", text)))
                        self.assertEqual(source.count("%"), text.count("%"))
                        self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                        self.assertEqual(source[len(source.rstrip()):], text[len(text.rstrip()):])
                        for token in ("GB", "URL", "IPv6"):
                            if token in source:
                                self.assertIn(token, text)
                        if source in exact:
                            self.assertEqual(exact[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_transfer_translations(self):
        required = {
            "TransferView": {"Flags", "Encryption", "Protocol", "Magnet", "Cancel download",
                             "Show only transferred files", "Torrent details", "Pause Torrent",
                             "Resume Torrent", "Uploaded %1 (%2%) "},
            "TransferViewModel": {"Flags", "Host", "Encryption", "Paused", "Seeding", "Waiting",
                                  "Download (%1 peers)", "Upload (%1 peers)",
                                  "%1 | Downloaded %2 (%3%)", "%1 | Uploaded %2"},
            "UIFileHasher": {"Copy magnet link to clipboard", "Copy search link to clipboard"},
            "UIFinishedTransfers": {"Show only completely finished transfers", "Full only"},
        }
        exact = {"Paused": "Pozastavené", "Seeding": "Zdieľanie", "Waiting": "Čakanie",
                 "Cancel download": "Zrušiť sťahovanie", "Pause Torrent": "Pozastaviť torrent",
                 "Resume Torrent": "Pokračovať v torrente", "Download (%1 peers)": "Sťahovanie (%1 partnerov)",
                 "Upload (%1 peers)": "Odosielanie (%1 partnerov)"}
        path = ROOT / "eiskaltdcpp-qt/translations/sk.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-sk-transfers-") as work:
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
                        if source != "Magnet":
                            self.assertNotEqual(source, text)
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(Counter(re.findall(r"%\d+|%n", source)),
                                         Counter(re.findall(r"%\d+|%n", text)))
                        self.assertEqual(source.count("%"), text.count("%"))
                        self.assertEqual(source[len(source.rstrip()):], text[len(text.rstrip()):])
                        if source in exact:
                            self.assertEqual(exact[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_connection_catalog_survives_compilation(self):
        self._assert_connection_catalog_survives_compilation("sk")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_slovak_tls_choices_survive_compilation(self):
        self._assert_tls_choices_survive_compilation("sk", {
            "TLS settings": "Nastavenia TLS",
            "Disable TLS": "Zakázať TLS",
            "Allow TLS": "Povoliť TLS",
            "Require TLS": "Vyžadovať TLS",
            "Allow untrusted certificate for hubs": "Povoliť nedôveryhodný certifikát pre huby",
            "Allow untrusted certificate for clients": "Povoliť nedôveryhodný certifikát pre klientov",
        })

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_active_catalog_is_complete_and_compiles(self):
        self._assert_active_catalog_is_complete_and_compiles("ka")

    def test_georgian_rich_help_and_commands(self):
        self._assert_rich_help_and_commands("ka", "ან")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_connection_help(self):
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    for m in c.findall("message")}
        original = entries(path)
        required = [("SettingsConnection", "Program need root privileges to open ports less than 1024")]
        required += [("UISettingsConnection", s) for s in (
            "Don't allow Hub/UPnP/DHT to override", "When enabled can hang program on exit.",
            "Automatically update external IP (restart required)", "Enable Throttle",
            "Alternate speed limit at time from ", "Reconnect to hub (seconds)",
            "Type-of-Service (ToS)", "Minimal cost")]
        prefixes = (
            "Select this mode if your computer is connected directly",
            "Select this mode if you are behind a firewall (router)",
            "Select this mode if none of the previous modes apply.",
            "Select this mode if you are behind a firewall/router",
            '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN"',
        )
        for prefix in prefixes:
            matches = [k for k in original if k[0] == "UISettingsConnection" and k[1].startswith(prefix)]
            self.assertEqual(1, len(matches), prefix)
            required.extend(matches)
        required += [("UISettingsConnection", source) for source in (
            "Select this mode if you can make unrestricted outbound connections.\n"
            "    This includes connecting to any IP address on any port.\n"
            "    Choose this option if unsure.",
            "Select this mode if you want to make outbound connections through\n"
            "    a SOCKS proxy. The caveats for Passive mode also apply here.\n"
            "    SOCKS5 proxies are not the same as HTTP proxies.\n"
            "    DC++'s SOCKS5 support does not include GSSAPI.",
        )]
        self.assertEqual(16, len(required))
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-connection-help-") as work:
            binary, output = Path(work) / "catalog.qm", Path(work) / "compiled.ts"
            subprocess.run(["lrelease", "-nounfinished", str(path), "-qm", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["lconvert", "-i", str(binary), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            compiled = entries(output)
            for key in required:
                with self.subTest(source=key[1][:80]):
                    self.assertIn(key, original)
                    value = original[key]
                    source = key[1]
                    self.assertNotIn(value.get("type"), {"unfinished", "obsolete", "vanished"})
                    text = "".join(value.itertext())
                    self.assertTrue(text.strip())
                    self.assertNotEqual(source, text)
                    self.assertIn(key, compiled)
                    self.assertEqual(text, "".join(compiled[key].itertext()))
                    self.assertEqual(source.count("\n"), text.count("\n"))
                    self.assertEqual(source[len(source.rstrip()):], text[len(text.rstrip()):])
                    self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                    self.assertEqual(Counter(re.findall(r"\d+", source)),
                                     Counter(re.findall(r"\d+", text)))
                    for token in ("TCP", "UDP", "TLS", "UPnP", "DHT", "IP", "SOCKS5",
                                  "GSSAPI", "HTTP", "DC++", "EiskaltDC++", "TOS", "ToS", "Linux"):
                        if token in source:
                            self.assertIn(token, text)
                    if source.startswith("<!DOCTYPE"):
                        for phrase in ("ამ TOS მნიშვნელობებიდან მაქსიმუმ ერთის მითითებაა შესაძლებელი.",
                                       "ამ პარამეტრს სუპერმომხმარებლის უფლებები სჭირდება!"):
                            self.assertIn(phrase, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_hub_chat_translations(self):
        required = {
            "IPv6", "Filter", "/back - set away-mode off\n", "/browse <nick> - browse user files\n",
            "/clear - clear chat window\n",
            "/kword add <keyword> - add user-defined keyword which will be highlighted in the chat\n",
            "/kword purge <keyword> - remove user-defined keyword\n",
            "/kword list - full list of keywords which will be highlighted in the chat\n",
            "/magnet - default action with magnet (0-ask, 1-search, 2-download)\n",
            "/close - close this hub\n", "/fav - add this hub to favorites\n",
            "/grant <nick> - grant extra slot to user\n", "/help, /?, /h - show this help\n",
            "/info <nick> - show info about user\n", "/ratio [show] - show ratio [send in chat]\n",
            "/refresh - update own file list\n", "/pm <nick> - begin private chat with user\n",
            "/ws param value - set gui option param in value (without value return current value of option)\n",
            "/dcpps param value - set core option param in value (without value return current value of option)\n",
            "/luafile <file> - load Lua file\n", "/lua <chunk> - execute Lua chunk\n",
            " joins the chat", "%1 is now offline", " has been removed from favorites.",
            "Favorite hub added.", "Favorite hub already exists.", "User offline.",
            "Stored password sent...", "Redirect request received to a hub that's already connected",
            "Users count: %3/%1 | Total share: %2", "Users count: %1 | Total share: %2",
            'Sorry, but nick "%1" is already taken by another user.', "Search flood detected: %1",
        }
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") == "HubFrame" for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-hub-chat-") as work:
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
                    if source != "IPv6":
                        self.assertNotEqual(source, text)
                    else:
                        self.assertEqual(source, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    self.assertEqual(Counter(re.findall(r"%\d+|%n", source)),
                                     Counter(re.findall(r"%\d+|%n", text)))
                    self.assertEqual(source.count("\n"), text.count("\n"))
                    self.assertEqual(source[len(source.rstrip()):], text[len(text.rstrip()):])
                    self.assertEqual(source[:len(source)-len(source.lstrip())],
                                     text[:len(text)-len(text.lstrip())])
                    if source.startswith("/"):
                        self.assertTrue(text.startswith(source.split(" - ", 1)[0] + " - "))
                        self.assertEqual(re.findall(r"\d+", source), re.findall(r"\d+", text))
                    if "Lua" in source:
                        self.assertIn("Lua", text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_download_retention_and_search(self):
        required = {
            "Automatically search for alternative download locations",
            "Every few minutes, the client will search for files in your\n"
            "Download Queue to find more download locations (more users/sources).\n"
            "The autosearch will find exact matches by searching via hash.\n"
            "The drawback is that it may won't find much more sources\n"
            "and will prevent manual searches on hubs where\n"
            "a predefined search interval rule is in effect.",
            "Automatically match queue for auto search hits",
            "Enable to automatically download a user's list and match them\n"
            "as sources for files in your Download Queue when they've been\n"
            "discovered as a source for one of the files you're downloading.\n"
            "If you download files in sets, that user is probably a source\n"
            "for many more files you're downloading. Enable if unsure.\n"
            "The drawbacks are that it might add slower sources, possibly\n"
            "increasing the time needed to complete a download,\n"
            "and that you'll download a lot of file lists.",
            "Skip zero-byte files",
            "When this option is enabled, any zero byte files you\n"
            "try to download will be silently skipped.\n"
            "Sometimes empty files contain information through their naming.\n"
            "However every file, even an empty one, will consume some drive space.",
            "Don't download files already in share",
            "This option will stop you from downloading files\n"
            "which already exist in one of your shared directories.",
            "Don't download files already in the queue",
            "This option will stop you from downloading files\nwhich already exist in your queue.",
            "Keep downloaded filelists",
            "Enable this option if you want to keep downloaded filelists\n"
            "in the download queue after they have been finished.\n"
            "After downloaded the queue items will have 'Finished' status\n"
            "and you can delete these finished entries anytime later.\n"
            "Note that if you physically move/delete your downloaded files from\n"
            "their target location, it won't result automatic deletion of\n"
            "the corresponding finished entries from the queue.\n"
            "If you disable this function, it won't remove existing finished items\n"
            "from the queue either, you must do that manually.",
            "Enable this option if you want to keep downloaded files and folders\n"
            "in the download queue after they have been finished.\n"
            "After downloaded the queue items will have 'Finished' status\n"
            "and you can delete these finished entries anytime later.\n"
            "Using this function you can keep track of what files are already\n"
            "downloaded right there in the download queue without the need\n"
            "of checking the Finished Downloads window.\n"
            "Note that if you physically move/delete your downloaded files from\n"
            "their target location, it won't result automatic deletion of\n"
            "the corresponding finished entries from the queue.\n"
            "If you disable this function, it won't remove existing finished items\n"
            "from the queue either, you must do that manually.",
            "Interval for auto search alternates (minutes)",
            "Segment size (in MB, 0 = auto segment size)",
            "Allow simultaneous uploads by one user from few NMDC hubs",
            "Allow simultaneous uploads by one IP",
        }
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") == "UISettingsDownloads" for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-retention-") as work:
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
                    self.assertNotEqual(source, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    self.assertEqual(source.count("\n"), text.count("\n"))
                    self.assertEqual(Counter(re.findall(r"\d+", source)),
                                     Counter(re.findall(r"\d+", text)))
                    for token in ("MB", "NMDC", "IP"):
                        if token in source:
                            self.assertRegex(text, r"(?<![A-Za-z/])" + re.escape(token) + r"(?![A-Za-z/])")

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_slow_source_translations(self):
        required = {
            "Auto-drop settings",
            "If the download speed of a file is below the entered value,\n"
            "the source will be dropped. (default: 1024 B/s)",
            "Check every x second(s) if the source is slow. (default: 10 s)",
            "Amount of time (in seconds) that has to pass since the download\n"
            "started before a source can be dropped. (default: 15 s)",
            "This is the maximal time since the last chunk of data for\n"
            "the file was received. It is meant to improve speed measurement\n"
            "accuracy and to prevent dropping sources that are going to be\n"
            "disconnected only because of their current inactivity. (default: 10 s)",
            "Minimum amount of sources before any sources\nwill be dropped. (default: 1)",
            "Minimum filesize before sources will be dropped (in KiB). (default: 0 KiB)",
            "Drop sources below (B/s)", "Check every (s)", "Min. elapsed (s)",
            "Max inactivity (s)", "Min sources online", "Min filesize (KiB)",
            "Auto-drop slow sources for all queue items (except filelists)",
            'This option removes the slow sources for all queue items (except file lists).\n'
            'To enable autodrop, turn on either "Autodrop slow sources for all queue items"\n'
            'or "Remove slow file lists" (or both of them).',
            "Remove slow filelists",
            'With this option enabled, EiskaltDC++ will delete a file list whose speed\n'
            'was recognized as too slow.\n'
            'To enable autodrop, turn on either "Autodrop slow sources for all queue items"\n'
            'or "Remove slow file lists" (or both of them).',
            "Don't remove the source when auto-dropping, only disconnect",
            "With this option enabled, sources will only be disconnected\nbut not removed from the queue.",
        }
        exact = {
            "Don't remove the source when auto-dropping, only disconnect":
                "ავტომატური გათიშვისას წყარო არ წაიშალოს, მხოლოდ კავშირი გაწყდეს",
            "With this option enabled, sources will only be disconnected\nbut not removed from the queue.":
                "თუ ეს ჩართულია, წყაროებთან მხოლოდ კავშირი გაწყდება,\nმაგრამ ისინი რიგიდან არ წაიშლება.",
        }
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") == "UISettingsDownloads" for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-slow-sources-") as work:
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
                    self.assertNotEqual(source, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    self.assertEqual(source.count("\n"), text.count("\n"))
                    self.assertEqual(Counter(re.findall(r"\d+", source)),
                                     Counter(re.findall(r"\d+", text)))
                    for token in ("B/s", "KiB", "EiskaltDC++"):
                        if token in source:
                            self.assertRegex(text, r"(?<![A-Za-z/])" + re.escape(token) + r"(?![A-Za-z/])")
                    if source in exact:
                        self.assertEqual(exact[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_download_limits_and_priorities(self):
        required = {
            "Use unfinished downloads directory",
            "This is another method of limiting simultaneous downloads,\n"
            "designed to prevent new downloads if your existing downloads\n"
            "exceed a specific threshold. It's measured in kibibytes per second,\n"
            "and a value of zero will disable this limit.",
            "This limits the number of simultaneous downloads.\nA value of 0 means no limit.",
            "Maximum simultaneous downloads (0 = infinite)",
            "No new downloads if speed exceeds (KiB/s, 0 = infinite)",
            "Note: Because of changing download speeds, this is not 100% accurate",
            "Public Hub lists",
            "This HTTP proxy will only be used to request\n"
            "the Public Hub List specified above.\n"
            "It's not used for uploads, downloads, or connecting to a hub.",
            "Http proxy (hub list only)", "Configure Public Hub Lists", "Auto-priority settings",
            "Files with the same or under the entered size\n"
            "will be set to Highest as priority. (default: 64 KiB)",
            "Files with the same or under the entered size, but above Highest\n"
            "prio max size, will be set to High as priority. (default: 0 KiB)",
            "Files with the same or under the entered size, but above Highest and\n"
            "High prio max size, will be set to Normal as priority. (default: 0 KiB)",
            "Files with the same or under the entered size, but above Highest, High\n"
            "and Normal prio max size, will be set to Low as priority. (default: 0 KiB)",
            "Highest priority max size (KiB)", "High priority max size (KiB)",
            "Normal priority max size (KiB)", "Low priority max size (KiB)",
            "Set lowest priority for newly added files larger than low priority size",
            "With this enabled, files that are larger than Low prio max size\n"
            "are set to Lowest as priority.\nIf disabled, the files will be set to Normal as priority.",
        }
        exact = {
            "Highest priority max size (KiB)": "უმაღლესი პრიორიტეტის მაქსიმალური ზომა (KiB)",
            "High priority max size (KiB)": "მაღალი პრიორიტეტის მაქსიმალური ზომა (KiB)",
            "Normal priority max size (KiB)": "ჩვეულებრივი პრიორიტეტის მაქსიმალური ზომა (KiB)",
            "Low priority max size (KiB)": "დაბალი პრიორიტეტის მაქსიმალური ზომა (KiB)",
        }
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") == "UISettingsDownloads" for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-download-limits-") as work:
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
                    self.assertNotEqual(source, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    self.assertEqual(source.count("\n"), text.count("\n"))
                    self.assertEqual(source.count("%"), text.count("%"))
                    self.assertEqual(Counter(re.findall(r"\d+", source)),
                                     Counter(re.findall(r"\d+", text)))
                    for token in ("KiB/s", "KiB", "HTTP"):
                        if token in source:
                            self.assertIn(token, text)
                    if source in exact:
                        self.assertEqual(exact[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_mainwindow_translations(self):
        required = {
            "Search Spy", "Away when not visible", "Exit", " | DHT nodes: %1",
            "Torrent files", "Unable to load file list: Invalid file list name",
            "Unable to open %1 port. Searching or file transfers will not work correctly until you change settings or turn off any application that might be using that port.",
            "Connectivity Manager: Warning", "Open magnet or Torrent file",
            "Enter a magnet link or local .torrent path:", "Torrent support is not ready.",
            "EiskaltDC++ is a graphical client for Direct Connect and ADC protocols.",
            'Please use <a href="https://github.com/Delitants/eiskaltdcpp/issues">https://github.com/Delitants/eiskaltdcpp/issues</a> to report bugs.<br/>',
            '2026 <a href="mailto:admin@nlight.org.ua">Neolo</a><br/>',
            "Georgian translation<br/>", "Torrent: %1", "Nickname required",
            "Enter a nickname in Preferences before connecting to hubs.",
            "Arch Linux maintainer", "Debian/Ubuntu maintainer", "macOS maintainer",
            "MS Windows maintainer", "creator of the logo of the project", "tiny updates of the logo",
        }
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") == "MainWindow" for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-mainwindow-") as work:
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
                    if not source.startswith('2026 <a href="mailto:'):
                        self.assertNotEqual(source, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    self.assertEqual(Counter(re.findall(r"%\d+|%n", source)),
                                     Counter(re.findall(r"%\d+|%n", text)))
                    self.assertEqual(re.findall(r"<[^>]+>", source), re.findall(r"<[^>]+>", text))
                    self.assertEqual(source[:len(source)-len(source.lstrip())],
                                     text[:len(text)-len(text.lstrip())])
                    for token in ("DHT", ".torrent", "EiskaltDC++", "Direct Connect", "ADC",
                                  "2026", "Neolo", "Arch Linux", "Debian/Ubuntu", "macOS", "MS Windows"):
                        if token in source:
                            self.assertIn(token, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_magnets_favorites_and_messages(self):
        required = {
            "Magnet": {"Run search alternatives manually.", "Download file via auto search alternatives",
                       "Some error ocurred when starting download:\n %1"},
            "Notification": {"Speed\nDownload: %1 Upload: %2\nStatistics\nDownloaded: %3 Uploaded: %4"},
            "PMWindow": {"%1 on hub %2"},
            "UIFavoriteHubEditor": {
                "This description will be shown in the list of favorite hubs.",
                "Auto update from Internet", "Minimum search interval (seconds)",
                "This nickname, or handle, will identify you when you join a hub.",
                "If hub requires password, define it here.",
                "This description will be shown in the user list.",
            },
            "UIFavoriteUsers": {"Auto grant slot to all users"},
            "UIMagnet": {"Opens search dialog and searches file.",
                         "Adds magnet to download queue. If autosearch for alternatives is enabled\n"
                         "in options, file will be downloaded without opening search dialog."},
            "UIPrivateMessage": {"Press Shift+Enter for multiline input"},
            "UIQuickConnect": {"IP address of DNS name of hub you want to connect."},
            "UIUserCommands": {"Send once per nick"},
        }
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-magnets-") as work:
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
                        self.assertNotEqual(source, text)
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(Counter(re.findall(r"%\d+|%n", source)),
                                         Counter(re.findall(r"%\d+|%n", text)))
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        for token in ("Shift+Enter", "IP", "DNS"):
                            if token in source:
                                self.assertIn(token, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_personal_history_and_notifications(self):
        required = {
            "UISettingsHistory": {
                "Clear search history on program exit",
                "Clear download directories history on program exit",
                "Number of search history elements to store:",
                "Number of download directories history elements to store:",
                "Memorize TTH search phrases",
            },
            "UISettingsNotification": {
                "Your nick is mentioned in main chat", "Favorite user join/part",
                "Allow other types of notifications",
                "Enable notifications with not active main window",
                "Enable notifications with active main window",
                "Change icon when main window is visible", "Play sound on active PM window",
                "If your sound files' format is unsupported by Qt, you can define\n"
                "custom media player here to play them.\nFor example: /usr/bin/mplayer",
            },
            "UISettingsPersonal": {
                "This description will show in the user list.\n"
                "It can be overridden on a per-hub basis in the Favorite Hub Properties.",
                "If you are in away mode, this message will be sent to everybody\n"
                "who sends private message to you.",
                "This nickname, or handle, will identify you when you join a hub.\n"
                "It can be overridden on a per-hub basis in the Favorite Hub Properties.",
                "Auto away interval (min)",
                "This field is for sharing your email address, if you wish it.",
                "This drop-down contains a number of common upload speeds (in Mbit/s),\n"
                "please choose the one closest to your own.\n"
                "Do not put in your download speed; this setting is a measure\n"
                "of how fast a source you are likely to be for other users.",
                "Mbit/s", "Default hub encoding",
            },
        }
        exact = {
            "Enable notifications with not active main window":
                "შეტყობინებების ჩართვა, როცა მთავარი ფანჯარა არააქტიურია",
            "Enable notifications with active main window":
                "შეტყობინებების ჩართვა, როცა მთავარი ფანჯარა აქტიურია",
            "Play sound on active PM window":
                "ხმის დაკვრა აქტიური პირადი შეტყობინების ფანჯრის დროსაც",
            "Mbit/s": "Mbit/s",
        }
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-personal-") as work:
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
                        if source != "Mbit/s":
                            self.assertNotEqual(source, text)
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(source.count("\n"), text.count("\n"))
                        for token in ("TTH", "Qt", "/usr/bin/mplayer", "Mbit/s"):
                            if token in source:
                                self.assertIn(token, text)
                        if source in exact:
                            self.assertEqual(exact[source], text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_sharing_translations(self):
        required = {
            "View share in simple mode", "Add", "Remove",
            "Don't share files and folders containing:",
            "Here you can specify templates for files you don't want to share. ",
            "Select and add the directory",
            "Share files with the Hidden attribute set and files inside directory with\n"
            "the Hidden attribute set.\n"
            "With this option off, files starting with a . (full stop) will not be shared.\n"
            "Note: this option requires a refresh of the file list,\n"
            "and may cause the client to appear to freeze.",
            "Auto remove non-existing directories", "FastHash options (restart needed)",
            "Private page mapping", "No reserve", "Proactive read from disk",
            "Use ADL search only in own file list",
            "Automatically open extra slot if speed is below (KiB/s, 0 = infinite)",
            "If the average upload speed drops below the given number,\n"
            "an additional upload slot will be granted.\n"
            "These slots are granted once every 30 seconds at maximum.\n"
            "If the speed of the original uploads increases,\n"
            "the granted slot will not be closed.",
            "Hashing delay on startup (seconds, -1 = infinity)",
            "Here you can set time interval of auto refreshing your share.",
            "Auto refresh time (minutes, 0 = off)", "Max hash speed (MB/s) (0 = infinite)",
            "You can limit maximum hash speed here\n"
            "if your system becomes slow while hashing files.",
            "Configure the number of slots that other users can occupy\n"
            "to download files from you. A remote user can use only\n"
            "one slot for each hub that they're on with you.\n"
            "EiskaltDC++ supports uploading file lists and files below 64 KiB\n"
            "to other DC++ users without requiring a slot.\n"
            "There's a max of 3 connections in addition to normal slots,\n"
            "these are called mini slots.\n"
            "Note: If a user leaves the hub, EiskaltDC++ will close his slots;\n"
            "if the user is back within 10 minutes, EiskaltDC++ will grant him a slot.",
        }
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {m.findtext("source"): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    if c.findtext("name") == "UISettingsSharing" for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-sharing-") as work:
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
                    self.assertNotEqual(source, text)
                    self.assertIn(source, compiled)
                    self.assertEqual(text, "".join(compiled[source].itertext()))
                    self.assertEqual(source.count("\n"), text.count("\n"))
                    self.assertEqual(source[len(source.rstrip()):], text[len(text.rstrip()):])
                    self.assertEqual(Counter(re.findall(r"-?\d+", source)),
                                     Counter(re.findall(r"-?\d+", text)))
                    for token in ("FastHash", "ADL", "KiB/s", "MB/s", "KiB", "EiskaltDC++", "DC++"):
                        if token in source:
                            self.assertIn(token, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_hub_and_browser_translations(self):
        required = {
            "ChatEdit": {"Address", "Select image"},
            "PublicHubs": {"Downloading public hub list...", "Copy &address to clipboard",
                           "Refreshing public hub list...", "Downloading public hub list... (%1)",
                           "Hub list downloaded... (%1)",
                           "Locally cached (as of %1) version of the hub list loaded (%2)",
                           "Cached hub list is corrupted or unsupported",
                           "Downloaded hub list is corrupted or unsupported (%1)"},
            "PublicHubsList": {"Public hub list URLs"},
            "SearchFrame": {"Don't hide this panel when search", 'Search progress of "%1" is %2%'},
            "ShareBrowser": {"Total share size: %1;  Files: %2", "Share browser error: %1",
                             "Selected %1 from %2 items; "},
            "UIPublicHubs": {"Refresh"}, "UserListModel": {"IPv6"},
        }
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-browser-") as work:
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
                        if source != "IPv6":
                            self.assertNotEqual(source, text)
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(Counter(re.findall(r"%\d+|%n", source)),
                                         Counter(re.findall(r"%\d+|%n", text)))
                        self.assertEqual(source.count("%"), text.count("%"))
                        self.assertEqual(source.count("&"), text.count("&"))
                        self.assertEqual(source[len(source.rstrip()):], text[len(text.rstrip()):])
                        for token in ("IPv6", "URL"):
                            if token in source:
                                self.assertIn(token, text)

    @unittest.skipUnless(shutil.which("lrelease") and shutil.which("lconvert"),
                         "lrelease and lconvert are required")
    def test_georgian_transfer_translations(self):
        required = {
            "TransferView": {"Protocol", "Cancel download", "Torrent details",
                             "Pause Torrent", "Resume Torrent"},
            "TransferViewModel": {"Paused", "Seeding", "Waiting", "Download (%1 peers)",
                                  "Upload (%1 peers)", "%1 | Downloaded %2 (%3%)",
                                  "%1 | Uploaded %2"},
            "UIFileHasher": {"Opens search dialog and searches file."},
            "UIFinishedTransfers": {"Show only completely finished transfers"},
        }
        exact = {"Paused": "შეჩერებულია", "Seeding": "განაწილება", "Waiting": "მოლოდინი",
                 "Cancel download": "ჩამოტვირთვის გაუქმება",
                 "Pause Torrent": "ტორენტის შეჩერება", "Resume Torrent": "ტორენტის გაგრძელება",
                 "Download (%1 peers)": "ჩამოტვირთვა (%1 პირი)",
                 "Upload (%1 peers)": "ატვირთვა (%1 პირი)"}
        path = ROOT / "eiskaltdcpp-qt/translations/ka.ts"
        def entries(catalog):
            return {(c.findtext("name"), m.findtext("source")): m.find("translation")
                    for c in ET.parse(catalog).getroot().findall("context")
                    for m in c.findall("message")}
        with tempfile.TemporaryDirectory(prefix="eiskalt-ka-transfers-") as work:
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
                        self.assertNotEqual(source, text)
                        self.assertIn((context, source), compiled)
                        self.assertEqual(text, "".join(compiled[context, source].itertext()))
                        self.assertEqual(Counter(re.findall(r"%\d+|%n", source)),
                                         Counter(re.findall(r"%\d+|%n", text)))
                        self.assertEqual(source.count("%"), text.count("%"))
                        if source in exact:
                            self.assertEqual(exact[source], text)

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
        catalogs = sorted((ROOT / "dcpp/po").glob("*.po"))
        catalogs += sorted((ROOT / "eiskaltdcpp-gtk/po").glob("*.po"))
        for path in catalogs:
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
