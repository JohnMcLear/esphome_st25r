---
name: ST25R Variant Comparison
description: Comprehensive comparison of all ST25R NFC reader variants, including features, registers, and migration notes
type: reference
---

# ST25R Variant Comparison Guide

This document provides a comprehensive comparison of all ST25R NFC reader variants to guide hardware selection and software migration decisions.

## Variant Families Overview

| Family | Variants | Target Market | Key Characteristics |
|--------|----------|---------------|---------------------|
| **ST25R39xx** | ST25R3911B, ST25R3914, ST25R3915, ST25R3916, ST25R3917, ST25R3918, ST25R3920 | Consumer, Industrial, Automotive | Full-featured, AAT support, capacitive sensing (pre-B), 512-byte FIFO |
| **ST25R39xxB** | ST25R3916B, ST25R3917B, ST25R3919B, ST25R3920B | Consumer, Industrial, Automotive | Enhanced AWS (0-82% modulation), EMVCo 3.1a/3.2a, no capacitive sensing |
| **ST25Rx00** | ST25R100, ST25R200 | Consumer, Industrial | Compact (4x4mm), 256-byte FIFO, inductive wake-up only, cost-optimized |
| **ST25R5xx Automotive** | ST25R300, ST25R500, ST25R501, ST25R210 | Automotive (AEC-Q100) | CCC Digital Key, -40°C to +125°C, enhanced thermal, no card emulation (R501) |
| **ST25R95** | ST25R95 | Consumer, Industrial | Legacy device, SPI up to 2 Mbps, 528-byte FIFO |
| **ST25RN300** | ST25RN300 | Mobile, Consumer | NCI interface, WLCSP49, enhanced Tx up to 2.2W, battery monitoring |

---

## Detailed Variant Specifications

### ST25R3916 / ST25R3917 (Non-B)

**Package:** VFQFPN32 (5x5mm), UFQFPN32 (5x5mm), WLCSP36

**Supply Voltage:** 2.6–5.5V (-40°C to +105°C), 2.4–5.5V (-20°C to +105°C)

**IO Voltage:** 1.65–5.5V

**Supported Protocols:**
- NFC-A / ISO14443A up to 848 kbit/s
- NFC-B / ISO14443B up to 848 kbit/s
- NFC-F / FeliCa up to 424 kbit/s
- NFC-V / ISO15693 up to 53 kbit/s
- Card emulation: NFC-A, NFC-F
- Active/passive P2P initiator and target

**Key Features:**
- 512-byte FIFO
- SPI up to 10 Mbit/s, I²C up to 3.4 Mbit/s
- **Capacitive sensing wake-up** (CSI/CSO pins)
- **Automatic Antenna Tuning (AAT)** via varicap
- Dynamic Power Output (DPO)
- Active Wave Shaping (AWS)
- Noise Suppression Receiver (NSR)
- AM/PM and I/Q demodulator
- ASK modulation depth: 5–40%
- Two independent single-ended antennas

**Temperature Range:** -40°C to +105°C

**Unique:** Capacitive sensing wake-up (removed in B variants)

---

### ST25R3916B / ST25R3917B / ST25R3919B / ST25R3920B

**Package:** VFQFPN32 (5x5mm), UFQFPN32 (5x5mm), WLCSP36

**Supply Voltage:** 2.6–5.5V (-40°C to +105°C), 2.4–5.5V (-20°C to +105°C)

**IO Voltage:** 1.65–5.5V

**Supported Protocols:** Same as ST25R3916/17

**Key Features:**
- 512-byte FIFO
- SPI up to 10 Mbit/s, I²C up to 3.4 Mbit/s
- **Enhanced AWS:** 0–82% modulation depth (vs 5-40% non-B)
- **EMVCo 3.1a/3.2a compliant** EMD handling
- **No capacitive sensing** (CSI/CSO are test pins only)
- Advanced wave shaping for NFC-A/B/V/F
- Finer Tx driver resistance stepping
- Wake-up via amplitude/phase measurement only

**Temperature Range:** -40°C to +105°C

