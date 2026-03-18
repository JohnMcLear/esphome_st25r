---
name: ST25R Register Map Differences
description: Detailed register map differences between ST25R variants for driver development
type: reference
---

# ST25R Register Map Differences

This document details the register map differences between ST25R variants to guide driver development and migration.

## Register Space Overview

All ST25R devices use two register spaces:
- **Space A:** 0x00–0x3F (64 registers) - Main configuration and status
- **Space B:** 0x40–0x7F (64 registers) - Extended configuration (not all variants)

**Important:** The SPI `write_register()` function in the current ESPHome component masks `addr & 0x3F`, preventing writes to Space B registers without driver modification.

---

## Common Registers (All Variants)

### Space A - Universal Registers

| Addr | Name | ST25R39xx | ST25R39xxB | ST25R200/100 | ST25R500 | ST25R300 | Notes |
|------|------|-----------|------------|--------------|----------|----------|-------|
| 0x00 | IO_CONF1 / OPERATION | ✓ | ✓ | ✓ | ✓ | **0x00** | ST25R300 uses 0x00 for Op Control |
| 0x01 | IO_CONF2 | ✓ | ✓ | ✓ | ✓ | **0x01** | ST25R300 uses 0x01 for Gen Conf |
| 0x02 | OP_CONTROL | ✓ | ✓ | ✓ | ✓ | ⚠️ | ST25R300 map is shifted |
| 0x03 | MODE / PROTOCOL | ✓ | ✓ | ✓ | ✓ | **0x14** | ST25R300 Protocol1 at 0x14 |
| 0x04 | BIT_RATE | ✓ | ✓ | ✓ | ✓ | ⚠️ | Bit rate handling differs |
| 0x05 | ISO14443A_CONF | ✓ | ✓ | ✓ | ✓ | **0x15** | ST25R300 TxProtocol1 at 0x15 |
| 0x0B | RX_CONF1 | ✓ | ✓ | ✓ | ✓ | **0x09** | ST25R300 RxAnalog1 at 0x09 |
| 0x0C | RX_CONF2 | ✓ | ✓ | ✓ | ✓ | ⚠️ | - |
| 0x16 | MASK_MAIN | ✓ | ✓ | ✓ | ✓ | **0x39** | ST25R300 IRQ_MASK1 at 0x39 |
| 0x17 | MASK_TIMER | ✓ | ✓ | ✓ | ✓ | **0x3A** | ST25R300 IRQ_MASK2 at 0x3A |
| 0x1A | IRQ_MAIN | ✓ | ✓ | ✓ | ✓ | **0x3C** | ST25R300 IRQ_STATUS1 at 0x3C |
| 0x1B | IRQ_TIMER | ✓ | ✓ | ✓ | ✓ | **0x3D** | ST25R300 IRQ_STATUS2 at 0x3D |
| 0x1C | IRQ_ERROR | ✓ | ✓ | ✓ | ✓ | **0x3E** | ST25R300 IRQ_STATUS3 at 0x3E |
| 0x1E | FIFO_STATUS1 | ✓ | ✓ | ✓ | ✓ | **0x36** | ST25R300 FIFO_STATUS1 at 0x36 |
| 0x1F | FIFO_STATUS2 | ✓ | ✓ | ✓ | ✓ | **0x37** | ST25R300 FIFO_STATUS2 at 0x37 |
| 0x20 | COLLISION_DISPLAY | ✓ | ✓ | ✓ | ✓ | **0x38** | ST25R300 COLLISION at 0x38 |
| 0x22 | NUM_TX_BYTES1 | ✓ | ✓ | ✓ | ✓ | **0x34** | ST25R300 TX_FRAME1 at 0x34 |
| 0x23 | NUM_TX_BYTES2 | ✓ | ✓ | ✓ | ✓ | **0x35** | ST25R300 TX_FRAME2 at 0x35 |
| 0x25 | AD_CONV_RESULT | ✓ | ✓ | ✓ | ✓ | **0x4C** | ST25R300 SENSE_RF at 0x4C |
| 0x28 | TX_DRIVER_CONF | ⚠️ | ⚠️ | ✓ | ✓ | **0x03** | **Different bit mapping** |
| 0x3F | IC_IDENTITY | ✓ | ✓ | ✓ | ✓ | ✓ | Same address (0x3F) |

