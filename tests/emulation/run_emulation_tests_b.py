"""
B-version (ST25R3916B, IC identity 0x30) emulation tests.

Verifies that ic_identity=0x30 → is_b_version_=true → RX_CONF3=0x00,
and that tag detection / Mifare auth still work correctly.

Run:
    pytest tests/emulation/run_emulation_tests_b.py -v

Requires:
    esphome compile tests/emulation/test-emulation-b.yaml
"""

import os
import re
import socket
import subprocess
import threading
import time

import pytest


YAML_PATH_B = "tests/emulation/test-emulation-b.yaml"
SOCKET3 = "/tmp/st25r_sim3.sock"
SOCKET4 = "/tmp/st25r_sim4.sock"


def find_binary_b():
    env_bin = os.environ.get("ST25R_SIM_B_BINARY", "")
    if env_bin and os.path.isfile(env_bin):
        return env_bin

    build_roots = [
        ".esphome/build/st25r-sim-b",
        "tests/emulation/.esphome/build/st25r-sim-b",
    ]
    suffixes = [
        ".pioenvs/st25r-sim-b/program",
        ".pioenvs/host/program",
        ".pio/build/host/program",
        "firmware",
    ]
    for br in build_roots:
        for s in suffixes:
            p = os.path.join(br, s)
            if os.path.isfile(p):
                return p
        if os.path.isdir(br):
            for root, dirs, files in os.walk(br):
                for f in files:
                    full = os.path.join(root, f)
                    if os.access(full, os.X_OK) and not f.endswith(".elf"):
                        return full
    return None


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
            print(f"[FW-B] {line}", flush=True)

    def wait_for(self, pattern, timeout=15):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                for line in self.log_lines:
                    if re.search(pattern, line):
                        return line
            time.sleep(0.1)
        raise TimeoutError(f"Pattern {pattern!r} not seen within {timeout}s")

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


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

    def add_tag(self, uid_hex, tag_type=None, key_a=None, key_b=None, ndef=None):
        parts = [f"ADD_TAG {uid_hex}"]
        if tag_type: parts.append(f"TYPE={tag_type}")
        if key_a:    parts.append(f"KEY_A={key_a}")
        if key_b:    parts.append(f"KEY_B={key_b}")
        if ndef:     parts.append(f"NDEF={ndef}")
        resp = self._send(" ".join(parts))
        assert "OK" in resp, f"add_tag failed: {resp}"

    def remove_tag(self, uid_hex):
        resp = self._send(f"REMOVE_TAG {uid_hex}")
        assert "OK" in resp, f"remove_tag failed: {resp}"

    def get_reg(self, addr_hex):
        resp = self._send(f"GET_REG {addr_hex}")
        return int(resp.strip(), 16)

    def set_ic_identity(self, val_hex):
        resp = self._send(f"SET_IC_IDENTITY {val_hex}")
        assert "OK" in resp, f"set_ic_identity failed: {resp}"


@pytest.fixture(scope="module")
def sim_b():
    binary = find_binary_b()
    if binary is None:
        pytest.skip("B-version host binary not found — run: esphome compile " + YAML_PATH_B)

    proc = SimProcess(binary)
    proc.start()

    ctrl3 = SimController(SOCKET3)
    ctrl4 = SimController(SOCKET4)
    try:
        ctrl3.wait_ready(timeout=30)
        ctrl4.wait_ready(timeout=30)
    except TimeoutError as e:
        proc.stop()
        pytest.fail(str(e))

    yield proc, ctrl3, ctrl4

    proc.stop()


UID4 = "DEA30D00"
UID7 = "041AA7675F6180"


