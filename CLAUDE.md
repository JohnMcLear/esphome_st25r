# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test Commands

Compile the SPI test config (targets ESP32, Arduino framework — used for CI):
```bash
esphome compile ci-test-spi.yaml
esphome compile ci-test-i2c.yaml
```

Compile the local hardware test (targets ESP32-C6, ESP-IDF framework):
```bash
esphome compile local-test.yaml
```

Flash and monitor the local hardware:
```bash
esphome run local-test.yaml
esphome logs local-test.yaml
```

Python lint (run from project root):
```bash
black --check components/
pylint components/
```

There is no automated test runner; CI is compile-only validation.

## Architecture

The component follows a three-layer design modelled on `esphome_pn532`:

```
components/st25r/          ← Abstract base: ISO14443A state machine, triggers, sensors
components/st25r_spi/      ← Concrete SPI transport (inherits ST25R + spi::SPIDevice)
components/st25r_i2c/      ← Concrete I2C transport (inherits ST25R + i2c::I2CDevice)
```

**Base class (`st25r::ST25R`)** — `PollingComponent`, hardware-agnostic:
- `setup()` / `update()` / `loop()` — ESPHome lifecycle
- `update()` runs every polling interval: STOP_ALL → field_on → amplitude measure → WUPA → `STATE_WUPA`
- `loop()` drives the non-blocking state machine: `STATE_IDLE → STATE_WUPA → STATE_ANTICOL → STATE_REINITIALIZING`
- Pure virtual transport methods: `read_register()`, `write_register()`, `write_command()`, `write_fifo()`, `read_fifo()`
- Tag removal: 3 consecutive scan misses = tag removed (`tag_miss_counts_`)
- Health check: 3 failures = `STATE_REINITIALIZING`

**SPI subclass (`st25r_spi::ST25RSpi`)** — inherits `ST25R` and `spi::SPIDevice`:
- SPI mode: `CLOCK_PHASE_TRAILING` (CPHA=1, Mode 1) ✓ — matches datasheet requirement
- SPI byte encoding: `0x40|reg` = read, `0x00|reg` = write, `0x80` = FIFO load, `0x9F` = FIFO read, direct command = raw byte

**Python schema (`__init__.py` files)**:
- `components/st25r/__init__.py` — defines `ST25R_SCHEMA` and `setup_st25r()` shared by both transports
- `components/st25r_spi/__init__.py` — extends `ST25R_SCHEMA` with SPI device schema
- `components/st25r/binary_sensor.py` — `binary_sensor` platform for tracking specific UIDs

## State Machine Flow

```
update():  STOP_ALL → field_on → measure amplitude → STOP_ALL → TRANSMIT_WUPA → STATE_WUPA

STATE_WUPA:   IRQ(RXS+RXE=ATQA received) → STOP_ALL → load FIFO {0x93,0x20} → TRANSMIT_WITHOUT_CRC → STATE_ANTICOL
              timeout(300ms) → finalize_scan → STATE_IDLE

STATE_ANTICOL: IRQ(RXE=UID received) → read 5 bytes from FIFO → transceive SELECT → if SAK&0x04: cascade
               → else: fire on_tag triggers → TRANSMIT_REQA → STATE_WUPA
               timeout(500ms) → finalize_scan → STATE_IDLE
```