### Key Register Details

#### IO_CONF1 (0x00)

| Bit | Name | Description |
|-----|------|-------------|
| 7 | - | Reserved |
| 6 | single | 0=differential, 1=single-ended |
| 5-0 | - | Configuration bits |

**Variant Differences:**
- ST25R39xx: Full power in differential mode
- ST25R39xxB: Same behavior
- ST25R200/100: Same
- ST25R500: Same

#### IO_CONF2 (0x01)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | sup3V | 1=3.3V supply, 0=5V supply |

**All variants:** Same behavior

#### OP_CONTROL (0x02)

| Bit | Name | Description |
|-----|------|-------------|
| 7 | en | Global enable |
| 6 | rx_en | Receiver enable |
| 5-4 | - | Reserved |
| 3 | tx_en | Transmitter enable |
| 2-0 | - | Operation mode |

**All variants:** Same behavior

#### MODE (0x03)

| Value | Mode | Description |
|-------|------|-------------|
| 0x00 | Idle | No operation |
| 0x08 | ISO14443A | NFC-A initiator |
| 0x09 | ISO14443B | NFC-B initiator |
| 0x0A | FeliCa | NFC-F initiator |
| 0x0B | ISO15693 | NFC-V initiator |
| 0x10 | P2P Init | Active P2P initiator |
| 0x11 | P2P Target | Active P2P target |
| 0x20 | CE Type A | Card emulation Type A |
| 0x21 | CE Type F | Card emulation Type F |

**Variant Differences:**
- ST25R200/100: Limited to 106 kbps for NFC-A/B
- ST25R500: NFC-V supports up to 212 kbps
- ST25R501: No card emulation modes

#### BIT_RATE (0x04)

| Bits | Name | Description |
|------|------|-------------|
| 7-6 | tx_rate | TX bit rate selector |
| 5-4 | rx_rate | RX bit rate selector |
| 3-0 | - | Reserved |

**Bit Rate Encoding:**
```
0x0: fc/128 (106 kbps)
0x1: fc/64  (212 kbps)
0x2: fc/32  (424 kbps)
0x3: fc/16  (848 kbps)
```

**Variant Differences:**
- ST25R200/100: Only 0x0 supported (106 kbps) for NFC-A/B
- ST25R500: NFC-V supports fc/32 (212 kbps)
- ST25R39xx: NFC-V limited to fc/128 (53 kbps)

#### ISO14443A_CONF (0x05)

| Bit | Name | Description |
|-----|------|-------------|
| 7-2 | - | Reserved |
| 1 | no_rx_par | 1=no parity reception |
| 0 | no_tx_par | 1=no parity transmission |

**Mifare Classic Mode:** Set to 0xC0 (no_tx_par | no_rx_par) for 9-bit software parity

**All variants:** Same behavior for NFC-A

---

## Variant-Specific Register Differences

### ST25R39xx vs ST25R39xxB - RX_CONF3 (0x0D)

**⚠️ This difference caused a complete reception failure on the STEVAL-MB17149B.**

RX_CONF3 bit layout (same address both variants):

| Bits | Name | Description |
|------|------|-------------|
| 7-5 | rg1_am[2:0] | AM channel gain: 0=full, 1-6=−2.5dB/step, **7=+5.5dB boost** |
| 4-2 | rg1_pm[2:0] | PM channel gain (same encoding) |
| 1 | lf_en | **1 = "LF signal on receiver input"** — routes receiver off the HF 13.56 MHz path |
| 0 | lf_op | LF operation mode |

