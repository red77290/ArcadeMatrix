#!/usr/bin/env python3
"""
scripts/test_tools_e2e.py
Hermetic End-to-End (E2E) validation suite for ArcadeMatrix PC & Console Companion Tools.

Validates every companion script directly from an isolated unzipped `ArcadeMatrix-tools.zip`
bundle (mirroring exact end-user usage without a Git repository checkout):
  1. Standalone `ArcadeMatrix-tools.zip` packaging, UNIX permissions, LF line-endings,
     and `sh -n` / `bash -n` syntax validation across all `.sh` scripts.
  2. Live local DMD mock servers:
       - HTTP server answering `GET /api/mqtt/broker-target` (port 8080 when free)
       - Raw TCP MQTT v3.1.1 broker capturing `CONNECT` + `PUBLISH` JSON payloads
  3. `recalbox_daemon/install.sh` E2E against isolated Batocera, Recalbox, and RetroPie
     SSH sandboxes + Action 2 (Check Logs) + live Batocera ES/configgen hook & universal
     `hi2txt` (`sf2.hi`) high-score celebration (`hiscore_record`) + live RetroPie/Recalbox
     double-forked daemon & watchdog (`arcadematrix_daemon.py` + `es_state.inf`).
  4. `recalbox_daemon/install.ps1` E2E executed through real PowerShell (`pwsh`, when
     installed on the host/CI runner) in `Auto-Detect` mode (>600 KB Base64 stdin payload
     piped to `/bin/sh -s`) across Batocera, Recalbox, and RetroPie + `-Action 2` Logs,
     verifying byte-for-byte SHA-256 integrity of the 150 KB `arcadematrix_companion_sync.py`.
  5. `pixelcade_sync/pixelcade_sync.sh` & `pixelcade_sync.ps1` offline archive extraction,
     system filtering, `console` auto-inclusion, and `_catalog/*.txt` manifest parity.
  6. `gif_indexation/generate_index.sh` & `generate_index.ps1` YOKO/TATE playlist indexing,
     macOS/Windows dotfile filtering, and LF `index.txt` / `playlists.json` parity.
  7. `bdf_to_amfont/bdf_to_amfont.py` & `font_conversion/generate_fonts.py` BDF -> `AMF1`
     binary font compilation (single-file and in-place SD folder batch mode).
  8. `anim_compiler/run_gif2amanim.sh` & `run_gif2amanim.ps1` standalone multi-frame GIF ->
     `AMAN` RGB565 binary animation compilation (verifying standalone `requirements.txt`).
  9. `mugen_extractor/mugen_extractor.py` end-to-end SFFv1 + AIR + DEF character extraction
     into `.fgt` RGB565 sprites + `index.txt` / `index.json`.
"""

import ast
import hashlib
import http.server
import io
import json
import os
import shutil
import signal
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import zipfile

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKAGE_TOOLS_PY = os.path.join(ROOT_DIR, "scripts", "package_tools.py")


def read_bytes(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def write_bytes(path: str, data: bytes) -> None:
    with open(path, "wb") as f:
        f.write(data)


def read_text(path: str) -> str:
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def write_text(path: str, data: str) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(data)


def find_pwsh() -> str | None:
    env_pwsh = os.environ.get("PWSH_BIN", "").strip()
    if env_pwsh and os.path.isfile(env_pwsh) and os.access(env_pwsh, os.X_OK):
        return env_pwsh
    which_pwsh = shutil.which("pwsh")
    if which_pwsh:
        return which_pwsh
    if os.path.isfile("/tmp/pwsh/pwsh") and os.access("/tmp/pwsh/pwsh", os.X_OK):
        return "/tmp/pwsh/pwsh"
    return None


PWSH_BIN = find_pwsh()


def _decode_mqtt_rem_len(sock: socket.socket) -> int | None:
    mult = 1
    val = 0
    for _ in range(4):
        b = sock.recv(1)
        if not b:
            return None
        digit = b[0]
        val += (digit & 0x7F) * mult
        if (digit & 0x80) == 0:
            return val
        mult *= 128
    return None


def _recv_exact(sock: socket.socket, n: int) -> bytes | None:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


class MiniMqttBroker:
    """Minimal thread-safe TCP MQTT v3.1.1 broker capturing all PUBLISH messages."""

    def __init__(self) -> None:
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind(("127.0.0.1", 0))
        self.port = self._srv.getsockname()[1]
        self._srv.listen(32)
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self.messages: list[tuple[str, dict | str]] = []
        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()

    def clear(self) -> None:
        with self._lock:
            self.messages.clear()

    def snapshot(self) -> list[tuple[str, dict | str]]:
        with self._lock:
            return list(self.messages)

    def wait_for(self, predicate, timeout: float = 5.0) -> dict | None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for topic, payload in self.snapshot():
                if isinstance(payload, dict) and predicate(topic, payload):
                    return payload
            time.sleep(0.05)
        return None

    def _accept_loop(self) -> None:
        self._srv.settimeout(0.2)
        while not self._stop.is_set():
            try:
                conn, _ = self._srv.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=self._client_loop, args=(conn,), daemon=True).start()

    def _client_loop(self, conn: socket.socket) -> None:
        conn.settimeout(3.0)
        try:
            while not self._stop.is_set():
                hdr = conn.recv(1)
                if not hdr:
                    break
                pkt_type = hdr[0] & 0xF0
                flags = hdr[0] & 0x0F
                rem_len = _decode_mqtt_rem_len(conn)
                if rem_len is None:
                    break
                body = _recv_exact(conn, rem_len) if rem_len > 0 else b""
                if body is None:
                    break

                if pkt_type == 0x10:  # CONNECT -> CONNACK
                    conn.sendall(b"\x20\x02\x00\x00")
                elif pkt_type == 0x30:  # PUBLISH
                    if len(body) < 2:
                        continue
                    tlen = struct.unpack(">H", body[:2])[0]
                    idx = 2 + tlen
                    topic = body[2:idx].decode("utf-8", errors="replace")
                    qos = (flags >> 1) & 0x03
                    pkt_id = None
                    if qos > 0:
                        if idx + 2 > len(body):
                            continue
                        pkt_id = body[idx : idx + 2]
                        idx += 2
                    raw_msg = body[idx:].decode("utf-8", errors="replace")
                    try:
                        parsed = json.loads(raw_msg)
                    except Exception:
                        parsed = raw_msg
                    with self._lock:
                        self.messages.append((topic, parsed))
                    if qos == 1 and pkt_id is not None:
                        conn.sendall(b"\x40\x02" + pkt_id)
                elif pkt_type == 0xC0:  # PINGREQ -> PINGRESP
                    conn.sendall(b"\xd0\x00")
                elif pkt_type == 0xE0:  # DISCONNECT
                    break
        except (socket.timeout, OSError):
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def close(self) -> None:
        self._stop.set()
        try:
            self._srv.close()
        except OSError:
            pass
        self._thread.join(timeout=1.5)


class ReusableTCPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    allow_reuse_address = True
    daemon_threads = True


class MockDmdHttpServer:
    """Local HTTP server answering GET /api/mqtt/broker-target on port 8080 (or ephemeral)."""

    def __init__(self, mqtt_port: int) -> None:
        self.mqtt_port = mqtt_port
        self.requests: int = 0
        parent = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path.startswith("/api/mqtt/broker-target"):
                    parent.requests += 1
                    body = json.dumps(
                        {
                            "host": "127.0.0.1",
                            "port": parent.mqtt_port,
                            "mode": "local",
                            "sync_show_hiscores": True,
                            "sync_show_ra": True,
                            "sync_celebrate_events": True,
                        }
                    ).encode("utf-8")
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                else:
                    self.send_response(404)
                    self.end_headers()

            def log_message(self, fmt, *args):
                pass

        try:
            self._httpd = ReusableTCPServer(("127.0.0.1", 8080), Handler)
            self.port = 8080
        except OSError:
            self._httpd = ReusableTCPServer(("127.0.0.1", 0), Handler)
            self.port = self._httpd.server_address[1]

        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()

    def close(self) -> None:
        try:
            self._httpd.shutdown()
            self._httpd.server_close()
        except OSError:
            pass
        self._thread.join(timeout=1.5)


def create_mock_ssh_bin(
    bin_dir: str,
    console_root: str,
    rewrite_stdin_and_b64: bool,
    http_port: int = 8080,
) -> dict[str, str]:
    """
    Creates a mock `ssh` shell wrapper (plus an isolated helper `.py` that passes SSH argv via
    JSON file rather than `python3` `/proc/*/cmdline`, avoiding false-positive matches in the
    console's `/proc` process-table scanner!) so `install.sh` and `install.ps1` execute real
    `/bin/sh` commands hermetically inside `console_root`.
    """
    os.makedirs(bin_dir, exist_ok=True)
    os.makedirs(os.path.join(console_root, "tmp"), exist_ok=True)

    helper_py = os.path.join(bin_dir, "_mock_ssh_prep.py")
    write_text(
        helper_py,
        """import json
import os
import re
import sys

root = os.environ["AM_MOCK_CONSOLE_ROOT"]
rewrite_payload = os.environ.get("AM_MOCK_REWRITE_PAYLOAD") == "1"
http_port = os.environ.get("AM_MOCK_HTTP_PORT", "8080")
args_file, out_script_file, out_stdin_file = sys.argv[1], sys.argv[2], sys.argv[3]

with open(args_file, "r", encoding="utf-8") as af:
    args = json.load(af)

_REMAP_RE = re.compile(r"(?<![A-Za-z0-9_])(/recalbox|/userdata|/opt/retropie|/tmp/arcadematrix_|/tmp/am_|/tmp/es_state\\.inf|/tmp/retroarch)")

def remap(text: str) -> str:
    text = _REMAP_RE.sub(lambda m: root + m.group(1), text)
    if http_port != "8080":
        text = text.replace("80 8080", f"80 {http_port}").replace("(80, 8080)", f"(80, {http_port})")
    return text

i = 0
target = None
cmd_parts = []
while i < len(args):
    a = args[i]
    if a == "--":
        i += 1
        if i < len(args):
            target = args[i]
            cmd_parts = args[i + 1 :]
        break
    if a in ("-T", "-N", "-f", "-q", "-v"):
        i += 1
        continue
    if a in ("-o", "-S", "-p", "-i", "-l", "-F"):
        i += 2
        continue
    if a == "-O":
        sys.exit(10)
    if target is None:
        target = a
    else:
        cmd_parts.append(a)
    i += 1

user = "root"
if target and "@" in target:
    user = target.split("@", 1)[0]

if os.environ.get("AM_MOCK_DENY_ROOT") == "1" and user == "root":
    sys.exit(255)

remote_cmd = " ".join(cmd_parts).strip()
if not remote_cmd or remote_cmd == "true":
    sys.exit(10)

stdin_bytes = sys.stdin.buffer.read()

if "sh -s" in remote_cmd:
    script_text = stdin_bytes.decode("utf-8", errors="surrogateescape").replace("\\r\\n", "\\n").replace("\\r", "\\n")
    out_lines = []
    in_b64 = False
    for line in script_text.splitlines(keepends=True):
        stripped = line.strip()
        if not in_b64 and "am_write_b64 " in line and "<<" in line:
            out_lines.append(remap(line))
            in_b64 = True
            continue
        if in_b64 and stripped.startswith("AM_B64_"):
            in_b64 = False
            out_lines.append(line)
            continue
        if in_b64:
            out_lines.append(line)
        else:
            out_lines.append(remap(line))
    with open(out_script_file, "wb") as sf:
        sf.write("".join(out_lines).encode("utf-8", errors="surrogateescape"))
    with open(out_stdin_file, "wb") as inf:
        inf.write(b"")
    sys.exit(0)

remapped_cmd = remap(remote_cmd)
if rewrite_payload and stdin_bytes:
    try:
        txt = stdin_bytes.decode("utf-8")
        stdin_bytes = remap(txt).encode("utf-8")
    except UnicodeDecodeError:
        pass

with open(out_script_file, "w", encoding="utf-8") as sf:
    sf.write(remapped_cmd + "\\n")
with open(out_stdin_file, "wb") as inf:
    inf.write(stdin_bytes)
sys.exit(0)
""",
    )

    ssh_sh = os.path.join(bin_dir, "ssh")
    write_text(
        ssh_sh,
        f"""#!/bin/sh
_af="{console_root}/tmp/ssh_args_$$.json"
_sf="{console_root}/tmp/ssh_cmd_$$.sh"
_if="{console_root}/tmp/ssh_in_$$.bin"
python3 -c 'import json,sys; open(sys.argv[1],"w").write(json.dumps(sys.argv[2:]))' "$_af" "$@"
python3 "{helper_py}" "$_af" "$_sf" "$_if"
_rc=$?
rm -f "$_af"
if [ "$_rc" = "10" ]; then
    rm -f "$_sf" "$_if"
    exit 0
fi
if [ "$_rc" != "0" ]; then
    rm -f "$_sf" "$_if"
    exit "$_rc"
fi
/bin/sh "$_sf" < "$_if"
_rc=$?
rm -f "$_sf" "$_if"
exit "$_rc"
""",
    )
    os.chmod(ssh_sh, 0o755)

    if rewrite_stdin_and_b64:
        b64_py = os.path.join(bin_dir, "base64")
        write_text(
            b64_py,
            """#!/usr/bin/env python3
import base64
import os
import re
import sys

root = os.environ["AM_MOCK_CONSOLE_ROOT"]
http_port = os.environ.get("AM_MOCK_HTTP_PORT", "8080")
_REMAP_RE = re.compile(r"(?<![A-Za-z0-9_])(/recalbox|/userdata|/opt/retropie|/tmp/arcadematrix_|/tmp/am_|/tmp/es_state\\.inf|/tmp/retroarch)")

if "-d" in sys.argv[1:] or "--decode" in sys.argv[1:]:
    raw = base64.b64decode(sys.stdin.buffer.read())
    try:
        txt = raw.decode("utf-8")
        txt = _REMAP_RE.sub(lambda m: root + m.group(1), txt)
        if http_port != "8080":
            txt = txt.replace("80 8080", f"80 {http_port}").replace("(80, 8080)", f"(80, {http_port})")
        raw = txt.encode("utf-8")
    except UnicodeDecodeError:
        pass
    sys.stdout.buffer.write(raw)
else:
    encoded = base64.b64encode(sys.stdin.buffer.read())
    sys.stdout.buffer.write(encoded)
""",
        )
        os.chmod(b64_py, 0o755)

    env = os.environ.copy()
    env["PATH"] = f"{bin_dir}:{env.get('PATH', '')}"
    env["AM_MOCK_CONSOLE_ROOT"] = console_root
    env["AM_MOCK_REWRITE_PAYLOAD"] = "1" if rewrite_stdin_and_b64 else "0"
    env["AM_MOCK_HTTP_PORT"] = str(http_port)
    env["HOME"] = os.path.join(console_root, "home_client")
    os.makedirs(env["HOME"], exist_ok=True)
    env["NO_PROXY"] = "127.0.0.1,localhost"
    env["no_proxy"] = "127.0.0.1,localhost"
    return env


