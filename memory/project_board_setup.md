---
name: Board and serial port setup
description: Which physical boards are connected to which serial ports
type: project
---

Current hardware setup (as of 2026-03-17):

- /dev/ttyACM0: X-NUCLEO-NFC12A1 (ST25R300), currently on 3.3V only (5V not wired yet)
- /dev/ttyACM1: Vicino board (ST25R3916B, B-variant), 5V supply, `supply_3v3: false`

Vicino board has varicaps (for AAT - Automatic Antenna Tuning).
Elechouse SPI module (non-B, ST25R3916) is a third board.

**Why:** User wired up X-NUCLEO-NFC12A1 and put it on ACM0. Moved Vicino to ACM1.
**How to apply:** Use `--device /dev/ttyACM0` for nucleo, `--device /dev/ttyACM1` for vicino.
