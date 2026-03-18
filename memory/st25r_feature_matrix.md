---
name: ST25R Feature Support Matrix
description: Complete feature matrix across all ST25R variants for hardware selection and feature planning
type: reference
---

# ST25R Feature Support Matrix

This document provides a comprehensive feature-by-feature comparison across all ST25R variants.

## Feature Categories

- [RF Communication](#rf-communication)
- [Protocol Support](#protocol-support)
- [Hardware Interfaces](#hardware-interfaces)
- [Power Management](#power-management)
- [Wake-up Features](#wake-up-features)
- [Antenna Features](#antenna-features)
- [Advanced Features](#advanced-features)
- [Physical Characteristics](#physical-characteristics)
- [Environmental](#environmental)

---

## RF Communication

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|----------|----------|---------|-----------|
| **Carrier Frequency** | 13.56 MHz | 13.56 MHz | 13.56 MHz | 13.56 MHz | 13.56 MHz | 13.56 MHz | 13.56 MHz | 13.56 MHz | 13.56 MHz | 13.56 MHz |
| **RF Output Power** | Up to 1.5W | Up to 1.5W | **Up to 1W** | Up to 1W | Up to 1W | Up to 1.5W | Up to 1.5W | Up to 1.5W | ? | **Up to 2.2W** |
| **Modulation Depth** | 5–40% | **0–82%** | 5–40% | 5–40% | 5–40% | 0–82% | 0–82% | 0–82% | ? | ? |
| **ASK Modulation** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **PSK Modulation** | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ | ✓ | ✓ | ? | ✓ |
| **Dynamic Power Output (DPO)** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ✓ |
| **Active Wave Shaping (AWS)** | ✓ | **Enhanced** | ✓ | ✓ | ✗ | ✓ | ✓ | ✓ | ? | ✓ |
| **Noise Suppression Receiver** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ✓ |
| **I/Q Demodulator** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ✓ |
| **AM/PM Demodulator** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ✓ |
| **Automatic Channel Selection** | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ | ✓ | ✓ | ? | ✓ |

---

## Protocol Support

### Reader/Writer Mode

| Protocol | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R95 | ST25RN300 |
|----------|--------------|------------|--------------|----------|----------|----------|----------|----------|---------|-----------|
| **NFC-A (ISO14443A)** | ✓ 848 kbps | ✓ 848 kbps | ✓ 848 kbps | ✓ 106 kbps | ✓ 106 kbps | ✓ 848 kbps | ✓ 848 kbps | ✓ 848 kbps | ✓ | ✓ 848 kbps |
| **NFC-B (ISO14443B)** | ✓ 848 kbps | ✓ 848 kbps | ✓ 848 kbps | ✓ 106 kbps | ✓ 106 kbps | ✓ 848 kbps | ✓ 848 kbps | ✓ 848 kbps | ✓ | ✓ 848 kbps |
| **NFC-F (FeliCa)** | ✓ 424 kbps | ✓ 424 kbps | ✓ 424 kbps | ✗ | ✗ | ✓ 424 kbps | ✓ 424 kbps | ✓ 424 kbps | ✓ | ✓ 424 kbps |
| **NFC-V (ISO15693)** | ✓ 53 kbps | ✓ 53 kbps | ✓ 53 kbps | ✓ 53 kbps | ✓ 53 kbps | **✓ 212 kbps** | **✓ 212 kbps** | **✓ 212 kbps** | ✓ | ✓ |
| **NFC Forum T1T** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **NFC Forum T2T** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **NFC Forum T3T** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **NFC Forum T4T** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **NFC Forum T5T** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **MIFARE Classic** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓* |
| **MIFARE DESFire** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Kovio BC** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ✓ |
| **CTS** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ✓ |
| **B'** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ✓ |

*MIFARE Classic feature availability depends on licensing

### Card Emulation Mode

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|----------|----------|---------|-----------|
| **ISO14443A CE** | ✓ | ✓ | ? | ✗ | ✗ | ✓ | ✓ | ✗ | ✓ | ✓ |
| **ISO14443B CE** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **FeliCa CE** | ✓ | ✓ | ? | ✗ | ✗ | ✓ | ✓ | ✗ | ✗ | ✓ |
| **NFC Forum CE** | ✓ | ✓ | ? | ✗ | ✗ | ✓ | ✓ | ✗ | ✗ | ✓ |
| **Intelligent Card Switching** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |

### Peer-to-Peer Mode

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|----------|----------|---------|-----------|
| **Active P2P Initiator** | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ | ✓ | ✓ | ? | ✓ |
| **Active P2P Target** | ✓ | ✓ | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | ? | ✗ |
| **Passive P2P Initiator** | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ | ✓ (Initiator)| ✓ | ? | ✓ |
| **Passive P2P Target** | ✓ | ✓ | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | ? | ✗ |
| **NFCIP-1** | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ | ✓ | ✓ | ? | ✓ |

### EMVCo Compliance

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|----------|----------|---------|-----------|
| **EMVCo Version** | 3.0 | **3.1a/3.2a** | ? | ? | ? | **3.2a** | **3.2a** | **DK** | ? | **3.2a** |
| **EMD Handling** | Basic | **Enhanced** | ? | Basic | Basic | **Enhanced** | **Enhanced** | **Enhanced** | ? | **Enhanced** |
| **CCC Digital Key** | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ | ✗ | ✓ | ✗ | ✗ |
| **USI WLC Reader** | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✗ | ✗ | ✗ |

---

## Hardware Interfaces

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R210 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|--------|----------|--------|---------|-----------|
| **SPI** | ✓ 10 Mbps | ✓ 10 Mbps | ✓ 6 Mbps | ✓ 10 Mbps | ✓ 6 Mbps | ✓ 10 Mbps | ✓ 10 Mbps | ✓ 10 Mbps | ✓ 10 Mbps | ✓ 2 Mbps | ✗ |
| **I²C** | ✓ 3.4 Mbps | ✓ 3.4 Mbps | ? | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ? | ✓ (NCI) |
| **NCI** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| **GPIO Count** | Multiple | Multiple | Multiple | Multiple | Multiple | Multiple | Multiple | Multiple | Multiple | ? | Multiple |
| **IRQ Pin** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **RESET Pin** | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ (opt) | ✓ (opt) | ✓ (opt) | ✓ (opt) | ✗ | ✗ |
| **Clock Output** | ✓ | ✓ | ? | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ? | ✗ |

---

## Power Management

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R210 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|--------|----------|--------|---------|-----------|
| **Supply Voltage** | 2.4–5.5V | 2.4–5.5V | 2.4–5.5V | 2.7–5.5V | 2.7–5.5V | 2.7–5.5V | **2.7–6.0V** | 2.7–5.5V | 2.7–5.5V | ? | 2.4–5.1V |
| **IO Voltage** | 1.65–5.5V | 1.65–5.5V | 1.65–5.5V | 2.7–5.5V | 2.7–5.5V | 1.65–5.5V | 1.65–5.5V | 1.65–5.5V | 1.65–5.5V | ? | 1.2/1.8V |
| **Integrated Regulators** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Power-Down Mode** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Hibernate Mode** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| **Battery Monitoring** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| **External DC-DC Support** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ (5.5V) |

---

## Wake-up Features

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|----------|----------|---------|-----------|
| **Capacitive Sensing** | ✓ | ✗ | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Inductive Wake-up** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Amplitude Measurement** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Phase Measurement** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Low Power Card Detection** | ✓ | ✓ | ✓ | ✓ (Improved) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Wake-up Timer** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Wake-up Periods** | 9.7–1237ms | 9.7–1237ms | 9.7–1237ms | 9.7–1737ms | 9.7–1737ms | 9.7–1737ms | 9.7–1737ms | 9.7–1737ms | ? | ? |
| **Auto Wake-up on RF** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Auto Wake-up on GPIO** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

---

## Antenna Features

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|----------|----------|---------|-----------|
| **Differential Antenna** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Two Single-Ended Antennas** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? |
| **Automatic Antenna Tuning** | ✓ | ✓ | ✓ (3914 only) | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Varicap Control** | ✓ | ✓ | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Antenna Voltage Measurement** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Antenna Phase Measurement** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Antenna Detuning Detection** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Coil Multiplexer Support** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? |

---

## Advanced Features

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|----------|----------|---------|-----------|
| **FIFO Size** | 512 bytes | 512 bytes | 96 bytes | 256 bytes | 256 bytes | 512 bytes | 256 bytes | 512 bytes | 528 bytes | N/A |
| **Hardware CRC** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Hardware Parity** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Collision Detection** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Collision Position** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Water Level IRQ** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **External Field Detection** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **RSSI Measurement** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Amplitude ADC** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Phase ADC** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **On-chip Temperature** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Firmware Update** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ (100%) |
| **In-Frame Synchronization** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| **OOFS with Ext. Clock** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |

---

## Physical Characteristics

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R210 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|--------|----------|--------|---------|-----------|
| **Package** | QFN32/WLCSP | QFN32/WLCSP | QFN32 | QFN24 | QFN24 | QFN32 | QFN32 | QFN24 | QFN32 | QFN32 | WLCSP49 |
| **Package Size** | 5x5mm / 3x3mm | 5x5mm / 3x3mm | 5x5mm | 4x4mm | 4x4mm | 5x5mm | 5x5mm | 4x4mm | ? | 5x5mm | 2.56x2.58mm |
| **Pin Count** | 32 / 36 | 32 / 36 | 32 | 24 | 24 | 32 | 32 | 24 | ? | 32 | 49 |
| **Datasheet** | DS12484 | DS13541 | DS11837 | DS13658 | DS14139 | DS14593 | **DS14655 Rev 2** | DS14983 | **(missing)** | DS12807 | DB5606 |
| **Exposed Pad** | GND | GND | GND | GND | GND | **GND (req)** | **GND (req)** | **GND (req)** | **GND (req)** | GND | N/A |
| **Wettable Flanks** | ✗ | ✗ | ✓ (3914) | ✗ | ✗ | ✗ | ✗ | ✓ | ? | ✗ | ✗ |
| **ECOPACK** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

---

## Environmental

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500 | ST25R300 | ST25R501 | ST25R210 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|----------|--------|----------|--------|---------|-----------|
| **Operating Temp Min** | -40°C | -40°C | -40°C | -40°C | -25°C | -40°C | -40°C | -40°C | -40°C | ? | -40°C |
| **Operating Temp Max** | +105°C | +105°C | **+125°C** | +85°C | +85°C | **+125°C** | +105°C | **+125°C** | **+125°C** | ? | +85°C |
| **Storage Temp Min** | -65°C | -65°C | -65°C | -65°C | -65°C | -65°C | -65°C | -65°C | -65°C | ? | -65°C |
| **Storage Temp Max** | +150°C | +150°C | +150°C | +150°C | +150°C | +150°C | +150°C | +150°C | +150°C | ? | +150°C |
| **AEC-Q100** | ✗ | ✗ | ✓ Grade 1 | ✗ | ✗ | ✓ Grade 1 | ✗ | ✓ Grade 1 | ✓ Grade 1 | ✗ | ✗ |
| **ESD Protection** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

---

## Feature Availability Legend

| Symbol | Meaning |
|--------|---------|
| ✓ | Feature supported |
| ✗ | Feature not supported |
| ? | Feature availability unknown / consult datasheet |
| **Bold** | Enhanced or unique feature |

---

## Key Differentiators by Variant Family

### ST25R3916/17 (Non-B)
- **Unique:** Capacitive sensing wake-up
- **Best for:** General purpose with capacitive wake-up requirement
- **Avoid if:** Need EMVCo 3.2a or enhanced AWS

### ST25R39xxB (B Variants)
- **Unique:** Enhanced AWS (0–82%), EMVCo 3.1a/3.2a
- **Best for:** Payment applications, advanced modulation requirements
- **Avoid if:** Need capacitive sensing (removed)

### ST25R3914/15 (Automotive 39xx)
- **Unique:** Up to 1W output, AEC-Q100, AAT on 3914
- **Best for:** Automotive with AAT requirement
- **Avoid if:** Need large FIFO (only 96 bytes)

### ST25R200 (Consumer Compact)
- **Unique:** Compact 4x4mm, cost-optimized full features
- **Best for:** Space-constrained consumer applications
- **Avoid if:** Need card emulation, high bit rates, or I²C

### ST25R100 (Consumer Basic)
- **Unique:** Most cost-effective variant
- **Best for:** Basic NFC reader applications
- **Avoid if:** Need AWS, high temperature, or fast SPI

### ST25R500 (Automotive Premium)
- **Unique:** CCC Digital Key, NFC-V 212 kbps, AEC-Q100
- **Best for:** Automotive digital key, center console
- **Avoid if:** Need card emulation in smallest package

### ST25R300 (Consumer/Industrial Payment)
- **Unique:** **256-byte FIFO** (smaller than ST25R500), **2.7–6.0V supply**, TruST25 Link
- **Best for:** Payment terminals, POS systems, EMVCo PCD 3.2a applications
- **Avoid if:** Need automotive grade (not AEC-Q100), large FIFO, or 125°C operation

### ST25R501 (Automotive Compact)
- **Unique:** Smallest automotive variant
- **Best for:** Door handles, space-constrained automotive
- **Avoid if:** Need card emulation (reader-only)

### ST25RN300 (Mobile NCI)
- **Unique:** NCI interface, 2.2W Tx, battery monitoring
- **Best for:** Mobile devices, battery-powered applications
- **Avoid if:** Need direct register access (NCI only)

---

## Selection Flowchart

```
Start
│
├─ Automotive required?
│  ├─ Yes → CCC Digital Key?
│  │         ├─ Yes → ST25R500 (full) or ST25R501 (compact, reader-only)
│  │         └─ No → ST25R3914/15 (AAT on 3914)
│  │
│  └─ No → Mobile/Battery powered?
│           ├─ Yes → ST25RN300 (NCI, lowest power)
│           └─ No → Payment/EMVCo 3.2a required?
│                    ├─ Yes → ST25R300 (consumer/industrial) or ST25R39xxB
│                    └─ No → Capacitive wake-up required?
│                             ├─ Yes → ST25R3916/17 (non-B)
│                             └─ No → Space constrained?
│                                      ├─ Yes → ST25R200 (full) or ST25R100 (basic)
│                                      └─ No → ST25R3916/17 or ST25R39xxB
```

---

## Revision History

| Date | Changes |
|------|---------|
| 2026-03-16 | Initial comprehensive feature matrix created from datasheets |
| 2026-03-20 | Updated ST25R300 to dedicated columns and added detailed specs |

---

## References

All information extracted from official STMicroelectronics datasheets and application notes in `docs/st_datasheets/`:
- DS12484 (ST25R3916/17)
- DS13541 (ST25R3916B/17B/19B)
- DS11837 (ST25R3914/15)
- DS13658 (ST25R200)
- DS14139 (ST25R100)
- DS14593 (ST25R500)
- **DS14655 (ST25R300)**
- **UM3536 (ST25R300 GUI)**
- DS14983 (ST25R501)
- DS12807 (ST25R95)
- DB5606 (ST25RN300)
