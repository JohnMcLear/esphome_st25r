# ST25R API and Protocol Documentation

This document provides technical reference for the ST25R component API, register map, SPI protocol, and supported protocol flows.

## Variant Support Overview

This component is primarily designed for **ST25R3916/ST25R3916B** but supports multiple ST25R variants. Key differences:

| Variant | FIFO | SPI Max | NFC-A/B Max | NFC-V Max | Card Emulation | AAT | Wake-up | Temp Range |
|---------|------|---------|-------------|-----------|----------------|-----|---------|------------|
| **ST25R3916/17** | 512 | 10 Mbps | 848 kbps | 53 kbps | ✓ | ✓ | Capacitive+Inductive | -40°C to +105°C |
| **ST25R3916B/17B/19B** | 512 | 10 Mbps | 848 kbps | 53 kbps | ✓ | ✓ | Inductive only | -40°C to +105°C |
| **ST25R3914/15** | 96 | 6 Mbps | 848 kbps | 53 kbps | ? | ✓ (3914) | Capacitive+Inductive | -40°C to +125°C |
| **ST25R200** | 256 | 10 Mbps | 106 kbps | 53 kbps | ✗ | ✗ | Inductive only | -40°C to +85°C |
| **ST25R100** | 256 | 6 Mbps | 106 kbps | 53 kbps | ✗ | ✗ | Inductive only | -25°C to +85°C |
| **ST25R500/300** | 512 | 10 Mbps | 848 kbps | **212 kbps** | ✓ | ✗ | Inductive only | -40°C to +125°C |
| **ST25R501** | 512 | 10 Mbps | 848 kbps | **212 kbps** | ✗ | ✗ | Inductive only | -40°C to +125°C |

**Critical Migration Notes:**
- **ST25R39xx → ST25R39xxB:** TX_DRIVER register (0x28) has different bit mapping; capacitive sensing removed
- **ST25R39xx → ST25R200/100:** Not pin-compatible; FIFO halved; no I²C; no high bit rates
- **ST25R39xx → ST25R500:** Not pin-compatible; GPIO multiplexing changed; NFC-V up to 212 kbps
- **Space B registers (0x40–0x7F):** Current driver masks `addr & 0x3F`, preventing writes without modification

See `memory/st25r_variant_comparison.md` for comprehensive variant details and `memory/st25r_register_differences.md` for register-level differences.

## Table of Contents

