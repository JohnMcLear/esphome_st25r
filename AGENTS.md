# AGENTS.md — Contributor & AI Agent Guide

This file documents how to work in this repository, intended for both human contributors and AI coding agents.

---

## Project Overview

This is an **external ESPHome component** for the ST25R3916 NFC reader chip. It provides ISO14443A (NFC-A) tag detection, UID reading, multi-tag anticollision, Mifare Classic authentication and block read, and tag presence/removal triggers for Home Assistant integration.

**Component locations:**
- `components/st25r/` — abstract base (C++ protocol logic, ISO14443A state machine, triggers, sensors)
- `components/st25r_spi/` — SPI transport variant
- `components/st25r_i2c/` — I2C transport variant

---

## Repository Structure

```
components/
  st25r/             Base component (st25r.h/cpp, crypto1.h/cpp, __init__.py, binary_sensor.py)
  st25r_spi/         SPI transport (st25r_spi.h/cpp, __init__.py)
  st25r_i2c/         I2C transport (st25r_i2c.h/cpp, __init__.py)
docs/
  fetch_datasheets.sh    Script: downloads all IC datasheets + board briefs from st.com
  st25r95.pdf            ST25R95 datasheet, legacy reader          (fetch_datasheets.sh)
  st25r3911b.pdf         ST25R3911B datasheet, 1.6 W               (fetch_datasheets.sh)
  st25r3916.pdf          ST25R3916 datasheet DS12484 Rev 8, 1.6 W  (primary reference)
  st25r3916b.pdf         ST25R3916B datasheet, 1.6 W               (fetch_datasheets.sh)
  st25r3917b.pdf         ST25R3917B datasheet, 1.6 W reduced       (fetch_datasheets.sh)
  st25r3919.pdf          ST25R3919 datasheet, 1.6 W automotive     (fetch_datasheets.sh)
  st25r3918.pdf          ST25R3918 datasheet, 2.2 W                (fetch_datasheets.sh)
  st25r3918b.pdf         ST25R3918B datasheet, 2.2 W               (fetch_datasheets.sh)
  st25r3920.pdf          ST25R3920 datasheet, 2.2 W automotive     (fetch_datasheets.sh)
  st25r100.pdf           ST25R100 datasheet, low-power reader      (fetch_datasheets.sh)
  st25r200.pdf           ST25R200 datasheet, low-power reader      (fetch_datasheets.sh)
  st25r300.pdf           ST25R300 datasheet, high-perf reader      (fetch_datasheets.sh)
  st25rn300.pdf          ST25RN300 datasheet, NFC-only variant     (fetch_datasheets.sh)
  st25r500.pdf           ST25R500 datasheet, high-perf reader      (fetch_datasheets.sh)
  boards/
    x-nucleo-nfc03a1.pdf   Data brief: ST25R95 eval shield         (fetch_datasheets.sh)
    x-nucleo-nfc05a1.pdf   Data brief: ST25R3911B eval shield      (fetch_datasheets.sh)
    x-nucleo-nfc06a1.pdf   Data brief: ST25R3916 eval shield       (fetch_datasheets.sh)
    x-nucleo-nfc08a1.pdf   Data brief: ST25R3916B/ST25R3918 shield (fetch_datasheets.sh)
    x-nucleo-nfc09a1.pdf   Data brief: ST25R3918 2.2 W shield      (fetch_datasheets.sh)
    x-nucleo-nfc12a1.pdf   Data brief: ST25R500/ST25R300 shield    (fetch_datasheets.sh)
memory/
  multitag_anticol.md   Multi-tag anticollision algorithm — bugs found, fixes applied
  datasheet_notes.md    Register map and SPI/I2C protocol details
.github/workflows/
  compile.yml        Compiles tests/ci-test-spi.yaml and tests/ci-test-i2c.yaml via ESPHome
tests/
  ci-test-spi.yaml   CI firmware config for SPI (Arduino, esp32dev, compile-only)
  ci-test-i2c.yaml   CI firmware config for I2C (Arduino, esp32dev, compile-only)
  local-test.yaml    Local dev firmware config (ESP32-C6 + Elechouse ST25R3916 module, SPI)
  test-vicino-i2c-c6.yaml  Local dev I2C config (ESP32-C6, SDA=GPIO10, SCL=GPIO19)
  test-*.yaml        Other local hardware test configs
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
esphome compile tests/ci-test-spi.yaml
esphome compile tests/ci-test-i2c.yaml
```

