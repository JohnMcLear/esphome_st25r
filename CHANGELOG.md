# Changelog

All notable changes to the ST25R3916 ESPHome component will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **`st25r.set_rf_field` / `st25r300.set_rf_field` actions**: turn the RF field on or off at runtime, driving the hardware immediately. Distinct from the existing `rf_field_enabled` option, which only seeds a flag the scan loop consults — setting that false leaves an already-energised antenna drawing current. The new action writes the operation register *and* updates the flag; both halves are required. Primarily for battery and deep-sleep designs, where the reader rather than the MCU dominates draw (ESP32 deep sleep is ~10 µA against tens of mA for an energised field). The ST25R300 overrides the method in C++ because its enable bits live in `REG_OPERATION` (0x00) rather than the ST25R3916's `OP_CONTROL` (0x02). Field-off issues STOP_ALL before cutting the drivers so nothing is mid-transmit, which would otherwise leave FIFO/IRQ state dirty and surface as phantom IRQs on the next field-on. Verified on hardware (ST25R300 / ESP32-C6): field drops before sleep and the reader resumes scanning and reads tags normally on wake. See "Deep sleep and power" in `API_DOCUMENTATION.md`.
- **`suppress_on_tag_for_isodep` flag** (default `false`): opt-in YAML option on the base ST25R schema (and ST25R300) that skips the `on_tag` fire for ISO 14443-4 capable tags (SAK bit 5 set). Application-agnostic — useful for deployments that bridge `on_tag` to `homeassistant.tag_scanned` and want to drop the Android HCE random anticol UID stream from HA's tag log. Passive tag (NTAG/MIFARE/ISO 15693) firing is unchanged.
- **Mifare Classic support**: Crypto1 stream cipher (`crypto1.cpp/h`), 3-pass mutual authentication (`mifare_authenticate_()`), and 16-byte block read (`mifare_read_block_()`) with full parity verification
- **Multi-tag anticollision**: ISO14443A binary tree search — detects all tags in field simultaneously; HALT+WUPA loop resumes tree traversal; per-UID miss-count for reliable removal detection
- **NDEF read**: Type 2 tags (NTAG / Ultralight) — reads URL and text records into Home Assistant
- **I2C transport**: `st25r_i2c` component (code-complete; awaiting hardware verification)
- **Chip health monitor**: IC_IDENTITY check + auto-reinitialization after repeated failures
- **RF field strength sensor**: exposed via `field_strength` sensor (MEASURE_AMPLITUDE)
- **Configurable Mifare keys**: `mifare_key_a` / `mifare_key_b` YAML options
- Test YAML configs moved to `tests/` folder

