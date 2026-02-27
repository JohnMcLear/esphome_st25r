# TODO: Feature & Reliability Parity with PN532

This list tracks the features and functionality required to bring the ST25R3916 component to parity with robust implementations like the PN532 component.

## Health Checks & Reliability
- [ ] **Heartbeat Health Check**: Periodically verify chip communication by reading the `IC_IDENTITY` register.
- [ ] **Component Status Tracking**: Mark the component as failed/unavailable if the hardware becomes unresponsive.
- [ ] **Auto-Recovery**: Automatically trigger a hardware reset and re-initialization sequence on communication failure.
- [ ] **Connectivity Binary Sensor**: Expose a binary sensor to Home Assistant indicating if the reader hardware is healthy.
- [ ] **Non-Blocking Logic**: Refactor the `update()` loop into a non-blocking state machine to avoid ESPHome "took too long" warnings.

## Protocol Support
- [ ] **Mifare Classic Support**: Implementation of authentication and sector reading/writing.
- [ ] **NDEF Parsing**: Support for reading NDEF records (URLs, Text, etc.).
- [ ] **Multi-Tag Anticollision**: Robust handling when multiple tags are in the field simultaneously.
- [ ] **ISO14443B Support**: Implementation of the Type B protocol.
- [ ] **FeliCa (NFC-F) Support**: Support for FeliCa cards.
- [ ] **ISO15693 (NFC-V) Support**: Support for vicinity cards.

## Advanced Features
- [ ] **Low Power "Sense" Mode**: Use capacitive/inductive wake-up to keep the RF field off until a tag is detected.
- [ ] **RSSI Sensor**: Expose tag signal strength as a sensor.
- [ ] **Supply Voltage Sensor**: Monitor internal chip voltage levels.
- [ ] **Card Emulation**: Allow the ESP32 to act as an NFC tag.

## Integration
- [ ] **NFC Base Class Integration**: Inherit from `esphome::nfc::NFCComponent` for standard ESPHome NFC automation compatibility.
- [ ] **Hardware IRQ Mapping**: Move from polling the IRQ pin to true hardware interrupts.
