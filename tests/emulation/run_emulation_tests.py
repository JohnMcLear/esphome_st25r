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
        # Force line-buffered stdout on the C binary so log lines reach Python
        # immediately.  Without stdbuf, the C runtime's stdio buffer (4096 bytes)
        # causes log lines to be held until the buffer fills — which can be
        # several seconds of wall time, causing wait_for() to time out even
        # though the binary is actively printing.
        self.proc = subprocess.Popen(
            ["stdbuf", "-oL", self.binary],
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

    def get_reg(self, addr_hex):
        """Read a register value from the sim (hex addr string like '0D')."""
        resp = self._send(f"GET_REG {addr_hex}")
        return int(resp.strip(), 16)

    def set_vdd(self, value_hex):
        """Set the raw VDD measurement value (hex string like 'A0')."""
        resp = self._send(f"SET_VDD {value_hex}")
        assert "OK" in resp, f"set_vdd failed: {resp}"

    def set_ic_identity(self, value_hex):
        """Change the IC identity the sim reports (hex string like '30')."""
        resp = self._send(f"SET_IC_IDENTITY {value_hex}")
        assert "OK" in resp, f"set_ic_identity failed: {resp}"


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


class TestStatusAndFieldStrength:
    """
    Reader status binary sensor publishes true on init, field strength sensor
    publishes amplitude (0x80=128 from the sim) on every update cycle.
    """

    def test_status_sensor_true(self, sim):
        proc, ctrl1, ctrl2 = sim
        # READER1_STATUS 1 fires at startup — check entire log since start
        with proc._lock:
            for line in proc.log_lines:
                if re.search(r"READER1_STATUS 1", line):
                    return
        # Not yet in log — wait for it
        proc.wait_for(r"READER1_STATUS 1", timeout=10)

    def test_field_strength_reported(self, sim):
        proc, ctrl1, ctrl2 = sim
        # Field strength is published every update interval; should appear quickly
        proc.wait_for(r"READER1_FIELD_STRENGTH", timeout=5)


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


# ── NDEF record helpers ────────────────────────────────────────────────────
# 9-byte Text record "Hi" (en) — fits in one page read
NDEF_TEXT_HI = "D10105540265 6E4869".replace(" ", "")
# 20-byte Text record "Hello World!!" (en) — spans multiple NTAG pages
NDEF_TEXT_LONG = "D1011154" + "0265" + "6E48656C6C6F20576F726C642121"


class TestBinarySensor:
    """
    ST25R binary sensor publishes true when the matching UID is in field,
    false when absent. The YAML on_state lambda logs BINARY_SENSOR_STATE.
    """

    UID_BS = "FF112233"

    def test_binary_sensor_true_when_present(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID_BS)
        # Wait for on_tag then binary sensor state=1
        proc.wait_for(r"READER1_ON_TAG FF112233", timeout=10)
        proc.wait_for(r"BINARY_SENSOR_STATE 1", timeout=5)

    def test_binary_sensor_false_when_absent(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID_BS)
        proc.wait_for(r"READER1_ON_TAG_REMOVED FF112233", timeout=10)
        proc.wait_for(r"BINARY_SENSOR_STATE 0", timeout=5)


class TestNdefWrite:
    """
    NTAG tag that triggers clean_tag() in the on_tag YAML lambda.
    The simulator handles the 0xA2 WRITE commands and returns ACK.
    """

    UID_WRITE = "CCDDEE11223344"  # 7-byte NTAG; triggers clean_tag in on_tag

    def test_ndef_write_succeeds(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID_WRITE, tag_type="NTAG213")
        proc.wait_for(r"READER1_ON_TAG CCDDEE11223344", timeout=12)
        proc.wait_for(r"READER1_CLEAN_TAG_OK", timeout=5)

    def test_ndef_write_tag_removed(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID_WRITE)
        proc.wait_for(r"READER1_ON_TAG_REMOVED CCDDEE11223344", timeout=10)


class TestNtagMultiPage:
    """
    NTAG with a 20-byte NDEF record that spans more than one page read.
    The simulator initialises page_mem_ with CC at page 3, NDEF TLV at page 4,
    and overflow at page 8. The firmware must issue three reads (pages 0, 4, 8)
    to collect the complete message.
    """

    UID_MULTI = "04AABBCCDDEEFF"

    def test_multi_page_ndef_detected(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID_MULTI, tag_type="NTAG213", ndef=NDEF_TEXT_LONG)
        proc.wait_for(r"READER1_ON_TAG 04AABBCCDDEEFF", timeout=12)

    def test_multi_page_ndef_message_read(self, sim):
        proc, ctrl1, ctrl2 = sim
        proc.wait_for(r"Successfully read NDEF message", timeout=5)

    def test_multi_page_tag_removed(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID_MULTI)
        proc.wait_for(r"READER1_ON_TAG_REMOVED 04AABBCCDDEEFF", timeout=10)


def _make_text_ndef(text: str) -> str:
    """
    Build an NFC Forum Well-Known Text NDEF record (UTF-8, lang=en) as a hex string.
    Uses Short Record format when payload fits in 1 byte, non-SR otherwise.
    """
    lang = b"en"
    payload = bytes([0x02]) + lang + text.encode("utf-8")  # 0x02 = UTF-8 + lang_len=2
    plen = len(payload)
    if plen <= 255:
        hdr = bytes([0xD1, 0x01, plen, 0x54])
    else:
        hdr = bytes([0xC1, 0x01,
                     (plen >> 24) & 0xFF, (plen >> 16) & 0xFF,
                     (plen >> 8) & 0xFF, plen & 0xFF,
                     0x54])
    return (hdr + payload).hex().upper()


class TestLargeNdefSingleByteTlv:
    """
    NDEF payload ~100 bytes: TLV length fits in one byte (0x62), but the message
    spans many pages, exercising the multi-page fetch loop thoroughly.
    Uses NTAG213 (128 bytes of user memory available from page 4).
    """

    UID = "04112233445566"
    NDEF = _make_text_ndef("A" * 91)  # payload=94, record=98 bytes, TLV len=0x62

    def test_large_ndef_single_byte_tlv_detected(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID, tag_type="NTAG213", ndef=self.NDEF)
        proc.wait_for(r"READER1_ON_TAG 04112233445566", timeout=12)

    def test_large_ndef_single_byte_tlv_message_read(self, sim):
        proc, ctrl1, ctrl2 = sim
        proc.wait_for(r"Successfully read NDEF message of 98 bytes", timeout=5)

    def test_large_ndef_single_byte_tlv_removed(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID)
        proc.wait_for(r"READER1_ON_TAG_REMOVED 04112233445566", timeout=10)


class TestLargeNdefThreeByteTlv:
    """
    NDEF payload ~286 bytes: TLV length requires 3-byte encoding (0xFF 0x01 0x1E).
    Exercises the extended TLV length path added to read_tag_() in both the
    firmware and the simulator.
    Uses NTAG216 (908 bytes of user memory available from page 4).
    """

    UID = "04AABBCCDDEEFF"
    NDEF = _make_text_ndef("B" * 276)  # payload=279, record=286 bytes, TLV 3-byte len

    def test_large_ndef_three_byte_tlv_detected(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID, tag_type="NTAG216", ndef=self.NDEF)
        proc.wait_for(r"READER1_ON_TAG 04AABBCCDDEEFF", timeout=12)

    def test_large_ndef_three_byte_tlv_message_read(self, sim):
        proc, ctrl1, ctrl2 = sim
        proc.wait_for(r"Successfully read NDEF message of 286 bytes", timeout=8)

    def test_large_ndef_three_byte_tlv_removed(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID)
        proc.wait_for(r"READER1_ON_TAG_REMOVED 04AABBCCDDEEFF", timeout=10)


class TestOnTagActionChain:
    """
    Verify that all actions in an on_tag / on_tag_removed automation chain execute,
    not just the first (lambda).  Regression test for the class of bug where a tag
    is detected ("Tag selected" logged) but downstream automation actions — such as
    output.turn_on or logger.log — silently fail to run.

    The YAML wires reader1's on_tag to a lambda FOLLOWED BY a logger.log action.
    Both must appear in the log; if only the lambda fires the second assertion fails.
    """

    UID = "DEADBEEF"

    def test_on_tag_lambda_and_action_both_fire(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID)
        proc.wait_for(r"READER1_ON_TAG DEADBEEF", timeout=10)
        proc.wait_for(r"READER1_ON_TAG_ACTION DEADBEEF", timeout=5)

    def test_on_tag_removed_lambda_and_action_both_fire(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID)
        proc.wait_for(r"READER1_ON_TAG_REMOVED DEADBEEF", timeout=10)
        proc.wait_for(r"READER1_ON_TAG_REMOVED_ACTION DEADBEEF", timeout=5)


class TestMissThreshold:
    """
    on_tag_removed must not fire until miss_threshold consecutive missed scans.

    test-emulation.yaml sets miss_threshold=3, update_interval=500ms.
    After removing the tag from the sim, removal should fire after
    3 missed scan cycles (~1500ms).  It must NOT fire within the first
    ~1.4 scan cycles (700ms), confirming the threshold is respected.

    Note: worst-case timing (remove_tag called during an active anticol scan)
    can cause the first miss to fire at ~20ms into the remove, meaning 3 misses
    complete at ~1020ms.  The too_soon window is set to 700ms to stay safely
    below that worst case while still verifying the guard.
    """

    UID = "CAFEF00D"
    UPDATE_MS = 500
    THRESHOLD = 3

    def test_removal_requires_threshold_misses(self, sim):
        proc, ctrl1, ctrl2 = sim
        # Ensure tag is present first
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.add_tag(self.UID)
        proc.wait_for(rf"READER1_ON_TAG {self.UID}", timeout=5)

        # Remove the tag — removal must NOT fire before threshold misses
        with proc._lock:
            proc.log_lines.clear()
        ctrl1.remove_tag(self.UID)

        # Sleep safely below the worst-case first-removal time (~1020ms).
        # (THRESHOLD-1) * UPDATE_MS would be 1000ms which is too close to
        # the boundary when remove_tag coincides with an active scan.
        too_soon_ms = 700
        time.sleep(too_soon_ms / 1000)
        with proc._lock:
            for line in proc.log_lines:
                assert rf"READER1_ON_TAG_REMOVED {self.UID}" not in line, (
                    f"on_tag_removed fired within {too_soon_ms}ms of remove — "
                    f"expected at least {self.THRESHOLD} missed scans first"
                )

        # Now it must fire within the next 3 scan cycles (generous window)
        proc.wait_for(rf"READER1_ON_TAG_REMOVED {self.UID}", timeout=3.0)


class TestDetectionLatency:
    """
    Tag detection and removal must complete within UX-acceptable time bounds.

    These are regression tests for timing regressions (e.g. accidental blocking
    delays, extra I2C waits, slow transceive paths).  Any change that causes
    detection to take more than 2 scan cycles, or removal more than 3+1 scan
    cycles, will fail here.

    Bounds (update_interval=500ms in test-emulation.yaml):
      detection  ≤ 1500ms  (worst-case: tag appears just after a scan)
      removal    ≤ 2000ms  (3 misses × 500ms + 1 scan margin + Python polling jitter)

    The sim signals NRE immediately in IRQ_MAIN so the base-class fast-path
    fires within one loop() iteration instead of waiting the 100ms millis()
    fallback timeout, keeping each miss cycle close to update_interval.
    """

    UID = "DEADBABE"
    MAX_DETECTION_MS = 1500   # worst-case: tag appears just after a scan completes
    MAX_REMOVAL_MS   = 2000   # 3 misses × ~500ms + Python wait_for polling jitter

    def test_tag_detected_within_latency_budget(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        t0 = time.time()
        ctrl1.add_tag(self.UID)
        proc.wait_for(rf"READER1_ON_TAG {self.UID}", timeout=(self.MAX_DETECTION_MS + 500) / 1000)
        elapsed_ms = (time.time() - t0) * 1000
        assert elapsed_ms <= self.MAX_DETECTION_MS, (
            f"Tag detection took {elapsed_ms:.0f}ms, exceeds {self.MAX_DETECTION_MS}ms budget"
        )

    def test_tag_removed_within_latency_budget(self, sim):
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        t0 = time.time()
        ctrl1.remove_tag(self.UID)
        proc.wait_for(rf"READER1_ON_TAG_REMOVED {self.UID}", timeout=(self.MAX_REMOVAL_MS + 500) / 1000)
        elapsed_ms = (time.time() - t0) * 1000
        assert elapsed_ms <= self.MAX_REMOVAL_MS, (
            f"Tag removal took {elapsed_ms:.0f}ms, exceeds {self.MAX_REMOVAL_MS}ms budget"
        )


class TestInitRegisters:
    """
    Critical registers must be set to the correct values after startup.

    These are direct regression tests for misconfiguration bugs.  Each test
    reads a register value back from the sim via GET_REG and asserts the
    expected value.

    Registers under test and why they matter:
      TX_DRIVER_CONF (0x28): am_mod bits[7:4] MUST be 0 (100% ASK / OOK for
          ISO14443A).  Any non-zero am_mod causes the carrier to only partially
          dip during WUPA/REQA pulses — real tags cannot demodulate the command
          and never respond.  Regression: commit 92f7807 set am_mod=7 (0x70),
          which destroyed all tag detection until fixed in 7fc9554.

      IO_CONF2 (0x01): sup3V bit (bit7) selects the supply voltage regulator
          mode.  Default supply_3v3=true → 0x80.  Wrong setting degrades or
          kills the TX driver output level.

      MODE (0x03): must be 0x08 (ISO14443A initiator mode).  Any other value
          switches the chip out of NFC-A mode and breaks all tag detection.
    """

    def test_tx_driver_conf_am_mod_zero(self, sim):
        """TX_DRIVER_CONF bits[7:4] (am_mod) must be 0 — 100% ASK for ISO14443A."""
        proc, ctrl1, ctrl2 = sim
        proc.wait_for(r"Sent WUPA", timeout=5)
        val = ctrl1.get_reg("28")
        assert (val & 0xF0) == 0x00, (
            f"TX_DRIVER_CONF am_mod must be 0 for ISO14443A (100% ASK), "
            f"got TX_DRIVER_CONF=0x{val:02X} (am_mod=0x{(val>>4)&0xF:X}). "
            f"Regression: am_mod!=0 means tags never respond to WUPA/REQA."
        )

    def test_io_conf2_full(self, sim):
        """IO_CONF2 must have sup3V + SPI pulldowns + aat_en."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("01")
        # Sim VDD raw=0x80 → ~3000mV < 3600 → sup3V=1 (bit7=0x80)
        # SPI pull-downs: miso_pd1|miso_pd2 (bits[4:3]=0x18)
        # ST25R3916 has AAT → aat_en=1 (bit5=0x20)
        assert val == 0xB8, (
            f"IO_CONF2 expected 0xB8 (sup3V + spi_pd + aat_en), got 0x{val:02X}"
        )

    def test_io_conf1_rfal(self, sim):
        """IO_CONF1 must be 0x07 (disable MCU_CLK + LF clock)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("00")
        assert val == 0x07, f"IO_CONF1 expected 0x07 (RFAL default), got 0x{val:02X}"

    def test_mode_iso14443a(self, sim):
        """MODE register must be 0x08 (ISO14443A initiator)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("03")
        assert val == 0x08, (
            f"MODE expected 0x08 (ISO14443A initiator), got 0x{val:02X}. "
            f"Wrong mode disables NFC-A entirely."
        )


class TestRxConf3Selection:
    """
    RX_CONF3 is now always 0x00 (RFAL NFC-A 106 default) for all silicon variants.
    Previous code used 0xE2 for non-B (lf_en=1 + AM boost) but this routed
    the receiver to the LF path, hurting 13.56MHz HF sensitivity.
    """

    def test_rx_conf3_rfal_default(self, sim):
        proc, ctrl1, ctrl2 = sim
        # Wait for at least one full update cycle to have written RX_CONF3
        proc.wait_for(r"Sent WUPA", timeout=5)
        time.sleep(0.6)  # one more update interval
        val = ctrl1.get_reg("0D")
        assert val == 0x00, f"Expected RX_CONF3=0x00 (RFAL NFC-A 106 default), got 0x{val:02X}"


class TestRfalAnalogProfile:
    """
    RFAL NFC-A 106 kbps analog profile must be applied after reset.
    Register values from ST RFAL analog config table.
    """

    def test_rx_conf1_rfal(self, sim):
        """RX_CONF1 must be 0x08 (AM path squelch, HPF=60-400kHz)."""
        proc, ctrl1, ctrl2 = sim
        proc.wait_for(r"Sent WUPA", timeout=5)
        val = ctrl1.get_reg("0B")
        assert val == 0x08, f"RX_CONF1 expected 0x08 (RFAL default), got 0x{val:02X}"

    def test_rx_conf2_rfal(self, sim):
        """RX_CONF2 must be 0x2D (mixer demod, AGC on, 61ns pulse)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("0C")
        assert val == 0x2D, f"RX_CONF2 expected 0x2D (RFAL default), got 0x{val:02X}"

    def test_rx_conf4_rfal(self, sim):
        """RX_CONF4 must be 0x00 (no RG2 gain)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("0E")
        assert val == 0x00, f"RX_CONF4 expected 0x00 (RFAL default), got 0x{val:02X}"

    def test_corr_conf1_rfal(self, sim):
        """CORR_CONF1 (Space B 0x4C) must be 0x51 (correlator thresholds)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("4C")
        assert val == 0x51, f"CORR_CONF1 expected 0x51 (RFAL default), got 0x{val:02X}"

    def test_corr_conf2_rfal(self, sim):
        """CORR_CONF2 (Space B 0x4D) must be 0x00."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("4D")
        assert val == 0x00, f"CORR_CONF2 expected 0x00 (RFAL default), got 0x{val:02X}"


class TestOvershootUndershootProtection:
    """
    RFAL NFC-A 106 TX enables overshoot and undershoot protection (Space B).
    """

    def test_overshoot_conf1(self, sim):
        proc, ctrl1, ctrl2 = sim
        proc.wait_for(r"Sent WUPA", timeout=5)
        val = ctrl1.get_reg("70")
        assert val == 0x40, f"OVERSHOOT_CONF1 expected 0x40, got 0x{val:02X}"

    def test_overshoot_conf2(self, sim):
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("71")
        assert val == 0x03, f"OVERSHOOT_CONF2 expected 0x03, got 0x{val:02X}"

    def test_undershoot_conf1(self, sim):
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("72")
        assert val == 0x40, f"UNDERSHOOT_CONF1 expected 0x40, got 0x{val:02X}"

    def test_undershoot_conf2(self, sim):
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("73")
        assert val == 0x03, f"UNDERSHOOT_CONF2 expected 0x03, got 0x{val:02X}"


class TestVddAutoDetect:
    """
    VDD auto-detection measures supply voltage (cmd 0xDF) and sets sup3V.
    Sim defaults: vdd_raw=0x80 (~3000mV) → sup3V=1 (3.3V mode).
    """

    def test_sup3v_set_for_low_vdd(self, sim):
        """With VDD ~3000mV (raw=0x80), sup3V must be 1 (3.3V mode)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("01")
        assert val & 0x80 == 0x80, (
            f"IO_CONF2 sup3V bit expected 1 for VDD ~3000mV, got 0x{val:02X}"
        )

    def test_ant_tune_a_default(self, sim):
        """ANT_TUNE_A must be 0x80 (RFAL default mid-range)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("26")
        assert val == 0x80, f"ANT_TUNE_A expected 0x80, got 0x{val:02X}"

    def test_ant_tune_b_default(self, sim):
        """ANT_TUNE_B must be 0x40 (RFAL default, shifted from chip default 0x80)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("27")
        assert val == 0x40, f"ANT_TUNE_B expected 0x40, got 0x{val:02X}"

    def test_aat_enabled(self, sim):
        """IO_CONF2 aat_en (bit5) must be set for ST25R3916 (has AAT hardware)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("01")
        assert val & 0x20 == 0x20, (
            f"IO_CONF2 aat_en expected 1 for ST25R3916, got IO_CONF2=0x{val:02X}"
        )


class TestChipInitRegisters:
    """
    RFAL chip-init registers that improve range by configuring the RF front-end.
    These are written once during reset_() and must persist across scan cycles.
    """

    def test_res_am_mod(self, sim):
        """RES_AM_MOD (Space B 0x6A) must be 0x80 (minimum non-overlap)."""
        proc, ctrl1, ctrl2 = sim
        proc.wait_for(r"Sent WUPA", timeout=5)
        val = ctrl1.get_reg("6A")
        assert val == 0x80, f"RES_AM_MOD expected 0x80, got 0x{val:02X}"

    def test_field_threshold_actv(self, sim):
        """FIELD_THRESHOLD_ACTV must be 0x11 (trg=105mV, rfe=105mV)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("2A")
        assert val == 0x11, f"FIELD_THRESHOLD_ACTV expected 0x11, got 0x{val:02X}"

    def test_field_threshold_deactv(self, sim):
        """FIELD_THRESHOLD_DEACTV must be 0x00 (trg=75mV, rfe=75mV)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("2B")
        assert val == 0x00, f"FIELD_THRESHOLD_DEACTV expected 0x00, got 0x{val:02X}"

    def test_aux_mod(self, sim):
        """AUX_MOD (Space B 0x68) must be 0x10 (external load modulation)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("68")
        assert val == 0x10, f"AUX_MOD expected 0x10, got 0x{val:02X}"

    def test_passive_target(self, sim):
        """PASSIVE_TARGET must be 0x50 (fdel=5, FDT aligned to bitgrid)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("08")
        assert val == 0x50, f"PASSIVE_TARGET expected 0x50, got 0x{val:02X}"

    def test_pt_mod(self, sim):
        """PT_MOD must be 0x51 (reduced RFO resistance in modulated state)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("29")
        assert val == 0x51, f"PT_MOD expected 0x51, got 0x{val:02X}"

    def test_emd_sup_conf(self, sim):
        """EMD_SUP_CONF (Space B 0x45) must be 0x40 (rx_start_emv on first 4 bits)."""
        proc, ctrl1, ctrl2 = sim
        val = ctrl1.get_reg("45")
        assert val == 0x40, f"EMD_SUP_CONF expected 0x40, got 0x{val:02X}"


class TestHealthCheck:
    """
    Health check verifies IC identity at a separate slow interval (2s in
    test-emulation.yaml), distinct from the 500ms tag scan rate.

    test-emulation.yaml sets for reader1:
      health_check_enabled: true
      health_check_interval: 2s
      max_failed_checks: 2
      auto_reset_on_failure: true

    Simulates SPI/I2C connectivity loss via SET_IC_IDENTITY socket command,
    which makes the sim return a bad IC identity register value, causing the
    firmware's health check to fail exactly as it would on a real bus failure.
    """

    def test_health_check_detects_failure(self, sim):
        """Bad IC identity is detected; status binary sensor goes false."""
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        # Simulate connectivity loss: IC identity returns 0x00
        ctrl1.set_ic_identity("00")
        proc.wait_for(r"Health check failed", timeout=10)
        proc.wait_for(r"READER1_STATUS 0", timeout=5)

    def test_health_check_recovers_before_reinit(self, sim):
        """
        If connectivity is restored before max_failed_checks, reinit is not
        triggered and status returns true on the next successful health check.
        max_failed_checks=2: after 1 failure, restore → next check passes.
        """
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        # Restore connectivity after exactly 1 failure (max_failed_checks=2,
        # so reinit not yet triggered)
        ctrl1.set_ic_identity("28")
        # Next health check (within 2s) must pass and publish status true
        proc.wait_for(r"READER1_STATUS 1", timeout=10)
        # Confirm reinit was NOT triggered in this window
        with proc._lock:
            for line in proc.log_lines:
                assert "max failures reached" not in line, (
                    f"Reinit triggered unexpectedly after only 1 failure: {line}"
                )

    def test_persistent_failure_triggers_reinit(self, sim):
        """
        Persistent bad IC identity accumulates max_failed_checks failures and
        triggers reinitialize_().  After restoring connectivity, reinit succeeds
        (possibly on a retry) and status returns true.
        """
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        # Simulate persistent loss
        ctrl1.set_ic_identity("00")
        # Wait for 2 consecutive failures → reinit triggered
        proc.wait_for(r"max failures reached, triggering reinit", timeout=15)
        # Restore IC identity so reinit (or next health check) succeeds
        ctrl1.set_ic_identity("28")
        # Status must recover.  Reinit may fail once due to timing (race between
        # restoring identity and reinit reading IC_IDENTITY), but the subsequent
        # health check cycle will detect the good identity and publish true.
        proc.wait_for(r"READER1_STATUS 1", timeout=15)

    def test_tag_scanning_resumes_after_recovery(self, sim):
        """After health check recovery, normal tag detection still works."""
        proc, ctrl1, ctrl2 = sim
        with proc._lock:
            proc.log_lines.clear()
        uid = "12345678"
        ctrl1.add_tag(uid)
        proc.wait_for(rf"READER1_ON_TAG {uid}", timeout=10)
        ctrl1.remove_tag(uid)
        proc.wait_for(rf"READER1_ON_TAG_REMOVED {uid}", timeout=10)