### Fixed
- **Reader stops reading tags while reporting healthy** (`st25r`, `st25r300`, and every transport): since 9ce68af, `STATE_ANTICOL` took any IRQ as an answer and only consulted its 20 ms timeout when no IRQ was set. The TXE for our own anticollision frame is an IRQ, so a tag that answered WUPA and then did not answer anticollision (pulled away at the edge of the field, or a noise "answer" to WUPA) left the scan waiting for a reply that would never come. On the ST25R300 the IRQ bits stay set until the next frame is sent, so the wait never ended. `update()` skips everything while a scan is in flight, *including the health check*, so the `status` sensor kept its last value (`true`) and the reader read nothing until rebooted. Seen twice in a fortnight on an ST25R300 door reader, each time after several days of uptime. Now anything short of a collision or a full 5-byte answer falls through to the timeout; a full answer is still accepted after the budget, so the 9ce68af fix holds. The decision lives in `scan_step.h` and is unit tested.
- **Scan watchdog** (`st25r`, `st25r300`): as a backstop, a scan that has not returned to idle within 10 s (`kScanWatchdogMs`) is aborted with a warning (`Scan stuck in state N ...`), so a wedge from any future cause costs one scan rather than the reader.
- **ST25R3916 transmit length** (`st25r`, `st25r_spi`, `st25r_i2c`): `transceive_ex()` wrote `len >> 8` into `NUM_TX_BYTES1`, which holds `ntx[12:5]`, so every frame of 32 bytes or more went out 32, 64, ... bytes short, and the no-CRC path programmed a length of 0 for frames under 32 bytes. Now `len >> 5` / `(len & 0x1F) << 3` on both paths (DS12484 4.5.42/43). ISO-DEP APDUs of 31 bytes or more are the visible case.
- **ST25R3916 receive length** (`st25r`, `st25r_spi`, `st25r_i2c`): the FIFO count now includes `fifo_b[9:8]` from `FIFO_STATUS2`, and a response longer than the 64-byte receive buffer fails with a warning instead of coming back truncated and reported as a success. Responses that fit are unchanged.
- **ST25R3916 received CRC** (`st25r`, `st25r_spi`, `st25r_i2c`): the chip checks CRC-A but leaves both bytes in the FIFO (DS12484 2.2.13; there is no `crc_2_fifo` bit as on the ST25R3911/3914). `transceive_ex()` now strips them for with-CRC exchanges, as ST's RFAL does, so `send_apdu()` responses end in SW1 SW2 and the Type 4 NDEF chain gets past SELECT. The Mifare Classic AUTH nonce, which carries no CRC, is kept whole. **Behaviour change:** on ST25R3916 readers every with-CRC response, including `send_apdu()`, is now 2 bytes shorter. Code that compensated for the trailing CRC, or stored values derived from it (for example a SEID string from the README example, which used to end in `9000`), needs updating.
- **Stack overrun on ST25R3916 NTAG reads**: the Type 2 read passed a 16-byte buffer for a reply that is 18 bytes in the FIFO. Callers of `transceive_()` now pass buffers sized for the 64-byte receive limit.
- The ST25R300 components (`st25r300`, `st25r300_spi`) are unaffected by the above: they have their own `transceive_ex()`, which already encoded the length correctly and stripped the CRC.
- **Stack overrun on ST25R300 NTAG reads** (`st25r300`, `st25r300_spi`): the Type 2 READ passed a 16-byte buffer, but `transceive_ex()` copies the whole reply, 16 data plus 2 CRC bytes, before stripping the CRC, so every NTAG read wrote 2 bytes past the end of the stack buffer. The NFC-V inventory, block read and write, the NFC-B ATQB and the DESELECT reply had the same undersized buffers against a 64-byte copy. All now use a 64-byte buffer. No logic change.
- `RESET_RX_GAIN` (0xD5) issued before each transceive to reset AGC/squelch
- Crypto1 parity bits correctly advance LFSR state via `crypto1_bit()` (not `crypto1_filter()`)
- Anticollision prefix bits correctly restored after `read_fifo()` (chip zeros them)
- CL1 collision state saved and restored before/after CL2 cascade anticollision
- WUPA (not REQA) used after HALTing a tag — Mifare Classic returns to HALT, not IDLE

### Planned
- ISO14443B support
- ISO15693 support
- NFC-F (FeliCa) support
- Mifare Classic NDEF (sector traversal)
- Write operations
- Low power / sense mode

### Known limitations (ISO-DEP path)

These apply when an `on_isodep_tag` trigger lambda performs a full
APDU exchange (e.g. an HMAC challenge/response against an Android
HCE service) before the default Type 4 NDEF read chain.

- **Component loop watchdog warning during cold-tap WTX.** The
  `on_isodep_tag` trigger lambda runs synchronously inside the
  driver's `read_tag()` path, including any `send_apdu()` calls.
  An Android HCE cold-start binding produces 1-3 successive
  S(WTX) requests before the I-Block reply lands; the full cold
  round can take 150-300 ms of wall clock. ESPHome's default
  50 ms loop-watchdog logs `Component took a long time for an
  operation (203 ms)` against the ST25R component. Cosmetic — no
  functional regression. Real fix is an async state machine
  (suspend after TX, resume on IRQ); out of scope for this PR.
- **NRT bump on the ST25R300 is phone-dependent.** The
  `transceive_ex()` path bumps NRT to ~309 ms (16-bit max at the
  4.72 µs step) while `isodep_active_` is set, to accommodate
  Android HCE cold-start latency. Verified against a DOOGEE
  Blade10 Ultra (85-150 ms cold, ~30 ms warm). Over-provisioned
  for faster phones (Pixel, Samsung) — slower than necessary but
  correct. Possibly undersized for slower Chinese OEM low-end
  phones that bind HCE in the 200-300 ms range; would need
  bumping further into the coarser NRT step family. A per-protocol
  NRT shadow would let ISO-DEP have its own NRT base without
  disturbing NFC-A anticol — clean refactor target for a future
  iteration.
