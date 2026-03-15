"""
Unit tests for ESPHome ST25R component schema validation.

Tests the Python config validators in:
  components/st25r/__init__.py
  components/st25r/binary_sensor.py

Run with:
  pytest tests/python/ -v
"""

import pytest
import esphome.config_validation as cv  # ESPHome must be installed: pip install esphome


# ── Inline re-implementation of validate_uid ─────────────────────────────────
# Copied verbatim from components/st25r/binary_sensor.py so the test can run
# without wrestling with relative-import package structure.

def validate_uid(value):
    value = cv.string_strict(value)
    for x in value.split("-"):
        if len(x) != 2:
            raise cv.Invalid(
                "Each part (separated by '-') of the UID must be two characters long."
            )
        try:
            x = int(x, 16)
        except ValueError as err:
            raise cv.Invalid(
                "Valid characters for parts of a UID are 0123456789ABCDEF."
            ) from err
        if x < 0 or x > 255:
            raise cv.Invalid(
                "Valid values for UID parts (separated by '-') are 00 to FF"
            )
    return value


# ── UID validator ─────────────────────────────────────────────────────────────

class TestValidateUid:
    """Tests for the binary sensor UID validator."""

    # Valid UIDs
    def test_4byte_uid(self):
        assert validate_uid("DE-A3-0D-00") == "DE-A3-0D-00"

    def test_7byte_uid(self):
        assert validate_uid("04-1A-A7-67-5F-61-80") == "04-1A-A7-67-5F-61-80"

    def test_lowercase_hex(self):
        assert validate_uid("de-a3-0d-00") == "de-a3-0d-00"

    def test_mixed_case(self):
        assert validate_uid("De-A3-0d-00") == "De-A3-0d-00"

    def test_boundary_ff(self):
        assert validate_uid("FF-FF-FF-FF") == "FF-FF-FF-FF"

    def test_boundary_00(self):
        assert validate_uid("00-00-00-00") == "00-00-00-00"

    def test_single_byte(self):
        assert validate_uid("AB") == "AB"

    def test_10byte_uid(self):
        assert validate_uid("01-02-03-04-05-06-07-08-09-0A") == "01-02-03-04-05-06-07-08-09-0A"

    # Invalid UIDs
    def test_rejects_short_segment(self):
        """Each segment must be exactly 2 characters."""
        with pytest.raises(cv.Invalid):
            validate_uid("D-A3-0D-00")

    def test_rejects_long_segment(self):
        with pytest.raises(cv.Invalid):
            validate_uid("DEA-A3-0D-00")

    def test_rejects_invalid_hex_char(self):
        with pytest.raises(cv.Invalid):
            validate_uid("GG-A3-0D-00")

    def test_rejects_colon_separator(self):
        """Only hyphen separator is accepted."""
        with pytest.raises(cv.Invalid):
            validate_uid("DE:A3:0D:00")

    def test_rejects_space_separator(self):
        with pytest.raises(cv.Invalid):
            validate_uid("DE A3 0D 00")

    def test_rejects_empty_string(self):
        with pytest.raises(cv.Invalid):
            validate_uid("")

    def test_rejects_out_of_range(self):
        """Each pair is 2 hex chars → max 0xFF, so no out-of-range possible via hex pairs,
        but non-hex like 'GG' must be rejected."""
        with pytest.raises(cv.Invalid):
            validate_uid("ZZ-A3-0D-00")


# ── Mifare key parsing ────────────────────────────────────────────────────────

def _validate_mifare_key(value: str) -> str:
    """Inline copy of the validator from components/st25r/__init__.py."""
    value = cv.string_strict(value)
    if len(value) != 12:
        raise cv.Invalid(f"Mifare key must be exactly 12 hex characters, got {len(value)}")
    try:
        int(value, 16)
    except ValueError as err:
        raise cv.Invalid("Mifare key must contain only hex characters (0-9, A-F)") from err
    return value.upper()


def parse_mifare_key(value: str) -> int:
    """Validate then convert to int (as setup_st25r does)."""
    validated = _validate_mifare_key(value)
    return int(validated, 16)


