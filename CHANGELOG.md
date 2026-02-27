# Changelog

All notable changes to the ST25R3916 ESPHome component will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned
- ISO14443B support
- ISO15693 support
- NFC-F (FeliCa) support
- NFC-V support
- NDEF message parsing
- Write operations
- Peer-to-peer mode
- Low power / sleep modes
- Adjustable field strength

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