Both must compile without errors before merging.

### Flash and monitor local hardware

```bash
esphome run tests/local-test.yaml
esphome logs tests/local-test.yaml
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

## Protocol Support

| Protocol | Status | Notes |
|---|---|---|
| ISO14443A (NFC-A) | **Working** | UID detection, multi-tag, anticollision |
| Mifare Classic auth | **Working** | Crypto1 3-pass auth; see below for clone card caveat |
| Mifare Classic block read | **Working** | 16-byte block read with parity verification |
| Mifare Classic NDEF | Not started | Would need sector/block traversal on top of auth |
| ISO14443B (NFC-B) | Not implemented | ST25R3916 supports it; MODE register value differs |
| ISO15693 (NFC-V) | Not implemented | ST25R3916 supports it; requires different MODE + protocol |
| FeliCa (NFC-F) | Not implemented | ST25R3916 supports it |

---

## Mifare Classic Implementation Notes

### Crypto1 authentication (`mifare_authenticate_()`)

3-pass mutual authentication flow:
1. Send AUTH1 (`0x60`/`0x61` + block) — tag responds with NT (4-byte nonce)
2. Compute NR+AR with Crypto1 (`crypto1_init`, prime with `NT ^ UID`, then `crypto1_byte`/`crypto1_bit` for 8 bytes)
3. Send NR+AR via `transceive_mifare_()` — tag responds with AT
4. Verify AT = `prng_successor(AR_plain, 32) ^ crypto1_word(cs, 0, 0)`

**Critical: parity bits advance Crypto1 LFSR state.** Use `crypto1_bit(cs, 0, 0)` (1-bit advance) for each parity byte — NOT `crypto1_filter(cs->odd)` (which reads output without advancing). This applies in both TX (NR/AR encoding) and RX (block data decryption) paths.

**AR computation:** `prng_successor(NT, 64)` — advance tag PRNG by 64 steps, then encode MSB-first over 4 bytes.

### Clone card behaviour

Cards where NT never changes (e.g. DEA30D00 always returns NT=0x009080A2) are clone/magic cards with broken PRNG. These respond to AUTH1 with the static NT, but silently HALT on receiving NR+AR. The implementation is cryptographically correct — clone cards simply do not complete authentication. Genuine NXP Mifare Classic 1K generates a random NT each time.

### `transceive_mifare_()` — 9-bit parity mode

Set `ISO14443A_CONF = 0xC0` (`no_tx_par | no_rx_par`) before each Mifare transceive so the chip passes raw bits without inserting/checking hardware parity. Host software packs/unpacks 9-bit frames (8 data + 1 parity per byte) into the FIFO manually.

Send `ST25R_CMD_RESET_RX_GAIN` (0xD5) before each transceive (both `transceive_ex_` and `transceive_mifare_`) to reset AGC/squelch and ensure clean reception.

---

## Known Issues

- **SPI Mode wrong in `st25r_spi.h`**: should be `CLOCK_PHASE_TRAILING` (Mode 1), currently `CLOCK_PHASE_LEADING` — works in practice due to signal timing margins.
- **IC identity check**: `(ic_identity >> 3) != 0x05` should be `(ic_identity & 0xF8) != 0x28`.
- **Space B register access broken**: `write_register()` masks `addr & 0x3F`, so registers 0x40–0x7F cannot be written via normal path. CORR_CONF1/2 left at factory defaults.

---

## Useful References

- [ESPHome external components guide](https://esphome.io/components/external_components.html)
- [ESPHome contributing guide](https://esphome.io/guides/contributing.html)
- ST25R datasheets — run `docs/fetch_datasheets.sh` to download all into `docs/`:
  - **Legacy**: ST25R95
  - **ST25R39xx 1.6 W**: ST25R3911B · ST25R3916 (`docs/st25r3916.pdf`, DS12484 Rev 8) · ST25R3916B · ST25R3917B · ST25R3919
  - **ST25R39xx 2.2 W**: ST25R3918 · ST25R3918B · ST25R3920
  - **ST25Rxxx low-power**: ST25R100 · ST25R200
  - **ST25Rxxx high-perf**: ST25R300 · ST25RN300 · ST25R500
  - **X-NUCLEO eval boards** (`docs/boards/`): NFC03A1 · NFC05A1 · NFC06A1 · NFC08A1 · NFC09A1 · NFC12A1
- Architecture pattern reference: [esphome_pn532](https://github.com/JohnMcLear/esphome_pn532)
