#!/usr/bin/env python3
"""Private loopback-only GOST 3.3.0 supervisor and generated protocol services.

No user endpoint, configuration, credential, torrent or payload is accepted.
The Unix control socket remains private; all generated files are owner-only.
"""
import json
import os
from pathlib import Path
import secrets
import socket
import socketserver
import stat
import struct
import subprocess
import sys
import threading
import time

os.umask(0o077)
ROOT = Path(sys.argv[2]).resolve()
LOCK = threading.Lock()
STATS = {"dns": [], "tracker_connect": 0, "tracker_announce": 0,
         "dht_queries": 0, "dht_replies": 0, "echo_udp": 0}


def put(name, data):
    tmp = ROOT / (name + ".tmp")
    tmp.write_text(json.dumps(data))
    tmp.replace(ROOT / name)


def bencode(x):
    if isinstance(x, int):
        return b"i" + str(x).encode() + b"e"
    if isinstance(x, bytes):
        return str(len(x)).encode() + b":" + x
    if isinstance(x, dict):
        return b"d" + b"".join(bencode(k) + bencode(v) for k, v in sorted(x.items())) + b"e"
    raise ValueError("unsupported fixture bencode")


def bdecode(data):
    def read(pos):
        tag = data[pos:pos + 1]
        if tag == b"d":
            result, pos = {}, pos + 1
            while data[pos:pos + 1] != b"e":
                key, pos = read(pos)
                result[key], pos = read(pos)
            return result, pos + 1
        if tag == b"i":
            end = data.index(b"e", pos)
            return int(data[pos + 1:end]), end + 1
        end = data.index(b":", pos)
        length = int(data[pos:end])
        return data[end + 1:end + 1 + length], end + 1 + length
    value, end = read(0)
    if end != len(data):
        raise ValueError("trailing bytes")
    return value


class TCP(socketserver.ThreadingTCPServer):
    daemon_threads = True


class TCP6(TCP):
    address_family = socket.AF_INET6


class UDP(socketserver.ThreadingUDPServer):
    daemon_threads = True


class UDP6(UDP):
    address_family = socket.AF_INET6


