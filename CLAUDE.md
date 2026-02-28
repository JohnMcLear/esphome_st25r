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
- `update()` runs every polling interval: health check → field strength → WUPA command → transitions to `STATE_WUPA`
- `loop()` drives the non-blocking state machine: `STATE_IDLE → STATE_WUPA → STATE_READ_UID → STATE_REINITIALIZING`
- Pure virtual transport methods that subclasses must implement: `read_register()`, `write_register()`, `write_command()`, `write_fifo()`, `read_fifo()`
- Tag removal uses a `missed_updates_` counter (3 consecutive misses = tag removed)
- Health check uses `health_check_failures_` counter (3 failures = `STATE_REINITIALIZING`)

**SPI subclass (`st25r_spi::ST25RSpi`)** — inherits `ST25R` and `spi::SPIDevice`:
- SPI mode: currently `CLOCK_PHASE_LEADING` (CPHA=0, Mode 0) — **datasheet requires Mode 1 (CLOCK_PHASE_TRAILING)**
- SPI command byte encoding: `0x40|reg` = read, `0x00|reg` = write, `0x80` = FIFO load, `0x9F` = FIFO read, `0xC0|cmd` = direct command

**Python schema (`__init__.py` files)**:
- `components/st25r/__init__.py` — defines `ST25R_SCHEMA` and `setup_st25r()` shared by both transports
- `components/st25r_spi/__init__.py` — extends `ST25R_SCHEMA` with SPI device schema
- `components/st25r/binary_sensor.py` — `binary_sensor` platform for tracking specific UIDs

## Key Configuration Options

| YAML key | Default | Description |
|---|---|---|
| `update_interval` | `1s` | Tag polling rate |
| `rf_power` | `15` | TX driver power 0–15 (15 = max) |
| `rf_field_enabled` | `true` | Enable RF field on startup |
| `status` | — | Binary sensor: chip health |
| `field_strength` | — | Sensor: raw amplitude ADC value |
| `irq_pin` | — | Optional; if omitted, polls IRQ_MAIN register |
| `reset_pin` | — | Optional hardware reset |

## Hardware Notes (ESP32-C6 + Elechouse ST25R3916 module)

- SPI pins: CLK=GPIO19, MISO=GPIO10, MOSI=GPIO18, CS=GPIO6, IRQ=GPIO7
- Avoid GPIO9 for CS on ESP32-C6 (strapping pin)
- I2C slave address: `0x50`
- I2C_EN pin: GND = SPI mode, VDD_D = I2C mode

## Known Bugs (from datasheet DS12484 Rev 8)

See `memory/MEMORY.md` for the full list. Critical issues still present in `st25r_spi/st25r_spi.h`:
1. **SPI mode wrong**: should be `CLOCK_PHASE_TRAILING` (Mode 1), not `CLOCK_PHASE_LEADING`
2. **IC identity check** in `st25r.cpp:45`: should be `(ic_identity & 0xF8) == 0x28`, not `(ic_identity >> 3) != 0x05`
3. **CLEAR_FIFO command** (`ST25R_CMD_CLEAR_FIFO` = `0xC3`): datasheet says `0xDB`; `0xC3` is "Stop all activities"

## Reference

- Datasheet: `docs/st25r3916.pdf` (DS12484 Rev 8)
- Register map and SPI protocol details: `memory/datasheet_notes.md`
- Pattern reference: `/home/jose/esphome_pn532`
