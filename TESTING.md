# ST25R Enhanced Component Hardware Testing Procedure

This document outlines the validation steps required to ensure the stability and functionality of the enhanced ST25R component across both I2C and SPI interfaces.

## 1. Requirements
- **Hardware:**
  - ESP32 (Required for sufficient GPIO count for simultaneous I2C + SPI testing).
  - **2 ST25R Modules:** One configured for I2C (dip switches 1:ON, 2:OFF) and one for SPI (dip switches 1:OFF, 2:ON).
  - **Tags Required:** You must have at least one of each of the following to verify formatting logic:
    - Mifare Classic 1k or 4k.
    - Mifare Ultralight or Ultralight C.
    - NTAG Series (e.g., NTAG213, 215, or 216).
- **Estimated Time:** 20–30 minutes (includes bus-switching and physical verification).

## 2. Test Configuration (Dual Interface Example)
This configuration tests the `ST25R` base logic by running both an I2C and an SPI hub simultaneously.

### GPIO Assignment Table (ESP32)
| Component | Bus/Pin | GPIO |
|---|---|---|
| **I2C Bus** | SDA / SCL | GPIO21 / GPIO22 |
| **SPI Bus** | CLK / MISO / MOSI | GPIO18 / GPIO19 / GPIO23 |
| **Hub 1 (I2C)** | Address | 0x24 |
| **Hub 2 (SPI)** | CS Pin | GPIO5 |

```yaml
i2c:
  sda: GPIO21
  scl: GPIO22

spi:
  clk_pin: GPIO18
  miso_pin: GPIO19
  mosi_pin: GPIO23

ST25R_i2c:
  - id: hub_i2c
    address: 0x24
    update_interval: 1s

ST25R_spi:
  - id: hub_spi
    cs_pin: GPIO5
    update_interval: 1s

binary_sensor:
  - platform: ST25R
    ST25R_id: hub_i2c
    uid: "AA-BB-CC-DD"
    name: "I2C Tag"
  - platform: ST25R
    ST25R_id: hub_spi
    uid: "AA-BB-CC-DD"
    name: "SPI Tag"
```

## 3. Test Cases

### Phase 1: Communication & Stability (10 mins)
| Test Case | Procedure | Expected Result |
|---|---|---|
| **Mixed Boot** | Power on with both I2C and SPI modules. | Logs show successful initialization for both `ST25R_i2c` and `ST25R_spi`. |
| **I2C Recovery** | Disconnect I2C SDA/SCL briefly. | `hub_i2c` enters backoff/re-init state machine. `hub_spi` remains unaffected. |
| **SPI Recovery** | Disconnect SPI CS pin briefly. | `hub_spi` enters backoff/re-init state machine. `hub_i2c` remains unaffected. |
| **Health Check** | Leave idle for 60s. | Both interfaces log successful periodic health checks. |

### Phase 2: Card Detection & Logic (10 mins)
| Test Case | Procedure | Expected Result |
|---|---|---|
| **Cross-Bus Read** | Present the same physical tag to I2C then SPI. | Both readers correctly identify the UID and trigger their respective binary sensors. |
| **Anti-Collision** | Present two different tags to the same reader simultaneously. | One tag is consistently selected and read (standard ST25R behavior). Main thread does not block. |
| **Flapping Fix** | Place a tag on the SPI reader and leave it. | `on_tag` fires once. No removals/re-adds occur in the logs during the next 60s. |

### Phase 3: NDEF Operations (10 mins)
| Test Case | Procedure | Expected Result |
|---|---|---|
| **I2C NDEF Write** | Perform an NDEF write on the I2C bus. | Successful write logged. Main thread remains responsive during the non-blocking cycle. |
| **SPI NDEF Write** | Perform an NDEF write on the SPI bus. | Successful write logged. |
| **Mifare Formatting** | Present a "virgin" Mifare Classic card to either bus. | Component successfully authenticates with default key, formats to NDEF, and writes URI. |

