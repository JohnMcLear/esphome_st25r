# ST25R NFC Reader Component for ESPHome

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![ESPHome](https://img.shields.io/badge/ESPHome-compatible-green.svg)](https://esphome.io)

ESPHome external component for STMicroelectronics ST25R family NFC readers. Detects ISO14443A tags, fires automations on tag present/removed, reads NDEF, and exposes sensors to Home Assistant.

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
| `supply_3v3` | `true` | Set `true` for 3.3V supply |
| `mifare_key_a` | `FFFFFFFFFFFF` | Mifare Classic Key A (12 hex chars) |
| `mifare_key_b` | `FFFFFFFFFFFF` | Mifare Classic Key B (12 hex chars) |
| `status` | — | Binary sensor: chip health (`true` = OK) |
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
  mifare_key_a: A0A1A2A3A4A5

  status:
    name: "NFC Reader Health"

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
    uid: "04-1A-A7-67-5F-61-80"

  - platform: st25r
    st25r_id: my_nfc_reader
    name: "Access Card"
    uid: "DE-A3-0D-00"
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

- ISO14443A: 4-byte, 7-byte, and 10-byte UIDs (Cascade Levels 1–3)
- Multi-tag detection — anticollision loop finds all tags simultaneously
- Mifare Classic Crypto1 authentication and block read
- NDEF read/write for Type 2 tags (NTAG / Ultralight)
- Tag presence/removal triggers with 3-miss debounce
- Binary sensor platform for specific-tag tracking
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
| ST25R300 | ⚠️ Compiles, untested | X-NUCLEO-NFC12A1; consumer/industrial payment, EMVCo PCD 3.2a |
| ST25R500 | ⚠️ Compiles, untested | Automotive CCC Digital Key |
| ST25R501 | ⚠️ Compiles, untested | Compact automotive (QFN24), reader-only |
| ST25R210 | ⚠️ Compiles, untested | Automotive variant |

---

## Troubleshooting

**No tags detected** — check wiring and confirm logs show `ST25R initialized successfully`. Verify IRQ pin is connected and not a strapping pin.

**ESP32-C6 boot issues** — avoid GPIO9 for CS (strapping pin).

**Mifare Classic auth fails** — if NT never changes, the card is a clone with a fixed PRNG. Genuine NXP Mifare Classic 1K cards are required.

**Slow response** — reduce `update_interval` to `250ms` or lower.
