# ST25R3916 API and Protocol Documentation

This document provides detailed technical information about the ST25R3916 API and supported protocols.

## Table of Contents

- [Component API](#component-api)
- [ISO14443A Protocol](#iso14443a-protocol)
- [Register Map](#register-map)
- [SPI Communication](#spi-communication)
- [Command Set](#command-set)
- [Interrupt Handling](#interrupt-handling)
- [FIFO Operations](#fifo-operations)
- [Protocol Implementation Details](#protocol-implementation-details)

## Component API

### Public Methods

#### `is_tag_present()`
```cpp
bool is_tag_present() const
```
Returns whether a tag is currently detected.

**Returns:** `true` if tag present, `false` otherwise

**Example:**
```cpp
if (id(my_reader).is_tag_present()) {
  // Tag is present
}
```

#### `get_current_uid()`
```cpp
std::string get_current_uid() const
```
Returns the UID of the currently detected tag.

**Returns:** Hexadecimal string of UID (e.g., "04A1B2C3D4E5F0")

**Example:**
```cpp
auto uid = id(my_reader).get_current_uid();
ESP_LOGI("main", "UID: %s", uid.c_str());
```

### Configuration Methods

#### `set_reset_pin()`
```cpp
void set_reset_pin(GPIOPin *reset_pin)
```
Sets the hardware reset pin.

**Parameters:**
- `reset_pin`: GPIO pin for hardware reset (optional)

#### `set_irq_pin()`
```cpp
void set_irq_pin(InternalGPIOPin *irq_pin)
```
Sets the interrupt request pin.

**Parameters:**
- `irq_pin`: GPIO pin for IRQ (required)

### Trigger Registration

#### `register_on_tag_trigger()`
```cpp
void register_on_tag_trigger(ST25R3916TagTrigger *trig)
```
Registers a callback for tag detection.

#### `register_on_tag_removed_trigger()`
```cpp
void register_on_tag_removed_trigger(ST25R3916TagRemovedTrigger *trig)
```
Registers a callback for tag removal.

## ISO14443A Protocol

### Protocol Overview

ISO14443A is the most common NFC protocol used for:
- NFC tags (NTAG, MIFARE Ultralight)
- Smart cards (MIFARE Classic, DESFire)
- Access control cards
- Payment cards (some types)

### Communication Flow

```
Reader                              Tag
------                              ---
  
1. Field ON
   ↓
2. REQA (Request Type A)
   ----------------------------->
                                  ←----  ATQA (Answer to Request)
3. Anticollision
   ----------------------------->
                                  ←----  UID (first 4 bytes)
4. SELECT
   ----------------------------->
                                  ←----  SAK (Select Acknowledge)

[Repeat 3-4 for cascade levels if needed]

5. Communication
   ----------------------------->
                                  ←----  Response
```

### Frame Format

#### REQA Command
```
Bit count: 7 bits (short frame)
Data: 0x26
No CRC
```

#### WUPA Command
```
Bit count: 7 bits (short frame)
Data: 0x52
No CRC
```

#### ATQA Response
```
Bytes: 2
Format: [ATQA0, ATQA1]
Example: [0x04, 0x00] for NTAG213
```

#### Anticollision Command
```
Bytes: 2
Format: [SEL, NVB]
SEL: 0x93 (cascade level 1), 0x95 (level 2), 0x97 (level 3)
NVB: 0x20 (full anticollision)
No CRC
```

#### UID Response
```
Bytes: 5
Format: [UID0, UID1, UID2, UID3, BCC]
BCC: XOR of UID bytes
No CRC
```

#### SELECT Command
```
Bytes: 9 (including CRC)
Format: [SEL, NVB, UID0, UID1, UID2, UID3, BCC, CRC0, CRC1]
SEL: 0x93 (cascade level 1)
NVB: 0x70 (full select)
```

#### SAK Response
```
Bytes: 3 (including CRC)
Format: [SAK, CRC0, CRC1]
SAK bits:
  - bit 2 (0x04): Cascade bit (UID not complete)
  - bit 5 (0x20): ISO14443-4 compliant
```

### UID Cascade Levels

#### Single Size UID (4 bytes)
```
Anticollision CL1  ->  4 byte UID
Select CL1         ->  SAK (bit 2 = 0)
Complete
```

#### Double Size UID (7 bytes)
```
Anticollision CL1  ->  CT (0x88) + 3 byte UID
Select CL1         ->  SAK (bit 2 = 1, continue)
Anticollision CL2  ->  4 byte UID
Select CL2         ->  SAK (bit 2 = 0)
Complete: 3 + 4 = 7 bytes
```

#### Triple Size UID (10 bytes)
```
Anticollision CL1  ->  CT (0x88) + 3 byte UID
Select CL1         ->  SAK (bit 2 = 1)
Anticollision CL2  ->  CT (0x88) + 3 byte UID
Select CL2         ->  SAK (bit 2 = 1)
Anticollision CL3  ->  4 byte UID
Select CL3         ->  SAK (bit 2 = 0)
Complete: 3 + 3 + 4 = 10 bytes
```

## Register Map

### System Registers (0x00-0x04)

#### IO_CONF1 (0x00)
```
Bit 7: rfu
Bit 6: single
Bit 5: rfu
Bit 4: rfu
Bit 3: out_cl (Output current limit)
Bit 2-0: rfu
```

#### OP_CONTROL (0x02)
```
Bit 7: tx_en (Transmitter enable)
Bit 6: rx_en (Receiver enable)
Bit 5-4: rfu
Bit 3: en_fd (Enable field detector)
Bit 2-0: rfu
```

#### MODE (0x03)
```
Bit 7: om3 (Operation mode bit 3)
Bit 6: om2 (Operation mode bit 2)
Bit 5: om1 (Operation mode bit 1)
Bit 4: om0 (Operation mode bit 0)
Bit 3: targ (Target mode)
Bit 2-0: rfu

Operation Modes:
0x00: Power Down
0x01: Standby
0x02: Transmit only
0x03: Receive only
0x08: NFC Active
0x0C: NFC Passive Target
```

### IRQ Registers (0x15-0x1A)

#### IRQ_MASK_MAIN (0x15)
```
Bit 7: ien_rxs (RX start)
Bit 6: ien_rxe (RX end)
Bit 5: ien_txe (TX end)
Bit 4: ien_err (Error)
Bit 3: ien_col (Collision)
Bit 2: ien_tx (TX)
Bit 1: ien_fwl (FIFO water level)
Bit 0: ien_osc (Oscillator)
```

#### IRQ_MAIN (0x18)
```
Same bit layout as IRQ_MASK_MAIN
Reading clears the register
```

### FIFO Registers (0x1B-0x1C)

#### FIFO_STATUS1 (0x1B)
```
Bits 7-0: FIFO byte count [7:0]
```

#### FIFO_STATUS2 (0x1C)
```
Bit 7-2: rfu
Bits 1-0: FIFO byte count [9:8]

Total FIFO bytes = (FIFO_STATUS2[1:0] << 8) | FIFO_STATUS1
```

## SPI Communication

### SPI Configuration

```
Mode: SPI Mode 0 (CPOL=0, CPHA=0)
Bit Order: MSB first
Clock: Up to 10 MHz
CS: Active low
```

### Command Formats

#### Read Register
```
Byte 1: 0x40 | (register & 0x3F)
Byte 2: [data read from register]
```

#### Write Register
```
Byte 1: 0x00 | (register & 0x3F)
Byte 2: [data to write]
```

#### Read FIFO
```
Byte 1: 0xBF
Byte 2-N: [FIFO data]
```

#### Write FIFO
```
Byte 1: 0x80
Byte 2-N: [data to write to FIFO]
```

#### Direct Command
```
Byte 1: 0xC0 | (command & 0x3F)
```

### SPI Timing

```
CS Setup Time: 10 ns minimum
CS Hold Time: 10 ns minimum
Clock Frequency: 10 MHz maximum
Data Setup Time: 10 ns
Data Hold Time: 10 ns
```

## Command Set

### System Commands

#### SET_DEFAULT (0xC1)
Resets all registers to default values.

#### CLEAR_FIFO (0xC2)
Clears both RX and TX FIFO.

#### STOP (0xFF)
Stops all ongoing operations.

### Transmission Commands

#### TRANSMIT_WITH_CRC (0xC4)
Transmits data from FIFO with automatic CRC append.

#### TRANSMIT_WITHOUT_CRC (0xC5)
Transmits data from FIFO without CRC.

#### TRANSMIT_REQA (0xC6)
Transmits REQA command (0x26, 7 bits).

#### TRANSMIT_WUPA (0xC7)
Transmits WUPA command (0x52, 7 bits).

### Field Commands

#### NFC_INITIAL_FIELD_ON (0xC8)
Turns on RF field for initiator mode.

#### NFC_RESPONSE_FIELD_ON (0xC9)
Turns on RF field for response.

### Mode Commands

#### GOTO_SENSE (0xCD)
Enters field detection mode.

#### GOTO_SLEEP (0xCE)
Enters sleep mode (low power).

### Calibration Commands

#### CALIBRATE_DRIVER_TIMING (0xD8)
Calibrates driver timing.

#### CALIBRATE_C_SENSOR (0xDD)
Calibrates capacitance sensor.

#### MEASURE_AMPLITUDE (0xD3)
Measures field amplitude.

## Interrupt Handling

### IRQ Sources

The ST25R3916 provides three IRQ registers:

1. **IRQ_MAIN** - Main interrupts
2. **IRQ_TIMER_NFC** - Timer interrupts
3. **IRQ_ERROR_WUP** - Error and wakeup interrupts

### Main Interrupts

```cpp
enum ST25R3916IRQ : uint32_t {
  IRQ_OSC = (1 << 0),   // Oscillator ready
  IRQ_FWL = (1 << 1),   // FIFO water level
  IRQ_RXS = (1 << 2),   // RX start
  IRQ_RXE = (1 << 3),   // RX end
  IRQ_TXE = (1 << 4),   // TX end
  IRQ_COL = (1 << 5),   // Collision detected
  IRQ_NRE = (1 << 9),   // No response error
  IRQ_GPE = (1 << 10),  // General purpose timer
  IRQ_CRC = (1 << 11),  // CRC error
  IRQ_PAR = (1 << 12),  // Parity error
  IRQ_ERR1 = (1 << 13), // Error 1
  IRQ_ERR2 = (1 << 14), // Error 2
  IRQ_WU_F = (1 << 19), // Wake-up field
};
```

### IRQ Handling Flow

```cpp
// Read IRQ status (also clears)
uint32_t irq_status = read_irq_status_();

// Check for specific interrupt
if (irq_status & IRQ_RXE) {
  // RX completed
  handle_rx_complete();
}

if (irq_status & IRQ_CRC) {
  // CRC error occurred
  handle_crc_error();
}
```

### IRQ Configuration

```cpp
// Enable specific interrupts
write_register_(IRQ_MASK_MAIN, 0xFE);      // Enable most
write_register_(IRQ_MASK_TIMER_NFC, 0xFF); // Enable all timer
write_register_(IRQ_MASK_ERROR_WUP, 0xFF); // Enable all error
```

## FIFO Operations

### FIFO Size
- Total capacity: 512 bytes
- Shared between RX and TX
- Configurable water level for interrupts

### Writing to FIFO

```cpp
void write_fifo_(const uint8_t *data, size_t length) {
  this->enable();
  this->write_byte(ST25R3916_WRITE_FIFO);
  for (size_t i = 0; i < length; i++) {
    this->write_byte(data[i]);
  }
  this->disable();
}
```

### Reading from FIFO

```cpp
size_t read_fifo_(uint8_t *data, size_t max_length) {
  // Read FIFO status
  uint8_t status1 = read_register_(FIFO_STATUS1);
  uint8_t status2 = read_register_(FIFO_STATUS2);
  
  size_t bytes_in_fifo = ((status2 & 0x03) << 8) | status1;
  size_t bytes_to_read = min(bytes_in_fifo, max_length);
  
  this->enable();
  this->write_byte(ST25R3916_READ_FIFO);
  for (size_t i = 0; i < bytes_to_read; i++) {
    data[i] = this->read_byte();
  }
  this->disable();
  
  return bytes_to_read;
}
```

### Clearing FIFO

```cpp
void clear_fifo_() {
  send_command_(CMD_CLEAR_FIFO);
}
```

## Protocol Implementation Details

### ISO14443A Tag Detection Sequence

```cpp
bool check_for_tag() {
  // 1. Turn on field
  field_on_();
  delay(5);  // ISO14443 requires 5ms minimum
  
  // 2. Send REQA
  uint8_t atqa[2];
  if (!iso14443a_reqa_(atqa)) {
    field_off_();
    return false;
  }
  
  // 3. Anticollision loop
  std::string uid;
  for (uint8_t cascade = 0; cascade < 3; cascade++) {
    uint8_t cascade_uid[4];
    uint8_t uid_length;
    
    if (!iso14443a_anticollision_(cascade, cascade_uid, &uid_length)) {
      break;
    }
    
    // Check for cascade tag
    if (cascade_uid[0] == 0x88) {
      // Add 3 bytes to UID
      append_uid(uid, &cascade_uid[1], 3);
    } else {
      // Final UID bytes
      append_uid(uid, cascade_uid, 4);
      
      // Select and finish
      if (!iso14443a_select_(cascade, cascade_uid)) {
        field_off_();
        return false;
      }
      break;
    }
    
    // Select cascade level
    if (!iso14443a_select_(cascade, cascade_uid)) {
      field_off_();
      return false;
    }
  }
  
  // 4. Turn off field
  field_off_();
  
  return true;
}
```

### Timing Requirements

```
REQA/WUPA to ATQA: 
  - Minimum: 78 µs (approx 106 carrier cycles)
  - Maximum: Depends on tag

Anticollision to UID:
  - Similar to REQA timing

SELECT to SAK:
  - 78 µs typical

Field activation:
  - 5 ms minimum before first command
  
Tag removal detection:
  - 1000 ms timeout (configurable)
```

### Error Handling

```cpp
// Check for communication errors
uint32_t irq = read_irq_status_();

if (irq & IRQ_NRE) {
  // No response - tag may have moved away
  ESP_LOGD(TAG, "No response from tag");
  return ERROR_NO_RESPONSE;
}

if (irq & IRQ_CRC) {
  // CRC error - communication problem
  ESP_LOGW(TAG, "CRC error");
  return ERROR_CRC;
}

if (irq & IRQ_PAR) {
  // Parity error
  ESP_LOGW(TAG, "Parity error");
  return ERROR_PARITY;
}

if (irq & IRQ_COL) {
  // Collision - multiple tags
  ESP_LOGW(TAG, "Collision detected");
  return ERROR_COLLISION;
}
```

## Performance Characteristics

### Timing

```
Tag Detection Time: 50-100 ms (complete UID read)
SPI Transaction: ~1 µs per byte at 1 MHz
Field Turn-on Time: 5 ms (ISO14443 requirement)
Polling Interval: 1 second (default, configurable)
```

### Power Consumption

```
Field On (active): 15 mA typical
Field Off (idle): 5 mA typical
Sleep Mode: <1 µA (not yet implemented)
```

### Reading Distance

```
Typical: 2-5 cm
Maximum: 8-10 cm (depends on antenna and tag)
Minimum: 0.5 cm
```

## Future Protocol Support

### Planned Additions

1. **ISO14443B**
   - WUPB command
   - ATTRIB command
   - Different frame format

2. **ISO15693**
   - 26 kbps data rate
   - Inventory command
   - Longer read range

3. **NFC-F (FeliCa)**
   - 212/424 kbps
   - Different collision resolution
   - Japanese transit cards

4. **NFC-V**
   - Vicinity cards
   - Extended range
   - Different modulation

## References

- ISO/IEC 14443-3:2018 - Initialization and anticollision
- ST25R3916 Datasheet
- NFC Forum specifications
- EMVCo specifications (for payment applications)

---

**Document Version:** 1.0  
**Last Updated:** 2024-02-26