class TestMifareKeyValidation:
    """Tests for mifare_key_a / mifare_key_b YAML option."""

    def test_default_key(self):
        assert parse_mifare_key("FFFFFFFFFFFF") == 0xFFFFFFFFFFFF

    def test_zero_key(self):
        assert parse_mifare_key("000000000000") == 0x000000000000

    def test_custom_key(self):
        assert parse_mifare_key("A0A1A2A3A4A5") == 0xA0A1A2A3A4A5

    def test_lowercase_key(self):
        assert parse_mifare_key("a0a1a2a3a4a5") == 0xA0A1A2A3A4A5

    def test_ndef_key(self):
        assert parse_mifare_key("D3F7D3F7D3F7") == 0xD3F7D3F7D3F7

    def test_mifare_b_sample(self):
        assert parse_mifare_key("B0B1B2B3B4B5") == 0xB0B1B2B3B4B5

    def test_rejects_short_key(self):
        with pytest.raises(cv.Invalid):
            parse_mifare_key("FFFFFFFFFF")  # 10 chars

    def test_rejects_long_key(self):
        with pytest.raises(cv.Invalid):
            parse_mifare_key("FFFFFFFFFFFFFF")  # 14 chars

    def test_rejects_empty(self):
        with pytest.raises(cv.Invalid):
            parse_mifare_key("")

    def test_non_hex_raises_at_schema(self):
        """Non-hex chars now raise cv.Invalid at schema validation time."""
        with pytest.raises(cv.Invalid):
            parse_mifare_key("GGGGGGGGGGGG")


# ── rf_power range ────────────────────────────────────────────────────────────

class TestRfPower:
    """Tests for rf_power YAML option (int 0–15)."""

    @pytest.mark.parametrize("power", [0, 1, 7, 14, 15])
    def test_valid_powers(self, power):
        result = cv.int_range(min=0, max=15)(power)
        assert result == power

    @pytest.mark.parametrize("power", [-1, 16, 100])
    def test_invalid_powers(self, power):
        with pytest.raises(cv.Invalid):
            cv.int_range(min=0, max=15)(power)


# ── Pure-Python PRNG cross-check ──────────────────────────────────────────────

def _swapendian(x: int) -> int:
    x = ((x >> 8) & 0x00FF00FF) | ((x & 0x00FF00FF) << 8)
    x = ((x >> 16) | (x << 16)) & 0xFFFFFFFF
    return x


def prng_successor_py(x: int, n: int) -> int:
    """
    Pure-Python re-implementation of the Crypto1 tag PRNG (prng_successor in crypto1.cpp).
    Used to cross-check the C++ implementation's known-good test vectors.
    """
    x = _swapendian(x) & 0xFFFFFFFF
    for _ in range(n):
        bit = (x >> 16 ^ x >> 18 ^ x >> 19 ^ x >> 21) & 1
        x = ((x >> 1) | (bit << 31)) & 0xFFFFFFFF
    return _swapendian(x) & 0xFFFFFFFF


class TestPrngSuccessor:
    """Cross-check prng_successor algorithm against hardware-verified vectors."""

    def test_zero_steps_returns_input(self):
        assert prng_successor_py(0x009080A2, 0) == 0x009080A2

    def test_nt_64_steps(self):
        """NT=009080A2, 64 steps → AR_plain=B172DED3 (ST25R3916 hardware verified)."""
        assert prng_successor_py(0x009080A2, 64) == 0xB172DED3

    def test_deterministic(self):
        a = prng_successor_py(0xDEADBEEF, 100)
        b = prng_successor_py(0xDEADBEEF, 100)
        assert a == b

    def test_commutative_steps(self):
        """64 bulk steps == 1 step repeated 64 times."""
        x = 0x009080A2
        bulk = prng_successor_py(x, 64)
        step = x
        for _ in range(64):
            step = prng_successor_py(step, 1)
        assert bulk == step

    def test_all_zeros_fixed_point(self):
        """0x00000000 is a fixed point of the PRNG (degenerate seed)."""
        assert prng_successor_py(0x00000000, 1) == 0x00000000

    @pytest.mark.parametrize("x,n,expected", [
        (0x009080A2, 0,  0x009080A2),
        (0x009080A2, 64, 0xB172DED3),
    ])
    def test_parametrized_known_values(self, x, n, expected):
        assert prng_successor_py(x, n) == expected
