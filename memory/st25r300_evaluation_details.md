---
name: ST25R300 Evaluation and Configuration Details
description: Technical insights and configuration patterns for ST25R300 based on the STEVAL-25R300KA GUI (UM3536)
type: reference
---

# ST25R300 Evaluation and Configuration Details

This document summarizes key technical insights and configuration patterns for the ST25R300 NFC reader, derived from the ST25R300 evaluation board GUI (UM3536).

## Evaluation Hardware
- **Kit**: STEVAL-25R300KA
- **Main Board**: STEVAL-M25B1 (STM32G0 based)
- **Expansion Board**: STEVAL-25R300A (contains the ST25R300 chip and antenna)

## Key Software Features (GUI-guided)

### 1. DPO CR (Dynamic Power Output - Current Regulation)
DPO CR is a software-based feature to optimize power consumption and range by regulating the transmitter current.
- **Goal**: Maintain $H_{MIN}$ at far distance (~4cm) while staying below $H_{MAX}$ at close distance.
- **Mechanism**: Adjusts driver resistance (`dres`) and regulator voltage (`rege`) based on internal current measurements.
- **Configurable Parameters**:
    - **Target current**: The desired current level.
    - **Threshold**: Hysteresis to avoid oscillation.
    - **rege min/max**: Limits for the internal regulator (min 3.3V recommended for measurement accuracy).
    - **dres min/max**: Limits for transmitter driver resistance.
- **AWS Integration**: DPO CR levels can be linked to different AWS (Active Wave Shaping) settings.

### 2. DLMA (Dynamic Load Modulation Amplitude)
Configures the load modulation amplitude during card emulation based on the perceived field strength from an external reader.
- **Visualization**: The GUI shows `CEM_RES` values mapped to field strength levels.
- **Usage**: Ensures reliable card emulation across different distances and reader field strengths.

### 3. AWS (Active Wave Shaping)
Controls the rising and falling edges of the modulated signal to improve signal integrity and EMVCo compliance.
- **Configuration**:
    - Fine-tune undershoot/overshoot patterns.
    - Select modulation index for each DPO level.
    - Managed via `DPO_A_106_Level0` to `Level3` (and similar for Mode B) in the Analog Config.

### 4. Low Power Wake-up (LPCD)
The ST25R300 supports inductive wake-up using amplitude and phase measurements of the I/Q channels.
- **Configuration**:
    - **Thresholds**: Blue lines in GUI indicate upper/lower thresholds for I and Q.
    - **Timer period**: 9.7ms to 1737ms.
    - **Auto Averaging**: Helps filter noise and slow environmental changes.

### 5. Card Emulation Modes
The GUI confirms support for:
- **NFC-A**: Type 4 Tag (T4T) emulation.
- **NFC-F**: Type 3 Tag (T3T) emulation.
- Includes NDEF record configuration (URI, Text, Image) within the GUI.

### 6. Analog Configuration (RFAL Patterns)
The ST25R300 uses the standard RFAL "Analog Config" pattern (from UM2890) for technology-specific register settings.
- **Common IDs**: `CHIP_INIT`, `POLL_COMMON`, `POLL_A_106`, `POLL_B_106`, `POLL_V_26`, `WAKEUP_ON`, `WAKEUP_OFF`.
- **DPO Extensions**: Specific IDs for DPO levels (e.g., `DPO_A_106_Level0`).

## Technical Constraints & Notes
- **FIFO Size**: 256 bytes (Confirmed by GUI and datasheet).
- **Interface**: SPI only (up to 10 Mbps).
- **Supply Voltage**: Supports up to 6.0V, allowing for higher RF output power compared to 5.5V variants.
- **TruST25 Link**: A feature for offline NFC tag identification mentioned in the ST25R300 context.

## Register Map Insights
The GUI includes a "Register Map" view (0x00 to 0x13 shown in screenshots) which matches the ST25R500/ST25R200 family structure rather than the legacy ST25R3911/16 structure.

| Address | Name | Key Bits / Function |
|---------|------|----------------------|
| 0x00 | Operation | Mode control |
| 0x02 | Regulator | Internal regulator settings (`rege`) |
| 0x03 | TX Driver Config | Driver resistance (`dres`) |
| 0x08 | GPIO Control | GPIO multiplexing |
| 0x0D | RX Digital | Receiver gain and filtering |
