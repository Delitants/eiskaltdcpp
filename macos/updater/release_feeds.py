"""Strict offline release-manifest and signed-feed preparation/verification."""
import base64
import binascii
import ctypes
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import xml.etree.ElementTree as ET

SPARKLE = "http://www.andymatuschak.org/xml-namespaces/sparkle"
SCHEMA = "https://eiskaltdcpp.org/xml-namespaces/updates/1"
PROFILES = {
    "intel": ("x86_64", "14.0", "macOS14-plus-Intel-x86_64"),
    "apple-silicon": ("arm64", "26.0", "macOS26-plus-AppleSilicon-arm64"),
}
BUNDLE = "com.github.eiskaltdcpp"
ARCHIVE_LIMIT = 512 * 1024 * 1024
FEED_LIMIT = 1024 * 1024
MARKER = b"<!-- sparkle-signatures:\n"
ET.register_namespace("sparkle", SPARKLE)
ET.register_namespace("eiskalt", SCHEMA)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def bounded_read(path, limit):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, "rb") as stream:
        info = os.fstat(stream.fileno())
        require(stat.S_ISREG(info.st_mode) and 0 < info.st_size <= limit, "Invalid input file type or size")
        result = stream.read(limit + 1)
        require(len(result) == info.st_size, "Input changed while reading")
        return result


