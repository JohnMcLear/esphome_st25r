# AGENTS.md — Contributor & AI Agent Guide

This file documents how to work in this repository, intended for both human contributors and AI coding agents.

---

## Project Overview

This is an **external ESPHome component** for the ST25R3916 NFC reader chip. It provides ISO14443A (NFC-A) tag detection, UID reading, multi-tag support, and tag presence/removal triggers for Home Assistant integration.

**Component locations:**
- `components/st25r/` — abstract base (C++ protocol logic, ISO14443A state machine, triggers, sensors)
- `components/st25r_spi/` — SPI transport variant
- `components/st25r_i2c/` — I2C transport variant

---

## Repository Structure

```
components/
  st25r/             Base component (st25r.h/cpp, __init__.py, binary_sensor.py)
  st25r_spi/         SPI transport (st25r_spi.h/cpp, __init__.py)
  st25r_i2c/         I2C transport (st25r_i2c.h/cpp, __init__.py)
docs/
  st25r3916.pdf      Datasheet DS12484 Rev 8 (primary reference)
memory/
  multitag_anticol.md   Multi-tag anticollision algorithm — bugs found, fixes applied
  datasheet_notes.md    Register map and SPI/I2C protocol details
.github/workflows/
  compile.yml        Compiles ci-test-spi.yaml and ci-test-i2c.yaml via ESPHome
ci-test-spi.yaml     CI firmware config for SPI
ci-test-i2c.yaml     CI firmware config for I2C
local-test.yaml      Local dev firmware config (ESP32-C6 + Elechouse ST25R3916 module)
```

---

## memory/ Folder

The `memory/` folder contains **hard-won technical knowledge** that is not derivable from reading the code alone. Always read relevant files here before making changes to the protocol or register configuration.

| File | Contents |
|---|---|
| `memory/multitag_anticol.md` | Complete multi-tag anticollision algorithm; ST25R3916-specific FIFO behaviour; Mifare Classic quirks; cascade (7-byte UID) state save/restore; NVB encoding |
| `memory/datasheet_notes.md` | Verified register map, SPI protocol, IRQ bit definitions, direct command table |

---

## Key Configuration Options

| YAML key | Default | Description |
|---|---|---|
| `update_interval` | `1s` | Tag polling rate |
| `rf_power` | `15` | TX driver power 0–15 (15 = max) |
| `rf_field_enabled` | `true` | Enable RF field on startup |
| `supply_3v3` | `true` | Set for 3.3 V supply (IO_CONF2 sup3V bit) |
| `status` | — | Binary sensor: chip health |
| `field_strength` | — | Sensor: raw amplitude ADC value |
| `irq_pin` | — | Optional; if omitted, polls IRQ_MAIN register |
| `reset_pin` | — | Optional hardware reset |

---

## Development Conventions

### C++ (component code)
- Follow the [ESPHome C++ style guide](https://esphome.io/guides/contributing.html).
- Use `esphome::` namespace conventions; component classes live in `namespace esphome { namespace st25r { ... } }`.
- Do not use `delay()` in the main scan loop — use `millis()`-based non-blocking state machines in `loop()`. Short `delay()` calls are acceptable inside `reset_()` and one-shot operations (HALT frame transmission, field-on stabilisation).
- Use `ESP_LOGD` / `ESP_LOGI` / `ESP_LOGW` / `ESP_LOGE` for logging. Do not leave noisy per-frame `LOGD` traces in merged code.
- Pure virtual transport methods (`read_register`, `write_register`, `write_command`, `write_fifo`, `read_fifo`) must be implemented by each transport subclass — never call them from the base class constructor.

### Python (ESPHome config schema)
- Schema files are `__init__.py` under each component folder.
- Follow PEP 8 style.
- New config options must have sensible defaults so existing YAML configs continue to work.

---

## Building & Testing

### Compile check (CI)

```bash
esphome compile ci-test-spi.yaml
esphome compile ci-test-i2c.yaml
```

Both must compile without errors before merging.

### Flash and monitor local hardware

```bash
esphome run local-test.yaml
esphome logs local-test.yaml
```

### Python lint

```bash
black --check components/
pylint components/
```

There is no automated test runner; CI is compile-only validation. See [`TESTING.md`](TESTING.md) for hardware validation procedures.

---

## Hardware (ESP32-C6 + Elechouse ST25R3916 module)

- SPI pins: CLK=GPIO19, MISO=GPIO10, MOSI=GPIO18, CS=GPIO6, IRQ=GPIO7
- Avoid GPIO9 for CS on ESP32-C6 (strapping pin)
- I2C slave address: `0x50`
- I2C_EN pin: GND = SPI mode, VDD_D = I2C mode

---

## Making Changes

### Adding a feature or bug fix
1. Read `memory/datasheet_notes.md` and `memory/multitag_anticol.md` before touching protocol code.
2. Edit C++ sources under `components/st25r/` (and/or `st25r_spi/`, `st25r_i2c/`).
3. Update the Python schema (`__init__.py`) if new YAML config options are added.
4. Update `README.md` for any new config variables.
5. If behaviour changes, update `TESTING.md` accordingly.
6. Compile both CI YAML configs and confirm zero errors.

### Modifying the anticollision state machine
Read `memory/multitag_anticol.md` first. Key invariants that must be preserved:
- `RX_CONF3 = 0xE2` must be written in `update()` before WUPA (not in `reset_()`)
- Use WUPA (not REQA) after HALTing a tag — Mifare Classic returns to HALT, not IDLE
- Send WUPA before each new anticol prefix branch (20 ms timeout per branch)
- OR saved prefix bits back into FIFO data after `read_fifo()` for UID reconstruction
- Save CL1 collision state before entering CL2 for cascade (7-byte UID) tags

### Modifying CI configs (`ci-test-*.yaml`)
- Minimal firmware configs for compile validation only.
- Do not add secrets or real pin assignments that break in headless CI.
- Real pin assignments live in `local-test.yaml`.

---

## Known Issues

- **SPI Mode wrong in `st25r_spi.h`**: should be `CLOCK_PHASE_TRAILING` (Mode 1), currently `CLOCK_PHASE_LEADING` — works in practice due to signal timing margins.
- **IC identity check**: `(ic_identity >> 3) != 0x05` should be `(ic_identity & 0xF8) != 0x28`.
- **Space B register access broken**: `write_register()` masks `addr & 0x3F`, so registers 0x40–0x7F cannot be written via normal path.
- **Mifare Classic NDEF**: authentication not yet implemented; tags are detected by UID only.
- **ISO14443B / FeliCa**: not supported.

---

## Useful References

- [ESPHome external components guide](https://esphome.io/components/external_components.html)
- [ESPHome contributing guide](https://esphome.io/guides/contributing.html)
- ST25R3916 datasheet: `docs/st25r3916.pdf` (DS12484 Rev 8)
- Architecture pattern reference: [esphome_pn532](https://github.com/JohnMcLear/esphome_pn532)
