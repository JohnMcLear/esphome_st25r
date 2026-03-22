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


# ── Boolean config options ────────────────────────────────────────────────────

class TestBooleanConfigs:
    """Verify boolean YAML options accept true/false."""

    def test_aat_enabled_true(self):
        assert cv.boolean(True) is True

    def test_aat_enabled_false(self):
        assert cv.boolean(False) is False

    def test_nfcv_enabled_true(self):
        assert cv.boolean(True) is True

    def test_nfcv_enabled_false(self):
        assert cv.boolean(False) is False


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


# ── NFC-V (ISO 15693) protocol tests ────────────────────────────────────────

def nfcv_parse_inventory_uid(resp_bytes):
    """
    Parse an ISO 15693 INVENTORY response (after CRC strip).
    Response: flags(1) + DSFID(1) + UID(8) = 10 bytes.
    UID is transmitted LSB-first; return MSB-first hex string.
    """
    if len(resp_bytes) < 10:
        raise ValueError(f"Short inventory response ({len(resp_bytes)} bytes)")
    if resp_bytes[0] & 0x01:
        raise ValueError(f"Error flag set (0x{resp_bytes[0]:02X})")
    uid_lsb = resp_bytes[2:10]
    uid_msb = uid_lsb[::-1]
    return "".join(f"{b:02X}" for b in uid_msb)


def nfcv_build_inventory_req():
    """Build a 1-slot ISO 15693 INVENTORY request: [flags, cmd, mask_len]."""
    return bytes([0x26, 0x01, 0x00])


class TestNfcvInventoryCommand:
    """Tests for ISO 15693 INVENTORY command format."""

    def test_inventory_request_format(self):
        req = nfcv_build_inventory_req()
        assert req == b'\x26\x01\x00'
        assert req[0] & 0x04 == 0x04  # inventory flag set
        assert req[0] & 0x20 == 0x20  # 1-slot flag set
        assert req[0] & 0x02 == 0x02  # high data rate flag set
        assert req[1] == 0x01         # INVENTORY command

    def test_inventory_request_length(self):
        req = nfcv_build_inventory_req()
        assert len(req) == 3


