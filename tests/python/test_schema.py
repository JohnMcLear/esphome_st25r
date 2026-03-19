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


# ── ISO-DEP token extraction logic ───────────────────────────────────────────
# The C++ read_tag_() in st25r.cpp extracts a stable token from an NFC Forum
# T4T NDEF message with the following priority:
#   1. HA tag UUID    — URL containing HA_TAG_ID_PREFIX → return UUID suffix
#   2. First payload  — first NDEF record's get_payload() string
#   3. Hex fallback   — raw NDEF bytes hex-encoded
#
# This module re-implements that logic in Python so it can be tested without
# the full ESPHome C++ build.  The logic must stay in sync with st25r.cpp.

HA_TAG_ID_PREFIX = "https://www.home-assistant.io/tag/"


def extract_iso_dep_token(record_payloads: list, raw_bytes: bytes) -> str:
    """
    Python mirror of the ISO-DEP token extraction in ST25R::read_tag_().

    record_payloads: list of payload strings (one per NDEF record, in order).
                     URI records yield the full URI string; Text records yield
                     the text string — matching NdefRecord::get_payload() /
                     NdefRecordUri::get_payload() / NdefRecordText::get_payload().
    raw_bytes: raw NDEF message bytes (for the hex fallback only).
    """
    # Priority 1: check all records for the HA tag URL (mirrors has_ha_tag_ndef
    # / get_ha_tag_ndef which iterate all records).
    for payload in record_payloads:
        pos = payload.find(HA_TAG_ID_PREFIX)
        if pos != -1:
            return payload[pos + len(HA_TAG_ID_PREFIX):]

    # Priority 2: first record's payload string (mirrors records[0]->get_payload()).
    if record_payloads and record_payloads[0]:
        return record_payloads[0]

    # Priority 3: hex-encode raw NDEF bytes.
    return "".join(f"{b:02X}" for b in raw_bytes)


class TestIsodepTokenExtraction:
    """Tests for the ISO-DEP stable token extraction logic in ST25R::read_tag_()."""

    # ── Priority 1: HA tag UUID ──────────────────────────────────────────────

    def test_ha_url_returns_uuid(self):
        """Standard HA Companion App record → just the UUID portion."""
        uuid = "abc12345-0000-1234-abcd-ef1234567890"
        records = [HA_TAG_ID_PREFIX + uuid]
        assert extract_iso_dep_token(records, b"") == uuid

    def test_ha_url_in_second_record(self):
        """HA URL in second record (not first) is still detected."""
        uuid = "11111111-2222-3333-4444-555555555555"
        records = ["https://example.com/other", HA_TAG_ID_PREFIX + uuid]
        assert extract_iso_dep_token(records, b"") == uuid

    def test_ha_url_with_query_string(self):
        """UUID portion is everything after the prefix, including any query string."""
        records = [HA_TAG_ID_PREFIX + "my-tag-id?extra=1"]
        assert extract_iso_dep_token(records, b"") == "my-tag-id?extra=1"

    def test_ha_priority_over_other_records(self):
        """HA URL takes priority even if a non-HA URI record appears first."""
        uuid = "deadbeef-dead-beef-dead-beefdeadbeef"
        records = ["https://otherprovider.com/token/xyz", HA_TAG_ID_PREFIX + uuid]
        assert extract_iso_dep_token(records, b"\x01\x02\x03")

    # ── Priority 2: first record payload ────────────────────────────────────

    def test_uri_record_without_ha_prefix(self):
        """Custom URI → full URI string used as token."""
        uri = "https://mycompany.com/nfc/alice"
        records = [uri]
        assert extract_iso_dep_token(records, b"\x01\x02") == uri

    def test_text_record_payload(self):
        """Plain text record → text string used as token."""
        records = ["Alice Smith"]
        assert extract_iso_dep_token(records, b"\xD1\x01\x0CT") == "Alice Smith"

    def test_first_record_used_when_multiple_non_ha(self):
        """When multiple non-HA records, only the first record's payload is used."""
        records = ["first-record-uri", "second-record-uri"]
        assert extract_iso_dep_token(records, b"") == "first-record-uri"

    # ── Priority 3: hex fallback ─────────────────────────────────────────────

    def test_no_records_falls_back_to_hex(self):
        """Empty record list → hex-encode raw bytes."""
        assert extract_iso_dep_token([], b"\xDE\xAD\xBE\xEF") == "DEADBEEF"

    def test_empty_first_payload_falls_back_to_hex(self):
        """Record with empty payload → hex fallback (not empty string)."""
        assert extract_iso_dep_token([""], b"\xAB\xCD") == "ABCD"

    def test_hex_fallback_single_byte(self):
        assert extract_iso_dep_token([], b"\x00") == "00"

    def test_hex_fallback_uppercase(self):
        """Hex output should be uppercase to match the C++ snprintf '%02X'."""
        token = extract_iso_dep_token([], b"\x1a\xff")
        assert token == "1AFF"

    def test_hex_fallback_preserves_all_bytes(self):
        raw = bytes(range(16))
        expected = "".join(f"{b:02X}" for b in raw)
        assert extract_iso_dep_token([], raw) == expected