- [Variant Support Overview](#variant-support-overview)
- [Component API](#component-api)
- [YAML Configuration](#yaml-configuration)
- [ISO14443A Protocol](#iso14443a-protocol)
- [Mifare Classic / Crypto1](#mifare-classic--crypto1)
- [Register Map](#register-map)
- [SPI Communication](#spi-communication)
- [Command Set](#command-set)
- [Interrupt Handling](#interrupt-handling)
- [FIFO Operations](#fifo-operations)

---

## Component API

### Public Methods

#### `is_tag_present()`
```cpp
bool is_tag_present() const
```
Returns `true` if any tag is currently in the present-tags set (i.e. confirmed seen within the miss-count window).

#### `ndef_write()`
```cpp
bool ndef_write(nfc::NdefMessage *message, bool format = false)
```
Write an NDEF message to a Type 2 tag (NTAG / Ultralight). Set `format = true` to format the tag first.

#### `clean_tag()`
```cpp
bool clean_tag()
```
Erase all NDEF content from the tag currently in field.

### Configuration Setters (called by generated code)

```cpp
void set_reset_pin(GPIOPin *reset_pin)
void set_irq_pin(InternalGPIOPin *irq_pin)
void set_rf_field_enabled(bool enabled)
void set_rf_power(uint8_t power)                  // 0–15; 15 = max driver strength
void set_supply_3v3(bool supply_3v3)
void set_mifare_key_a(uint64_t key)               // 48-bit key, e.g. 0xFFFFFFFFFFFFULL
void set_mifare_key_b(uint64_t key)
void set_health_check_enabled(bool enabled)       // enable/disable IC liveness check
void set_health_check_interval(uint32_t ms)       // milliseconds between checks (default 60000)
void set_max_failed_checks(uint8_t n)             // failures before reinit (default 3)
void set_auto_reset_on_failure(bool enabled)      // auto-reinit on max failures
```

### Trigger Registration

```cpp
void register_on_tag_trigger(ST25RTagTrigger *trig)
void register_on_tag_removed_trigger(ST25RTagRemovedTrigger *trig)
void register_tag(ST25RBinarySensor *tag)
void set_status_binary_sensor(binary_sensor::BinarySensor *sensor)
void set_field_strength_sensor(sensor::Sensor *sensor)
```

### Protected Transport Interface (implement in subclass)

```cpp
virtual uint8_t read_register(uint8_t reg) = 0;
virtual void write_register(uint8_t reg, uint8_t value) = 0;
virtual void write_command(uint8_t command) = 0;
virtual void write_fifo(const uint8_t *data, size_t len) = 0;
virtual void read_fifo(uint8_t *data, size_t len) = 0;
```

> **Note:** `write_register` masks `addr & 0x3F`, so Space B registers (0x40–0x7F) cannot be written via the SPI transport without fixing the driver.

---

## YAML Configuration

### SPI

```yaml
spi:
  clk_pin: GPIO19
  miso_pin: GPIO10
  mosi_pin: GPIO18

st25r_spi:
  id: my_st25r
  cs_pin: GPIO6
  irq_pin: GPIO7
  update_interval: 500ms
  rf_power: 15
  supply_3v3: true
  on_tag:
    then:
      - logger.log:
          format: "Tag: %s"
          args: ['x.c_str()']
  on_tag_removed:
    then:
      - logger.log:
          format: "Removed: %s"
          args: ['x.c_str()']
```

### I2C

```yaml
i2c:
  sda: GPIO10
  scl: GPIO19

st25r_i2c:
  id: my_st25r
  address: 0x50
  irq_pin: GPIO5
  update_interval: 1s
  on_tag:
    then:
      - logger.log:
          format: "Tag: %s"
          args: ['x.c_str()']
```

### Binary Sensor (specific tag tracking)

```yaml
binary_sensor:
  - platform: st25r
    st25r_id: my_st25r
    name: "My NFC Ring"
    uid: "04-1A-A7-67-5F-61-80"
```

### Health check

The health check reads the `IC_IDENTITY` register (0x3F) at a configurable interval — independently of the tag scan rate — to verify the chip is still alive on the bus. This detects SPI/I2C connectivity loss, brownout, or chip hang that would otherwise silently stop tag detection.

```yaml
st25r_spi:
  # ...
  # Check chip identity every 60 s (default). After 3 consecutive failures,
  # reinitialize_() is called. The 500 ms tag scan rate is unaffected.
  health_check_enabled: true       # default: true
  health_check_interval: 60s       # default: 60s — independent of update_interval
  max_failed_checks: 3             # default: 3 — consecutive failures before reinit
  auto_reset_on_failure: true      # default: true — call reinitialize_() on failure
```

**How it works:**
1. Every `health_check_interval`, `update()` reads `IC_IDENTITY` (0x3F).
2. If `(identity & 0xF8) != 0x28` and `!= 0x30`, the check fails and `health_check_failures_` is incremented.
3. The `status` binary sensor publishes `false` on any failure.
4. Once `health_check_failures_ >= max_failed_checks` and `auto_reset_on_failure` is set, `state_` is set to `STATE_REINITIALIZING`.
5. `reinitialize_()` performs a GPIO reset (if `reset_pin` configured) then calls `reset_()`.
6. On success: `health_check_failures_` is cleared, `last_health_check_ms_` is zeroed (forcing an immediate re-check), and the `status` sensor publishes `true`.
7. After up to 5 failed reinit attempts, the component calls `mark_failed()`.

### Optional sensors

```yaml
st25r_spi:
  # ...
  status:
    name: "NFC Reader Health"   # binary_sensor: true=OK, false=hardware fault or recovering
  field_strength:
    name: "NFC Field Strength"  # sensor: raw amplitude ADC value
```

---

## ISO14443A Protocol

### Multi-tag detection flow

This component implements a full ISO14443A binary tree anticollision search, detecting all tags simultaneously:

```
update() → WUPA command → STATE_WUPA
  ↓ IRQ_RXE or IRQ_COL
  anticollision loop (STATE_ANTICOL)
    ↓ collision → narrow prefix → retry
    ↓ no collision → SELECT → read SAK
      if SAK bit2 set → cascade (CL2/CL3)
      else → tag complete → HALT tag → WUPA → next branch
  ↓ no more tags (NRE timeout)
  finalize_scan_() → fire triggers → STATE_IDLE
```

### Frame formats

#### WUPA (wake both IDLE and HALT state tags)
```
7 bits, no CRC, value: 0x52
```
> REQA (0x26) only wakes IDLE-state tags. After HALTing a Mifare Classic, use WUPA.

#### Anticollision frame (variable prefix length)
```
SEL byte: 0x93 (CL1), 0x95 (CL2), 0x97 (CL3)
NVB byte: encodes full_bytes and partial_bits
UID prefix: 0–4 bytes
```

#### SELECT frame
```
[SEL, 0x70, UID0, UID1, UID2, UID3, BCC, CRC0, CRC1]
```

#### SAK response
```
[SAK, CRC0, CRC1]
SAK bit 2 (0x04): cascade — UID incomplete, do next level
SAK bit 5 (0x20): ISO14443-4 compliant (T=CL card)
SAK = 0x08: Mifare Classic 1K
SAK = 0x18: Mifare Classic 4K
SAK = 0x00: Mifare Ultralight / NTAG
```

### NUM_TX_BYTES encoding (anticollision partial frames)

For N full bytes + B partial bits:
```
NUM_TX_BYTES1 (0x22) = N >> 5
NUM_TX_BYTES2 (0x23) = ((N & 0x1F) << 3) | (B & 0x07)
```

### COLLISION_DISPLAY decoding

Register 0x20 after a collision IRQ:
```
c_byte = (col_raw >> 4) & 0x0F   // full bytes before collision
c_bit  = (col_raw >> 1) & 0x07   // bit within that byte
col_pos_abs = c_byte * 8 + c_bit  // from start of TX frame (incl. SEL+NVB)
uid_col_pos = col_pos_abs - 16    // subtract 2 header bytes
```

---

## Mifare Classic / Crypto1

### Authentication flow (3-pass mutual auth)

```
Reader                              Tag
──────                              ───
AUTH1 (0x60/0x61 + block) ────────>
                           <──────  NT (4-byte nonce)
NR + AR (8 bytes, encrypted) ─────>
                           <──────  AT (4 bytes, encrypted)
[Verify AT; further commands encrypted]
```

### Crypto1 key schedule

```cpp
crypto1_init(&cs, key);          // load 48-bit key into LFSR
crypto1_word(&cs, NT ^ UID, 0);  // prime with tag nonce XOR UID
```

### NR + AR computation

```cpp
// NR: reader nonce (4 bytes), encrypted + parity
for (int i = 0; i < 4; i++) {
    nr_enc[i]     = crypto1_byte(&cs, nr[i], 0) ^ nr[i];
    nr_par[i]     = crypto1_bit(&cs, 0, 0) ^ ODD_PARITY[nr[i]];
}

// AR: prng_successor(NT, 64), MSB-first, encrypted + parity
uint32_t ar_plain = prng_successor(NT, 64);
for (int i = 0; i < 4; i++) {
    uint8_t b     = (ar_plain >> (24 - 8*i)) & 0xFF;
    ar_enc[i]     = crypto1_byte(&cs, 0, 0) ^ b;
    ar_par[i]     = crypto1_bit(&cs, 0, 0) ^ ODD_PARITY[b];
}
```

### AT verification

```cpp
uint32_t at_expected = prng_successor(ar_plain, 32) ^ crypto1_word(&cs, 0, 0);
// Compare against received AT bytes
```

### Critical rule: parity bits advance LFSR

`crypto1_filter(cs->odd)` reads the output without advancing the LFSR state.
`crypto1_bit(cs, 0, 0)` advances by 1 bit AND returns the output.

**Always use `crypto1_bit(cs, 0, 0)` for parity bytes**, both when encoding TX and decoding RX. Using `crypto1_filter` here causes all subsequent keystream bytes to be 1 bit behind and is undetectable until AT verification fails.

### Clone card behaviour

Cards where NT never changes (fixed nonce) are clone/magic cards with broken PRNG. They respond to AUTH1 with the static NT but silently HALT on receiving NR+AR. This is not a driver bug — genuine NXP Mifare Classic 1K generates a new random NT on each auth attempt.

### 9-bit parity transceive (Mifare mode)

Mifare Classic frames use 9-bit format: 8 data bits + 1 parity bit per byte. The chip is set to `ISO14443A_CONF = 0xC0` (`no_tx_par | no_rx_par`) so software packs/unpacks these manually:

```
TX FIFO layout for N bytes:
  byte 0 bits [7:0] = data[0]
  byte 1 bits [0]   = parity[0], bits [7:1] = data[1][6:0]
  ... (9-bit packed, LSB first)
```

---

## Register Map

Verified against DS12484 Rev 8 (ST25R3916). All addresses are Space A (0x00–0x3F).

### Variant-Specific Register Notes

**TX_DRIVER_CONF (0x28) - Critical Difference:**

| Variant | am_mod (bits 7-4) | d_res (bits 3-0) |
|---------|-------------------|------------------|
| ST25R3916/17 | 5–40% (0x0=~5%, 0x7=~12%, 0xF=~40%) | 4-bit range |
| ST25R3916B/17B/19B | **0–82%** (0x0=0%, 0x7=~10%, 0xF=82%) | **Extended range, finer steps** |
| ST25R200/100 | Similar to 3916 | Similar to 3916 |
| ST25R500 | Similar to 3916B | Similar to 3916B |

> **Migration Warning:** Cannot directly copy TX_DRIVER_CONF values between ST25R39xx and ST25R39xxB. Recalculate based on desired modulation index.

**FIFO Capacity by Variant:**
- ST25R3914/15: **96 bytes**
- ST25R200/100: **256 bytes**
- ST25R39xx/39xxB/500/501: **512 bytes**
- ST25R95: **528 bytes**

**BIT_RATE (0x04) - NFC-V Differences:**
- ST25R39xx/39xxB/200/100: NFC-V max 53 kbps (fc/128)
- ST25R500/501: NFC-V max **212 kbps** (fc/64)

### Register Table

| Address | Name | Key bits / usage |
|---------|------|-----------------|
| 0x00 | IO_CONF1 | bit6=single (0=differential, full power) |
| 0x01 | IO_CONF2 | bit0=sup3V (1 for 3.3V supply) |
| 0x02 | OP_CONTROL | bit7=en, bit6=rx_en, bit3=tx_en |
| 0x03 | MODE | 0x08 = ISO14443A initiator; see variant notes for CE support |
| 0x04 | BIT_RATE | 0x00 = fc/128 (NFC-A 106 kbps); NFC-V max varies by variant |
| 0x05 | ISO14443A_CONF | bit0=antcl (anticollision enable); 0xC0=no_tx_par+no_rx_par (Mifare mode) |
| 0x0B | RX_CONF1 | Receiver config (Table 40) |
| 0x0C | RX_CONF2 | Receiver config (Table 41) |
| 0x16 | MASK_MAIN | 0x00 = unmask all IRQ_MAIN sources |
| 0x17 | MASK_TIMER | 0x00 = unmask all (includes NRE) |
| 0x1A | IRQ_MAIN | Read-to-clear; Table 62 |
| 0x1B | IRQ_TIMER | Read-to-clear; Table 63 |
| 0x1C | IRQ_ERROR | Read-to-clear; Table 64 |
| 0x1E | FIFO_STATUS1 | bits[7:0] = FIFO byte count LSB |
| 0x1F | FIFO_STATUS2 | bits[7:6]=count MSB, bit5=underflow, bit4=overflow, bits[3:1]=fifo_lb |
| 0x20 | COLLISION_DISPLAY | bits[7:4]=c_byte, bits[3:1]=c_bit |
| 0x22 | NUM_TX_BYTES1 | ntx[12:5] |
| 0x23 | NUM_TX_BYTES2 | ntx[4:0] in bits[7:3], nbtx[2:0] in bits[2:0] |
| 0x25 | AD_CONV_RESULT | Amplitude measurement output |
| 0x28 | TX_DRIVER_CONF | **Variant-dependent:** see table above |
| 0x3F | IC_IDENTITY | (ic & 0xF8) == 0x28 for ST25R3916/3916B |

### Space B Registers (0x40–0x7F)

> **Critical:** The SPI `write_register()` masks `addr & 0x3F`, so Space B registers **cannot be written** without fixing the driver.

**Space B by Variant:**
- **ST25R39xx:** Capacitive sensing (0x40–0x4F), CORR_CONF (0x60–0x6F)
- **ST25R39xxB:** AWS registers (0x50–0x5F), EMD registers (0x60–0x6F), TAD (0x40–0x4F)
- **ST25R500:** GPIO multiplexing (0x60–0x6F)
- **ST25R200/100:** Minimal Space B support

**Leave Space B registers at factory defaults** unless driver is updated to support Space B writes.

---

## IRQ Registers

### IRQ_MAIN (0x1A) — Table 62

| Bit | Mask | Name | Meaning |
|-----|------|------|---------|
| 7 | 0x80 | I_osc | Oscillator stable |
| 6 | 0x40 | I_wl | FIFO water level |
| 5 | 0x20 | I_rxs | Start of receive |
| 4 | 0x10 | I_rxe | **End of receive** ← tag responded |
| 3 | 0x08 | I_txe | End of transmission |
| 2 | 0x04 | I_col | **Bit collision** ← anticollision needed |
| 1 | 0x02 | I_rx_rest | Automatic reception restart |

### IRQ_TIMER (0x1B) — Table 63

| Bit | Mask | Name | Meaning |
|-----|------|------|---------|
| 7 | 0x80 | I_dct | Direct command terminated |
| 6 | 0x40 | I_nre | **No-response timer expired** ← scan timeout |
| 5 | 0x20 | I_gpe | General purpose timer |
| 4 | 0x10 | I_eon | External field detected |
| 3 | 0x08 | I_eof | External field dropped |

### IRQ clearing (p.47)

IRQ bits are cleared by: reading the IRQ register, Set Default command, Stop All command.
**Read IRQ registers BEFORE issuing Stop All** — Stop All clears the IRQ state.

---

## SPI Communication

### SPI Mode

**Mode 1: CPOL=0, CPHA=1** (clock idles low, data sampled on trailing/falling edge).
ESPHome: `CLOCK_PHASE_TRAILING`.

### Frame format (Table 11, p.50)

| Operation | First byte | Following bytes |
|-----------|-----------|-----------------|
| Register write | `0x00 \| (addr & 0x3F)` | 1 data byte |
| Register read | `0x40 \| (addr & 0x3F)` | 1 byte clocked in |
| FIFO write | `0x80` | 1–512 data bytes |
| FIFO read | `0x9F` | N bytes clocked in |
| Direct command | command byte (e.g. `0xC7`) | none |

---

## Command Set

Verified from DS12484 Table 13 (p.59). Commands are universal across variants unless noted.

### Universal Commands (All Variants)

| Command | Hex | Description |
|---------|-----|-------------|
| SET_DEFAULT | 0xC1 | Reset all registers to power-up defaults |
| STOP_ALL | 0xC2/0xC3 | Stop all activities; clears FIFO state |
| TRANSMIT_WITH_CRC | 0xC4 | Transmit FIFO contents with CRC-A appended |
| TRANSMIT_WITHOUT_CRC | 0xC5 | Transmit without CRC |
| TRANSMIT_REQA | 0xC6 | Send REQA short frame (wakes IDLE tags) |
| TRANSMIT_WUPA | 0xC7 | Send WUPA short frame (wakes IDLE + HALT tags) |
| FIELD_ON | 0xC8 | NFC initial field ON (RF collision avoidance) |
| FIELD_OFF | 0xC9 | Turn off RF field |
| GOTO_SENSE | 0xCD | Go to sense/idle state |
| GOTO_SLEEP | 0xCE | Go to sleep/halt state |
| MASK_RECEIVE | 0xD0 | Stop receivers/RX decoders |
| UNMASK_RECEIVE | 0xD1 | Start receivers/RX decoders |
| MEASURE_AMPLITUDE | 0xD3 | Measure RF field amplitude |
| RESET_RX_GAIN | 0xD5 | Reset AGC/squelch — **call before each transceive** |
| ADJUST_REGULATORS | 0xD6 | Calibrate internal regulators |

### ST25R39xxB - New Commands

| Command | Hex | Description |
|---------|-----|-------------|
| AWS_ENABLE | 0xD8 | Enable enhanced Active Wave Shaping |
| EMD_CONFIGURE | 0xD9 | Configure EMVCo 3.1a/3.2a EMD handling |

### ST25R500 - New Commands

| Command | Hex | Description |
|---------|-----|-------------|
| GPIO_MUX_CONFIG | 0xDA | Configure GPIO multiplexing for passive load modulation |

> **There is no dedicated Clear FIFO command.** Use STOP_ALL (0xC3) to clear FIFO state.
> **0xDB is NOT a valid command** — earlier documentation was wrong.

---

## FIFO Operations

### Capacity by Variant

| Variant | FIFO Size | Notes |
|---------|-----------|-------|
| ST25R3914/15 | 96 bytes | Smallest FIFO, limits frame size |
| ST25R200/100 | 256 bytes | Half of ST25R39xx |
| ST25R39xx/39xxB | 512 bytes | Standard |
| ST25R500/501 | 512 bytes | Standard |
| ST25R95 | 528 bytes | Largest FIFO |

### FIFO_STATUS decoding

```cpp
uint8_t s1 = read_register(FIFO_STATUS1);  // 0x1E
uint8_t s2 = read_register(FIFO_STATUS2);  // 0x1F
uint16_t bytes_in_fifo = ((s2 & 0xC0) >> 6 << 8) | s1;
uint8_t last_byte_bits  = (s2 >> 1) & 0x07;  // fifo_lb: valid bits in last byte (0=8)
bool overflow  = (s2 & 0x10) != 0;
bool underflow = (s2 & 0x20) != 0;
```

### Write FIFO

```
SPI: [0x80] [data byte 0] [data byte 1] ... [data byte N-1]
```

### Read FIFO

```
SPI: [0x9F] [read N bytes]
```

### Clear FIFO

Issue `STOP_ALL` (0xC3). There is no separate clear command.

---

## Protocol Support Status

### Current Component Implementation

| Protocol | Status | Notes |
|----------|--------|-------|
| ISO14443A (NFC-A) | **Working** | Full multi-tag anticollision |
| Mifare Classic auth + block read | **Working** | 9-bit parity handled in software |
| NDEF read (Type 2 / NTAG) | **Working** | Type 2 tags only |
| ISO14443B (NFC-B) | Not implemented | Hardware supports up to 848 kbps |
| ISO15693 (NFC-V) | Not implemented | 53 kbps (212 kbps on ST25R500/501) |
| FeliCa (NFC-F) | Not implemented | Hardware supports up to 424 kbps |
| Mifare Classic NDEF | Not implemented | Requires Mifare Classic support |
| Card Emulation | Not implemented | ST25R39xx/39xxB/500 only; not on R200/R100/R501 |
| P2P (Active/Passive) | Not implemented | ST25R39xx/39xxB/500 only |

### Protocol Support by Variant

| Protocol | ST25R3916/17 | ST25R39xxB | ST25R200/100 | ST25R500 | ST25R501 |
|----------|--------------|------------|--------------|----------|----------|
| NFC-A (848 kbps) | ✓ | ✓ | 106 kbps only | ✓ | ✓ |
| NFC-B (848 kbps) | ✓ | ✓ | 106 kbps only | ✓ | ✓ |
| NFC-F (424 kbps) | ✓ | ✓ | ✗ | ✓ | ✓ |
| NFC-V (53/212 kbps) | 53 kbps | 53 kbps | 53 kbps | **212 kbps** | **212 kbps** |
| Card Emulation | ✓ | ✓ | ✗ | ✓ | ✗ |
| P2P Active | ✓ | ✓ | ✗ | ✓ | ✓ |
| P2P Passive | ✓ | ✓ | ✗ | ✓ | ✓ |
| Mifare Classic | ✓ | ✓ | ✓ | ✓ | ✓ |

---

## References

### Datasheets
- ST25R3916/17: DS12484 Rev 8 (docs/st25r3916.pdf)
- ST25R3916B/17B/19B: DS13541 Rev 10
- ST25R200: DS13658 Rev 2
- ST25R100: DS14139 Rev 5
- ST25R500: DS14593 Rev 1
- ST25R501: DS14983 Rev 1
- ST25R3914/15: DS11837 Rev 9
- ST25R95: DS12807 Rev 4
- ST25RN300: DB5606 Rev 3

### Application Notes
- **AN5768:** Migrating from ST25R39xx to ST25R39xxB
- **AN5965:** Migrating from ST25R39xx to ST25R200
- **AN6313:** Migrating from ST25R39xx to ST25R500/300
- **AN6143:** Migrating from ST25R95 to ST25R200
- **AN5320:** Wake-up mode (ST25R3916/16B/17/17B/18/19B/20/20B)
- **AN5993:** Wake-up mode (ST25R200/100)
- **AN6298:** Wake-up mode (ST25R300/500/501/210)
- **AN5322:** Automatic Antenna Tuning (AAT)
- **AN6121:** NFC coil multiplexer for multi-antenna switching
- **AN5584:** Thermal design (ST25R39xx)
- **AN6463:** Thermal design (ST25R210/300/500/501)
- **AN5592:** Single-ended antenna matching
- **AN6134:** Crystal oscillator design

### Internal Documentation
- `memory/st25r_variant_comparison.md` - Comprehensive variant comparison
- `memory/st25r_register_differences.md` - Register-level differences
- `memory/multitag_anticol.md` - Multi-tag anticollision implementation

### Standards
- ISO/IEC 14443-3:2018 — Initialization and anticollision
- ISO/IEC 14443-4:2018 — Transmission protocol
- ISO/IEC 15693-3:2019 — Air interface and initialization
- EMVCo 3.2a — Contactless specifications
- NFC Forum Technical Specifications

### Algorithms
- Crypto1 algorithm: Courtois, Nohl et al. (2008) — public academic specification
