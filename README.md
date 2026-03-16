# ST25R NFC Reader Component for ESPHome

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![ESPHome](https://img.shields.io/badge/ESPHome-compatible-green.svg)](https://esphome.io)

An ESPHome component for the STMicroelectronics ST25R family of NFC reader ICs.

## Supported Hardware

### ✅ Fully Supported

| Variant | Package | Notes |
|---------|---------|-------|
| **ST25R3916 / ST25R3916B** | QFN32/WLCSP | Primary target, fully tested |
| **ST25R3917 / ST25R3917B** | QFN32/WLCSP | Reduced feature set, register-compatible |
| **ST25R3919B** | QFN32/WLCSP | EMVCo 3.2a compliant variant |
| **ST25R3920 / ST25R3920B** | QFN32/WLCSP | High-power variant, register-compatible |

**Verified module:** Elechouse ST25R3916 (SPI and I2C modes both supported).

### ⚠️ Partially Supported (Untested)

| Variant | Package | Notes |
|---------|---------|-------|
| **ST25R3914 / ST25R3915** | QFN32 | Automotive grade, smaller FIFO (96 bytes), AAT on 3914 only |
| **ST25R500** | QFN32 | Automotive CCC Digital Key, different pinout, NFC-V up to 212 kbps |
| **ST25R300** | QFN32 | Feature-reduced ST25R500 derivative (datasheet DS14655 not in collection) |
| **ST25R210** | QFN32 | Automotive variant (datasheet not in collection, see AN6092/AN6279) |
| **ST25R501** | QFN24 | Compact automotive, reader-only (no card emulation) |

> **Note:** ST25R300/210/500/501 use the same register map but have different pinouts. Hardware redesign required.
> **Missing datasheets:** DS14655 (ST25R300), ST25R210 datasheet — only application notes available.

### ❌ Not Supported

| Variant | Reason |
|---------|--------|
| **ST25RN300** | Uses **NCI protocol** instead of direct register access. Requires complete driver rewrite with NCI stack. |
| **ST25R200** | Different register map, smaller FIFO (256 bytes), no I²C. Future support possible. |
| **ST25R100** | Cost-reduced variant, no AWS, slower SPI. Future support possible. |
| **ST25R95** | Legacy device, different command set. Migrate to ST25R200 or ST25R39xxB for new designs. |

### Why ST25RN300 is Not Supported

The **ST25RN300** uses the **NCI (NFC Controller Interface)** protocol, which is fundamentally different from the direct register access used by other ST25R variants:

| Feature | ST25R39xx/500 | ST25RN300 |
|---------|---------------|-----------|
| **Interface** | Direct SPI register access | NCI over I²C/SPI |
| **Driver Architecture** | Register reads/writes | NCI command/response packets |
| **Configuration** | Write registers directly | NCI RF Discovery configuration |
| **Firmware** | Fixed in hardware | 100% re-flashable firmware |
| **Driver Complexity** | ~2000 lines | Would require ~5000+ lines (NCI stack) |

**NCI Protocol Overview:**
- Host sends NCI commands (e.g., `RF_DISCOVER_CMD`, `RF_CONFLICT_RESOLV_CMD`)
- Controller responds with NCI responses and notifications
- Requires state machine for NCI connection management
- Different packet format: `[Header: 3 bytes] [Payload Length] [Payload]`

**What would be needed for ST25RN300 support:**
1. Complete NCI protocol stack implementation
2. NCI RF Discovery configuration engine
3. Firmware update mechanism (ST25RN300 is firmware-based)
4. Different power management (battery-optimized)
5. New ESPHome component (`st25rn300_nci`) — cannot reuse existing driver

For mobile/battery applications, consider using the **ST25R200** or **ST25R3916B** with low-power wake-up modes instead.

See `memory/st25r_variant_comparison.md` for comprehensive variant details.

## Features

- ✅ SPI and I2C transport support
- ✅ Full ISO14443A (NFC-A): 4-byte, 7-byte, and 10-byte UIDs (Cascade Levels 1–3)
- ✅ Multi-tag detection — anticollision loop finds all tags in field simultaneously
- ✅ Mifare Classic authentication (Crypto1, 3-pass mutual auth) and block read
- ✅ NDEF read for Type 2 tags (NTAG / Ultralight)
- ✅ Tag presence and removal triggers (`on_tag` / `on_tag_removed`)
- ✅ Binary sensor platform for specific-tag tracking
- ✅ Chip health monitoring with automatic recovery and `mark_failed`
- ✅ RF field strength sensor (amplitude ADC)
- ✅ Configurable RF power and Mifare Classic keys
- ✅ Hardware reset pin support

---

## Installation

```yaml
external_components:
  - source: github://JohnMcLear/esphome-st25r
    components: [ st25r, st25r_spi, st25r_i2c ]
```

---

## Basic Example (SPI)

Detect any ISO14443A tag and log its UID:

```yaml
esphome:
  name: nfc-reader

esp32:
  board: esp32-c6-devkitc-1
  variant: esp32c6
  framework:
    type: esp-idf

external_components:
  - source: github://JohnMcLear/esphome-st25r
    components: [ st25r, st25r_spi, st25r_i2c ]

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
          format: "Tag detected: %s"
          args: ['x.c_str()']
  on_tag_removed:
    then:
      - logger.log:
          format: "Tag removed: %s"
          args: ['x.c_str()']
```

---

## Advanced Example (SPI — all options)

```yaml
esphome:
  name: nfc-reader-advanced

esp32:
  board: esp32-c6-devkitc-1
  variant: esp32c6
  framework:
    type: esp-idf

external_components:
  - source: github://JohnMcLear/esphome-st25r
    components: [ st25r, st25r_spi, st25r_i2c ]

spi:
  clk_pin: GPIO19
  miso_pin: GPIO10
  mosi_pin: GPIO18

st25r_spi:
  id: my_nfc_reader
  cs_pin: GPIO6
  irq_pin: GPIO7
  reset_pin: GPIO8          # Optional: hardware reset for reliable recovery
  update_interval: 500ms    # How often to scan for tags (default 1s)
  rf_power: 15              # TX driver power 0–15 (15 = max range, default 15)
  rf_field_enabled: true    # Keep RF field on (default true)
  supply_3v3: true          # Must be true for 3.3V supply (default true)
  mifare_key_a: A0A1A2A3A4A5   # Mifare Classic Key A (default FFFFFFFFFFFF)
  mifare_key_b: B0B1B2B3B4B5   # Mifare Classic Key B (default FFFFFFFFFFFF)

  # Reader health binary sensor — true = chip OK, false = hardware fault
  status:
    name: "NFC Reader Health"

  # RF field amplitude — proxy for tag proximity / field quality
  field_strength:
    name: "NFC Field Strength"

  # Fires when any new tag enters the field; x = UID string e.g. "04-1A-A7-67"
  on_tag:
    then:
      - logger.log:
          format: "Tag detected: %s"
          args: ['x.c_str()']
      - homeassistant.event:
          event: esphome.nfc_tag_detected
          data:
            uid: !lambda 'return x;'

  # Fires when a tag that was present is no longer seen (3-miss threshold)
  on_tag_removed:
    then:
      - logger.log:
          format: "Tag removed: %s"
          args: ['x.c_str()']
      - homeassistant.event:
          event: esphome.nfc_tag_removed
          data:
            uid: !lambda 'return x;'

# Track specific tags as binary sensors in Home Assistant
binary_sensor:
  - platform: st25r
    st25r_id: my_nfc_reader
    name: "NFC Ring"
    uid: "04-1A-A7-67-5F-61-80"   # 7-byte UID, hyphen-separated

  - platform: st25r
    st25r_id: my_nfc_reader
    name: "Access Card"
    uid: "DE-A3-0D-00"             # 4-byte UID
```

---

## I2C Example

```yaml
external_components:
  - source: github://JohnMcLear/esphome-st25r
    components: [ st25r, st25r_spi, st25r_i2c ]

i2c:
  sda: GPIO10
  scl: GPIO19
  frequency: 50kHz

st25r_i2c:
  id: my_nfc_reader
  address: 0x50
  irq_pin: GPIO5
  update_interval: 1s
  on_tag:
    then:
      - logger.log:
          format: "Tag: %s"
          args: ['x.c_str()']
```

> **I2C_EN pin:** On the Elechouse module, connect I2C_EN to VDD_D to enable I2C mode. GND = SPI mode.

---

## Configuration Options

| Key | Default | Description |
|-----|---------|-------------|
| `update_interval` | `1s` | Tag polling rate |
| `rf_power` | `15` | TX driver strength 0–15 (15 = max) |
| `rf_field_enabled` | `true` | Keep RF field on between scans |
| `supply_3v3` | `true` | Must be `true` for 3.3V supply |
| `irq_pin` | — | IRQ GPIO (optional; falls back to register polling) |
| `reset_pin` | — | Hardware reset GPIO (optional) |
| `mifare_key_a` | `FFFFFFFFFFFF` | Mifare Classic Key A (12 hex chars) |
| `mifare_key_b` | `FFFFFFFFFFFF` | Mifare Classic Key B (12 hex chars) |
| `status` | — | Binary sensor: chip health (true = OK) |
| `field_strength` | — | Sensor: RF amplitude ADC reading |

---

## Wiring (ESP32-C6 + Elechouse ST25R3916, SPI mode)

| ST25R3916 | ESP32-C6 | Notes |
|-----------|----------|-------|
| VDD | 3.3V | |
| GND | GND | |
| MOSI | GPIO18 | |
| MISO | GPIO10 | |
| SCK | GPIO19 | |
| CS | GPIO6 | Avoid GPIO9 (strapping pin) |
| IRQ | GPIO7 | |
| I2C_EN | GND | Selects SPI mode |

---

## Troubleshooting

- **No tags detected**: Verify wiring and IRQ pin. Check logs for `ST25R initialized successfully`.
- **Strapping pins (ESP32-C6)**: Avoid GPIO9 for CS — causes boot failures.
- **Mifare Classic auth fails**: If NT never changes between attempts, the card is a clone with broken PRNG. Genuine NXP Mifare Classic 1K cards are required for Crypto1 authentication to succeed.
- **Slow polling**: Reduce `update_interval` (e.g. `250ms`) for faster response.

---

Made with ❤️ for the ESPHome community