def decode(value, size):
    require(type(value) is str and len(value) == 4 * ((size + 2) // 3), "Invalid signature or public key encoding")
    try:
        data = base64.b64decode(value, validate=True)
    except binascii.Error as error:
        raise ValueError("Invalid signature or public key encoding") from error
    require(len(data) == size and base64.b64encode(data).decode("ascii") == value,
            "Noncanonical signature or public key encoding")
    return data


def text(value, maximum, empty=False):
    require(type(value) is str and (empty or bool(value.strip())), "Invalid manifest text")
    require(len(value.encode("utf-8")) <= maximum, "Manifest text exceeds limit")
    require(all(ord(c) >= 32 or c in "\n\r\t" for c in value) and "\x7f" not in value, "Control character in manifest text")
    require(all(ord(c) not in (0xfffe, 0xffff) for c in value), "Invalid XML character in manifest text")


def integer(value, maximum):
    require(type(value) is int and 0 < value <= maximum, "Invalid manifest number")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate manifest field")
        result[key] = value
    return result


@dataclass(frozen=True)
class Manifest:
    data: dict
    directory: Path


def load_manifest(path):
    path = Path(path)
    value = json.loads(bounded_read(path, FEED_LIMIT), object_pairs_hook=unique_object)
    require(type(value) is dict and set(value) == {
        "schema_version", "version", "build_number", "channel", "bundle_id", "notes", "assets"}, "Unexpected manifest fields")
    require(type(value["schema_version"]) is int and value["schema_version"] == 1, "Unsupported manifest schema")
    integer(value["build_number"], 9223372036854775807)
    text(value["version"], 128)
    require(re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z.+-]{0,127}", value["version"]), "Invalid release version")
    require(value["channel"] in ("stable", "preview"), "Invalid release channel")
    require(value["bundle_id"] == BUNDLE, "Unexpected bundle identity")
    text(value["notes"], 16384, empty=True)
    assets = value["assets"]
    require(type(assets) is list and len(assets) == 2, "Exactly two release flavors are required")
    seen = set()
    for asset in assets:
        require(type(asset) is dict and set(asset) == {
            "flavor", "arch", "minimum_os", "filename", "url", "size", "sha256", "ed_signature"}, "Unexpected asset fields")
        flavor = asset["flavor"]
        require(type(flavor) is str and flavor in PROFILES and flavor not in seen, "Invalid or duplicate release flavor")
        seen.add(flavor)
        arch, minimum, suffix = PROFILES[flavor]
        require(asset["arch"] == arch and asset["minimum_os"] == minimum, "Wrong release architecture or minimum OS")
        filename = "EiskaltDCpp-" + value["version"] + "-" + suffix + ".zip"
        require(asset["filename"] == filename, "Wrong release archive filename")
        url = "https://github.com/Delitants/eiskaltdcpp/releases/download/v" + value["version"] + "/" + filename
        require(asset["url"] == url, "Wrong release archive URL")
        integer(asset["size"], ARCHIVE_LIMIT)
        require(type(asset["sha256"]) is str and re.fullmatch(r"[0-9a-f]{64}", asset["sha256"]), "Invalid archive digest")
        decode(asset["ed_signature"], 64)
    return Manifest(value, path.absolute().parent.resolve())


def check_previous(current, previous):
    if previous is None:
        return
    old = load_manifest(previous)
    require(current.data["bundle_id"] == old.data["bundle_id"] and
            current.data["build_number"] > old.data["build_number"], "Build number does not increase")


def snapshot_assets(manifest, directory):
    """Hash and copy the same opened bytes so external tools see an immutable snapshot."""
    result = {}
    for asset in manifest.data["assets"]:
        source = manifest.directory / asset["filename"]
        fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        destination = directory / asset["filename"]
        with os.fdopen(fd, "rb") as stream, destination.open("xb") as output:
            destination.chmod(0o600)
            info = os.fstat(stream.fileno())
            require(stat.S_ISREG(info.st_mode) and info.st_size == asset["size"], "Archive type or size mismatch")
            digest = hashlib.sha256()
            size = 0
            while True:
                chunk = stream.read(1024 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                require(size <= asset["size"], "Archive grew while reading")
                digest.update(chunk); output.write(chunk)
            require(size == asset["size"] and digest.hexdigest() == asset["sha256"], "Archive digest mismatch")
        result[asset["flavor"]] = destination
    return result


def invoke(executable, *arguments):
    try:
        result = subprocess.run([str(executable), *map(str, arguments)], capture_output=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError("Signing or verification tool could not complete") from error
    # Never forward tool output, which can contain key material or private paths.
    require(result.returncode == 0, "Signing or verification tool rejected the input")
    return result.stdout


def feed_tree(manifest, asset):
    data = manifest.data
    root = ET.Element("rss", {"version": "2.0"})
    channel = ET.SubElement(root, "channel")
    ET.SubElement(channel, "title").text = "EiskaltDC++ " + asset["flavor"]
    item = ET.SubElement(channel, "item")
    ET.SubElement(item, "title").text = "EiskaltDC++ " + data["version"]
    ET.SubElement(item, "{" + SPARKLE + "}version").text = str(data["build_number"])
    ET.SubElement(item, "{" + SPARKLE + "}shortVersionString").text = data["version"]
    ET.SubElement(item, "{" + SPARKLE + "}minimumSystemVersion").text = asset["minimum_os"]
    if data["channel"] == "preview":
        ET.SubElement(item, "{" + SPARKLE + "}channel").text = "preview"
    ET.SubElement(item, "{" + SCHEMA + "}bundleIdentifier").text = data["bundle_id"]
    ET.SubElement(item, "{" + SCHEMA + "}cpu").text = asset["arch"]
    ET.SubElement(item, "description").text = data["notes"]
    ET.SubElement(item, "enclosure", {
        "url": asset["url"], "length": str(asset["size"]), "type": "application/octet-stream",
        "{" + SPARKLE + "}os": "macos", "{" + SPARKLE + "}edSignature": asset["ed_signature"],
        "{" + SCHEMA + "}sha256": asset["sha256"],
    })
    return root


def make_feeds(manifest_path, output, signer, previous=None):
    manifest = load_manifest(manifest_path)
    check_previous(manifest, previous)
    output = Path(output).absolute()
    require(not output.exists() and not output.is_symlink(), "Output already exists")
    require(output.parent.is_dir(), "Output parent does not exist")
    signer = Path(signer).absolute()
    require(signer.is_file() and os.access(signer, os.X_OK), "Signer is not executable")
    with tempfile.TemporaryDirectory(prefix=".eiskalt-feeds-", dir=output.parent) as temporary:
        stage = Path(temporary)
        snapshots = snapshot_assets(manifest, stage)
        for asset in manifest.data["assets"]:
            invoke(signer, "--verify", snapshots[asset["flavor"]], asset["ed_signature"])
        feeds = stage / "feeds"; feeds.mkdir(mode=0o700)
        for asset in sorted(manifest.data["assets"], key=lambda value: value["flavor"]):
            path = feeds / (asset["flavor"] + ".xml")
            xml = ET.tostring(feed_tree(manifest, asset), encoding="utf-8", xml_declaration=True)
            # XML normalizes literal carriage returns; references preserve notes.
            path.write_bytes(xml.replace(b"\r", b"&#13;"))
            # Sparkle's warning inserter reparses XML and rewrites CR references.
            # Sign the already serialized bytes without that optional rewrite.
            invoke(signer, path, "--disable-signing-warning")
            signed_payload(path)
            invoke(signer, "--verify", path)
        require(not output.exists() and not output.is_symlink(), "Output appeared during preparation")
        # The pair is promoted together, never as individually visible feeds.
        publish_directory(feeds, output)


def publish_directory(source, destination):
    # POSIX rename can replace an empty directory created after our preflight.
    # macOS RENAME_EXCL (available since 10.12) preserves that concurrent owner.
    library = ctypes.CDLL(None, use_errno=True)
    rename = getattr(library, "renamex_np", None)
    require(rename is not None, "Exclusive feed promotion requires macOS")
    rename.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(os.fsencode(source), os.fsencode(destination), 4) != 0:
        raise OSError(ctypes.get_errno(), "Exclusive feed promotion refused")


def signed_payload(path):
    content = bounded_read(path, FEED_LIMIT)
    require(content.count(MARKER) == 1, "Missing or duplicate feed signature")
    offset = content.index(MARKER)
    require(offset > 0, "Empty signed feed")
    lines = content[offset + len(MARKER):].split(b"\n")
    require(len(lines) in (3, 4) and lines[2] == b"-->" and
            (len(lines) == 3 or lines[3] == b"") and
            lines[0].startswith(b"edSignature: ") and lines[1] == b"length: " + str(offset).encode("ascii"),
            "Invalid feed signature trailer")
    try:
        signature = decode(lines[0][13:].decode("ascii"), 64)
    except UnicodeError as error:
        raise ValueError("Invalid feed signature encoding") from error
    return content[:offset], signature


def structure(element, depth=1):
    require(depth <= 5, "Feed nesting exceeds limit")
    require(not (element.tail or "").strip(), "Unexpected text outside feed element")
    contents = element.text or ""
    if len(element):
        require(not contents.strip(), "Unexpected feed container text")
        contents = ""
    return (element.tag, sorted(element.attrib.items()), contents,
            [structure(child, depth + 1) for child in element])


def verify_signature(openssl, public_der, data, signature, directory):
    signature_path = directory / "signature.bin"
    signature_path.write_bytes(signature)
    invoke(openssl, "pkeyutl", "-verify", "-pubin", "-inkey", public_der, "-keyform", "DER",
           "-rawin", "-in", data, "-sigfile", signature_path)


def verify_feeds(manifest_path, feeds, public_key, openssl="openssl", previous=None):
    key = decode(public_key, 32)
    manifest = load_manifest(manifest_path)
    check_previous(manifest, previous)
    require(invoke(openssl, "version").startswith(b"OpenSSL 3."), "OpenSSL 3 is required")
    feeds = Path(feeds)
    require(feeds.is_dir() and not feeds.is_symlink(), "Missing feeds directory")
    require(sorted(path.name for path in feeds.iterdir()) == ["apple-silicon.xml", "intel.xml"], "Unexpected feed files")
    with tempfile.TemporaryDirectory(prefix="eiskalt-feed-verify-") as temporary:
        directory = Path(temporary)
        public = directory / "public.der"
        public.write_bytes(bytes.fromhex("302a300506032b6570032100") + key)
        snapshots = snapshot_assets(manifest, directory)
        for asset in manifest.data["assets"]:
            verify_signature(openssl, public, snapshots[asset["flavor"]], decode(asset["ed_signature"], 64), directory)
            payload, signature = signed_payload(feeds / (asset["flavor"] + ".xml"))
            signed = directory / "payload.xml"; signed.write_bytes(payload)
            verify_signature(openssl, public, signed, signature, directory)
            require(b"<!DOCTYPE" not in payload and b"<!ENTITY" not in payload, "Feed declarations are forbidden")
            without_declaration = re.sub(rb"\A<\?xml[^?]*\?>", b"", payload)
            require(b"<?" not in without_declaration, "Feed processing instructions are forbidden")
            root = ET.fromstring(payload)
            require(sum(1 for _ in root.iter()) <= 4096, "Feed element limit exceeded")
            require(structure(root) == structure(feed_tree(manifest, asset)), "Feed metadata does not match manifest")
