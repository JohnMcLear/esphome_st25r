"""
Shared helpers for ST25R emulation tests.

Provides SimProcess (manages the ESPHome host binary) and SimController
(controls a simulated reader via Unix socket).
"""

import os
import re
import socket
import subprocess
import threading
import time


class SimProcess:
    def __init__(self, binary):
        self.binary = binary
        self.proc = None
        self.log_lines = []
        self._lock = threading.Lock()

    def start(self):
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
                        return False
            time.sleep(0.1)
        return True

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

    def add_tag(self, uid_hex, tag_type=None, key_a=None, key_b=None,
                ndef=None):
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

    def set_nre_mode(self, mode: str):
        """Control NRE signalling: 'hw' (IRQ_TIMER only) or 'sim' (both, default)."""
        resp = self._send(f"SET_NRE_MODE {mode}")
        assert "OK" in resp, f"set_nre_mode failed: {resp}"

    def get_pending_timer(self) -> int:
        """Return pending_irq_timer_ without clearing (for test verification)."""
        resp = self._send("GET_PENDING_TIMER")
        return int(resp.strip(), 16)