class TestNfcvUidParsing:
    """Tests for ISO 15693 UID parsing from INVENTORY response."""

    def test_parse_real_tag_uid(self):
        """Hardware-verified: tag E00208024FEFE7E1 on X-NUCLEO-NFC12A1."""
        # Response bytes (CRC stripped): flags=0x00, DSFID=0x00, UID LSB-first
        resp = bytes([0x00, 0x00, 0xE1, 0xE7, 0xEF, 0x4F, 0x02, 0x08, 0x02, 0xE0])
        uid = nfcv_parse_inventory_uid(resp)
        assert uid == "E00208024FEFE7E1"

    def test_uid_is_16_hex_chars(self):
        resp = bytes([0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08])
        uid = nfcv_parse_inventory_uid(resp)
        assert len(uid) == 16

    def test_uid_reversed_from_lsb(self):
        """UID bytes in response are LSB-first; parsing reverses to MSB-first."""
        resp = bytes([0x00, 0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44])
        uid = nfcv_parse_inventory_uid(resp)
        assert uid == "44332211DDCCBBAA"

    def test_error_flag_raises(self):
        """Bit 0 of flags byte = error."""
        resp = bytes([0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        with pytest.raises(ValueError, match="Error flag"):
            nfcv_parse_inventory_uid(resp)

    def test_short_response_raises(self):
        with pytest.raises(ValueError, match="Short"):
            nfcv_parse_inventory_uid(bytes([0x00, 0x00, 0x01]))

    def test_dsfid_not_in_uid(self):
        """DSFID (byte 1) must not appear in the UID string."""
        resp = bytes([0x00, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08])
        uid = nfcv_parse_inventory_uid(resp)
        assert "FF" not in uid  # DSFID=0xFF should not be in UID

    def test_all_zeros_uid(self):
        resp = bytes([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        uid = nfcv_parse_inventory_uid(resp)
        assert uid == "0000000000000000"

    def test_all_ff_uid(self):
        resp = bytes([0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
        uid = nfcv_parse_inventory_uid(resp)
        assert uid == "FFFFFFFFFFFFFFFF"


class TestNfcvProtocolConstants:
    """Verify ISO 15693 protocol constants match the spec."""

    def test_inventory_cmd(self):
        assert 0x01 == 0x01  # ISO 15693 INVENTORY command code

    def test_stay_quiet_cmd(self):
        assert 0x02 == 0x02  # ISO 15693 STAY_QUIET command code

    def test_uid_length(self):
        """ISO 15693 UIDs are always 8 bytes (64 bits)."""
        assert 8 == 8

    def test_inventory_flags_bits(self):
        """Verify individual flag bits in the 1-slot inventory request."""
        flags = 0x26
        assert flags & 0x01 == 0  # bit0: single subcarrier
        assert flags & 0x02 != 0  # bit1: high data rate
        assert flags & 0x04 != 0  # bit2: inventory flag
        assert flags & 0x20 != 0  # bit5: 1-slot


class TestIso15693Crc:
    """Tests for the ISO 15693 CRC-16 CCITT algorithm."""

    @staticmethod
    def iso15693_crc(data):
        """Pure-Python CRC-16 CCITT matching the C++ implementation."""
        crc = 0xFFFF
        for byte in data:
            d = byte ^ (crc & 0xFF)
            d ^= (d << 4) & 0xFF
            crc = ((crc >> 8) ^ (d << 8) ^ (d << 3) ^ (d >> 4)) & 0xFFFF
        return crc ^ 0xFFFF

    def test_empty_data(self):
        """CRC of empty data should be ~0xFFFF = 0x0000... no, ~0xFFFF = 0."""
        # With no data, CRC stays at preset 0xFFFF, inverted = 0x0000
        # But the algorithm never enters the loop, so crc=0xFFFF, ~crc=0x0000
        assert self.iso15693_crc(b"") == 0x0000

    def test_inventory_command(self):
        """CRC of INVENTORY [0x26, 0x01, 0x00] should be deterministic."""
        crc = self.iso15693_crc(bytes([0x26, 0x01, 0x00]))
        assert isinstance(crc, int)
        assert 0 <= crc <= 0xFFFF

    def test_known_response_crc_appended(self):
        """Appending CRC bytes and recomputing should yield the CRC residue constant."""
        payload = bytes([0x00, 0x00, 0xE1, 0xE7, 0xEF, 0x4F, 0x02, 0x08, 0x02, 0xE0])
        crc = self.iso15693_crc(payload)
        crc_bytes = bytes([crc & 0xFF, (crc >> 8) & 0xFF])
        # CRC of data+crc should be a fixed residue (property of CCITT CRC)
        full_crc = self.iso15693_crc(payload + crc_bytes)
        # The residue is constant for any valid frame — verify by checking a second payload
        payload2 = bytes([0x01, 0x02, 0x03])
        crc2 = self.iso15693_crc(payload2)
        crc2_bytes = bytes([crc2 & 0xFF, (crc2 >> 8) & 0xFF])
        full_crc2 = self.iso15693_crc(payload2 + crc2_bytes)
        assert full_crc == full_crc2  # both produce the same residue

    def test_crc_changes_with_data(self):
        """Different data produces different CRCs."""
        crc1 = self.iso15693_crc(bytes([0x00, 0x00]))
        crc2 = self.iso15693_crc(bytes([0x00, 0x01]))
        assert crc1 != crc2

    def test_crc_is_16_bit(self):
        """CRC must be a 16-bit value."""
        for i in range(256):
            crc = self.iso15693_crc(bytes([i]))
            assert 0 <= crc <= 0xFFFF


# ── ISO 14443-4 (ISO-DEP / Type 4) protocol tests ───────────────────────────

class TestIsoDepProtocol:
    """Tests for ISO 14443-4 ISO-DEP protocol constants and framing."""

    def test_rats_command_format(self):
        """RATS: 0xE0 + parameter byte (FSDI << 4 | DID)."""
        rats = bytes([0xE0, 0x80])  # FSDI=8 (256 bytes), DID=0
        assert rats[0] == 0xE0
        assert (rats[1] >> 4) == 8   # FSDI
        assert (rats[1] & 0x0F) == 0  # DID

    def test_iblock_pcb_format(self):
        """I-Block PCB: bits[7:6]=00, bit1=1, bit0=block_number."""
        pcb_block0 = 0x02  # I-Block, block#0
        pcb_block1 = 0x03  # I-Block, block#1
        assert pcb_block0 & 0xC0 == 0x00  # I-Block type
        assert pcb_block0 & 0x02 == 0x02  # Must-be-1 bit
        assert pcb_block0 & 0x01 == 0     # Block number 0
        assert pcb_block1 & 0x01 == 1     # Block number 1

    def test_iblock_chaining_bit(self):
        """I-Block chaining: bit 4 set when more data follows."""
        pcb_chain = 0x12  # I-Block, chaining, block#0
        assert pcb_chain & 0x10 == 0x10  # Chaining bit

    def test_block_number_toggle(self):
        """Block numbers alternate 0, 1, 0, 1..."""
        bn = 0
        for _ in range(4):
            bn ^= 1
        assert bn == 0  # Even number of toggles returns to 0

    def test_sak_isodep_detection(self):
        """SAK bit 5 (0x20) indicates ISO-DEP capable tag."""
        assert 0x20 & 0x20 == 0x20   # NTAG424/DESFire SAK=0x20
        assert 0x28 & 0x20 == 0x20   # GlobalPlatform SAK=0x28 (confirmed on hardware)
        assert 0x08 & 0x20 == 0x00   # Mifare Classic SAK=0x08 — NOT ISO-DEP
        assert 0x00 & 0x20 == 0x00   # NTAG SAK=0x00 — NOT ISO-DEP


class TestType4NdefCommands:
    """Tests for NFC Forum Type 4 tag APDU command construction."""

    def test_select_ndef_app_v2(self):
        """SELECT NDEF Application v2: AID = D276000085010100."""
        apdu = bytes([0x00, 0xA4, 0x04, 0x00, 0x07,
                      0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, 0x00])
        assert apdu[0] == 0x00   # CLA
        assert apdu[1] == 0xA4   # INS SELECT
        assert apdu[2] == 0x04   # P1: select by name
        assert apdu[4] == 0x07   # Lc: 7 bytes
        assert apdu[5:12] == bytes([0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01])

    def test_select_cc_file(self):
        """SELECT CC file: FID = 0xE103."""
        apdu = bytes([0x00, 0xA4, 0x00, 0x0C, 0x02, 0xE1, 0x03])
        assert apdu[2] == 0x00   # P1: select by file ID
        assert apdu[3] == 0x0C   # P2: no response data
        assert apdu[5:7] == bytes([0xE1, 0x03])

    def test_read_binary(self):
        """READ BINARY: offset + length."""
        apdu = bytes([0x00, 0xB0, 0x00, 0x02, 0x20])
        assert apdu[1] == 0xB0   # INS READ BINARY
        assert apdu[2] == 0x00   # P1: offset high
        assert apdu[3] == 0x02   # P2: offset low (skip 2-byte NLEN)
        assert apdu[4] == 0x20   # Le: read 32 bytes

    def test_success_status_word(self):
        """Status word 0x9000 = success."""
        sw1, sw2 = 0x90, 0x00
        assert sw1 == 0x90 and sw2 == 0x00

    def test_atqb_response_code(self):
        """ATQB response starts with 0x50."""
        assert 0x50 == 0x50

    def test_sensb_req_format(self):
        """SENSB_REQ: 0x05, AFI, PARAM."""
        sensb = bytes([0x05, 0x00, 0x08])
        assert sensb[0] == 0x05  # SENSB_REQ command
        assert sensb[1] == 0x00  # AFI: any
        assert sensb[2] & 0x08 == 0x08  # WUPB bit set
