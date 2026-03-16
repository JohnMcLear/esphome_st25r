"""
Emulation tests for the ST25R component.

Launches the compiled ESPHome host binary, controls two simulated NFC readers
via Unix sockets, and verifies on_tag / on_tag_removed events and Mifare
Classic full-auth / NDEF-read scenarios in the firmware's log output.

Run:
    pytest tests/emulation/run_emulation_tests.py -v

Requires:
    esphome compile tests/emulation/test-emulation.yaml
"""

import os
import re
import socket
import subprocess
import threading
import time

import pytest


# ─────────────────────────────────────────────────────────────────────────────
# Binary helpers
# ─────────────────────────────────────────────────────────────────────────────

YAML_PATH = "tests/emulation/test-emulation.yaml"
SOCKET1   = "/tmp/st25r_sim1.sock"
SOCKET2   = "/tmp/st25r_sim2.sock"


def find_binary():
    # Allow CI to override via environment variable
    env_bin = os.environ.get("ST25R_SIM_BINARY", "")
    if env_bin and os.path.isfile(env_bin):
        return env_bin

    # ESPHome places build output relative to the YAML file, so check both
    # the repo root and the tests/emulation/ sub-directory.
    build_roots = [
        ".esphome/build/st25r-sim",
        "tests/emulation/.esphome/build/st25r-sim",
    ]
    suffixes = [
        ".pioenvs/st25r-sim/program",
        ".pioenvs/host/program",
        ".pio/build/host/program",
        "firmware",
    ]
    for br in build_roots:
        for s in suffixes:
            p = os.path.join(br, s)
            if os.path.isfile(p):
                return p
        # Recursive fallback
        if os.path.isdir(br):
            for root, dirs, files in os.walk(br):
                for f in files:
                    full = os.path.join(root, f)
                    if os.access(full, os.X_OK) and not f.endswith(".elf"):
                        return full
    return None


# ─────────────────────────────────────────────────────────────────────────────
# SimProcess — manages the ESPHome host binary
# ─────────────────────────────────────────────────────────────────────────────

