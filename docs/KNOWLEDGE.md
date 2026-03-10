# ST25R3916 Knowledge Base

## Chip Identification

To distinguish between the ST25R3916 and ST25R3916B, read the `IC Identity Register` (`0x3F`).

- **ST25R3916**: 
  - Type Code: `0x28` (Bits 7:3 = `00101`)
  - Identity Mask: `(ID & 0xF8) == 0x28`
- **ST25R3916B**: 
  - Type Code: `0x30` (Bits 7:3 = `00110`)
  - Identity Mask: `(ID & 0xF8) == 0x30`
  - Usually revision `ID & 0x07` is `>= 1`.

## Initialization Procedure

A robust initialization sequence involves the following steps:

1.  **Set Default**: Execute the `SET_DEFAULT` direct command (`0xC1`).
2.  **MISO Configuration (SPI)**: 
    - Increase MISO driving level: Set `IO_CONF2` (`0x01`) bit `io_drv_lvl` (`1<<2`).
    - Enable MISO pull-downs: Set `IO_CONF2` (`0x01`) bits `miso_pd1` (`1<<3`) and `miso_pd2` (`1<<4`).
3.  **Overheat Protection Fix (non-B version ONLY)**:
    - **Mandatory for ST25R3916 (non-B)**. Write `0x10` to register `0x04` using the `TEST_ACCESS` command prefix (`0xFC`).
    - **Do NOT apply to ST25R3916B.**
4.  **Oscillator Startup**:
    - Enable oscillator: Set `OP_CONTROL` (`0x02`) bit `en` (`1<<7`).
    - Wait for `osc_ok` bit (`1<<4`) in `AUX_DISPLAY` (`0x31`) to become stable (approx 700µs).
5.  **RC Calibration (B version only)**:
    - Execute the `RC_CAL` direct command (`0xEA`).
6.  **Supply Voltage Configuration**:
    - Measure VDD (can use `MEASURE_VDD` command `0xDF`).
    - If VDD < 3.6V, set `sup3V` bit (`1<<7`) in `IO_CONF2` (`0x01`).
    - If VDD >= 3.6V, clear `sup3V` bit.

## Register Map Nuances

### Space B Registers
Registers with the `0x40` bit set (e.g., `EMD_SUP_CONF` at `0x45`) belong to "Space B". These are often accessed by preceding the operation with the `SPACE_B_ACCESS` command or using specific SPI frame formats depending on the driver implementation.

### Common Direct Commands
- `0xC1`: Set Default
- `0xD6`: Adjust Regulators
- `0xDA`: Clear RSSI
- `0xDB`: Clear FIFO
- `0xDF`: Measure VDD
- `0xEA`: RC Calibration (ST25R3916B)
- `0xFC`: Test Access

## Analog Configuration (RFAL)

The ST25R3916 uses an extensive analog configuration table to fine-tune RF performance for different technologies (NFC-A, B, F, V) and bitrates.

### Configuration Structure
Configurations are often defined as mode entries containing:
- **Mode ID**: A 16-bit identifier combining technology, bitrate, and direction (TX/RX).
- **Register Count**: Number of register sets to follow.
- **Register Sets**: Triplets of (Register Address, Mask, Value).

### Key Analog Registers
- `RX_CONF1` to `RX_CONF4` (`0x0B`-`0x0E`): Receiver gain, filters, and squelch settings.
- `CORR_CONF1` & `CORR_CONF2` (`0x0C`-`0x0D` in Space B): Correlator settings for bit detection.
- `TX_DRIVER` (`0x28`): Modulation index and driver resistance.
- `AWS_CONF1` & `AWS_CONF2` (`0x2E`-`0x2F` in Space B): Automatic Wave Shaping configuration (primarily for ST25R3916B).

### Tuning for ST25R3916 vs ST25R3916B
- **ST25R3916**: Typically uses overshoot/undershoot protection registers (`0x30`-`0x33` in Space B).
- **ST25R3916B**: Introduces **Automatic Wave Shaping (AWS)**, which replaces manual overshoot/undershoot tuning. AWS settings are stored in registers `0x2E`-`0x2F` and `0x34`-`0x39` in Space B.