- **Software CRC strip on the ST25R300 is correctness-critical.**
  The chip's `RX_CRC` register validates but does not strip the
  trailing CRC16. Every `with_crc=true` response gets the trailing
  two bytes removed by `strip_trailing_crc()` in `isodep_wtx.h`. The
  helper is gated on `with_crc=true` and the chip-level CRC has
  already validated the frame before we touch the FIFO, so the
  bytes we strip are guaranteed-CRC-good padding. The ST25R3916
  behaves the same way (an earlier version of this note said it
  strips in hardware; it does not) and is handled by
  `st25r3916_finish_rx()` in `st25r3916_frame.h`.
- **No hardware-in-loop CI for the full APDU exchange.** Host-
  side C++ tests cover the WTX state machine (`isodep_wtx.h`)
  and the CRC strip; the full Android HCE round runs only on
  bench hardware. Software emulators that can speak HCE-style
  APDU with realistic cold-start timing don't exist in any
  maintained form. The C++ helpers are factored so they're
  testable without the chip dependency surface
  (`isodep_process_loop`, `strip_trailing_crc`); anything chip-
  specific is exercised by the bench config.

## [1.0.0] - 2024-02-26

### Added
- Initial release
- Full ISO14443A (NFC-A) support
- Automatic tag detection and UID reading
- Tag presence/removal triggers
- SPI interface support
- Hardware and software reset functionality
- IRQ-based operation
- Comprehensive documentation and examples
- CI/CD pipeline with GitHub Actions
- Basic and advanced example configurations
- Multi-reader support
- Access control example

### Features
- Reads ISO14443A tag UIDs (4, 7, and 10 byte UIDs)
- Configurable polling interval
- On-tag and on-tag-removed callbacks
- Home Assistant integration via events
- Status logging and debugging
- Field on/off control
- FIFO operations
- Register read/write operations
- Full ST25R3916 register map support

### Hardware Support
- ST25R3916 (IC Identity: 0x05)
- ST25R3916B (IC Identity: 0x0A)
- ESP32 (primary target)
- ESP8266 (tested, limited support)

### Configuration Options
- `cs_pin`: SPI chip select pin (required)
- `irq_pin`: Interrupt request pin (required)
- `reset_pin`: Hardware reset pin (optional)
- `update_interval`: Tag polling interval (default: 1s)
- `on_tag`: Tag detection trigger
- `on_tag_removed`: Tag removal trigger

### Documentation
- Comprehensive README with setup instructions
- API reference documentation
- Multiple example configurations
- Troubleshooting guide
- Hardware connection diagrams
- Home Assistant integration examples
- Contributing guidelines

### CI/CD
- Automated compilation tests
- Code linting (Python and C++)
- Format checking
- ESPHome version compatibility tests
- Documentation link checking
- Security scanning
- Automated releases on tags

### Known Limitations
- ISO14443B not yet implemented
- ISO15693 not yet implemented
- NFC-F/V not yet implemented
- Write operations not yet supported
- NDEF parsing not yet implemented
- Sleep mode not yet implemented

## [0.1.0] - 2024-02-20 (Beta)

### Added
- Initial beta release
- Basic ISO14443A tag detection
- UID reading for 4-byte UIDs
- SPI communication
- Simple example configuration

### Known Issues
- 7 and 10 byte UIDs not fully tested
- Limited error handling
- No comprehensive documentation

---

## Version History Summary

- **1.0.0** - Full production release with complete ISO14443A support
- **0.1.0** - Initial beta release with basic functionality

## Upgrade Notes

### Upgrading to 1.0.0 from 0.1.0

No breaking changes. Configuration remains compatible. New features:
- Improved UID reading for all tag types
- Better error handling
- Enhanced documentation
- CI/CD pipeline

## Future Roadmap

### Version 1.1.0 (Planned Q2 2024)
- ISO14443B support
- Improved power management
- Field strength adjustment

### Version 1.2.0 (Planned Q3 2024)
- ISO15693 support
- Write operations
- NDEF parsing

### Version 2.0.0 (Planned Q4 2024)
- NFC-F/V support
- Peer-to-peer mode
- Advanced authentication

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for how to contribute to this project.

## Support

For issues, questions, or feature requests, please use:
- GitHub Issues: https://github.com/yourusername/esphome-st25r3916/issues
- GitHub Discussions: https://github.com/yourusername/esphome-st25r3916/discussions