**Migration Notes from ST25R39xx:**
- Pin-to-pin compatible except capacitive sensing removed
- **VDD_AM capacitor:** Change from 2.2µF to 10–50nF when using enhanced AWS
- **TX_DRIVER register (0x28):** d_res bits extended, am_mod mapping changed
- New AWS registers for advanced shaping
- Legacy mode available (disable rgs_am for ST25R39xx behavior)

---

### ST25R3914 / ST25R3915 (Automotive)

**Package:** VFQFPN32 (5x5mm) wettable flanks (3914), QFN32 (5x5mm) (3915)

**Supply Voltage:** 2.4–5.5V

**IO Voltage:** 1.65–5.5V

**Supported Protocols:**
- ISO18092 (NFCIP-1) Active P2P
- ISO14443A/B, ISO15693, FeliCa
- High bit rates up to 848 kbit/s

**Key Features:**
- **AEC-Q100 Grade 1 qualified**
- **Up to 1W output power** (differential)
- **AAT support** (ST25R3914 only, not 3915)
- Capacitive sensing wake-up
- 96-byte FIFO (smaller than 3916)
- SPI up to 6 Mbit/s
- Temperature: -40°C to +125°C

**Unique:** Highest output power in 39xx family, AAT on 3914 only

---

### ST25R200

**Package:** UFQFPN24 (4x4mm)

**Supply Voltage:** 2.7–5.5V

**IO Voltage:** 2.7–5.5V

**Supported Protocols:**
- NFC-A / ISO14443A at 106 kbit/s only
- NFC-B / ISO14443B at 106 kbit/s only
- NFC-V / ISO15693 up to 53 kbit/s
- NFC Forum T1T, T2T, T4T, T5T
- Proprietary: Kovio, CTS, B'

**Key Features:**
- **256-byte FIFO** (half of ST25R39xx)
- SPI up to 10 Mbit/s
- **No I²C support**
- **Inductive wake-up only** (no capacitive)
- Low Power Card Detection (LPCD)
- Dynamic Power Output (DPO)
- Active Wave Shaping (AWS)
- Noise Suppression Receiver (NSR)
- Two independent single-ended antennas

**Temperature Range:** -40°C to +85°C

**Migration Notes from ST25R39xx:**
- **Not pin-compatible** (24-pin vs 32-pin)
- **No VDD_TX pin**
- **No capacitive sensing**
- **No I²C interface**
- Smaller FIFO (256 vs 512 bytes)
- Different register map for some features
- Wake-up timer periods differ

---

### ST25R100

**Package:** UFQFPN24 (4x4mm)

**Supply Voltage:** 2.7–5.5V

**IO Voltage:** 2.7–5.5V

**Supported Protocols:** Same as ST25R200

**Key Features:**
- **256-byte FIFO**
- **SPI up to 6 Mbit/s** (slower than R200)
- **No AWS** (Active Wave Shaping)
- Inductive wake-up only
- Low Power Card Detection (LPCD)
- Two independent single-ended antennas

**Temperature Range:** -25°C to +85°C

**Differences from ST25R200:**
- Slower SPI (6 vs 10 Mbit/s)
- No Active Wave Shaping
- Lower temperature range (-25°C vs -40°C min)
- Cost-optimized variant

---

### ST25R500 (Automotive)

**Package:** VFQFPN32 (5x5mm)

**Supply Voltage:** 2.7–5.5V

**IO Voltage:** 1.65–5.5V

**Supported Protocols:**
- NFC-A / ISO14443A up to 848 kbit/s
- NFC-B / ISO14443B up to 848 kbit/s
- NFC-V / ISO15693 up to **212 kbit/s** (faster than 39xx)
- NFC-F / FeliCa up to 424 kbit/s
- **Card emulation:** NFC-A 106 kbit/s, NFC-F 212/424 kbit/s
- Passive P2P mode