class Echo(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(5)
        try:
            while data := self.request.recv(65536):
                self.request.sendall(data)
        except (TimeoutError, ConnectionError):
            pass


class Packet(socketserver.BaseRequestHandler):
    def handle(self):
        data, sock = self.request
        kind = self.server.kind
        reply = b""
        with LOCK:
            try:
                if kind == "dns":
                    pos, labels = 12, []
                    while data[pos]:
                        size = data[pos]
                        labels.append(data[pos + 1:pos + 1 + size].decode("ascii"))
                        pos += size + 1
                    name = ".".join(labels)
                    qtype = struct.unpack("!H", data[pos + 1:pos + 3])[0]
                    STATS["dns"].append({"name": name, "type": qtype, "source": self.client_address[0]})
                    question = data[12:pos + 5]
                    answer = b""
                    if name.endswith(".fixture.test") and qtype in (1, 28):
                        address = socket.inet_pton(socket.AF_INET if qtype == 1 else socket.AF_INET6,
                                                   "127.0.0.1" if qtype == 1 else "::1")
                        answer = b"\xc0\x0c" + struct.pack("!HHIH", qtype, 1, 0, len(address)) + address
                    reply = data[:2] + struct.pack("!HHHHH", 0x8180, 1, bool(answer), 0, 0) + question + answer
                elif kind == "tracker":
                    action, tx = struct.unpack("!II", data[8:16])
                    if action == 0 and data[:8] == bytes.fromhex("0000041727101980"):
                        STATS["tracker_connect"] += 1
                        reply = struct.pack("!IIQ", 0, tx, 0x1122334455667788)
                    elif action == 1 and len(data) >= 98:
                        STATS["tracker_announce"] += 1
                        reply = struct.pack("!IIIII", 1, tx, 1, 0, 1)
                elif kind == "dht":
                    query = bdecode(data)
                    if query.get(b"y") != b"q" or len(query[b"a"][b"id"]) != 20:
                        return
                    STATS["dht_queries"] += 1
                    reply = bencode({b"t": query[b"t"], b"y": b"r", b"r": {
                        b"id": b"L" * 20, b"nodes": b"", b"token": b"fixture-token"}})
                    STATS["dht_replies"] += 1
                else:
                    STATS["echo_udp"] += 1
                    reply = data
                put("services.json", STATS)
            except (ValueError, IndexError, KeyError, struct.error):
                return
        if reply:
            sock.sendto(reply, self.client_address)


def safe_root():
    original = Path(sys.argv[2])
    if original.is_symlink() or not ROOT.is_dir():
        raise RuntimeError("fixture directory must already exist and not be a symlink")
    mode = ROOT.stat()
    if mode.st_uid != os.getuid() or stat.S_IMODE(mode.st_mode) != 0o700:
        raise RuntimeError("fixture directory must be owned by this user and mode 0700")
    # Keep identities out of the checkout and any live profile by construction.
    temp = Path(os.environ.get("TMPDIR", "/tmp")).resolve()
    if not (ROOT.is_relative_to(Path("/tmp").resolve()) or ROOT.is_relative_to(temp)):
        raise RuntimeError("fixture directory must be below the system temporary directory")


def command(value):
    with socket.socket(socket.AF_UNIX) as control:
        control.settimeout(10)
        control.connect(str(ROOT / "control.sock"))
        control.sendall(value.encode())
        return control.recv(65536).decode()


def supervise():
    binary = Path(os.environ["GOST_BIN"]).resolve(strict=True)
    version = subprocess.check_output([str(binary), "-V"], text=True).strip()
    if not version.startswith("gost v3.3.0 "):
        raise RuntimeError("requires the independently verified GOST 3.3.0 artifact")
    if sys.platform != "darwin":
        raise RuntimeError("fixture containment currently requires macOS Seatbelt")
    services = []
    gost = None
    capture = None
    log = (ROOT / "gost.log").open("ab")
    try:
        manifest = {"directory": str(ROOT), "version": version, "relay_min": 48000, "relay_max": 48063}
        for key, klass, handler, kind in [
            ("tcp4", TCP, Echo, ""), ("tcp6", TCP6, Echo, ""),
            ("udp4", UDP, Packet, "echo"), ("udp6", UDP6, Packet, "echo"),
            ("dns", UDP, Packet, "dns"), ("tracker4", UDP, Packet, "tracker"),
            ("tracker6", UDP6, Packet, "tracker"), ("dht", UDP, Packet, "dht")]:
            host = "::1" if klass.address_family == socket.AF_INET6 else "127.0.0.1"
            server = klass((host, 0), handler)
            server.kind = kind
            services.append(server)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            manifest[key] = server.server_address[1]
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            manifest["proxy"] = reservation.getsockname()[1]
        auth = {"username": "fixture-" + secrets.token_hex(8), "password": secrets.token_hex(24)}
        put("auth.json", auth)
        with (ROOT / "openssl.log").open("wb") as out:
            for args in [
                ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=Disposable Fixture CA",
                 "-addext", "basicConstraints=critical,CA:TRUE", "-keyout", "ca.key", "-out", "ca.pem"],
                ["req", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=127.0.0.1", "-keyout", "server.key", "-out", "server.csr"],
            ]:
                subprocess.run(["openssl"] + args, cwd=ROOT, stdout=out, stderr=out, check=True)
            (ROOT / "extensions.cnf").write_text("subjectAltName=IP:127.0.0.1,IP:::1,DNS:proxy.fixture.test\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n")
            subprocess.run(["openssl", "x509", "-req", "-in", "server.csr", "-CA", "ca.pem", "-CAkey", "ca.key",
                            "-CAcreateserial", "-days", "1", "-extfile", "extensions.cnf", "-out", "server.pem"],
                           cwd=ROOT, stdout=out, stderr=out, check=True)
        tls = {"certFile": str(ROOT / "server.pem"), "keyFile": str(ROOT / "server.key"),
               "options": {"minVersion": "VersionTLS12"}}
        config = {"services": [{"name": "disposable-loopback", "addr": "127.0.0.1:" + str(manifest["proxy"]),
                   "handler": {"type": "socks5", "auth": auth, "tls": tls,
                               "metadata": {"udp": True, "udp.bindRange.min": "48000", "udp.bindRange.max": "48063"}},
                   "listener": {"type": "tcp"}, "resolver": "fixture"}],
                  "resolvers": [{"name": "fixture", "nameservers": [{"addr": "127.0.0.1:" + str(manifest["dns"]), "prefer": "ipv4"}]}],
                  "log": {"level": "debug"}}
        put("gost.json", config)
        if os.environ.get("GOST_CAPTURE") == "1":
            ports = [manifest[key] for key in ("proxy", "dns", "tracker4", "tracker6", "dht", "udp4", "udp6", "tcp4", "tcp6")]
            expression = "(host 127.0.0.1 or host ::1) and (" + " or ".join("port " + str(p) for p in ports) + ")"
            with (ROOT / "capture.log").open("wb") as capture_log:
                capture = subprocess.Popen(["/usr/sbin/tcpdump", "-p", "-i", "lo0", "-n", "-U", "-w", str(ROOT / "fixture.pcap"), expression],
                                           stdout=capture_log, stderr=capture_log)
            time.sleep(.2)
            if capture.poll() is not None:
                raise RuntimeError("requested packet capture unavailable; inspect capture.log")
        policy = '(version 1)(allow default)(deny network*)(allow network-outbound (remote ip "localhost:*"))(allow network-bind (local ip "localhost:*"))(allow network-inbound (local ip "localhost:*"))'
        (ROOT / "gost.sb").write_text(policy)
        def launch():
            p = subprocess.Popen(["/usr/bin/sandbox-exec", "-f", str(ROOT / "gost.sb"), str(binary), "-C", str(ROOT / "gost.json")], stdout=log, stderr=log)
            for _ in range(100):
                if p.poll() is not None:
                    raise RuntimeError("GOST exited; inspect private gost.log")
                try:
                    with socket.create_connection(("127.0.0.1", manifest["proxy"]), .1):
                        return p
                except OSError:
                    time.sleep(.05)
            p.terminate()
            p.wait(timeout=5)
            raise RuntimeError("GOST readiness timeout")
        gost = launch()
        with socket.socket(socket.AF_UNIX) as control:
            control.bind(str(ROOT / "control.sock"))
            control.listen(4)
            control.settimeout(900)
            put("fixture.json", manifest)
            put("services.json", STATS)
            print(json.dumps(manifest), flush=True)
            while True:
                conn, _ = control.accept()
                with conn:
                    op = conn.recv(32).decode()
                    if op == "stop":
                        conn.sendall(b"stopped")
                        break
                    if op in ("restart", "down"):
                        if gost is not None:
                            gost.terminate()
                            gost.wait(timeout=5)
                            gost = None
                        if op == "restart":
                            gost = launch()
                        conn.sendall(b"ok")
    finally:
        if gost is not None:
            gost.terminate()
            gost.wait(timeout=5)
        if capture is not None and capture.poll() is None:
            capture.terminate()
            capture.wait(timeout=5)
        for service in services:
            service.shutdown()
            service.server_close()
        (ROOT / "control.sock").unlink(missing_ok=True)
        log.close()


if __name__ == "__main__":
    safe_root()
    op = sys.argv[1]
    if op == "start":
        if any(ROOT.iterdir()):
            raise RuntimeError("start requires an empty private temporary directory")
        with (ROOT / "supervisor.log").open("wb") as err:
            child = subprocess.Popen([sys.executable, __file__, "supervise", str(ROOT)],
                                     stdout=subprocess.PIPE, stderr=err, start_new_session=True)
            line = child.stdout.readline()
            if not line:
                child.wait(timeout=10)
                raise RuntimeError("fixture failed; inspect private supervisor.log")
            print(line.decode().strip())
            child.stdout.close()
    elif op == "stop":
        print(command("stop"))
        for _ in range(100):
            if not (ROOT / "control.sock").exists():
                break
            time.sleep(.05)
        else:
            raise RuntimeError("supervisor cleanup timeout")
    elif op == "supervise":
        supervise()
    else:
        raise RuntimeError("invalid operation")
