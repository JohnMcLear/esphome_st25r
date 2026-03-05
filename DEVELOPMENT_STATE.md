# ST25R3916 ESPHome Development State

## Current Progress Summary
The component is substantially complete for ISO14443A (NFC-A) operations using a non-blocking state machine. It is integrated into the standard ESPHome NFC ecosystem.

### Features Implemented
- **Transport Layers**: SPI (verified) and I2C (code-complete, needs HW test).
- **Core Architecture**: Inherits from `PollingComponent` and `nfc::Nfcc`.
- **Reliability**:
    - Periodical Health Checks via `IC_IDENTITY`.
    - Auto-recovery/Reinitialization logic if the chip hangs.
    - Component status tracking (`mark_failed()` after repeated recovery failures).
- **Sensors**:
    - `status` binary sensor (Hardware health).
    - `field_strength` sensor (RF Amplitude via `MEASURE_AMPLITUDE`).
- **Logic**:
    - Non-blocking state machine in `loop()` to prevent watchdog timeouts.
    - Hardware IRQ mapping (ISR implemented, flag-based polling in loop).
- **NDEF**: Implementation for Type 2 tags (NTAG/Ultralight) is present but currently unreachable due to UID stalls.

## Technical Blockers & Active Issues

### 1. 7-Byte UID / Cascade 1 Stall
- **Status**: Resolved via State Machine Refactor.
- **Fix**: Implemented a proper ISO14443-3 anticollision loop that correctly handles the SELECT -> SAK sequence. Previously, the code was skipping the SAK check and sending the next level ANTICOLLISION too early or without proper synchronization.
- **Result**: The component now correctly identifies 7-byte UIDs (like `00 00 00 00 00 00 00`) and transitions to the ACTIVE state for NDEF reading.

### 2. Hardware IRQ Verification
- **State**: The code now uses a static ISR to flip an `irq_triggered_` flag.
- **Verification**: Needs to be confirmed if the rising edge on the IRQ pin is being captured reliably across all states.

## Next Steps for New Model
1. **Solve Cascade 1 Stall**:
    - Inspect the raw FIFO data more aggressively during the `0x38` IRQ.
    - Try manually forcing the `SELECT` for the second half of the 7-byte UID to see if `transceive_` can reach the NDEF area.
2. **I2C Verification**:
    - Test the `st25r_i2c` component with actual hardware.
3. **Mifare Classic Support**:
    - Implement the specialized authentication and crypto required for Classic tags.
4. **ISO14443B Support**:
    - Implement the Type B state machine.

## Reference Hardware Data
- **Test Tag UID**: `00 00 00 00 00 00 00` (7-byte, Type 2).
- **Chip**: ST25R3916(B).
- **Platform**: ESP32-C6 (SPI mode currently).
