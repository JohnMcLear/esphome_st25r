# TODO

## In Progress
- [ ] **ST25R300 hardware validation** — component compiles clean; needs X-NUCLEO-NFC12A1 board to verify init sequence, tag detection, and NDEF read

## Protocol
- [x] **ISO-DEP / NFC Type 4 Tag (ISO 14443-4)** — Android/Apple Wallet pass support: RATS activation, APDU exchange, NFC Forum T4T NDEF read; stable NDEF-derived token used as tracking key for phones with random UIDs (issue #27)
- [ ] **ISO14443B** — ST25R3916 supports it; needs MODE register change and Type B state machine
- [ ] **ISO15693 (NFC-V)** — ST25R3916 supports it; needs MODE change and ISO15693 protocol layer
- [ ] **FeliCa (NFC-F)** — ST25R3916 supports it; needs NFC-F protocol layer
- [ ] **Mifare Classic NDEF** — sector/block traversal on top of existing Crypto1 auth to read NDEF from Mifare Classic formatted cards

## Features
- [ ] **Low power wake-up** — use ST25R3916 capacitive/inductive sense to keep RF field off until a tag is detected
- [ ] **Card emulation** — allow ESP32 to act as an NFC tag (requires significant driver work)

## Done
- [x] ISO14443A multi-tag detection (anticollision, Cascade Level 1–3)
- [x] Mifare Classic Crypto1 3-pass auth and block read
- [x] NDEF read/write for Type 2 tags (NTAG / Ultralight)
- [x] SPI and I2C transport for ST25R3916
- [x] ST25R300 component skeleton (st25r300 + st25r300_spi)
- [x] Non-blocking state machine (no ESPHome watchdog warnings)
- [x] Chip health monitor with auto-recovery and `mark_failed`
- [x] Hardware IRQ with register-poll fallback
- [x] RF field strength sensor (`field_strength`)
- [x] Binary sensor platform for per-UID tracking
- [x] `nfc::Nfcc` base class integration (standard ESPHome NFC automation)