**`lf_en=1` (bit1) disables HF NFC reception.** Any value with bit1 set (0xE2, 0xE3, etc.) will prevent the chip from receiving ATQA/ANTICOL responses on any properly matched 13.56 MHz antenna.

**Value `0xE2` = `0b11100010`:**
- `rg1_am=7` (+5.5 dB boost) — may help compensate for a weak, untuned antenna
- `lf_en=1` — disables HF receive path

This value happened to work on the non-B Elechouse module (small ferrite, unmatched antenna) but **silently kills tag detection on the B-version STEVAL board** (well-tuned 66×66 mm PCB antenna).

**Correct values:**
- ST25R3916 (non-B, Elechouse module): `0xE2` — gain boost compensates for weak antenna
- ST25R3916B (STEVAL, tuned PCB antenna): `0x00` — HF path, full gain

The driver uses `is_b_version_` (set from IC_IDENTITY in `reset_()`) to select the right value.

**Symptom of wrong value:** `WUPA timeout: IRQ_MAIN=0x00 IRQ_TIMER=0x00 AMP=64` — field is on (AMP>0), WUPA sent, but zero IRQ ever fires. Tag is invisible.

---

### ST25R39xx vs ST25R39xxB - TX_DRIVER_CONF (0x28)

This is the **most critical difference** for migration.

#### ST25R39xx (Non-B)

```
Bit 7-4: am_mod[3:0] - ASK modulation depth
  0x0: ~5%
  0x7: ~12%
  0xF: ~40%
  
Bit 3-0: d_res[3:0] - Driver resistance
  0x0: Maximum power
  0xF: Minimum power
```

#### ST25R39xxB

```
Bit 7-4: am_mod[3:0] - ASK modulation depth (extended range)
  0x0: 0%
  0x1: ~5%
  0x7: ~10%
  0xF: 82%
  
Bit 3-0: d_res[3:0] - Driver resistance (finer granularity)
  0x0: Maximum power (extended range)
  0xF: Minimum power
  Steps are smaller than non-B variants
```

**Migration Table:**

| Application | ST25R39xx Value | ST25R39xxB Value |
|-------------|-----------------|------------------|
| NFC-A 10% modulation | 0x7X | 0x6X |
| NFC-A 30% modulation | 0xCX | 0xAX |
| Maximum power | 0xX0 | 0xX0 |
| Minimum power | 0xXF | 0xXF |

**Note:** X = driver resistance setting. Recalculate based on antenna matching.

---

### ST25R39xx - Capacitive Sensing Registers (Space B)

**Removed in ST25R39xxB** - Pins repurposed as TAD (Test Analog Digital)

| Addr | Name | Description |
|------|------|-------------|
| 0x40 | CAPACITIVE_SENSING_CONF | Capacitive sensor configuration |
| 0x41 | CAPACITIVE_SENSING_RESULT | Measurement result |

**Migration:** Remove all capacitive sensing code when migrating to B variants. Use inductive wake-up instead.

---

### ST25R39xxB - New AWS Registers

**Enhanced Active Wave Shaping** - New in B variants

| Addr | Name | Description |
|------|------|-------------|
| 0x50 | AWS_CONF1 | AWS configuration 1 |
| 0x51 | AWS_CONF2 | AWS configuration 2 |
| 0x52 | AWS_TIMING | AWS timing parameters |
| 0x53 | AWS_SHAPE_CONF | Wave shaping profile |

**Enable Enhanced AWS:**
```cpp
// Set rgs_am bit in auxiliary modulation register
write_register(AUX_MODULATION, read_register(AUX_MODULATION) | 0x01);

// Configure AWS registers
write_register(AWS_CONF1, 0xXX);
write_register(AWS_CONF2, 0xXX);
write_register(AWS_TIMING, 0xXX);
```

