---
name: Board and serial port setup
description: Which physical boards are connected to which serial ports
type: project
---

Current hardware setup (as of 2026-03-18):

- /dev/ttyACM0: X-NUCLEO-NFC12A1 (ST25R300), **5V supplied and working**, IC identity 0xB1
- /dev/ttyACM1: Vicino board (ST25R3916B, B-variant), 5V supply, `supply_3v3: false`

Vicino board has varicaps (for AAT - Automatic Antenna Tuning).
Elechouse SPI module (non-B, ST25R3916) is a third board (no fixed port).

**Why:** User wired up X-NUCLEO-NFC12A1 on ACM0. Vicino on ACM1.
**How to apply:** Use `--device /dev/ttyACM0` for nucleo, `--device /dev/ttyACM1` for vicino.

## Test configs
- `tests/test-nucleo-nfc12a1-spi-c6.yaml` → ACM0, components: st25r300 + st25r300_spi
- `tests/local-test.yaml` → Elechouse module (st25r + st25r_spi, ST25R3916)
- `tests/test-steval-mb17149-b-c6.yaml` → Vicino board (ST25R3916B)

## X-NUCLEO-NFC12A1 Wiring (ESP32-C6)
CLK=GPIO19, MISO=GPIO10, MOSI=GPIO18, CS=GPIO6, IRQ=GPIO7, RESET=GPIO8 (strapping pin, use with care)
5V to VIN on the Nucleo shield — chip requires 5V for full TX power.
IC identity: 0xB1 = ST25R300 rev 1.

## Working Status (2026-03-18)
X-NUCLEO-NFC12A1 (ST25R300): ISO14443A tag detection WORKING.
- NTAG 7-byte UID 04DC1F4A113C80 detected every 1s cycle
- on_tag / on_tag_removed triggers working, GPIO2 LED confirmed
- Two init fixes required vs naive defaults — see st25r300_init_sequence.md