class TestBVersionInit:
    """B-version chip identity (0x30) is accepted — no mismatch error, status fires."""

    def test_no_identity_mismatch(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        proc.wait_for(r"ST25R3916B", timeout=10)

    def test_status_sensor_true(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            for line in proc.log_lines:
                if re.search(r"READER1_B_STATUS 1", line):
                    return
        proc.wait_for(r"READER1_B_STATUS 1", timeout=10)


class TestBVersionRxConf3:
    """
    B-version sim (IC=0x30) must write RX_CONF3=0x00, not 0xE2.
    0xE2 sets lf_en=1 which routes receiver to LF path, killing HF NFC reception.
    """

    def test_rx_conf3_is_zero(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        proc.wait_for(r"Sent WUPA", timeout=5)
        time.sleep(0.6)
        val = ctrl3.get_reg("0D")
        assert val == 0x00, (
            f"Expected RX_CONF3=0x00 for B-version sim (IC=0x30), got 0x{val:02X}. "
            "lf_en=1 (0xE2) routes receiver to LF path and kills NFC tag detection."
        )


class TestBVersionTagDetection:
    """Tag detection works end-to-end with B-version identity."""

    def test_4byte_tag_detected(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.add_tag(UID4)
        line = proc.wait_for(r"READER1_B_ON_TAG DEA30D00", timeout=10)
        assert "DEA30D00" in line

    def test_4byte_tag_removed(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.remove_tag(UID4)
        proc.wait_for(r"READER1_B_ON_TAG_REMOVED DEA30D00", timeout=10)

    def test_7byte_tag_detected(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.add_tag(UID7, tag_type="NTAG213")
        proc.wait_for(r"READER1_B_ON_TAG 041AA7675F6180", timeout=10)

    def test_7byte_tag_removed(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.remove_tag(UID7)
        proc.wait_for(r"READER1_B_ON_TAG_REMOVED 041AA7675F6180", timeout=10)


class TestBVersionMifareAuth:
    """Mifare Classic Crypto1 auth works correctly on B-version sim path."""

    UID_MFC = "CAFEBABE"

    def test_mifare_auth_succeeds(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.add_tag(self.UID_MFC, tag_type="MIFARE_1K")
        proc.wait_for(r"READER1_B_ON_TAG CAFEBABE", timeout=12)
        proc.wait_for(r"Mifare auth OK", timeout=5)

    def test_mifare_tag_removed(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.remove_tag(self.UID_MFC)
        proc.wait_for(r"READER1_B_ON_TAG_REMOVED CAFEBABE", timeout=10)


class TestBVersionNfcv:
    """NFC-V tag detection works on B-version (IC=0x30) via streaming mode."""

    NFCV_UID = "E00208024FEFE7E1"

    def test_nfcv_detected_and_trigger(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.add_tag(self.NFCV_UID, tag_type="ISO15693")
        proc.wait_for(rf"NFC-V tag: {self.NFCV_UID}", timeout=10)
        proc.wait_for(rf"READER1_B_ON_TAG {self.NFCV_UID}", timeout=5)

    def test_nfcv_removed(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.remove_tag(self.NFCV_UID)
        proc.wait_for(rf"READER1_B_ON_TAG_REMOVED {self.NFCV_UID}", timeout=10)


class TestBVersionRfalAnalogProfile:
    """
    B-version must also get the full RFAL NFC-A 106 analog profile.
    Same register values as non-B — RFAL doesn't differentiate.
    """

    def test_rx_conf1_rfal(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        proc.wait_for(r"Sent WUPA", timeout=5)
        val = ctrl3.get_reg("0B")
        assert val == 0x08, f"RX_CONF1 expected 0x08 (RFAL), got 0x{val:02X}"

    def test_rx_conf2_rfal(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("0C")
        assert val == 0x2D, f"RX_CONF2 expected 0x2D (RFAL), got 0x{val:02X}"

    def test_corr_conf1_rfal(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("4C")
        assert val == 0x51, f"CORR_CONF1 expected 0x51 (RFAL), got 0x{val:02X}"

    def test_overshoot_conf1(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("70")
        assert val == 0x40, f"OVERSHOOT_CONF1 expected 0x40, got 0x{val:02X}"

    def test_undershoot_conf1(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("72")
        assert val == 0x40, f"UNDERSHOOT_CONF1 expected 0x40, got 0x{val:02X}"

    def test_aat_enabled(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("01")
        assert val & 0x20 == 0x20, (
            f"IO_CONF2 aat_en expected 1 for ST25R3916B, got IO_CONF2=0x{val:02X}"
        )

    def test_vdd_auto_detect(self, sim_b):
        """B-version must also auto-detect VDD — verify via sup3V register bit."""
        proc, ctrl3, ctrl4 = sim_b
        proc.wait_for(r"Sent WUPA", timeout=10)
        val = ctrl3.get_reg("01")
        assert val & 0x80 == 0x80, (
            f"IO_CONF2 sup3V expected 1 for VDD ~3000mV on B-version, got 0x{val:02X}"
        )


class TestBVersionChipInitRegisters:
    """B-version must also get the full RFAL chip-init registers."""

    def test_io_conf1(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        proc.wait_for(r"Sent WUPA", timeout=5)
        val = ctrl3.get_reg("00")
        assert val == 0x07, f"IO_CONF1 expected 0x07, got 0x{val:02X}"

    def test_io_conf2_spi_pulldowns(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("01")
        assert val & 0x18 == 0x18, (
            f"IO_CONF2 SPI pulldowns (bits[4:3]) expected set, got 0x{val:02X}"
        )

    def test_pt_mod(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("29")
        assert val == 0x51, f"PT_MOD expected 0x51, got 0x{val:02X}"

    def test_emd_sup_conf(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("45")
        assert val == 0x40, f"EMD_SUP_CONF expected 0x40, got 0x{val:02X}"

    def test_aux_mod(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("68")
        assert val == 0x10, f"AUX_MOD expected 0x10, got 0x{val:02X}"

    def test_res_am_mod(self, sim_b):
        proc, ctrl3, ctrl4 = sim_b
        val = ctrl3.get_reg("6A")
        assert val == 0x80, f"RES_AM_MOD expected 0x80, got 0x{val:02X}"


class TestBVersionHealthCheck:
    """
    Health check on the B-version (IC identity 0x30) honours all four config options
    (health_check_enabled, health_check_interval=2s, max_failed_checks=2,
    auto_reset_on_failure=true) set in test-emulation-b.yaml.

    Uses SET_IC_IDENTITY to simulate connectivity loss and recovery, same as the
    main suite's TestHealthCheck.
    """

    def test_health_check_detects_failure(self, sim_b):
        """Bad IC identity is detected on B-version; status goes false."""
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.set_ic_identity("00")
        proc.wait_for(r"Health check failed", timeout=10)
        proc.wait_for(r"READER1_B_STATUS 0", timeout=5)

    def test_persistent_failure_triggers_reinit(self, sim_b):
        """After max_failed_checks (2) consecutive failures, reinit is triggered and
        status recovers once IC identity is restored."""
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        ctrl3.set_ic_identity("00")
        proc.wait_for(r"max failures reached, triggering reinit", timeout=15)
        ctrl3.set_ic_identity("30")
        proc.wait_for(r"READER1_B_STATUS 1", timeout=15)

    def test_tag_scanning_resumes_after_recovery(self, sim_b):
        """Tag detection works normally after health check recovery."""
        proc, ctrl3, ctrl4 = sim_b
        with proc._lock:
            proc.log_lines.clear()
        uid = "AABBCCDD"
        ctrl3.add_tag(uid)
        proc.wait_for(rf"READER1_B_ON_TAG {uid}", timeout=10)
        ctrl3.remove_tag(uid)
        proc.wait_for(rf"READER1_B_ON_TAG_REMOVED {uid}", timeout=10)