**Key Features:**
- **AEC-Q100 Grade 1 qualified**
- 512-byte FIFO
- SPI up to 10 Mbit/s
- **No I²C** (SPI only)
- **CCC Digital Key compliant**
- **USI WLC reader**
- Inductive wake-up (LPCD)
- I/Q demodulator with channel summation
- Dynamic Power Output (DPO)
- Active Wave Shaping (AWS)
- Noise Suppression Receiver (NSR)
- One differential or two single-ended antennas

**Temperature Range:** -40°C to **+125°C**

**Migration Notes from ST25R39xx:**
- **Not pin-compatible** despite 32-pin package
- **New RESET pin** (active-high, optional, tie to GND if unused)
- **No capacitive sensing** (CSI/CSO removed)
- **No I²C_EN pin** (SPI only)
- **No AAT tune voltage pins**
- **Exposed pad (pin 33) must be connected to GND** (thermal + electrical)
- **No clock output for MCU**
- Different GPIO multiplexing for passive load modulation

---

### ST25R300 (Consumer/Industrial Payment)

**Package:** UQFPN32 / UFQFPN32 (5x5mm)

**Datasheet:** DS14655 Rev 2 - June 2025

**Supply Voltage:** 2.7–6.0V (higher than ST25R500)

**IO Voltage:** 1.65–5.5V

**Supported Protocols:**
- NFC-A / ISO14443A up to 848 kbit/s
- NFC-B / ISO14443B up to 848 kbit/s
- NFC-V / ISO15693 up to **212 kbit/s**
- NFC-F / FeliCa up to 424 kbit/s
- **Card emulation:** NFC-A 106 kbit/s, NFC-F 212/424 kbit/s
- **Passive P2P mode**
- **TruST25 Link** (patented offline NFC tag identification)

**Key Features:**
- **EMVCo PCD 3.2a compliant** (payment terminal optimized)
- **256-byte FIFO** (half of ST25R500!)
- **NFC Forum universal device**
- **USI WLC reader device** (wireless charging)
- SPI up to 10 Mbit/s
- **No I²C** (SPI only)
- Inductive wake-up (LPCD)
- I/Q demodulator with channel summation
- Dynamic Power Output (DPO)
- Active Wave Shaping (AWS)
- Noise Suppression Receiver (NSR)
- One differential or two single-ended antennas
- **CE load modulation** support

**Temperature Range:** -40°C to **+105°C** (not 125°C like automotive)

**Key Differences from ST25R500:**
- **FIFO: 256 bytes** (vs 512 bytes on ST25R500)
- **Supply voltage: 2.7–6.0V** (vs 2.7–5.5V on ST25R500)
- **Temperature: -40°C to +105°C** (vs -40°C to +125°C automotive)
- **Not AEC-Q100 qualified** (consumer/industrial, not automotive)
- **TruST25 Link** feature (offline tag identification)
- Optimized for POS terminals, payment applications
- **No CCC Digital Key** (consumer focus)

**Applications:**
- EMVCo PCD 3.2a contactless payment terminals
- Access control
- NFC Forum universal devices
- WPC Qi out-of-band communication
- NFC wireless charging (WLC) poller
- WPC Ki power transmitter (PTx) communications

---

### ST25R210 (Automotive)

**Package:** QFN32 (exact dimensions in AN6279)

**Datasheet:** **NOT in collection** — only application notes available

**Known characteristics:**
- Automotive variant in ST25R500 family
- Same register map as ST25R500
- AEC-Q100 qualified
- Temperature Range: -40°C to +125°C

**Available documentation:**
- AN6092: Antenna design (ST25R210/300/500/501)
- AN6279: Layout recommendations (ST25R300/500/501/210)
- AN6298: Wake-up mode (ST25R300/500/501/210)
- AN6463: Thermal design (ST25R210/300/500/501)

**Note:** Exact specifications unknown without dedicated datasheet.

---

### ST25R501 (Automotive Compact)

**Package:** VFQFPN24 (4x4mm) wettable flanks

**Supply Voltage:** 2.7–5.5V

**IO Voltage:** 1.65–5.5V

