---
name: ST25R Variant Comparison
description: Comprehensive comparison of all ST25R NFC reader variants, including features, registers, and migration notes
type: reference
---

# ST25R Variant Comparison Guide

This document provides a technical comparison of the ST25R family of NFC/HF RFID reader ICs.

## Architectural Branches

The ST25R family is divided into four distinct architectural branches based on their register maps and communication protocols.

| Branch | Variants | Interface | Protocol Type | Key Characteristic |
|--------|----------|-----------|---------------|-------------------|
| **Legacy** | ST25R3911B/14/16/16B/17/19B | SPI / I2C | Register-mapped | Address 0x00 = IO Configuration 1 |
| **Unified** | ST25R100/200/300/500/501 | SPI / I2C | Register-mapped | Address 0x00 = Operation Register |
| **Command** | ST25R95 (CR95HF legacy) | SPI | Command-Response | No direct register map; uses IDs like `0x02` (ProtocolSelect) |
| **NCI** | ST25RN300 | I2C | NCI 2.x | Standard NFC Controller Interface (NCI) compliant |

## Unified Architecture Variants

The "Unified" family (R-series) uses a shared register philosophy but has two distinct sub-layouts.

### 1. Consumer Series (R100, R200)
- **Target**: Low-power, consumer electronics.
- **Register Map**:
    - 0x00: Operation
    - 0x01: General Configuration
    - 0x02: Regulator
    - 0x03: TX Driver
    - 0x04: TX Modulation 1
    - 0x05: TX Modulation 2
    - **0x06**: RX Analog Settings 1 (Starts here)
- **Unique Traits**: Minimalistic, lacks dedicated CE modulation registers at 0x06/0x07.

### 2. Performance/Automotive Series (R300, R500, R501)
- **Target**: Payment (EMVCo), Automotive (CCC), High-power.
- **Register Map**:
    - 0x00: Operation
    - 0x01: General Configuration
    - 0x02: VDD_DR Regulator (Higher precision)
    - 0x03: TX Driver
    - 0x04: TX Modulation 1
    - 0x05: TX Modulation 2
    - **0x06**: CE Modulation 1 (Unique)
    - **0x07**: CE Modulation 2 (Unique)
    - **0x08**: GPIO Control (Unique)
    - **0x09**: RX Analog Settings 1 (Starts here)
- **Unique Traits**: Higher TX power (up to 2.2W on R500), Active Wave Shaping (AWS), Dynamic Power Output (DPO).

## Feature Matrix Summary

| Feature | Legacy (3916) | Unified (R200) | Unified (R300/500) | Command (R95) |
|---------|---------------|----------------|--------------------|---------------|
| **FIFO Size** | 96 Bytes | 256 Bytes | 256 Bytes | 528 Bytes |
| **Max Bitrate** | 848 kbit/s | 106 kbit/s | 848 kbit/s | 106 kbit/s |
| **Supply Voltage** | 2.4 - 5.5V | 2.7 - 5.5V | 2.7 - 6.0V (R300) | 2.7 - 5.5V |
| **AWS** | ✓ | ✓ | ✓ (Advanced) | ✗ |
| **DPO** | ✓ | ✓ | ✓ (Advanced) | ✗ |
| **NSR** | ✓ | ✓ | ✓ | ✗ |
| **AAT** | ✓ | ✓ | ✓ | ✗ |
| **CE Mode** | ✓ (T4T) | ✗ | ✓ (T3T/T4T) | ✓ (T4T) |

## Key Documents by Variant

| Variants | Datasheet | Application & Migration Notes |
|----------|-----------|-------------------------------|
| ST25R3911B | DS11793 | AN4974 |
| ST25R3916/17 | DS12484 | AN5240, AN5276, AN5320, AN5322, AN5584 |
| ST25R39xxB | DS13541 | AN5768 (migration), AN5320, AN5322, AN5896 |
| ST25R3914/15 | DS11837 | AN5240, AN5276, AN5320 |
| ST25R200 | DS13658 | AN5965 (migration), AN5984, AN5993, AN6065 |
| ST25R100 | DS14139 | AN5965, AN5993, AN6065 |
| ST25R500 / 300 | DS14593 / DS14655 | AN6313, AN6279, AN6298, AN6092, UM3536 (GUI) |
| ST25R501 | DS14983 | AN6279, AN6298 |
| ST25R95 | DS12807 | AN5248, AN6143 (migration) |
| ST25RN300 | DB5606 | UM3585 |

## Migration Notes (Legacy to Unified)

When migrating from ST25R3916 to ST25R300/500:
1.  **Register 0x00**: Change from `IO Configuration 1` to `Operation`.
2.  **SPI Commands**: Commands like `Set Default` changed from `0xC1` to `0x60`.
3.  **Bits 6 & 5 Rule**: For the Unified architecture, any SPI byte with bits [6:5] = `11` is treated as a command or special mode, not a register address.
4.  **Interrupts**: The IRQ status register locations and bit definitions have changed significantly. Always check the specific IRQ Mask and Status registers (typically starting at 0x39+).