**Critical datasheet §4.4.4 rule**: Before `Transmit with/without CRC`, the sequence MUST be:
`Clear FIFO (0xDB)` → set `NUM_TX_BYTES` → write FIFO data → issue transmit command.
(WUPA and REQA are exempt — they don't need Clear FIFO first.)

## Key Configuration Options

| YAML key | Default | Description |
|---|---|---|
| `update_interval` | `1s` | Tag polling rate |
| `rf_power` | `15` | TX driver power 0–15 (15 = max, d_res=0) |
| `rf_field_enabled` | `true` | Enable RF field on startup |
| `status` | — | Binary sensor: chip health |
| `field_strength` | — | Sensor: raw amplitude ADC value (0–255), published each scan |
| `irq_pin` | — | IRQ GPIO (required) |
| `reset_pin` | — | Optional hardware reset |

## Hardware Notes (ESP32-C6 + Elechouse ST25R3916 module)

- SPI pins: CLK=GPIO19, MISO=GPIO10, MOSI=GPIO18, CS=GPIO6, IRQ=GPIO7
- Avoid GPIO9 for CS on ESP32-C6 (strapping pin)
- I2C slave address: `0x50`
- I2C_EN pin: GND = SPI mode, VDD_D = I2C mode
- `supply_3v3: false` in local-test.yaml (5V supply)

## Direct Commands (Table 13, DS12484 Rev 8)

| Code | Name | Notes |
|------|------|-------|
| 0xC0/C1 | Set default | Power-up state, resets all registers |
| 0xC2/C3 | **Stop all activities** | Stops TX/RX/timers, performs Clear FIFO, clears IRQ |
| 0xC4 | Transmit with CRC | Requires: Clear FIFO → NUM_TX_BYTES → FIFO data → this command |
| 0xC5 | Transmit without CRC | Same requirement as above |
| 0xC6 | Transmit REQA | No Clear FIFO needed |
| 0xC7 | Transmit WUPA | No Clear FIFO needed |
| 0xC8 | NFC initial field ON | RF collision avoidance then field on |
| 0xD3 | Measure amplitude | Result in AD_CONV_RESULT (0x25), fires DCT interrupt |
| 0xD6 | Adjust regulators | Optimises internal TX supply voltage; run once after field_on |
| 0xDB | **Clear FIFO** | Clears FIFO data only — does NOT clear IRQ |

**In code enums:**
- `ST25R_CMD_STOP_ALL = 0xC2` ✓
- `ST25R_CMD_CLEAR_FIFO = 0xDB` ✓
- `ST25R_CMD_SET_DEFAULT = 0xC1` ✓

## TX Driver Register (0x28, Table 76-78)

- **bits[7:4] = am_mod**: AM modulation index → **am_mod=5 = 10%** (ISO14443 minimum, maximises carrier power). Currently set.
- **bits[3:0] = d_res**: RFO driver resistance → **d_res=0 = 1.0× (max power)**. Set via `rf_power: 15`.
- Write: `0x50 | d_res` (am_mod=5, 10% modulation)

| am_mod | Modulation % |
|--------|-------------|
| 5 | 10% ← ISO14443 minimum, max carrier power |
| 7 | 12% ← old default |
| 15 | 40% |

## Antenna Tuning Control Registers (0x26 AAT-A, 0x27 AAT-B)

- 8-bit DAC input for antenna impedance matching
- Voltage = (0.044 + 0.868 × value/255) × VDD_A
- Default both = 0x80 (mid-scale)
- Sweeping these changes antenna resonance — measure with Measure Amplitude (0xD3) to find optimum

AAT is available in both the ST25R3916 and ST25R3916B

## RX Configuration (for maximum perpendicular-ring sensitivity)

| Register | Address | Value | Meaning |
|----------|---------|-------|---------|
| RX_CONF1 | 0x0B | 0x08 | Standard ISO14443A RX |
| RX_CONF2 | 0x0C | 0x1F | AGC enabled, full-period, **agc_alg=1** (reset algorithm, recommended for ISO14443A) |
| RX_CONF3 | 0x0D | 0xE2 | **rg1_am=7 (+5.5 dB AM gain boost)**, lf_en=1 |
| RX_CONF4 | 0x0E | 0x00 | Default |

RX_CONF3 rg1_am values: 0=full gain, 1–6=−2.5 dB/step (max −15 dB), **7=+5.5 dB boost**

## Bugs Fixed

All bugs from the original CLAUDE.md are now fixed:
1. ~~SPI mode wrong~~ — `CLOCK_PHASE_TRAILING` (Mode 1) ✓
2. ~~IC identity check~~ — `(ic_identity & 0xF8) == 0x28` ✓
3. ~~CLEAR_FIFO = 0xC3~~ — `ST25R_CMD_CLEAR_FIFO = 0xDB` ✓
4. **STOP_ALL was 0xC0** (undefined) → fixed to `0xC2` ✓
5. **Anticol TX ordering** — FIFO loaded before transmit command ✓ (was inverted)
6. **NUM_TX_BYTES2=0 for no-CRC** — fixed to `(len & 0x1F) << 3` ✓

## Reference

- Datasheet: `docs/st25r3916.pdf` (DS12484 Rev 8)
- Pattern reference: `/home/jose/esphome_pn532`