**Supported Protocols:**
- NFC-A / ISO14443A up to 848 kbit/s
- NFC-B / ISO14443B up to 848 kbit/s
- NFC-V / ISO15693 up to 212 kbit/s
- NFC-F / FeliCa up to 424 kbit/s
- **No card emulation** (reader-only)
- Passive P2P (Initiator only)

**Key Features:**
- **AEC-Q100 Grade 1 qualified**
- **Compact 4x4mm package**
- CCC Digital Key compliant
- NFC Forum reader device
- Inductive wake-up (LPCD)
- I/Q demodulator
- DPO, AWS, NSR
- One differential or two single-ended antennas

**Temperature Range:** -40°C to +125°C

**Unique:** Smallest automotive variant, reader-only (no card emulation)

---

### ST25R210 (Automotive)

**Package:** Consult datasheet

**Specifications:** Automotive variant, similar to ST25R500/300 family

**Temperature Range:** -40°C to +125°C

---

### ST25R95

**Package:** VFQFPN32 (5x5mm)

**Supply Voltage:** Consult datasheet

**Supported Protocols:**
- NFC-A / ISO14443A reader
- NFC-B / ISO14443B reader
- NFC-F / FeliCa reader
- NFC-V / ISO15693 reader
- NFC-A card emulation
- MIFARE Classic compatible

**Key Features:**
- **528-byte FIFO** (largest)
- **SPI up to 2 Mbit/s** (slower than newer variants)
- Legacy device
- Dedicated frame controller

**Status:** Legacy, migrate to ST25R200 or ST25R39xxB for new designs

---

### ST25RN300 (NCI Reader)

**Package:** WLCSP49 (2.559 x 2.581mm)

**Supply Voltage:** 2.4–5.1V (battery powered)

**IO Voltage:** 1.2V, 1.8V compatible

**Supported Protocols:**
- **NCI interface** (unique in ST25R family)
- NFC Forum Type 1, 2, 3, 4, 5 tags
- FeliCa
- ISO15693
- Card emulation: ISO14443A/B, FeliCa
- Passive P2P

**Key Features:**
- **NCI communication interface** (I²C-based)
- **Enhanced Tx drive up to 2.2W**
- **External DC-DC support up to 5.5V**
- **Battery voltage monitoring**
- **Proprietary in-frame synchronization (IFS)** for CE mode
- **OOFS with external reference clock** in CE
- **Fractional-N PLL:** 19.2–76.8 MHz
- **Multiple crystal support:** 27.12, 54.24, 32.768 kHz, 16, 32 MHz
- **100% re-flashable firmware**
- GPIOs, chip-enable pin

**Temperature Range:** -40°C to +85°C

**Unique:** Only NCI-based device, mobile-optimized, smallest package

---

## Feature Comparison Matrix