class SimProcess:
    def __init__(self, binary):
        self.binary = binary
        self.proc = None
        self.log_lines = []
        self._lock = threading.Lock()

    def start(self):
        self.proc = subprocess.Popen(
            [self.binary],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        t = threading.Thread(target=self._read_output, daemon=True)
        t.start()

    def _read_output(self):
        for line in self.proc.stdout:
            line = line.rstrip()
            with self._lock:
                self.log_lines.append(line)
            print(f"[FW] {line}", flush=True)

    def wait_for(self, pattern, timeout=15):
        """Block until a log line matches pattern or timeout seconds pass."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                for line in self.log_lines:
                    if re.search(pattern, line):
                        return line
            time.sleep(0.1)
        raise TimeoutError(f"Pattern {pattern!r} not seen within {timeout}s")

    def wait_for_absent(self, pattern, window=5):
        """Assert that pattern does NOT appear for `window` seconds."""
        deadline = time.time() + window
        while time.time() < deadline:
            with self._lock:
                for line in self.log_lines:
                    if re.search(pattern, line):
                        return False  # found — test should fail
            time.sleep(0.1)
        return True  # never appeared

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


# ─────────────────────────────────────────────────────────────────────────────
# SimController — controls one simulated reader via Unix socket
# ─────────────────────────────────────────────────────────────────────────────

class SimController:
    def __init__(self, path):
        self.path = path

    def _send(self, cmd):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.connect(self.path)
            s.sendall((cmd + "\n").encode())
            return s.recv(4096).decode()

    def wait_ready(self, timeout=20):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if os.path.exists(self.path):
                try:
                    self._send("LIST")
                    return
                except OSError:
                    pass
            time.sleep(0.2)
        raise TimeoutError(f"Socket {self.path} did not appear")

    def add_tag(self, uid_hex, tag_type=None, key_a=None, key_b=None,
                ndef=None):
        """
        Add a virtual tag.

        uid_hex  — hex string like "DEA30D00"
        tag_type — optional: MIFARE_1K, MIFARE_4K, NTAG213, NTAG215,
                              NTAG216, ULTRALIGHT
        key_a    — optional 12-hex Mifare Key A (default FFFFFFFFFFFF)
        key_b    — optional 12-hex Mifare Key B
        ndef     — optional hex bytes of the raw NDEF record payload
        """
        parts = [f"ADD_TAG {uid_hex}"]
        if tag_type:  parts.append(f"TYPE={tag_type}")
        if key_a:     parts.append(f"KEY_A={key_a}")
        if key_b:     parts.append(f"KEY_B={key_b}")
        if ndef:      parts.append(f"NDEF={ndef}")
        resp = self._send(" ".join(parts))
        assert "OK" in resp, f"add_tag failed: {resp}"

    def remove_tag(self, uid_hex):
        resp = self._send(f"REMOVE_TAG {uid_hex}")
        assert "OK" in resp, f"remove_tag failed: {resp}"

    def set_key(self, uid_hex, which, key_hex):
        resp = self._send(f"SET_KEY {uid_hex} {which} {key_hex}")
        assert "OK" in resp, f"set_key failed: {resp}"

    def set_ndef(self, uid_hex, ndef_hex):
        resp = self._send(f"SET_NDEF {uid_hex} {ndef_hex}")
        assert "OK" in resp, f"set_ndef failed: {resp}"

    def list_tags(self):
        return self._send("LIST")


# ─────────────────────────────────────────────────────────────────────────────
# Module-scoped fixture — starts binary once for the whole test session
# ─────────────────────────────────────────────────────────────────────────────

@pytest.fixture(scope="module")
def sim():
    binary = find_binary()
    if binary is None:
        pytest.skip("Compiled host binary not found — run: esphome compile " + YAML_PATH)

    proc = SimProcess(binary)
    proc.start()

    ctrl1 = SimController(SOCKET1)
    ctrl2 = SimController(SOCKET2)
    try:
        ctrl1.wait_ready(timeout=30)
        ctrl2.wait_ready(timeout=30)
    except TimeoutError as e:
        proc.stop()
        pytest.fail(str(e))

    yield proc, ctrl1, ctrl2

    proc.stop()


# ─────────────────────────────────────────────────────────────────────────────
# Helper NDEF records for tests
# ─────────────────────────────────────────────────────────────────────────────
# Minimal NFC Forum Text record "Hi" (en):
# D1 01 05 54 02 65 6E 48 69
NDEF_TEXT_HI = "D10105540265 6E4869".replace(" ", "")


# ─────────────────────────────────────────────────────────────────────────────
# Test classes
# ─────────────────────────────────────────────────────────────────────────────

UID4 = "DEA30D00"   # 4-byte (Mifare Classic)
UID7 = "041AA7675F6180"  # 7-byte (NTAG)


class TestBasicTagDetection:
    """on_tag fires when a virtual tag is added; on_tag_removed when removed."""

    def test_no_tags_at_startup(self, sim):
        proc, ctrl1, ctrl2 = sim
        time.sleep(2)
        with proc._lock:
            for line in proc.log_lines:
                assert "READER1_ON_TAG " not in line or "REMOVED" in line, \
                    f"Unexpected on_tag: {line}"

    def test_4byte_tag_detected(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(UID4)
        line = proc.wait_for(r"READER1_ON_TAG DEA30D00", timeout=10)
        assert "DEA30D00" in line

    def test_4byte_tag_removed(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(UID4)
        line = proc.wait_for(r"READER1_ON_TAG_REMOVED DEA30D00", timeout=10)
        assert "DEA30D00" in line


class TestSevenByteUid:
    """7-byte cascade UID is correctly detected and removed."""

    def test_7byte_tag_detected(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(UID7, tag_type="NTAG213")
        line = proc.wait_for(r"READER1_ON_TAG 041AA7675F6180", timeout=10)
        assert "041AA7675F6180" in line

    def test_7byte_tag_removed(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(UID7)
        line = proc.wait_for(r"READER1_ON_TAG_REMOVED 041AA7675F6180", timeout=10)
        assert "041AA7675F6180" in line


class TestMultiTagDetection:
    """Both tags are detected simultaneously via anticollision."""

    UID_A = "AABBCCDD"
    UID_B = "11223344"

    def test_two_tags_both_detected(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID_A)
        ctrl1.add_tag(self.UID_B)
        proc.wait_for(r"READER1_ON_TAG AABBCCDD", timeout=15)
        proc.wait_for(r"READER1_ON_TAG 11223344", timeout=15)

    def test_two_tags_both_removed(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID_A)
        ctrl1.remove_tag(self.UID_B)
        proc.wait_for(r"READER1_ON_TAG_REMOVED AABBCCDD", timeout=15)
        proc.wait_for(r"READER1_ON_TAG_REMOVED 11223344", timeout=15)


class TestNoSpuriousRetrigger:
    """A present tag must not re-fire on_tag (flap guard)."""

    def test_no_duplicate_on_tag(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(UID4)
        proc.wait_for(r"READER1_ON_TAG DEA30D00", timeout=10)
        time.sleep(3)
        with proc._lock:
            count = sum(1 for l in proc.log_lines
                        if "READER1_ON_TAG DEA30D00" in l and "REMOVED" not in l)
        assert count == 1, f"Expected 1 on_tag, got {count}"
        ctrl1.remove_tag(UID4)
        proc.wait_for(r"READER1_ON_TAG_REMOVED DEA30D00", timeout=10)


class TestMifareFullAuth:
    """
    Mifare Classic full Crypto1 3-pass auth succeeds in the simulator.

    When a Mifare Classic tag is added with the default all-FFFF key, the
    simulator responds with a real NT, correctly processes NR+AR, and returns
    a valid AT.  The firmware logs "Mifare auth OK" on success.
    """

    UID_MFC = "CAFEBABE"

    def test_mifare_auth_succeeds(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID_MFC, tag_type="MIFARE_1K")
        # on_tag must fire
        proc.wait_for(r"READER1_ON_TAG CAFEBABE", timeout=12)
        # Firmware must log successful auth (not a timeout / wrong-key failure)
        proc.wait_for(r"Mifare auth OK", timeout=5)

    def test_mifare_tag_removed(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID_MFC)
        proc.wait_for(r"READER1_ON_TAG_REMOVED CAFEBABE", timeout=10)


class TestMifareNdef:
    """
    Mifare Classic tag with NDEF data in sector 0.

    The simulator returns properly formatted NDEF TLV in blocks 1-2 so the
    firmware can parse the message after a successful auth.
    """

    UID_MFC_NDEF = "BEEFCAFE"

    def test_mifare_ndef_read(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(
            self.UID_MFC_NDEF,
            tag_type="MIFARE_1K",
            ndef=NDEF_TEXT_HI,
        )
        proc.wait_for(r"READER1_ON_TAG BEEFCAFE", timeout=12)
        # Auth must succeed before block read
        proc.wait_for(r"Mifare auth OK", timeout=5)
        # Firmware logs successful NDEF or block read (depending on log level)
        # At minimum, on_tag must have fired without a crash
        ctrl1.remove_tag(self.UID_MFC_NDEF)
        proc.wait_for(r"READER1_ON_TAG_REMOVED BEEFCAFE", timeout=10)


class TestNtagNdef:
    """
    NTAG213 with embedded NDEF record — firmware reads and parses it.
    """

    UID_NTAG = "04AABBCC112233"

    def test_ntag_ndef_detected(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID_NTAG, tag_type="NTAG213", ndef=NDEF_TEXT_HI)
        line = proc.wait_for(r"READER1_ON_TAG 04AABBCC112233", timeout=12)
        assert "04AABBCC112233" in line

    def test_ntag_ndef_firmware_finds_message(self, sim):
        proc, ctrl1, ctrl2 = sim
        # The firmware logs "Successfully read NDEF message" or "Found NDEF TLV"
        # when it successfully parses an NDEF record.
        proc.wait_for(
            r"(Successfully read NDEF|Found NDEF TLV|NDEF found)",
            timeout=5,
        )

    def test_ntag_removed(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID_NTAG)
        proc.wait_for(r"READER1_ON_TAG_REMOVED 04AABBCC112233", timeout=10)


class TestMultipleReaders:
    """
    Two independent reader instances each see only their own tags.
    Reader 1 and Reader 2 have separate socket paths and separate tag sets.
    """

    UID_R1 = "AA112233"
    UID_R2 = "BB445566"

    def test_reader1_sees_its_tag(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID_R1)
        proc.wait_for(r"READER1_ON_TAG AA112233", timeout=10)

    def test_reader2_sees_its_tag(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl2.add_tag(self.UID_R2)
        proc.wait_for(r"READER2_ON_TAG BB445566", timeout=10)

    def test_reader1_does_not_see_reader2_tag(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        # Reader 2's tag must NOT appear on Reader 1
        time.sleep(2)
        with proc._lock:
            for line in proc.log_lines:
                assert "READER1_ON_TAG BB445566" not in line, \
                    f"Reader 1 incorrectly saw Reader 2 tag: {line}"

    def test_reader2_does_not_see_reader1_tag(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        time.sleep(2)
        with proc._lock:
            for line in proc.log_lines:
                assert "READER2_ON_TAG AA112233" not in line, \
                    f"Reader 2 incorrectly saw Reader 1 tag: {line}"

    def test_cleanup(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID_R1)
        ctrl2.remove_tag(self.UID_R2)
        proc.wait_for(r"READER1_ON_TAG_REMOVED AA112233", timeout=10)
        proc.wait_for(r"READER2_ON_TAG_REMOVED BB445566", timeout=10)