**VDD_AM Capacitor:** Change from 2.2µF to 10–50nF when enabling enhanced AWS.

---

### ST25R39xxB - EMVCo 3.1a/3.2a EMD Registers

**EMD (Electromagnetic Disturbance) Handling**

| Addr | Name | Description |
|------|------|-------------|
| 0x58 | EMD_CONF | EMD configuration |
| 0x59 | EMD_THRESHOLD | EMD detection threshold |
| 0x5A | EMD_COUNTER | EMD event counter |

**EMVCo 3.2a Compliance:** Required for payment applications. Automatically enabled in RFAL library.

---

### ST25R200/100 - Simplified Register Set

**Removed Features (no registers):**
- No AAT registers
- No capacitive sensing registers
- No I²C configuration registers
- Simplified IRQ mask registers

**Wake-up Timer Register Differences:**

| Addr | Name | ST25R39xx | ST25R200/100 |
|------|------|-----------|--------------|
| 0x30 | WUT_CONFIG | ✓ | ✓ (different bit mapping) |
| 0x31 | WUT_PERIOD | ✓ | ✓ (different encoding) |

**ST25R200/100 Wake-up Period Encoding:**
```
wut[3:0] in WUT_CONFIG:
  0: 9.7 ms
  1: 13.3 ms
  2: 19.3 ms
  3: 26.6 ms
  4: 38.7 ms
  5: 53.2 ms
  6: 77.3 ms
  7: 106.3 ms
  8: 154.7 ms
  9: 212.7 ms
  10: 309.3 ms
  11: 425.3 ms
  12: 618.6 ms
  13: 850.6 ms
  14: 1237.3 ms
  15: 1737.3 ms
```

---

### ST25R500 - New GPIO Multiplexing Registers

**Passive Load Modulation GPIO Control**

| Addr | Name | Description |
|------|------|-------------|
| 0x60 | GPIO_MUX_CONF | GPIO multiplexing configuration |
| 0x61 | GPIO1_CONF | GPIO1 function selection |
| 0x62 | GPIO2_CONF | GPIO2 function selection |
| 0x63 | TAD_MUX_CONF | TAD pin multiplexing |

**Passive Load Modulation Options:**
- GPIO1 only
- GPIO1 + GPIO2
- GPIO1 + GPIO2 + TAD1 + TAD2

**Migration from ST25R39xx:**
- EXT_LM pin removed
- Configure via GPIO_MUX_CONF instead

---

### ST25R500 - Regulator Control Registers

**Enhanced Regulator Configuration**

| Addr | Name | Description |
|------|------|-------------|
| 0x06 | REGULATOR_CONF | Regulator configuration |
| 0x07 | VDD_DR_CONFIG | VDD_DR regulator setting |
| 0x08 | PSRR_CONFIG | PSRR optimization |

**VDD_DR Configuration:**
- Fixed voltage mode
- Adaptive voltage drop mode (reference to VDD)

**Power-Down Support:**
- New registers for maintaining VDD_D in PD mode
- Regulators disabled in PD mode

---

### ST25R500 - NFC-V High Bit Rate Registers

**NFC-V up to 212 kbps**

| Addr | Name | Description |
|------|------|-------------|
| 0x09 | NFC_V_CONF | NFC-V configuration |
| 0x0A | NFC_V_BITRATE | NFC-V bit rate selection |

**Bit Rate Options:**
```
0x0: fc/256 (53 kbps)
0x1: fc/128 (106 kbps)
0x2: fc/64  (212 kbps) - ST25R500 only
```

---

### ST25RN300 - NCI Interface

**No direct register access** - All communication via NCI protocol

**NCI Command Format:**
```
[Header: 3 bytes] [Payload Length: 1 byte] [Payload: N bytes]
```

**Register-like Configuration via NCI:**
- RF Discovery configuration
- RF Parameter configuration
- Device configuration

**Migration:** Complete software rewrite required. Cannot use direct register access.