| Feature | ST25R3916/17 | ST25R39xxB | ST25R3914/15 | ST25R200 | ST25R100 | ST25R500/300 | ST25R501 | ST25R95 | ST25RN300 |
|---------|--------------|------------|--------------|----------|----------|--------------|----------|---------|-----------|
| **Package** | QFN32/WLCSP | QFN32/WLCSP | QFN32 | QFN24 | QFN24 | QFN32 | QFN24 | QFN32 | WLCSP49 |
| **FIFO Size** | 512 | 512 | 96 | 256 | 256 | 512 | 512 | 528 | N/A |
| **SPI Speed** | 10 Mbps | 10 Mbps | 6 Mbps | 10 Mbps | 6 Mbps | 10 Mbps | 10 Mbps | 2 Mbps | NCI |
| **I²C Support** | ✓ | ✓ | ? | ✗ | ✗ | ✗ | ✗ | ? | ✓ (NCI) |
| **NFC-A Max** | 848 kbps | 848 kbps | 848 kbps | 106 kbps | 106 kbps | 848 kbps | 848 kbps | 106 kbps | 848 kbps |
| **NFC-B Max** | 848 kbps | 848 kbps | 848 kbps | 106 kbps | 106 kbps | 848 kbps | 848 kbps | 106 kbps | 848 kbps |
| **NFC-F Max** | 424 kbps | 424 kbps | 424 kbps | ✗ | ✗ | 424 kbps | 424 kbps | ✓ | 424 kbps |
| **NFC-V Max** | 53 kbps | 53 kbps | ✓ | 53 kbps | 53 kbps | **212 kbps** | **212 kbps** | ✓ | ✓ |
| **Card Emulation** | ✓ | ✓ | ? | ✗ | ✗ | ✓ | ✗ | ✓ | ✓ |
| **P2P Active** | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ | ✓ | ? | ✓ |
| **P2P Passive** | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ | ✓ | ? | ✓ |
| **AAT Support** | ✓ | ✓ | ✓ (3914) | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Capacitive Wake-up** | ✓ | ✗ | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Inductive Wake-up** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **AWS** | ✓ | **Enhanced** | ✓ | ✓ | ✗ | ✓ | ✓ | ? | ✓ |
| **DPO** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ✓ |
| **NSR** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ✓ |
| **Two Antennas** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ? | ? |
| **AEC-Q100** | ✗ | ✗ | ✓ | ✗ | ✗ | ✓ | ✓ | ✗ | ✗ |
| **Temp Max** | 105°C | 105°C | **125°C** | 85°C | 85°C | **125°C** | **125°C** | ? | 85°C |
| **EMVCo** | 3.0 | **3.1a/3.2a** | ? | ? | ? | **3.2a** | **DK** | ? | **3.2a** |
| **CCC Digital Key** | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✗ | ✗ |

---

## Register Differences

### TX_DRIVER Register (0x28) - ST25R39xx vs ST25R39xxB

**ST25R39xx:**
```
Bits [7:4]: am_mod[3:0] - ASK modulation depth (5-40%)
Bits [3:0]: d_res[3:0] - Driver resistance (4 bits)
```

**ST25R39xxB:**
```
Bits [7:4]: am_mod[3:0] - ASK modulation depth (0-82%, different mapping)
Bits [3:0]: d_res[3:0] - Driver resistance (extended range, finer steps)
```

**Migration:** Cannot directly copy register values. Recalculate based on desired modulation index.

### Modulation Depth Mapping

| am_mod Value | ST25R39xx (%) | ST25R39xxB (%) |
|--------------|---------------|----------------|
| 0x0 | ~5 | 0 |
| 0x7 | ~12 | ~10 |
| 0xF | ~40 | 82 |

### Removed Registers in ST25R39xxB

- **Capacitive sensing registers** (CSI/CSO functionality removed)
- Pins repurposed as TAD (Test Analog Digital)

### New Registers in ST25R39xxB

- **Advanced AWS configuration registers**
- **EMD handling registers** (EMVCo 3.1a)
- **Tx driver stepping registers**

### ST25R200/100 Register Differences

- **No AAT registers** (feature not supported)
- **No capacitive sensing registers**
- **Different wake-up timer register mapping**
- **Simplified IRQ mask registers**

### ST25R500/300 Register Differences

- **New GPIO multiplexing registers** (passive load modulation)
- **Different regulator control registers**
- **No AAT registers**
- **Enhanced NFC-V bit rate registers** (up to 212 kbps)

---

## Wake-up Mode Comparison

### ST25R3916/17 (Non-B)

**Wake-up Sources:**
1. **Capacitive sensing** (unique to non-B)
2. Amplitude measurement
3. Phase measurement

**Wake-up Periods:** 9.7ms to 1237.3ms (configurable)

**Power Consumption:** Ultra-low in wake-up mode

### ST25R39xxB

**Wake-up Sources:**
1. Amplitude measurement
2. Phase measurement
3. **No capacitive sensing** (removed)

**New Feature:** Wake-up with varicap settling time (meas_tx_del)

### ST25R200/100

**Wake-up Sources:**
1. **Inductive measurement only** (I/Q channels)
2. No capacitive sensing

**Wake-up Periods:** 9.7ms to 1737.3ms (similar to 39xx)

**LPCD:** Low Power Card Detection with improved range

### ST25R500/300/501/210

