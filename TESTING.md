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

### Phase 1: Communication & Stability
| Test Case | Operator Action | Expected Result |
|---|---|---|
| **Mixed Boot** | **Readers:** Empty. **Action:** Power cycle ESP32. | Logs show initialization for both `ST25R_i2c` and `ST25R_spi`. |
| **I2C Recovery** | **Readers:** Empty. **Action:** Briefly disconnect I2C SDA wire. | `hub_i2c` enters backoff. `hub_spi` unaffected. Reconnect to see recovery. |
| **SPI Recovery** | **Readers:** Empty. **Action:** Briefly disconnect SPI CS wire. | `hub_spi` enters backoff. `hub_i2c` unaffected. Reconnect to see recovery. |
| **Health Check** | **Readers:** Empty. **Action:** Leave idle for 60s. | Both log periodic version/health checks. |
| **Silent ACK Fail**| **Action:** Simulate a partial response failure. | Component tracks failure in loop(), increments consecutive_failures_, and eventually triggers re-init. |

### Phase 2: Card Detection & Logic
| Test Case | Operator Action | Expected Result |
|---|---|---|
| **Cross-Bus Read** | **Action 1:** Place Mifare Classic on I2C. **Action 2:** Move same card to SPI. | Both readers correctly identify UID and trigger sensors. |
| **Anti-Collision** | **Action:** Place two different tags (e.g., NTAG and Ultralight) on I2C simultaneously. | One tag consistently read. No main thread blocking. |
| **Flapping Fix** | **Action:** Place a tag on SPI and leave it for 60s. | `on_tag` fires once. No removal/re-add logs during dwell. |
| **Dual Detection** | **Action:** Place one card on I2C and another on SPI simultaneously. | Both cards detected and held in ON state concurrently. |
| **Removal Logic** | **Action:** Place tag on I2C/SPI and wait for detection. **Action 2:** Remove tag. | `on_tag` triggers on placement. `on_tag_removed` triggers within 2s of removal. |
| **Response Timing**| **Action:** Rapidly place tag into field 5 times. | Average detection time (log message to tag identifier) is <300ms. |

### Phase 3: NDEF Operations
| Test Case | Operator Action | Expected Result |
|---|---|---|
| **NDEF Read/Write** | **Action:** Place NTAG on I2C. Observe write verification logs. | Successful NDEF interaction logged. Main thread responsive. |
| **Mifare Format** | **Action:** Place "virgin" Mifare Classic on SPI. | Component authenticates, formats, and writes URI successfully. |


## 4. Success Criteria
- [x] **SPI Hardware:** ST25R initializes and reads tags reliably over the SPI bus without timeouts or data corruption.
- [x] **Dual Bus Operation:** Simultaneous I2C and SPI readers function correctly on the same ESP32-C6.
- [x] Multi-Tag Detection: Correctly identifies and parses 2 tags in a single poll.
- [ ] **Tag Removal:** `on_tag_removed` triggers reliably across all tag types.
- [ ] **Low-Latency Reads:** Average detection speed remains under the 300ms threshold for snappy UI feedback.
- [x] Non-blocking: No `delay()` or `took a long time` warnings during normal polling.
- [x] Portability: NDEF and Mifare logic performs identically on I2C and SPI.
- [x] Isolation: Physical failure/noise on one bus does not crash the other.
- [x] Format compatibility: Both `AA-BB` and `AA:BB` formats accepted in YAML.
- [x] **Silent Failure Tracking:** Loop timeouts (ACK but no response) correctly trigger warnings and auto-reset logic.
- [ ] **Mifare Authentication:** Resolve intermittent failures with non-standard keys.
- [ ] **Robust Counterfeit Detection:** Module correctly identifies emulated clones using hardware-level diagnostic checks.
- [ ] **NTAG216 Stability:** NDEF writing completes without timing out on high-capacity NTAG216 modules.