---

### ST25R300 - Unified Architecture

The ST25R300 (and R300 series) uses a significantly different register map compared to the 3916/500 family. It features a unified 0x00–0x57 address space without the Space A/B split.

#### OPERATION (0x00)

Replaces `OP_CONTROL` and `IO_CONF1` for basic enable.

| Bit | Name | Description |
|-----|------|-------------|
| 6 | tx_en | Transmitter enable |
| 5 | rx_en | Receiver enable |
| 4 | vdddr_en | VDD_DR regulator enable |
| 3 | en | Global chip enable |

#### PROTOCOL1 (0x14)

Replaces `MODE` for protocol selection.

| Value | Mode | Description |
|-------|------|-------------|
| 0x01 | ISO14443A | NFC-A initiator |
| 0x02 | ISO14443B | NFC-B initiator |
| 0x03 | FeliCa | NFC-F initiator |
| 0x05 | ISO15693 | NFC-V initiator |

#### TX_PROTOCOL1 (0x15)

Replaces `ISO14443A_CONF` for parity/CRC.

| Bit | Name | Description |
|-----|------|-------------|
| 6 | a_tx_par | 1=ISO14443A TX parity enable |
| 5 | tx_crc | 1=TX CRC enable |
| 4 | tr_am | 0=OOK, 1=AM modulation |

#### FIFO SPI Access

ST25R300 uses fixed SPI command bytes for FIFO access instead of register-based streaming:
- **FIFO Write:** `0x5F`
- **FIFO Read:** `0xDF` (0x80 | 0x5F)

#### ST25R300 Command Set

| Command | Hex | Description |
|---------|-----|-------------|
| SET_DEFAULT | 0x61 | Reset to power-up state |
| STOP_ALL | 0x63 | Stop activities + clear FIFO |
| CLEAR_FIFO | 0x65 | Clear FIFO only |
| FIELD_ON | 0x6F | RF field on |
| MASK_RX | 0x71 | Stop receiver |
| UNMASK_RX | 0x73 | Start receiver |

---

## IRQ Register Details

### IRQ_MAIN (0x1A) - Read-to-Clear

| Bit | Mask | Name | Description |
|-----|------|------|-------------|
| 7 | 0x80 | I_osc | Oscillator stable |
| 6 | 0x40 | I_wl | FIFO water level |
| 5 | 0x20 | I_rxs | Start of receive |
| 4 | 0x10 | I_rxe | End of receive (tag responded) |
| 3 | 0x08 | I_txe | End of transmission |
| 2 | 0x04 | I_col | Bit collision detected |
| 1 | 0x02 | I_rx_rest | RX restart |
| 0 | 0x01 | - | Reserved |

**All variants:** Same behavior

### IRQ_TIMER (0x1B) - Read-to-Clear

| Bit | Mask | Name | Description |
|-----|------|------|-------------|
| 7 | 0x80 | I_dct | Direct command terminated |
| 6 | 0x40 | I_nre | No-response timer expired |
| 5 | 0x20 | I_gpe | General purpose timer |
| 4 | 0x10 | I_eon | External field detected |
| 3 | 0x08 | I_eof | External field dropped |
| 2-0 | - | - | Reserved |

**All variants:** Same behavior

### IRQ_ERROR (0x1C) - Read-to-Clear

| Bit | Mask | Name | Description |
|-----|------|------|-------------|
| 7 | 0x80 | I_parity | Parity error |
| 6 | 0x40 | I_crc | CRC error |
| 5 | 0x20 | I_fifo_ov | FIFO overflow |
| 4 | 0x10 | I_fifo_un | FIFO underflow |
| 3-0 | - | - | Reserved |

**All variants:** Same behavior

---

## FIFO Status Registers

### FIFO_STATUS1 (0x1E)

| Bit | Name | Description |
|-----|------|-------------|
| 7-0 | fifo_count[7:0] | FIFO byte count LSB |