**Wake-up Sources:**
1. Inductive measurement (I/Q channels)
2. No capacitive sensing

**Automotive Optimized:** Reliable wake-up across -40°C to +125°C

---

## Antenna Configuration

### Differential vs Single-Ended

| Variant | Differential | Two Single-Ended | AAT Support |
|---------|-------------|------------------|-------------|
| ST25R3916/17 | ✓ | ✓ | ✓ |
| ST25R39xxB | ✓ | ✓ | ✓ |
| ST25R3914 | ✓ | ✓ | ✓ |
| ST25R3915 | ✓ | ✓ | ✗ |
| ST25R200 | ✓ | ✓ | ✗ |
| ST25R100 | ✓ | ✓ | ✗ |
| ST25R500 | ✓ | ✓ | ✗ |
| ST25R501 | ✓ | ✓ | ✗ |

### AAT Implementation (ST25R3916/17, 39xxB, 3914)

**Components:**
- **Varicap diodes:** STPTIC-0N200 or equivalent
- **Control voltage:** 0–5V from DAC or PWM
- **VDD_AM capacitor:**
  - ST25R39xx: 2.2µF
  - ST25R39xxB with AWS: 10–50nF

**Tuning Range:** Typically ±10% of center frequency

**Algorithm:** Measure harmonics (2nd, 3rd) and adjust VCC to minimize

---

## Migration Paths

### From ST25R39xx to ST25R39xxB

**Hardware:**
- Pin-to-pin compatible
- Remove capacitive sensing circuit
- Change VDD_AM capacitor if using enhanced AWS (2.2µF → 10–50nF)

**Software:**
- Recalculate TX_DRIVER (0x28) values
- Enable enhanced AWS if desired (rgs_am bit)
- Remove capacitive sensing code
- Update EMVCo EMD handling for 3.1a/3.2a

### From ST25R39xx to ST25R200

**Hardware:**
- **Redesign PCB** (24-pin vs 32-pin, not compatible)
- Remove I²C interface (SPI only)
- Remove capacitive sensing circuit
- No VDD_TX pin

**Software:**
- Update FIFO size (512 → 256 bytes)
- Remove AAT code
- Remove capacitive sensing code
- Update wake-up timer configuration
- Check register map differences

### From ST25R39xx to ST25R500

**Hardware:**
- **Redesign PCB** (different pinout despite 32-pin)
- Add RESET pin (optional, tie to GND if unused)
- Connect exposed pad to GND (multiple vias)
- Remove capacitive sensing circuit
- Remove I²C interface (SPI only)
- Remove AAT circuit

**Software:**
- Update GPIO multiplexing for passive load modulation
- Update regulator configuration
- NFC-V bit rate can be increased (up to 212 kbps)
- Remove capacitive sensing code
- Remove AAT code

### From ST25R95 to ST25R200

**Hardware:**
- Redesign PCB (24-pin vs 32-pin)
- SPI speed can increase (2 → 10 Mbps)

**Software:**
- Update FIFO size (528 → 256 bytes)
- Update SPI timing
- Check protocol support (ST25R200 lacks some features)

### From ST25R39xx to ST25RN300 (Mobile)

**Hardware:**
- **Complete redesign** (WLCSP49, different interface)
- NCI interface instead of direct SPI register access
- Battery power management
- External DC-DC converter

**Software:**
- **Complete rewrite** (NCI protocol stack)
- Different command set
- Firmware update mechanism
- Power management optimization

---

## Selection Guide

### For Automotive Applications

**CCC Digital Key:**
- **ST25R500:** Full-featured, card emulation, QFN32
- **ST25R501:** Reader-only, compact QFN24
- **ST25R300:** Cost-optimized alternative

**General Automotive:**
- **ST25R3914/15:** AEC-Q100, up to 1W output, AAT on 3914
- **ST25R210:** Consult datasheet

### For Consumer/Industrial

**High Performance:**
- **ST25R3916B/17B:** Latest, EMVCo 3.2a, enhanced AWS
- **ST25R3916/17:** Legacy, capacitive sensing, lower cost