def stop_sandbox_daemons(console_root: str) -> None:
    """Stops any double-forked daemon and supervisor spawned inside `console_root`."""
    tmp_dir = os.path.join(console_root, "tmp")
    stop_file = os.path.join(tmp_dir, "arcadematrix_daemon.stop")
    try:
        os.makedirs(tmp_dir, exist_ok=True)
        write_text(stop_file, "stop\n")
    except OSError:
        pass

    pids: list[int] = []
    for name in ("arcadematrix_supervisor.pid", "arcadematrix_daemon.pid", "arcadematrix_mqtt_pub.pid"):
        pf = os.path.join(tmp_dir, name)
        if os.path.isfile(pf):
            try:
                val = int(read_text(pf).strip())
                if val > 1:
                    pids.append(val)
            except Exception:
                pass

    if os.path.isdir("/proc"):
        for entry in os.listdir("/proc"):
            if not entry.isdigit():
                continue
            pid = int(entry)
            if pid in (1, os.getpid()):
                continue
            try:
                cmdline = read_bytes(f"/proc/{pid}/cmdline").decode("utf-8", errors="ignore")
                if console_root in cmdline:
                    pids.append(pid)
            except OSError:
                continue

    for pid in set(pids):
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass

    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        alive = False
        for pid in set(pids):
            try:
                os.kill(pid, 0)
                alive = True
            except OSError:
                pass
        if not alive:
            break
        time.sleep(0.05)


