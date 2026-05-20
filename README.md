# ST25R NFC Reader Component for ESPHome

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![ESPHome](https://img.shields.io/badge/ESPHome-compatible-green.svg)](https://esphome.io)

ESPHome external component for STMicroelectronics ST25R family NFC readers. Detects ISO 14443A (NFC-A) and ISO 15693 (NFC-V) tags, fires automations on tag present/removed, reads NDEF, and exposes sensors to Home Assistant.

---

## Quick Start

```yaml
esphome:
  name: nfc-reader

esp32:
  board: esp32-c6-devkitc-1
  variant: esp32c6
  framework:
    type: esp-idf

external_components:
  - source: github://JohnMcLear/esphome_st25r
    components: [st25r, st25r_spi]

spi:
  clk_pin: GPIO19
  miso_pin: GPIO10
  mosi_pin: GPIO18

st25r_spi:
  cs_pin: GPIO6
  irq_pin: GPIO7
  on_tag:
    then:
      - logger.log:
          format: "Tag: %s"
          args: ['x.c_str()']
  on_tag_removed:
    then:
      - logger.log:
          format: "Removed: %s"
          args: ['x.c_str()']
```

Flash, open logs — you should see `ST25R initialized successfully` and then a UID every time a tag is presented.

---

## Configuration Options

| Key | Default | Description |
|-----|---------|-------------|
| `cs_pin` | required | SPI chip select |
| `irq_pin` | — | IRQ GPIO (recommended; falls back to polling without it) |
| `reset_pin` | — | Hardware reset GPIO (optional, improves recovery; required on ST25R300) |
| `update_interval` | `1s` | Tag polling rate |
| `rf_power` | `15` | TX driver strength 0–15 (15 = max range) |
| `rf_field_enabled` | `true` | Keep RF field on between scans |
| `supply_3v3` | `true` | *Deprecated on ST25R3916* — VDD is auto-detected. Only used on ST25R300. |
| `nfcv_enabled` | `true` | Enable ISO 15693 (NFC-V) tag detection alongside NFC-A |
| `nfcb_enabled` | `true` | Enable ISO 14443B (NFC-B) tag detection alongside NFC-A |
| `aat_enabled` | `true` | Automatic Antenna Tuning — hill-climbing optimizer for max range (requires varicaps) |
| `suppress_on_tag_for_isodep` | `false` | Skip the `on_tag` fire for ISO-DEP (Type 4A) tags. Recommended `true` for mixed passive + Android HCE deployments — see "Passive tags + HCE phones" below. |
| `mifare_key_a` | `FFFFFFFFFFFF` | Mifare Classic Key A (12 hex chars) |
| `mifare_key_b` | `FFFFFFFFFFFF` | Mifare Classic Key B (12 hex chars) |
| `health_check_enabled` | `true` | Periodically verify chip identity via IC_IDENTITY register |
| `health_check_interval` | `60s` | How often to run the liveness check (independent of tag scan rate) |
| `max_failed_checks` | `3` | Consecutive failures before triggering a reinitialise |
| `auto_reset_on_failure` | `true` | Automatically reinitialise the chip after `max_failed_checks` failures |
| `status` | — | Binary sensor: chip health (`true` = OK, `false` = fault or recovering) |
| `field_strength` | — | Sensor: RF amplitude ADC reading |

---

## Advanced Example

Track specific tags as binary sensors, fire Home Assistant events, and expose reader health:

```yaml
st25r_spi:
  id: my_nfc_reader
  cs_pin: GPIO6
  irq_pin: GPIO7
  update_interval: 500ms
  rf_power: 15
  rf_field_enabled: true
  supply_3v3: true          # ST25R3916 auto-detects VDD and overrides this
  rx_gain_boost: false

  # Protocol options
  nfcv_enabled: true        # ISO 15693 (NFC-V) tag detection alongside NFC-A
  nfcb_enabled: true        # ISO 14443B (NFC-B) tag detection alongside NFC-A
  aat_enabled: true          # Automatic Antenna Tuning at startup (improves range on varicap boards)

  # Antenna tuning DAC values (starting point for AAT, or static if aat_enabled: false)
  ant_tune_a: 0x80
  ant_tune_b: 0x40

  # Mifare Classic keys
  mifare_key_a: A0A1A2A3A4A5
  mifare_key_b: FFFFFFFFFFFF

  # Tag presence debounce: consecutive missed scans before on_tag_removed fires
  miss_threshold: 3

  # Health check: verify chip identity every 60 s (independent of 500 ms scan rate).
  # After 3 consecutive failures the chip is automatically reinitialised.
  health_check_enabled: true
  health_check_interval: 60s
  max_failed_checks: 3
  auto_reset_on_failure: true

  status:
    name: "NFC Reader Health"   # true = OK, false = chip fault or recovering

  field_strength:
    name: "NFC Field Strength"

  on_tag:
    then:
      - homeassistant.event:
          event: esphome.nfc_tag_detected
          data:
            uid: !lambda 'return x;'

  on_tag_removed:
    then:
      - homeassistant.event:
          event: esphome.nfc_tag_removed
          data:
            uid: !lambda 'return x;'

binary_sensor:
  - platform: st25r
    st25r_id: my_nfc_reader
    name: "NFC Ring"
    uid: "04-1A-A7-67-5F-61-80"          # 7-byte NFC-A

  - platform: st25r
    st25r_id: my_nfc_reader
    name: "Access Card"
    uid: "DE-A3-0D-00"                    # 4-byte NFC-A

  - platform: st25r
    st25r_id: my_nfc_reader
    name: "ISO 15693 Badge"
    uid: "E0-02-08-02-4F-EF-E7-E1"       # 8-byte NFC-V
```

---

## I2C

> Connect I2C_EN to VDD_D on the Elechouse module to switch to I2C mode (GND = SPI).

```yaml
external_components:
  - source: github://JohnMcLear/esphome_st25r
    components: [st25r, st25r_i2c]

i2c:
  sda: GPIO10
  scl: GPIO19
  frequency: 50kHz

st25r_i2c:
  irq_pin: GPIO7
  update_interval: 1s
  on_tag:
    then:
      - logger.log:
          format: "Tag: %s"
          args: ['x.c_str()']
```

---

## Features

- **ISO 14443A (NFC-A):** 4-byte, 7-byte, and 10-byte UIDs (Cascade Levels 1–3)
- **ISO 14443B (NFC-B):** 4-byte PUPI detection via SENSB_REQ/ATQB (passports, transit cards)
- **ISO 14443-4 (ISO-DEP):** Type 4 tag communication via RATS/ATS + I-Block framing
  - NDEF Type 4 tag reading (SELECT NDEF app → SELECT CC → READ BINARY)
- **ISO 15693 (NFC-V):** 8-byte UID detection, dual-protocol with NFC-A
  - NDEF read/write for Type 5 tags (READ/WRITE_SINGLE_BLOCK)
  - ST25R3916: software 1-of-4 encoding + Manchester decoding (streaming mode)
  - ST25R300: built-in hardware NFC-V codec
- Multi-tag detection — anticollision loop finds all NFC-A tags simultaneously
- Mifare Classic Crypto1 authentication and block read
- NDEF read/write for Type 2 tags (NTAG / Ultralight), Type 4 tags (ISO-DEP), and Type 5 tags (ISO 15693)
- **Automatic Antenna Tuning (AAT)** — hill-climbing optimizer for ANT_TUNE_A/B at startup; +20% range on varicap-equipped boards (confirmed on STEVAL-MB17149B: 50mm → 60mm)
- Tag presence/removal triggers with 3-miss debounce
- Binary sensor platform for specific-tag tracking (supports 4/7/8-byte UIDs)
- VDD auto-detection for optimal regulator configuration
- Chip health monitoring with automatic recovery
- RF field strength sensor
- Configurable RF power and Mifare Classic keys
- SPI and I2C transport (ST25R3916); SPI (ST25R300)

---

## Supported Hardware

### `st25r` / `st25r_spi` / `st25r_i2c` — ST25R39xx family

| Chip | Status | Variant notes |
|------|--------|---------------|
| ST25R3916 | ✅ Verified | Primary target; Elechouse module tested (SPI + I2C) |
| ST25R3916B | ✅ Verified | B-variant detected via IC identity; lf_en routing handled |
| ST25R3917 / ST25R3917B | ⚠️ Untested | Register-compatible; no EMVCo, no AAT |
| ST25R3918 | ⚠️ Untested | Register-compatible; appears alongside 3916 family in ST app notes |
| ST25R3919B | ⚠️ Untested | EMVCo 3.2a compliant variant |
| ST25R3920 / ST25R3920B | ⚠️ Untested | High-power variant; register-compatible |
| ST25R3914 | ⚠️ Untested | Automotive; 96-byte FIFO (vs 512) — FIFO handling may need adjustment |
| ST25R3915 | ⚠️ Untested | Automotive; 96-byte FIFO, no AAT |

### `st25r300` / `st25r300_spi` — ST25R300 family

All four chips share the same register map (per ST application notes AN6279, AN6298, AN6313).

| Chip | Status | Variant notes |
|------|--------|---------------|
| ST25R300 | ✅ Verified | X-NUCLEO-NFC12A1; consumer/industrial payment, EMVCo PCD 3.2a |
| ST25R500 | ⚠️ Untested | Automotive CCC Digital Key |
| ST25R501 | ⚠️ Untested | Compact automotive (QFN24), reader-only |
| ST25R210 | ⚠️ Untested | Automotive variant |

---

## ISO 14443-4 (ISO-DEP) / Type 4 Tags

Tags with SAK bit 5 set (e.g., DESFire, NTAG424, GlobalPlatform cards) are automatically activated via RATS/ATS after NFC-A SELECT. The firmware then attempts to read NDEF data using the standard Type 4 tag flow:

```
1. RATS (0xE0 0x80) → ATS (frame size, timing)
2. SELECT NDEF Application (AID: D276000085010100)
3. SELECT Capability Container (FID: 0xE103)
4. READ BINARY CC (15 bytes)
5. SELECT NDEF File (from CC)
6. READ BINARY NDEF data
```

**APDU format** (wrapped in I-Block framing automatically):

| Command | CLA | INS | P1 | P2 | Data |
|---------|-----|-----|----|----|------|
| SELECT by name | 00 | A4 | 04 | 00 | AID bytes |
| SELECT by FID | 00 | A4 | 00 | 0C | File ID (2 bytes) |
| READ BINARY | 00 | B0 | offset_hi | offset_lo | Le (length) |

**Supported tags:** Any ISO 14443-4 compliant tag (SAK & 0x20). Tags without an NDEF application (e.g., payment cards, GlobalPlatform) will still be detected by UID — only the NDEF read step is skipped.

**Verified on hardware:** GlobalPlatform card SAK=0x28, ATS TL=13 bytes, RATS/ATS + I-Block exchange successful on STEVAL-MB17149B.

### Passive tags + HCE phones at the same reader

Most "smart home NFC" deployments bridge `on_tag` directly to
Home Assistant's tag scanner integration so that NTAG stickers,
MIFARE rings, and ISO 15693 cards become first-class tag entities:

```yaml
on_tag:
  - homeassistant.tag_scanned: !lambda 'return x;'
```

Android phones in Host Card Emulation (HCE) mode randomise their
4-byte NFC-A anticollision UID per tap — this is an OS-level
privacy feature, not configurable on the phone side. With the
bridge above, every HCE tap injects a one-shot `08-XX-XX-XX` tag
entity into HA's tag log that no trusted-tag automation will ever
match, drowning out real passive credentials.

Set `suppress_on_tag_for_isodep: true` to drop the `on_tag` fire
for ISO 14443-4 capable tags (SAK bit 5 set) while keeping every
passive tag firing normally:

```yaml
st25r_spi:
  id: my_reader
  cs_pin: GPIO6
  suppress_on_tag_for_isodep: true
  on_tag:
    - homeassistant.tag_scanned: !lambda 'return x;'
```

The flag is application-agnostic — `on_isodep_tag` triggers and
any application-layer component listening to ISO-DEP tags still
fire as normal. Only the synthetic-UID `on_tag` path is gated.
Default `false` preserves the historical behaviour for upstream
users who don't expect HCE phones at their reader.

### Custom APDU Exchange (Lambda)

Use `send_apdu()` from an `on_tag` lambda for multi-step APDU conversations. Block numbers toggle automatically between calls. RATS/ATS activation happens automatically when the tag is first detected.

**Yubikey OTP example** (reads OTP from Yubico applet):

```yaml
st25r_spi:
  id: my_reader
  cs_pin: GPIO6
  irq_pin: GPIO7
  on_tag:
    then:
      - lambda: |-
          if (!id(my_reader).is_isodep_active()) return;  // skip non-ISO-DEP tags

          uint8_t resp[64];
          uint8_t len = 0;

          // Step 1: SELECT Yubico OTP applet (AID: A0000005272001)
          uint8_t select_yubico[] = {0x00, 0xA4, 0x04, 0x00, 0x07,
                                     0xA0, 0x00, 0x00, 0x05, 0x27, 0x20, 0x01, 0x00};
          if (!id(my_reader).send_apdu(select_yubico, sizeof(select_yubico), resp, len) ||
              len < 2 || resp[len-2] != 0x90) {
            ESP_LOGW("yubikey", "SELECT Yubico OTP failed");
            return;
          }
          ESP_LOGI("yubikey", "Yubico OTP applet selected (%u bytes)", len);

          // Step 2: Read OTP (Yubico proprietary command)
          uint8_t read_otp[] = {0x00, 0x01, 0x00, 0x00, 0x00};
          len = 0;
          if (id(my_reader).send_apdu(read_otp, sizeof(read_otp), resp, len) && len > 2) {
            // OTP is in resp[0..len-3], SW1 SW2 at end
            std::string otp;
            for (int i = 0; i < len - 2; i++) {
              char hex[3];
              snprintf(hex, sizeof(hex), "%02X", resp[i]);
              otp += hex;
            }
            ESP_LOGI("yubikey", "OTP: %s (SW=%02X%02X)", otp.c_str(), resp[len-2], resp[len-1]);
          }
```

**Generic APDU example** (read UID from any ISO-DEP tag):

```yaml
on_tag:
  then:
    - lambda: |-
        if (!id(my_reader).is_isodep_active()) return;
        uint8_t resp[64];
        uint8_t len = 0;
        // GET DATA (UID): CLA=0xFF, INS=0xCA, P1=0x00, P2=0x00, Le=0x00
        uint8_t get_uid[] = {0xFF, 0xCA, 0x00, 0x00, 0x00};
        if (id(my_reader).send_apdu(get_uid, sizeof(get_uid), resp, len)) {
          ESP_LOGI("apdu", "Response: %u bytes, SW=%02X%02X", len, resp[len-2], resp[len-1]);
        }
```

**Payment card/ring SEID example** (reads stable Secure Element ID — hardware-verified):

Many payment cards use random UIDs that change every scan for privacy. The SEID (from CPLC data) is a **permanent, stable identifier** that never changes — ideal for access control.

```yaml
on_tag:
  then:
    - lambda: |-
        if (!id(my_reader).is_isodep_active()) return;
        uint8_t resp[64];
        uint8_t len = 0;

        // GET DATA CPLC (80 CA 9F 7F 2C) — reads Secure Element ID
        // Works on GlobalPlatform payment cards/rings (SAK=0x20)
        // Hardware-verified: SEID stable across scans even with random UID cards
        uint8_t get_seid[] = {0x80, 0xCA, 0x9F, 0x7F, 0x2C};
        if (id(my_reader).send_apdu(get_seid, sizeof(get_seid), resp, len) && len > 2) {
          std::string seid;
          for (int i = 0; i < len - 2; i++) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02X", resp[i]);
            seid += hex;
          }
          ESP_LOGI("payment", "SEID: %s (SW=%02X%02X)", seid.c_str(), resp[len-2], resp[len-1]);
        }
```

> **Tested on STEVAL-MB17149B** with a random-UID payment card (SAK=0x20): NFC-A UID changed every scan (08D28688, 0889CE54, 0893A298...) but SEID remained constant across all reads.

> **Note:** `send_apdu()` can be called multiple times within the same `on_tag` lambda for multi-step conversations. The I-Block block number toggles automatically.

---

## Troubleshooting

**No tags detected** — check wiring and confirm logs show `ST25R initialized successfully`. Verify IRQ pin is connected and not a strapping pin.

**ESP32-C6 boot issues** — avoid GPIO9 for CS (strapping pin).

**Mifare Classic auth fails** — if NT never changes, the card is a clone with a fixed PRNG. Genuine NXP Mifare Classic 1K cards are required.

**Slow response** — reduce `update_interval` to `250ms` or lower.
