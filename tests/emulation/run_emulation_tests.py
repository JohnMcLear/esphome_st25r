"""
Emulation tests for the ST25R component.

Launches the compiled ESPHome host binary, controls the simulated NFC field
via a Unix socket, and verifies that on_tag / on_tag_removed events appear
in the firmware's log output.

Run:
    pytest tests/emulation/run_emulation_tests.py -v

Requires:
    - `esphome compile tests/emulation/test-emulation.yaml` to have succeeded.
    - The compiled binary at the path returned by find_binary().
"""

import glob
import os
import re
import socket
import subprocess
import sys
import threading
import time

import pytest


# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

SOCKET_PATH = "/tmp/st25r_sim.sock"
YAML_PATH = "tests/emulation/test-emulation.yaml"

# Locate the compiled host binary (ESPHome puts it under .esphome/build/).
def find_binary():
    patterns = [
        ".esphome/build/st25r-sim/.pioenvs/host/program",
        ".esphome/build/st25r-sim/.pio/build/host/program",
        ".esphome/build/st25r-sim/firmware",
    ]
    for p in patterns:
        if os.path.isfile(p):
            return p
    # Fallback: search recursively for an executable named "program" or "firmware"
    for root, dirs, files in os.walk(".esphome/build/st25r-sim"):
        for f in files:
            full = os.path.join(root, f)
            if os.access(full, os.X_OK) and not f.endswith(".elf"):
                return full
    return None


class SimProcess:
    """Manages the ESPHome host binary and collects its stdout."""

    def __init__(self, binary):
        self.binary = binary
        self.proc = None
        self.log_lines = []
        self._lock = threading.Lock()
        self._reader = None

    def start(self):
        self.proc = subprocess.Popen(
            [self.binary],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self._reader = threading.Thread(target=self._read_output, daemon=True)
        self._reader.start()

    def _read_output(self):
        for line in self.proc.stdout:
            line = line.rstrip()
            with self._lock:
                self.log_lines.append(line)
            print(f"[FW] {line}", flush=True)

    def wait_for(self, pattern, timeout=15):
        """Block until a log line matches `pattern` or `timeout` seconds pass."""
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
    """Sends commands to the simulator socket."""

    def __init__(self, path=SOCKET_PATH):
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
        raise TimeoutError("Simulator socket did not appear")

    def add_tag(self, uid_hex):
        resp = self._send(f"ADD_TAG {uid_hex}")
        assert "OK" in resp, f"add_tag failed: {resp}"

    def remove_tag(self, uid_hex):
        resp = self._send(f"REMOVE_TAG {uid_hex}")
        assert "OK" in resp, f"remove_tag failed: {resp}"

    def list_tags(self):
        return self._send("LIST")


# ─────────────────────────────────────────────────────────────────────────────
# Fixtures
# ─────────────────────────────────────────────────────────────────────────────

@pytest.fixture(scope="module")
def sim():
    binary = find_binary()
    if binary is None:
        pytest.skip("Compiled host binary not found — run: esphome compile " + YAML_PATH)

    proc = SimProcess(binary)
    proc.start()

    ctrl = SimController()
    try:
        ctrl.wait_ready(timeout=30)
    except TimeoutError:
        proc.stop()
        pytest.fail("Simulator socket never appeared — is the binary working?")

    yield proc, ctrl

    proc.stop()


# ─────────────────────────────────────────────────────────────────────────────
# Test cases
# ─────────────────────────────────────────────────────────────────────────────

# 4-byte UID used throughout: DE A3 0D 00  (Mifare Classic clone UID)
UID4 = "DEA30D00"
# 7-byte UID: 04 1A A7 67 5F 61 80  (NFC ring)
UID7 = "041AA7675F6180"


class TestBasicTagDetection:
    """Verify on_tag fires when a virtual tag is added."""

    def test_no_tags_at_startup(self, sim):
        proc, ctrl = sim
        # Wait for at least 3 update cycles (3 s) with no tag present.
        time.sleep(3)
        with proc._lock:
            for line in proc.log_lines:
                assert "SIM_ON_TAG " not in line, f"Unexpected on_tag: {line}"

    def test_4byte_tag_detected(self, sim):
        proc, ctrl = sim
        # Clear log before adding tag
        with proc._lock:
            proc.log_lines.clear()

        ctrl.add_tag(UID4)
        line = proc.wait_for(r"SIM_ON_TAG DEA30D00", timeout=10)
        assert "DEA30D00" in line

    def test_4byte_tag_removed(self, sim):
        proc, ctrl = sim
        # Ensure tag is present first (carry-over from previous test).
        with proc._lock:
            proc.log_lines.clear()

        ctrl.remove_tag(UID4)
        # Removal requires 3 missed scans (update_interval=1s → ~3s + margin).
        line = proc.wait_for(r"SIM_ON_TAG_REMOVED DEA30D00", timeout=10)
        assert "DEA30D00" in line


class TestSevenByteUid:
    """Verify a 7-byte (cascade) UID is correctly detected and removed."""

    def test_7byte_tag_detected(self, sim):
        proc, ctrl = sim
        with proc._lock:
            proc.log_lines.clear()

        ctrl.add_tag(UID7)
        # ESPHome renders 7-byte UIDs in upper hex without separators.
        line = proc.wait_for(r"SIM_ON_TAG 041AA7675F6180", timeout=10)
        assert "041AA7675F6180" in line

    def test_7byte_tag_removed(self, sim):
        proc, ctrl = sim
        with proc._lock:
            proc.log_lines.clear()

        ctrl.remove_tag(UID7)
        line = proc.wait_for(r"SIM_ON_TAG_REMOVED 041AA7675F6180", timeout=10)
        assert "041AA7675F6180" in line


class TestMultiTagDetection:
    """Verify both tags are detected when two are in the field simultaneously."""

    UID_A = "AABBCCDD"
    UID_B = "11223344"

    def test_two_tags_both_detected(self, sim):
        proc, ctrl = sim
        with proc._lock:
            proc.log_lines.clear()

        ctrl.add_tag(self.UID_A)
        ctrl.add_tag(self.UID_B)

        proc.wait_for(r"SIM_ON_TAG AABBCCDD", timeout=15)
        proc.wait_for(r"SIM_ON_TAG 11223344", timeout=15)

    def test_two_tags_both_removed(self, sim):
        proc, ctrl = sim
        with proc._lock:
            proc.log_lines.clear()

        ctrl.remove_tag(self.UID_A)
        ctrl.remove_tag(self.UID_B)

        proc.wait_for(r"SIM_ON_TAG_REMOVED AABBCCDD", timeout=15)
        proc.wait_for(r"SIM_ON_TAG_REMOVED 11223344", timeout=15)


class TestNoSpuriousRetrigger:
    """A present tag must not fire on_tag a second time (flap guard)."""

    def test_no_duplicate_on_tag(self, sim):
        proc, ctrl = sim
        with proc._lock:
            proc.log_lines.clear()

        ctrl.add_tag(UID4)
        # Wait 5 s (5 scan cycles) — on_tag should appear exactly once.
        proc.wait_for(r"SIM_ON_TAG DEA30D00", timeout=10)
        time.sleep(5)

        with proc._lock:
            count = sum(1 for l in proc.log_lines if "SIM_ON_TAG DEA30D00" in l
                        and "REMOVED" not in l)
        assert count == 1, f"Expected 1 on_tag, got {count}"

        # Clean up
        ctrl.remove_tag(UID4)
        proc.wait_for(r"SIM_ON_TAG_REMOVED DEA30D00", timeout=10)