## Elechouse Wilson ESP32 + ST25R3916 Specifics

### Confirmed Pinout (ESP32-C6 ST25R Relay Board)
- **MOSI**: 18
- **MISO**: 10
- **SCK (CLK)**: 19
- **CS**: 6
- **IRQ**: 7
- **LED**: 2 (Status LED)

### Original Wilson Board Pinout (for reference)
Based on the Elechouse sample code for the Wilson board:
- **MOSI**: 23
- **MISO**: 19
- **SCK**: 18
- **CS (SS)**: 5
- **IRQ**: 4
- **LED**: 2 (Optional status LED)

### Elechouse Custom Antenna Tuning (AAT)
The Elechouse sample code uses specific Antenna Auto-Tuning (AAT) values in its analog configuration table, which likely correspond to their specific PCB antenna design.

- **Poller Mode (Typical)**:
  - `ANT_TUNE_A`: `0x80`
  - `ANT_TUNE_B`: `0x40`
- **Listener Mode**:
  - `ANT_TUNE_A`: `0x00`
  - `ANT_TUNE_B`: `0xE0`

### Elechouse Analog Configuration Highlights
Compared to the standard RFAL tables, the Elechouse configuration often:
- Disables overshoot and undershoot protection (`0x00` in registers `0x30`-`0x33` of Space B) for many modes.
- Uses `0x2D` or `0x3D` for `RX_CONF2` in many polling modes.
- Configures `PT_MOD` (`0x29`) to `0x51` during initialization to reduce RFO resistance in the modulated state.
- Enables `lm_dri` (`1<<4`) in `AUX_MOD` (`0x28` in Space B) to use internal load modulation.

## Antenna Tuning (AAT)

Based on the provided tuning test data, the following configurations are relevant for optimizing RF performance:

- **Matching Capacitors**: Tuning scans reference `39pF` and `47pF` primary matching capacitor values.
- **Damping Resistors**: Typically `1R5` or `2R4` resistors are used to adjust the Q factor.
- **Enclosure Compensation**: The presence of an acrylic enclosure significantly affects antenna resonance compared to "free air" conditions.
- **AAT Registers**: `ANT_TUNE_A` (`0x26`) and `ANT_TUNE_B` (`0x27`) allow for fine-tuning. The tuning voltage is approximately `(0.044 + 0.868 * val/255) * VDD_A`.

## Summary of Useful Commands
- **Set Default**: `0xC1`
- **Stop**: `0xC2`
- **Transmit with CRC**: `0xC4`
- **Clear FIFO**: `0xDB`
- **Measure VDD**: `0xDF`
- **RC Calibration**: `0xEA` (ST25R3916B only)
- **Test Access**: `0xFC` (Used for overheat fix on non-B)

## Initialization Issues

### Premature Overheat Protection (ST25R3916 non-B version)

**Issue:** The ST25R3916 (non-B version) can have internal overheat protection trigger prematurely below its rated junction temperature.

**Findings:**
- A specific initialization sequence is required immediately after power-on and the "Set default" command.
- **Required Action:** Write the value `0x10` to register address `0x04`.
- **Command Frame:** `FCh / 04h / 10h` (where `FCh` is the Register Write command).
- **RFAL Configuration:** In `rfalAnalogConfigTbl.h`, this is represented as `0x84U, 0x10, 0x10` under the `CHIP_INIT` section.
- **Applicability:** This is mandatory for the **ST25R3916** (non-B version). It must be kept for **normal operation**, not just for specific measurements.
- **Contraindications:** This should **NOT** be applied to the **ST25R3916B**.

**Documentation References:**
- **AN5584** (ST25R39xx NFC reader thermal design), Section 2.4.
- **DS12484** (ST25R3916 Datasheet), Chapter 4.1.

**Source:** [ST Community Forum - ST25R3916 and ST25R3916B possible default analog config issue](https://community.st.com/t5/st25-nfc-rfid-tags-and-readers/st25r3916-and-st25r3916b-possible-default-analog-config-issue/td-p/575923)