### FIFO_STATUS2 (0x1F)

| Bit | Name | Description |
|-----|------|-------------|
| 7-6 | fifo_count[9:8] | FIFO byte count MSB |
| 5 | underflow | FIFO underflow flag |
| 4 | overflow | FIFO overflow flag |
| 3-1 | fifo_lb | Last byte valid bits (0=8 bits) |
| 0 | - | Reserved |

**FIFO Capacity by Variant:**
- ST25R3914/15: 96 bytes
- ST25R200/100/300: 256 bytes
- ST25R39xx/39xxB/500/501: 512 bytes
- ST25R95: 528 bytes

**Decoding:**
```cpp
uint8_t s1 = read_register(0x1E);
uint8_t s2 = read_register(0x1F);
uint16_t bytes_in_fifo = ((s2 & 0xC0) >> 6 << 8) | s1;
uint8_t last_byte_bits = (s2 >> 1) & 0x07;  // 0 = 8 bits
bool overflow  = (s2 & 0x10) != 0;
bool underflow = (s2 & 0x20) != 0;
```

---

## IC Identity Register

### IC_IDENTITY (0x3F)

| Bit | Name | Description |
|-----|------|-------------|
| 7-3 | ic_type | IC type identifier |
| 2-0 | ic_version | IC version/revisions |

**IC Type Values:**
```
ST25R3916:   (ic_type & 0xF8) == 0x28
ST25R3916B:  (ic_type & 0xF8) == 0x30   ← different from non-B!
ST25R200:    Different value
ST25R500:    Different value
```

**Driver Detection:**
```cpp
uint8_t id = read_register(0x3F);
uint8_t chip_type = id & 0xF8;
bool is_b_version = (chip_type == 0x30);
if (chip_type != 0x28 && chip_type != 0x30) {
    // unknown chip
}
```

---

## Space B Registers (0x40–0x7F)

**Important:** Current ESPHome driver masks `addr & 0x3F`, preventing Space B access.

### ST25R39xx Space B

| Addr | Name | Description |
|------|------|-------------|
| 0x40-0x4F | Capacitive sensing | Removed in B variants |
| 0x50-0x5F | Reserved/Test | - |
| 0x60-0x6F | CORR_CONF | Correlator configuration |
| 0x70-0x7F | Test registers | Factory test only |

### ST25R39xxB Space B

| Addr | Name | Description |
|------|------|-------------|
| 0x40-0x4F | TAD registers | Test Analog Digital (repurposed) |
| 0x50-0x5F | AWS registers | Enhanced wave shaping |
| 0x60-0x6F | EMD registers | EMVCo EMD handling |
| 0x70-0x7F | Test registers | Factory test only |

### ST25R500 Space B

| Addr | Name | Description |
|------|------|-------------|
| 0x60-0x6F | GPIO multiplexing | Passive load modulation |
| 0x70-0x7F | Test registers | Factory test only |

---

## Command Set Differences

### Universal Commands (All Variants)

| Command | Hex | Description |
|---------|-----|-------------|
| SET_DEFAULT | 0xC1 | Reset to power-up defaults |
| STOP_ALL | 0xC2/0xC3 | Stop all activity, clear FIFO |
| TRANSMIT_WITH_CRC | 0xC4 | Transmit FIFO with CRC-A |
| TRANSMIT_WITHOUT_CRC | 0xC5 | Transmit without CRC |
| TRANSMIT_REQA | 0xC6 | Send REQA (wake IDLE tags) |
| TRANSMIT_WUPA | 0xC7 | Send WUPA (wake IDLE+HALT tags) |
| FIELD_ON | 0xC8 | Turn RF field on |
| FIELD_OFF | 0xC9 | Turn RF field off |
| GOTO_SENSE | 0xCD | Go to sense/idle state |
| GOTO_SLEEP | 0xCE | Go to sleep/halt state |
| MASK_RECEIVE | 0xD0 | Stop receivers |
| UNMASK_RECEIVE | 0xD1 | Start receivers |
| MEASURE_AMPLITUDE | 0xD3 | Measure RF amplitude |
| RESET_RX_GAIN | 0xD5 | Reset AGC/squelch |
| ADJUST_REGULATORS | 0xD6 | Calibrate regulators |

