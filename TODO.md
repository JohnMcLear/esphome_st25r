# TODO List

## v1.1 Priorities
- [x] **RF Power Control**: Implement ability to adjust RF output power/field strength to optimize for different antennas and power constraints.
- [x] **Health Check**: Periodically verify chip communication (e.g., via `IC_IDENTITY`) and implement auto-recovery if the hardware hangs.

## Feature & Reliability Parity with PN532
- [x] **Component Status Tracking**: Mark the component as failed/unavailable in ESPHome if the hardware becomes unresponsive.
- [x] **Connectivity Binary Sensor**: Expose a binary sensor to Home Assistant indicating if the reader hardware is healthy.
- [x] **Non-Blocking Logic**: Refactor the `update()` loop into a non-blocking state machine to avoid ESPHome "took too long" warnings.
- [ ] **I2C Verification**: Comprehensive testing of the I2C transport layer with actual hardware.
- [x] **I2C CI Tests**: Add I2C-based compilation tests to the CI workflow.

## Protocol Support
- [ ] **Mifare Classic Support**: Implementation of authentication and sector reading/writing.
- [ ] **NDEF Parsing**: Support for reading NDEF records (URLs, Text, etc.).
- [x] **Multi-Tag Anticollision**: Robust handling when multiple tags are in the field simultaneously.
- [ ] **ISO14443B Support**: Implementation of the Type B protocol.
- [ ] **FeliCa (NFC-F) Support**: Support for FeliCa cards.
- [ ] **ISO15693 (NFC-V) Support**: Support for vicinity cards.

## Advanced Features
- [ ] **Low Power "Sense" Mode**: Use capacitive/inductive wake-up to keep the RF field off until a tag is detected.
- [x] **RSSI Sensor**: Expose tag signal strength as a sensor (implemented as `field_strength`).
- [ ] **Supply Voltage Sensor**: Monitor internal chip voltage levels.
- [ ] **Card Emulation**: Allow the ESP32 to act as an NFC tag.

## Integration
- [x] **NFC Base Class Integration**: Inherit from `esphome::nfc::Nfcc` for standard ESPHome NFC automation compatibility.
- [ ] **Hardware IRQ Mapping**: Move from polling the IRQ pin to true hardware interrupts.