**Cost-Optimized:**
- **ST25R200:** Compact, full features except high bit rates
- **ST25R100:** Basic, no AWS, slower SPI

**Mobile/Battery:**
- **ST25RN300:** NCI interface, lowest power, smallest

**Legacy:**
- **ST25R95:** Migrate to ST25R200 or ST25R39xxB

### For Multi-Antenna Systems

All variants support two independent single-ended antennas. For more than two antennas:

- Use external **NMOS multiplexer** (see AN6121)
- Control via MCU GPIO or ST25R GPIO pins
- Switching sequence: disconnect unused antenna → connect active antenna

---

## Key Documents by Variant

| Variant | Datasheet | Application Notes |
|---------|-----------|-------------------|
| ST25R3916/17 | DS12484 | AN5240, AN5276, AN5320, AN5322, AN5584 |
| ST25R39xxB | DS13541 | AN5768 (migration), AN5320, AN5322, AN5896 |
| ST25R3914/15 | DS11837 | AN5240, AN5276, AN5320 |
| ST25R200 | DS13658 | AN5965 (migration), AN5984, AN5993, AN6065 |
| ST25R100 | DS14139 | AN5965, AN5993, AN6065 |
| ST25R500 / 300 | DS14593 / DS14655 | AN6313, AN6279, AN6298, AN6092, UM3536 (GUI) |
| ST25R501 | DS14983 | AN6279, AN6298 |
| ST25R95 | DS12807 | AN5248, AN6143 (migration) |
| ST25RN300 | DB5606 | UM3585 |
| Multi-antenna | - | AN6121 |
| Crystal design | - | AN6134 |
| Thermal design | - | AN5584, AN6463 |
| Single-ended antenna | - | AN5592 |

---

## Common Pitfalls

1. **Assuming pin compatibility:** Only ST25R39xx ↔ ST25R39xxB is pin-compatible
2. **RX_CONF3 `lf_en` bit on B-version:** `0xE2` sets `lf_en=1` which routes the receiver to LF mode, silently disabling 13.56 MHz NFC reception. Non-B Elechouse modules tolerated it due to a weak/untuned antenna; B-version STEVAL with a tuned PCB antenna cannot receive any tag. Use `0x00` for B-version. Symptom: `WUPA timeout AMP>0 IRQ=0x00`.
3. **IC_IDENTITY differs between non-B and B-version:** non-B = `(id & 0xF8) == 0x28`; B-version = `(id & 0xF8) == 0x30`. Both must be accepted.
4. **Capacitive sensing code on B variants:** Feature removed, will not work
5. **VDD_AM capacitor value:** Wrong value causes AWS instability
6. **TX_DRIVER register values:** Cannot copy between 39xx and 39xxB
7. **FIFO size assumptions:** 96/256/512/528 bytes depending on variant
8. **SPI speed assumptions:** 2/6/10 Mbps depending on variant
9. **Temperature range:** 85°C/105°C/125°C depending on variant
10. **NFC-V bit rate:** 53 kbps vs 212 kbps on automotive variants
11. **Card emulation:** Not available on ST25R501, ST25R200, ST25R100
12. **I²C support:** Only on ST25R39xx, ST25R39xxB, ST25RN300 (NCI)

---

## Revision History

| Date | Changes |
|------|---------|
| 2026-03-16 | Initial comprehensive variant comparison created from datasheets |

---

## References

- All datasheets in `docs/st_datasheets/`
- Migration application notes: AN5768, AN5965, AN6313, AN6143
- Feature application notes: AN5320 (wake-up), AN5322 (AAT), AN6121 (multi-antenna)
- ST25R3916 datasheet: DS12484 Rev 8
- ST25R3916B datasheet: DS13541 Rev 10
- ST25R200 datasheet: DS13658 Rev 2
- ST25R100 datasheet: DS14139 Rev 5
- ST25R500 datasheet: DS14593 Rev 1
- ST25R501 datasheet: DS14983 Rev 1
- ST25RN300 data brief: DB5606 Rev 3
