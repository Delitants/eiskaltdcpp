"""Source-aware coverage, in addition to completeness of existing entries."""
import ast
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def qt_keys(path):
    return {(context.findtext("name"), message.findtext("source"),
             message.findtext("comment", ""))
            for context in ET.parse(path).getroot().findall("context")
            for message in context.findall("message")
            if message.find("translation") is None
            or message.find("translation").get("type") not in {"obsolete", "vanished"}}


def gettext_entries(text):
    entries = {}
    for block in re.split(r"\n\s*\n", text):
        fields = {match[1]: "".join(ast.literal_eval(line) for line in match[2].splitlines())
                  for match in re.finditer(
                      r'^(msgid(?:_plural)?|msgstr(?:\[\d+\])?) ("[^\n]*"(?:\n"[^\n]*")*)',
                      block, re.M)}
        if fields.get("msgid"):
            entries[(fields["msgid"], fields.get("msgid_plural", ""))] = fields
    return entries


class CurrentSourceCoverage(unittest.TestCase):
    @unittest.skipUnless(shutil.which("lupdate"), "lupdate is required")
    def test_all_current_qt_source_keys_are_in_every_catalog(self):
        with tempfile.TemporaryDirectory(prefix="eiskalt-source-coverage-") as work:
            output = Path(work) / "source.ts"
            sources = [ROOT / path for path in ("torrent", "eiskaltdcpp-qt/src",
                       "eiskaltdcpp-qt/ui", "eiskaltdcpp-qt/scriptengine")]
            sources += sorted(ROOT.glob("*.h"))
            subprocess.run(["lupdate", *map(str, sources), "-no-obsolete",
                            "-locations", "none", "-ts", str(output)],
                           check=True, capture_output=True, text=True)
            required = qt_keys(output)
        self.assertGreater(len(required), 2000)
        for path in sorted((ROOT / "eiskaltdcpp-qt/translations").glob("*.ts")):
            with self.subTest(catalog=path.name):
                missing = required - qt_keys(path)
                self.assertFalse(missing, f"{path.name}: {len(missing)} missing current-source keys; "
                                 f"first keys: {sorted(missing)[:3]}")

    @unittest.skipUnless(shutil.which("xgettext") and shutil.which("msgcat"),
                         "xgettext and msgcat are required")
    def test_all_current_core_and_dht_messages_are_in_every_catalog(self):
        with tempfile.TemporaryDirectory(prefix="eiskalt-core-coverage-") as work:
            output = Path(work) / "source.pot"
            sources = sorted(p for directory in (ROOT / "dcpp", ROOT / "dht")
                             for p in directory.iterdir() if p.suffix in {".cpp", ".h"})
            subprocess.run(["xgettext", "--language=C++", "--from-code=UTF-8",
                            "--keyword=_", "--keyword=N_", "--keyword=F_",
                            "--keyword=F_:2", "--flag=F_:2:pass-c-format",
                            "--keyword=FN_:1,2", "--no-location", "--output=" + str(output),
                            *map(str, sources)], check=True, capture_output=True, text=True)
            required = gettext_entries(output.read_text())
        self.assertGreater(len(required), 200)
        for path in sorted((ROOT / "dcpp/po").glob("*.po")):
            canonical = subprocess.run(["msgcat", "--no-wrap", str(path)],
                                       check=True, capture_output=True, text=True).stdout
            with self.subTest(catalog=path.name):
                missing = required.keys() - gettext_entries(canonical).keys()
                self.assertFalse(missing, f"{path.name}: {len(missing)} missing current-source keys; "
                                 f"first keys: {sorted(missing)[:3]}")


if __name__ == "__main__":
    unittest.main()
