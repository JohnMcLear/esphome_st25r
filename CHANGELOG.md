# Changelog

All notable changes to the ST25R3916 ESPHome component will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Mifare Classic support**: Crypto1 stream cipher (`crypto1.cpp/h`), 3-pass mutual authentication (`mifare_authenticate_()`), and 16-byte block read (`mifare_read_block_()`) with full parity verification
- **Multi-tag anticollision**: ISO14443A binary tree search — detects all tags in field simultaneously; HALT+WUPA loop resumes tree traversal; per-UID miss-count for reliable removal detection
- **NDEF read**: Type 2 tags (NTAG / Ultralight) — reads URL and text records into Home Assistant
- **I2C transport**: `st25r_i2c` component (code-complete; awaiting hardware verification)
- **Chip health monitor**: IC_IDENTITY check + auto-reinitialization after repeated failures
- **RF field strength sensor**: exposed via `field_strength` sensor (MEASURE_AMPLITUDE)
- **Configurable Mifare keys**: `mifare_key_a` / `mifare_key_b` YAML options
- Test YAML configs moved to `tests/` folder

### Fixed
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
  trailing CRC16 (unlike the ST25R3916 where the same bit also
  strips). Every `with_crc=true` response gets the trailing two
  bytes removed by `strip_trailing_crc()` in `isodep_wtx.h`. The
  helper is gated on `with_crc=true` and the chip-level CRC has
  already validated the frame before we touch the FIFO, so the
  bytes we strip are guaranteed-CRC-good padding. Covered by
  unit tests; flagged here because it's a behaviour the
  ST25R3916 path doesn't need.
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