### ST25R39xxB - New Commands

| Command | Hex | Description |
|---------|-----|-------------|
| AWS_ENABLE | 0xD8 | Enable enhanced AWS |
| EMD_CONFIGURE | 0xD9 | Configure EMD handling |

### ST25R500 - New Commands

| Command | Hex | Description |
|---------|-----|-------------|
| GPIO_MUX_CONFIG | 0xDA | Configure GPIO multiplexing |

### Invalid Commands

**0xDB is NOT a valid command** - Earlier documentation was incorrect.

---

## Driver Migration Checklist

### From ST25R39xx to ST25R39xxB

- [ ] Update IC detection: `(id & 0xF8) == 0x28` → also accept `0x30`; set `is_b_version_` flag
- [ ] **Update RX_CONF3: use `0x00` for B-version** (not `0xE2` — `lf_en=1` kills HF reception on tuned antennas)
- [ ] Update TX_DRIVER_CONF (0x28) calculation (am_mod mapping differs)
- [ ] Remove capacitive sensing code
- [ ] Add enhanced AWS configuration (optional)
- [ ] Update EMVCo EMD handling for 3.1a/3.2a
- [ ] Change VDD_AM capacitor (2.2µF → 10–50nF if using AWS)

### From ST25R39xx to ST25R200

- [ ] Update FIFO size (512 → 256 bytes)
- [ ] Remove AAT code
- [ ] Remove capacitive sensing code
- [ ] Remove I²C code (SPI only)
- [ ] Update wake-up timer configuration
- [ ] Check NFC-V bit rate (53 kbps max)
- [ ] Update PCB (24-pin vs 32-pin)

### From ST25R39xx to ST25R500

- [ ] Update FIFO handling (same size, different timing)
- [ ] Remove AAT code
- [ ] Remove capacitive sensing code
- [ ] Remove I²C code (SPI only)
- [ ] Add GPIO multiplexing configuration
- [ ] Update regulator configuration
- [ ] NFC-V can use 212 kbps
- [ ] Add RESET pin handling (optional)
- [ ] Connect exposed pad to GND
- [ ] Update PCB (different pinout)

### From ST25R39xx to ST25R300

- [ ] Update IC detection: `(id & 0xF8) == 0xB0`
- [ ] **Complete register map remap**: Addresses are shifted from ST25R39xx family
- [ ] Update operation control (0x02 → 0x00)
- [ ] Update FIFO access (0x5F/0xDF command bytes)
- [ ] Update IRQ registers (Shifted to 0x3C-0x3E)
- [ ] Update direct command hex values (0xC1 → 0x61, etc.)
- [ ] Update regulator control logic (new VDD_DR management)
- [ ] Update protocol configuration (MODE 0x03 → PROTOCOL1 0x14)

### From Any Variant to ST25RN300

- [ ] Complete software rewrite (NCI stack)
- [ ] Remove all direct register access
- [ ] Implement NCI protocol
- [ ] Update power management
- [ ] Add firmware update mechanism
- [ ] Complete PCB redesign (WLCSP49)

---

## Revision History

| Date | Changes |
|------|---------|
| 2026-03-16 | Initial register map comparison created from datasheets |

---

## References

- ST25R3916 datasheet: DS12484 Rev 8
- ST25R3916B datasheet: DS13541 Rev 10
- ST25R200 datasheet: DS13658 Rev 2
- ST25R500 datasheet: DS14593 Rev 1
- Migration notes: AN5768, AN5965, AN6313