def make_sf2_hi_bytes(top_score: int, initials: str = "KEN") -> bytes:
    """Builds a valid 80-byte Street Fighter II (`sf2.hi`, 10 x 8-byte records) file."""
    def bcd4(val: int) -> bytes:
        s = f"{val:08d}"[-8:]
        return bytes(int(s[i : i + 2], 16) for i in range(0, 8, 2))

    tag = (initials.upper() + "   ")[:3].encode("ascii")
    buf = bytearray(80)
    buf[0:8] = bcd4(top_score) + tag + b"\x00"
    buf[8:16] = bcd4(max(10000, top_score // 2)) + b"RYU\x00"
    return bytes(buf)


def build_synthetic_bdf(bdf_path: str) -> None:
    """Writes a minimal valid BDF 2.1 bitmap font covering ASCII '0', '1', 'A', and space."""
    write_text(
        bdf_path,
        """STARTFONT 2.1
FONT -ArcadeMatrix-Test-Medium-R-Normal--6-60-75-75-C-40-ISO10646-1
SIZE 6 75 75
FONTBOUNDINGBOX 4 6 0 -1
STARTPROPERTIES 2
FONT_ASCENT 5
FONT_DESCENT 1
ENDPROPERTIES
CHARS 4
STARTCHAR space
ENCODING 32
SWIDTH 400 0
DWIDTH 4 0
BBX 4 6 0 -1
BITMAP
00
00
00
00
00
00
ENDCHAR
STARTCHAR zero
ENCODING 48
SWIDTH 400 0
DWIDTH 4 0
BBX 4 6 0 -1
BITMAP
60
90
90
90
60
00
ENDCHAR
STARTCHAR one
ENCODING 49
SWIDTH 400 0
DWIDTH 4 0
BBX 4 6 0 -1
BITMAP
20
60
20
20
70
00
ENDCHAR
STARTCHAR A
ENCODING 65
SWIDTH 400 0
DWIDTH 4 0
BBX 4 6 0 -1
BITMAP
60
90
F0
90
90
00
ENDCHAR
ENDFONT
""",
    )


class CompanionToolsE2ETest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        # Place work_dir on normal disk under HOME if writable so PowerShell never warns about tmpfs drive roots
        base_tmp = ROOT_DIR if os.access(ROOT_DIR, os.W_OK) else None
        cls.work_dir = tempfile.mkdtemp(prefix=".am_tools_e2e_", dir="/tmp")
        cls.zip_path = os.path.join(cls.work_dir, "ArcadeMatrix-tools.zip")
        res = subprocess.run(
            [sys.executable, PACKAGE_TOOLS_PY, "--output", cls.zip_path],
            capture_output=True,
            text=True,
            check=False,
        )
        if res.returncode != 0:
            raise RuntimeError(f"package_tools.py failed:\n{res.stdout}\n{res.stderr}")

        cls.extract_root = os.path.join(cls.work_dir, "unzipped")
        with zipfile.ZipFile(cls.zip_path, "r") as zf:
            for info in zf.infolist():
                target = os.path.join(cls.extract_root, info.filename)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                write_bytes(target, zf.read(info.filename))
                mode = (info.external_attr >> 16) & 0o777
                if mode:
                    os.chmod(target, mode)

        cls.tools_dir = os.path.join(cls.extract_root, "ArcadeMatrix-tools")
        cls.mqtt = MiniMqttBroker()
        cls.http = MockDmdHttpServer(cls.mqtt.port)
        _ = base_tmp

    @classmethod
    def tearDownClass(cls) -> None:
        cls.http.close()
        cls.mqtt.close()
        shutil.rmtree(cls.work_dir, ignore_errors=True)

    # ------------------------------------------------------------------
    # 1. Standalone Archive Integrity & Shell Syntax (`sh -n` / `bash -n`)
    # ------------------------------------------------------------------
    def test_01_standalone_archive_permissions_and_shell_syntax(self) -> None:
        sh_count = 0
        py_count = 0
        for root, _dirs, files in os.walk(self.tools_dir):
            for fname in sorted(files):
                fpath = os.path.join(root, fname)
                if fname.endswith((".sh", ".py")):
                    raw = read_bytes(fpath)
                    self.assertNotIn(b"\r", raw, f"CR byte found in {fpath}")
                    st_mode = os.stat(fpath).st_mode & 0o777
                    self.assertEqual(st_mode, 0o755, f"Expected 0755 on {fpath}, got {oct(st_mode)}")

                if fname.endswith(".sh"):
                    sh_count += 1
                    first_line = read_text(fpath).splitlines()[0].strip()
                    shell_bin = "/bin/sh" if first_line == "#!/bin/sh" else "bash"
                    chk = subprocess.run([shell_bin, "-n", fpath], capture_output=True, text=True)
                    self.assertEqual(chk.returncode, 0, f"{shell_bin} -n failed on {fpath}: {chk.stderr}")
                elif fname.endswith(".py"):
                    py_count += 1
                    ast.parse(read_text(fpath), filename=fpath)

        self.assertGreaterEqual(sh_count, 8)
        self.assertGreaterEqual(py_count, 6)
        self.assertTrue(os.path.isfile(os.path.join(self.tools_dir, "anim_compiler", "requirements.txt")))

    # ------------------------------------------------------------------
    # 2. Batocera Byte-Exact Transfer (`install.sh`) + Live Hook & High-Score E2E
    # ------------------------------------------------------------------
    def test_02_batocera_install_sh_byte_exact_and_live_hiscore_e2e(self) -> None:
        self.mqtt.clear()
        console_root = os.path.join(self.work_dir, "bato_sh")
        os.makedirs(os.path.join(console_root, "userdata", "system"), exist_ok=True)
        bin_dir = os.path.join(self.work_dir, "bin_bato_sh")
        env = create_mock_ssh_bin(bin_dir, console_root, rewrite_stdin_and_b64=False, http_port=self.http.port)

        install_sh = os.path.join(self.tools_dir, "recalbox_daemon", "install.sh")
        res = subprocess.run(
            ["bash", install_sh],
            input="10.0.0.11\n1\n127.0.0.1\n1\n\n\n",
            capture_output=True,
            text=True,
            env=env,
            timeout=20,
        )
        self.assertEqual(res.returncode, 0, f"install.sh Batocera failed:\n{res.stdout}\n{res.stderr}")
        self.assertIn("Console identified as: batocera", res.stdout)
        self.assertIn("DMD /api/mqtt/broker-target ->", res.stdout)

        src_comp = os.path.join(self.tools_dir, "recalbox_daemon", "arcadematrix_companion_sync.py")
        expected_comp = (
            read_text(src_comp)
            .replace("{{DMD_HOST}}", "127.0.0.1")
            .replace("{{BROKER}}", "127.0.0.1")
            .replace("{{TOPIC}}", "system/playing/batocera")
            .encode("utf-8")
        )
        deployed_comp_path = os.path.join(
            console_root, "userdata", "system", "scripts", "arcadematrix_companion_sync.py"
        )
        deployed_comp = read_bytes(deployed_comp_path)
        self.assertEqual(
            hashlib.sha256(deployed_comp).hexdigest(),
            hashlib.sha256(expected_comp).hexdigest(),
            "Deployed Batocera arcadematrix_companion_sync.py is not byte-identical!",
        )
        self.assertGreater(len(deployed_comp), 140_000)

        for evt in ("game-selected", "system-selected", "game-start", "game-end"):
            wrapper = os.path.join(
                console_root,
                "userdata",
                "system",
                "configs",
                "emulationstation",
                "scripts",
                evt,
                "arcadematrix_mqtt.sh",
            )
            self.assertTrue(os.access(wrapper, os.X_OK), f"Missing ES wrapper {wrapper}")

        # Execute the deployed Batocera hook LIVE inside console_root
        import re as _re
        _remap_re = _re.compile(r"(?<![A-Za-z0-9_])(/recalbox|/userdata|/opt/retropie|/tmp/arcadematrix_|/tmp/am_|/tmp/es_state\.inf|/tmp/retroarch)")
        hook_path = os.path.join(console_root, "userdata", "system", "scripts", "arcadematrix_mqtt.sh")
        for p in (hook_path, deployed_comp_path):
            txt = _remap_re.sub(lambda m: console_root + m.group(1), read_text(p))
            if self.http.port != 8080:
                txt = txt.replace("80 8080", f"80 {self.http.port}").replace("(80, 8080)", f"(80, {self.http.port})")
            write_text(p, txt)

        hi_dir = os.path.join(console_root, "userdata", "saves", "mame", "mame0278", "hiscore")
        rom_dir = os.path.join(console_root, "userdata", "roms", "mame")
        os.makedirs(hi_dir, exist_ok=True)
        os.makedirs(rom_dir, exist_ok=True)
        hi_file = os.path.join(hi_dir, "sf2.hi")
        rom_file = os.path.join(rom_dir, "sf2.zip")
        write_bytes(rom_file, b"PK\x05\x06" + b"\x00" * 18)
        write_bytes(hi_file, make_sf2_hi_bytes(50000, "RYU"))

        try:
            subprocess.run(["/bin/sh", hook_path, "system-selected", "mame"], env=env, check=True, timeout=5)
            browsing_evt = self.mqtt.wait_for(
                lambda t, p: t == "system/playing/batocera" and p.get("status") == "browsing" and p.get("system") == "mame"
            )
            self.assertIsNotNone(browsing_evt, f"Missing Batocera browsing MQTT event; got {self.mqtt.snapshot()}")

            subprocess.run(
                ["/bin/sh", hook_path, "gameStart", "mame", "libretro", "mame0278", rom_file],
                env=env,
                check=True,
                timeout=5,
            )
            playing_evt = self.mqtt.wait_for(
                lambda t, p: t == "system/playing/batocera" and p.get("status") == "playing" and p.get("game") == "sf2"
            )
            self.assertIsNotNone(playing_evt, f"Missing Batocera playing MQTT event; got {self.mqtt.snapshot()}")

            meta_evt = self.mqtt.wait_for(
                lambda t, p: t == "system/playing/batocera" and p.get("status") == "sync_meta" and p.get("game") == "sf2"
            )
            self.assertIsNotNone(meta_evt, f"Missing Batocera sync_meta MQTT event; got {self.mqtt.snapshot()}")
            self.assertEqual(meta_evt["hiscores"][0]["score"], "50,000")
            self.assertEqual(meta_evt["hiscores"][0]["tag"], "RYU")
            self.assertEqual(meta_evt["hiscores"][0]["rank"], 1)

            time.sleep(0.05)
            write_bytes(hi_file, make_sf2_hi_bytes(950000, "KEN"))
            subprocess.run(["/bin/sh", hook_path, "gameStop"], env=env, check=True, timeout=5)

            stopped_evt = self.mqtt.wait_for(
                lambda t, p: t == "system/playing/batocera" and p.get("status") == "stopped"
            )
            self.assertIsNotNone(stopped_evt, "Missing Batocera stopped MQTT event")

            rec_evt = self.mqtt.wait_for(
                lambda t, p: t == "system/playing/batocera"
                and p.get("status") == "celebrate"
                and p.get("kind") == "highscore"
            )
            self.assertIsNotNone(rec_evt, f"Missing celebrate highscore event; got {self.mqtt.snapshot()}")
            self.assertEqual(rec_evt["score"], "950,000")
            self.assertEqual(rec_evt["tag"], "KEN")
            self.assertEqual(rec_evt["rank"], 1)
        finally:
            stop_sandbox_daemons(console_root)

    # ------------------------------------------------------------------
    # 3. Recalbox & RetroPie Live Daemon + Watchdog (`install.sh` + Action 2 Logs)
    # ------------------------------------------------------------------
    def test_03_recalbox_and_retropie_install_sh_live_daemon_and_logs(self) -> None:
        install_sh = os.path.join(self.tools_dir, "recalbox_daemon", "install.sh")

        # --- 3A. Recalbox Auto-Detect + Live Daemon Startup + Action 2 Check Logs ---
        rb_root = os.path.join(self.work_dir, "recalbox_sh")
        os.makedirs(os.path.join(rb_root, "recalbox", "share"), exist_ok=True)
        rb_bin = os.path.join(self.work_dir, "bin_rb_sh")
        rb_env = create_mock_ssh_bin(rb_bin, rb_root, rewrite_stdin_and_b64=True, http_port=self.http.port)

        try:
            self.mqtt.clear()
            res = subprocess.run(
                ["bash", install_sh],
                input="10.0.0.12\n1\n127.0.0.1\n1\n\n\n",
                capture_output=True,
                text=True,
                env=rb_env,
                timeout=25,
            )
            self.assertEqual(res.returncode, 0, f"install.sh Recalbox failed:\n{res.stdout}\n{res.stderr}")
            self.assertIn("Console identified as: recalbox", res.stdout)
            self.assertIn("Daemon PID", res.stdout)

            es_inf = os.path.join(rb_root, "tmp", "es_state.inf")
            write_text(es_inf, "State=browsing\nAction=systembrowsing\nSystemId=neogeo\nGamePath=\nGame=\n")

            rb_evt = self.mqtt.wait_for(
                lambda t, p: t == "system/playing/recalbox" and p.get("status") == "browsing" and p.get("system") == "neogeo"
            )
            self.assertIsNotNone(rb_evt, f"Live Recalbox daemon did not publish browsing event; got {self.mqtt.snapshot()}")

            log_res = subprocess.run(
                ["bash", install_sh],
                input="10.0.0.12\n2\n1\n\n\n",
                capture_output=True,
                text=True,
                env=rb_env,
                timeout=15,
            )
            self.assertEqual(log_res.returncode, 0, f"install.sh Action 2 failed:\n{log_res.stdout}\n{log_res.stderr}")
            self.assertIn("PUB ok", log_res.stdout)
        finally:
            stop_sandbox_daemons(rb_root)

        # --- 3B. RetroPie Auto-Detect (root denied -> fallback 'pi') + ES/runcommand Hooks ---
        rp_root = os.path.join(self.work_dir, "retropie_sh")
        os.makedirs(os.path.join(rp_root, "opt", "retropie", "configs", "all"), exist_ok=True)
        rp_bin = os.path.join(self.work_dir, "bin_rp_sh")
        rp_env = create_mock_ssh_bin(rp_bin, rp_root, rewrite_stdin_and_b64=True, http_port=self.http.port)
        rp_env["AM_MOCK_DENY_ROOT"] = "1"

        try:
            self.mqtt.clear()
            res = subprocess.run(
                ["bash", install_sh],
                input="10.0.0.13\n1\n127.0.0.1\n1\n\n\n",
                capture_output=True,
                text=True,
                env=rp_env,
                timeout=25,
            )
            self.assertEqual(res.returncode, 0, f"install.sh RetroPie failed:\n{res.stdout}\n{res.stderr}")
            self.assertIn("Console identified as: retropie (User: pi)", res.stdout)
            self.assertIn("RetroPie Daemon PID", res.stdout)

            onstart = os.path.join(rp_root, "opt", "retropie", "configs", "all", "runcommand-onstart.sh")
            onend = os.path.join(rp_root, "opt", "retropie", "configs", "all", "runcommand-onend.sh")
            subprocess.run(["/bin/sh", onstart, "arcade", "lr-fbneo", "/home/pi/RetroPie/roms/arcade/pacman.zip"], check=True)

            rp_play = self.mqtt.wait_for(
                lambda t, p: t == "system/playing/retropie" and p.get("status") == "playing" and p.get("game") == "pacman"
            )
            self.assertIsNotNone(rp_play, f"RetroPie runcommand-onstart did not trigger playing MQTT; got {self.mqtt.snapshot()}")

            subprocess.run(["/bin/sh", onend], check=True)
            rp_stop = self.mqtt.wait_for(
                lambda t, p: t == "system/playing/retropie" and p.get("status") == "stopped"
            )
            self.assertIsNotNone(rp_stop, "RetroPie runcommand-onend did not trigger stopped MQTT")
        finally:
            stop_sandbox_daemons(rp_root)

    # ------------------------------------------------------------------
    # 4. PowerShell `install.ps1` E2E (>600 KB Base64 Auto-Detect Payload)
    # ------------------------------------------------------------------
    def test_04_install_ps1_powershell_e2e_all_three_consoles(self) -> None:
        if not PWSH_BIN:
            self.skipTest("pwsh not installed on this host")

        install_ps1 = os.path.join(self.tools_dir, "recalbox_daemon", "install.ps1")

        # --- 4A. Batocera via install.ps1 Auto-Detect (exact byte-for-byte SHA-256 check!) ---
        bato_root = os.path.join(self.work_dir, "bato_ps1")
        os.makedirs(os.path.join(bato_root, "userdata", "system"), exist_ok=True)
        bato_bin = os.path.join(self.work_dir, "bin_bato_ps1")
        bato_env = create_mock_ssh_bin(bato_bin, bato_root, rewrite_stdin_and_b64=False, http_port=self.http.port)

        res = subprocess.run(
            [
                PWSH_BIN,
                "-NoLogo",
                "-NoProfile",
                "-NonInteractive",
                "-File",
                install_ps1,
                "-TargetIp",
                "http://10.0.0.21:80/",
                "-Action",
                "1",
                "-DmdIp",
                "http://127.0.0.1:8080/",
                "-OsChoice",
                "1",
            ],
            cwd=ROOT_DIR,
            capture_output=True,
            text=True,
            env=bato_env,
            timeout=30,
        )
        self.assertEqual(res.returncode, 0, f"install.ps1 Batocera failed:\n{res.stdout}\n{res.stderr}")
        self.assertIn("Done! batocera one-shot hooks installed", res.stdout)

        src_comp = os.path.join(self.tools_dir, "recalbox_daemon", "arcadematrix_companion_sync.py")
        expected_comp = (
            read_text(src_comp)
            .replace("{{DMD_HOST}}", "127.0.0.1")
            .replace("{{BROKER}}", "127.0.0.1")
            .replace("{{TOPIC}}", "system/playing/batocera")
            .encode("utf-8")
        )
        deployed_comp = read_bytes(
            os.path.join(bato_root, "userdata", "system", "scripts", "arcadematrix_companion_sync.py")
        )
        self.assertEqual(
            hashlib.sha256(deployed_comp).hexdigest(),
            hashlib.sha256(expected_comp).hexdigest(),
            "install.ps1 Base64 transfer corrupted 150 KB arcadematrix_companion_sync.py!",
        )

        # --- 4B. Recalbox + RetroPie (root -> pi fallback) + Action 2 Check Logs via install.ps1 ---
        for sys_name, rel_marker, deny_root in (
            ("recalbox", os.path.join("recalbox", "share"), False),
            ("retropie", os.path.join("opt", "retropie", "configs", "all"), True),
        ):
            c_root = os.path.join(self.work_dir, f"{sys_name}_ps1")
            os.makedirs(os.path.join(c_root, rel_marker), exist_ok=True)
            c_bin = os.path.join(self.work_dir, f"bin_{sys_name}_ps1")
            c_env = create_mock_ssh_bin(c_bin, c_root, rewrite_stdin_and_b64=True, http_port=self.http.port)
            if deny_root:
                c_env["AM_MOCK_DENY_ROOT"] = "1"

            try:
                r_ins = subprocess.run(
                    [
                        PWSH_BIN,
                        "-NoLogo",
                        "-NoProfile",
                        "-NonInteractive",
                        "-File",
                        install_ps1,
                        "-TargetIp",
                        "10.0.0.22",
                        "-Action",
                        "1",
                        "-DmdIp",
                        "127.0.0.1",
                        "-OsChoice",
                        "1",
                    ],
                    cwd=ROOT_DIR,
                    capture_output=True,
                    text=True,
                    env=c_env,
                    timeout=35,
                )
                self.assertEqual(r_ins.returncode, 0, f"install.ps1 {sys_name} failed:\n{r_ins.stdout}\n{r_ins.stderr}")
                self.assertIn(f"Done! {sys_name} is configured", r_ins.stdout)

                r_log = subprocess.run(
                    [
                        PWSH_BIN,
                        "-NoLogo",
                        "-NoProfile",
                        "-NonInteractive",
                        "-File",
                        install_ps1,
                        "-TargetIp",
                        "10.0.0.22",
                        "-Action",
                        "2",
                        "-OsChoice",
                        "1",
                    ],
                    cwd=ROOT_DIR,
                    capture_output=True,
                    text=True,
                    env=c_env,
                    timeout=20,
                )
                self.assertEqual(r_log.returncode, 0, f"install.ps1 Action 2 {sys_name} failed:\n{r_log.stdout}")
                self.assertIn(f"Detected: {sys_name}", r_log.stdout)
            finally:
                stop_sandbox_daemons(c_root)

    # ------------------------------------------------------------------
    # 5. `pixelcade_sync.sh` & `pixelcade_sync.ps1` Offline Zip Parity
    # ------------------------------------------------------------------
    def test_05_pixelcade_sync_sh_and_ps1_parity(self) -> None:
        mock_zip = os.path.join(self.work_dir, "mock_pixelcade.zip")
        with zipfile.ZipFile(mock_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("pixelcade-master/mame/pacman.png", b"\x89PNG\r\n\x1a\nPACMAN")
            zf.writestr("pixelcade-master/mame/sf2.gif", b"GIF89aSF2")
            zf.writestr("pixelcade-master/snes/smw.png", b"\x89PNG\r\n\x1a\nSMW")
            zf.writestr("pixelcade-master/console/mame.png", b"\x89PNG\r\n\x1a\nCONSOLE_MAME")
            zf.writestr("pixelcade-master/_catalog/_folders.txt", "console\nmame\nsnes\n")
            zf.writestr("pixelcade-master/_catalog/mame.txt", "pacman.png\nsf2.gif\n")
            zf.writestr("pixelcade-master/_catalog/snes.txt", "smw.png\n")
            zf.writestr("pixelcade-master/scripts/generate_catalog.py", "# ignored\n")

        dest_sh = os.path.join(self.work_dir, "px_out_sh")
        px_sh = os.path.join(self.tools_dir, "pixelcade_sync", "pixelcade_sync.sh")
        env = os.environ.copy()
        env["LOCAL_ZIP"] = mock_zip
        env["DEST"] = dest_sh
        r_sh = subprocess.run(["bash", px_sh, "mame"], capture_output=True, text=True, env=env, timeout=15)
        self.assertEqual(r_sh.returncode, 0, f"pixelcade_sync.sh failed:\n{r_sh.stdout}\n{r_sh.stderr}")

        def collect_rel_files(base: str) -> set[str]:
            out = set()
            for r, _ds, fs in os.walk(base):
                for fn in fs:
                    out.add(os.path.relpath(os.path.join(r, fn), base).replace(os.sep, "/"))
            return out

        expected_files = {
            "mame/pacman.png",
            "mame/sf2.gif",
            "console/mame.png",
            "_catalog/_folders.txt",
            "_catalog/mame.txt",
        }
        self.assertEqual(collect_rel_files(dest_sh), expected_files)

        if PWSH_BIN:
            dest_ps1 = os.path.join(self.work_dir, "px_out_ps1")
            px_ps1 = os.path.join(self.tools_dir, "pixelcade_sync", "pixelcade_sync.ps1")
            r_ps1 = subprocess.run(
                [
                    PWSH_BIN,
                    "-NoLogo",
                    "-NoProfile",
                    "-NonInteractive",
                    "-File",
                    px_ps1,
                    "-ZipPath",
                    mock_zip,
                    "-Dest",
                    dest_ps1,
                    "-Systems",
                    "mame",
                ],
                cwd=ROOT_DIR,
                capture_output=True,
                text=True,
                timeout=20,
            )
            self.assertEqual(r_ps1.returncode, 0, f"pixelcade_sync.ps1 failed:\n{r_ps1.stdout}\n{r_ps1.stderr}")
            self.assertEqual(collect_rel_files(dest_ps1), expected_files)

    # ------------------------------------------------------------------
    # 6. `generate_index.sh` & `generate_index.ps1` Parity & Junk Filtering
    # ------------------------------------------------------------------
    def test_06_gif_indexation_sh_and_ps1_parity(self) -> None:
        def seed_sd(sd_root: str) -> None:
            os.makedirs(os.path.join(sd_root, "gifs", "arcade"), exist_ok=True)
            os.makedirs(os.path.join(sd_root, "gifs", ".Spotlight-V100"), exist_ok=True)
            os.makedirs(os.path.join(sd_root, "gifs_tate", "shmups"), exist_ok=True)
            write_bytes(os.path.join(sd_root, "gifs", "arcade", "pacman.gif"), b"GIF89a")
            write_bytes(os.path.join(sd_root, "gifs", "arcade", "sf2.png"), b"\x89PNG")
            write_bytes(os.path.join(sd_root, "gifs", "arcade", "._pacman.gif"), b"appledouble")
            write_bytes(os.path.join(sd_root, "gifs", "arcade", "readme.txt"), b"ignore")
            write_bytes(os.path.join(sd_root, "gifs", ".Spotlight-V100", "junk.gif"), b"GIF89a")
            write_bytes(os.path.join(sd_root, "gifs_tate", "shmups", "ikaruga.gif"), b"GIF89a")

        sd_sh = os.path.join(self.work_dir, "sd_idx_sh")
        seed_sd(sd_sh)
        gen_sh = os.path.join(self.tools_dir, "gif_indexation", "generate_index.sh")
        r_sh = subprocess.run(["bash", gen_sh, sd_sh], capture_output=True, text=True, timeout=10)
        self.assertEqual(r_sh.returncode, 0, f"generate_index.sh failed:\n{r_sh.stdout}\n{r_sh.stderr}")

        pl_yoko = json.loads(read_text(os.path.join(sd_sh, "gifs", "playlists.json")))
        pl_tate = json.loads(read_text(os.path.join(sd_sh, "gifs_tate", "playlists.json")))
        self.assertEqual(pl_yoko, {"arcade": {"path": "/gifs/arcade", "count": 2}})
        self.assertEqual(pl_tate, {"shmups": {"path": "/gifs_tate/shmups", "count": 1}})
        idx_raw = read_bytes(os.path.join(sd_sh, "gifs", "arcade", "index.txt"))
        self.assertNotIn(b"\r", idx_raw)
        self.assertEqual(sorted(idx_raw.decode("utf-8").splitlines()), ["pacman.gif", "sf2.png"])

        if PWSH_BIN:
            sd_ps1 = os.path.join(self.work_dir, "sd_idx_ps1")
            seed_sd(sd_ps1)
            gen_ps1 = os.path.join(self.tools_dir, "gif_indexation", "generate_index.ps1")
            r_ps1 = subprocess.run(
                [PWSH_BIN, "-NoLogo", "-NoProfile", "-NonInteractive", "-File", gen_ps1, "-Path", sd_ps1],
                cwd=ROOT_DIR,
                capture_output=True,
                text=True,
                timeout=15,
            )
            self.assertEqual(r_ps1.returncode, 0, f"generate_index.ps1 failed:\n{r_ps1.stdout}\n{r_ps1.stderr}")
            pl_yoko_ps1 = json.loads(read_text(os.path.join(sd_ps1, "gifs", "playlists.json")))
            self.assertEqual(pl_yoko_ps1, pl_yoko)
            idx_ps1 = read_bytes(os.path.join(sd_ps1, "gifs", "arcade", "index.txt"))
            self.assertNotIn(b"\r", idx_ps1)
            self.assertEqual(idx_ps1.decode("utf-8").splitlines(), ["pacman.gif", "sf2.png"])

    # ------------------------------------------------------------------
    # 7. Font Converters (`bdf_to_amfont.py` & `font_conversion/generate_fonts.py`)
    # ------------------------------------------------------------------
    def test_07_bdf_to_amfont_and_batch_generate_fonts(self) -> None:
        sd_fonts = os.path.join(self.work_dir, "sd_fonts", "fonts")
        os.makedirs(sd_fonts, exist_ok=True)
        bdf_single = os.path.join(self.work_dir, "single.bdf")
        amf_single = os.path.join(self.work_dir, "single.amf")
        build_synthetic_bdf(bdf_single)

        bdf_py = os.path.join(self.tools_dir, "bdf_to_amfont", "bdf_to_amfont.py")
        r1 = subprocess.run([sys.executable, bdf_py, bdf_single, amf_single], capture_output=True, text=True)
        self.assertEqual(r1.returncode, 0, f"bdf_to_amfont.py failed:\n{r1.stderr}")
        raw_amf = read_bytes(amf_single)
        self.assertEqual(raw_amf[:4], b"AMF1")

        bdf_in_sd = os.path.join(sd_fonts, "retro6.bdf")
        build_synthetic_bdf(bdf_in_sd)
        gen_fonts_py = os.path.join(self.tools_dir, "font_conversion", "generate_fonts.py")
        r2 = subprocess.run(
            [sys.executable, gen_fonts_py, os.path.dirname(sd_fonts)], capture_output=True, text=True
        )
        self.assertEqual(r2.returncode, 0, f"generate_fonts.py failed:\n{r2.stdout}\n{r2.stderr}")
        amf_in_sd = os.path.join(sd_fonts, "retro6.amf")
        self.assertTrue(os.path.isfile(amf_in_sd))
        self.assertFalse(os.path.exists(bdf_in_sd), "Source .bdf should be cleaned up after .amf generation")
        self.assertEqual(read_bytes(amf_in_sd), raw_amf)

    # ------------------------------------------------------------------
    # 8. Standalone Animation Compiler (`run_gif2amanim.sh` & `run_gif2amanim.ps1`)
    #    & MUGEN Character Extractor (`mugen_extractor.py`)
    # ------------------------------------------------------------------
    def test_08_standalone_anim_compiler_and_mugen_extractor(self) -> None:
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow not installed in test Python environment")

        gif_path = os.path.join(self.work_dir, "spin.gif")
        frames = []
        for idx, col in enumerate(((255, 0, 0), (0, 255, 0), (0, 0, 255))):
            im = Image.new("RGB", (16, 16), (0, 0, 0))
            for xy in range(4 + idx, 12):
                im.putpixel((xy, xy), col)
            frames.append(im)
        frames[0].save(gif_path, save_all=True, append_images=frames[1:], duration=50, loop=0)

        out_sh = os.path.join(self.work_dir, "spin_sh.amanim")
        run_sh = os.path.join(self.tools_dir, "anim_compiler", "run_gif2amanim.sh")
        r_sh = subprocess.run(["bash", run_sh, gif_path, out_sh, "--fps", "25"], capture_output=True, text=True, timeout=30)
        self.assertEqual(r_sh.returncode, 0, f"run_gif2amanim.sh failed:\n{r_sh.stdout}\n{r_sh.stderr}")
        hdr = read_bytes(out_sh)[:14]
        magic, ver, w, h, fcnt, fps, _pal = struct.unpack("<4sBHHHBH", hdr)
        self.assertEqual((magic, ver, w, h, fcnt, fps), (b"AMAN", 1, 16, 16, 3, 25))

        if PWSH_BIN:
            out_ps1 = os.path.join(self.work_dir, "spin_ps1.amanim")
            run_ps1 = os.path.join(self.tools_dir, "anim_compiler", "run_gif2amanim.ps1")
            r_ps1 = subprocess.run(
                [PWSH_BIN, "-NoLogo", "-NoProfile", "-NonInteractive", "-File", run_ps1, gif_path, out_ps1, "--fps", "25"],
                cwd=ROOT_DIR,
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(r_ps1.returncode, 0, f"run_gif2amanim.ps1 failed:\n{r_ps1.stdout}\n{r_ps1.stderr}")
            self.assertEqual(read_bytes(out_ps1), read_bytes(out_sh))

        char_dir = os.path.join(self.work_dir, "chars", "ryu_e2e")
        os.makedirs(char_dir, exist_ok=True)

        spr = Image.new("P", (16, 24), 0)
        pal = [0, 0, 0] * 256
        pal[3:6] = [240, 240, 240]
        pal[6:9] = [220, 30, 30]
        spr.putpalette(pal)
        for y in range(4, 22):
            for x in range(4, 12):
                spr.putpixel((x, y), 1)
        for x in range(4, 12):
            spr.putpixel((x, 5), 2)
        pcx_buf = io.BytesIO()
        spr.save(pcx_buf, format="PCX")
        pcx_bytes = pcx_buf.getvalue()

        sff_hdr = bytearray(512)
        sff_hdr[0:12] = b"ElecbyteSpr\x00"
        struct.pack_into("<I", sff_hdr, 20, 1)
        struct.pack_into("<I", sff_hdr, 24, 512)
        sub_hdr = struct.pack("<IIhhHHHxB", 0, len(pcx_bytes), 8, 24, 0, 0, 0, 0) + b"\x00" * 12
        write_bytes(os.path.join(char_dir, "ryu_e2e.sff"), bytes(sff_hdr) + sub_hdr + pcx_bytes)

        write_text(
            os.path.join(char_dir, "ryu_e2e.air"),
            "[Begin Action 0]\n0,0, 0,0, 6\n\n[Begin Action 20]\n0,0, 0,0, 6\n\n[Begin Action 200]\n0,0, 0,0, 5\n",
        )
        write_text(
            os.path.join(char_dir, "ryu_e2e.def"),
            "[Info]\nname = \"Ryu E2E\"\ndisplayname = \"Ryu\"\n[Files]\nsprite = ryu_e2e.sff\nanim = ryu_e2e.air\n",
        )

        out_fgt_dir = os.path.join(self.work_dir, "fighters_32")
        mugen_py = os.path.join(self.tools_dir, "mugen_extractor", "mugen_extractor.py")
        r_mugen = subprocess.run(
            [sys.executable, mugen_py, "--src", os.path.dirname(char_dir), "--dest", out_fgt_dir, "--workers", "1"],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(r_mugen.returncode, 0, f"mugen_extractor.py failed:\n{r_mugen.stdout}\n{r_mugen.stderr}")
        self.assertTrue(os.path.isfile(os.path.join(out_fgt_dir, "index.txt")))
        idx_txt = read_text(os.path.join(out_fgt_dir, "index.txt")).strip()
        self.assertTrue(idx_txt.startswith("ryu_e2e,"), f"Unexpected fighters index.txt: {idx_txt}")


if __name__ == "__main__":
    print(f"🔧 Running ArcadeMatrix Companion Tools E2E Suite (pwsh={PWSH_BIN or 'SKIPPED'})...")
    unittest.main(verbosity=2)
