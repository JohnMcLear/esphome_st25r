# ST25R NFC Reader Component for ESPHome

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![ESPHome](https://img.shields.io/badge/ESPHome-compatible-green.svg)](https://esphome.io)

An ESPHome component for the STMicroelectronics ST25R family of NFC reader ICs.

## Supported Units

- **ST25R3916 / ST25R3916B**: High-performance NFC universal device.
- **ST25R3917 / ST25R3917B**: Reduced feature set version.
- **ST25R3919 / ST25R3920**: Automotive grade versions.

## Features

- ✅ SPI and I2C transport support
- ✅ Full ISO14443A support (NFC-A)
- ✅ 4-byte, 7-byte, and 10-byte UID support (Cascade Levels 1-3)
- ✅ Tag presence and removal triggers
- ✅ Binary sensor platform for specific tag tracking
- ✅ Hardware reset support

## Installation

Add to your ESPHome YAML configuration:

```yaml
external_components:
  - source: github://JohnMcLear/esphome_st25r
    components: [ st25r, st25r_spi, st25r_i2c ]
```

## Configuration

### SPI Configuration

```yaml
spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

st25r_spi:
  cs_pin: GPIO5
  irq_pin: GPIO21
  reset_pin: GPIO22  # Optional
  update_interval: 1s
  on_tag:
    then:
      - logger.log:
          format: "Tag detected: %s"
          args: ['x.c_str()']
```

### I2C Configuration

```yaml
i2c:
  sda: GPIO21
  scl: GPIO22

st25r_i2c:
  address: 0x50
  irq_pin: GPIO4
  update_interval: 1s
  on_tag:
    then:
      - logger.log:
          format: "Tag detected: %s"
          args: ['x.c_str()']
```

### Full Configuration Example

```yaml
st25r_spi:
  id: my_nfc_reader
  cs_pin: GPIO5
  irq_pin: GPIO21
  reset_pin: GPIO22      # Optional: Hardware reset
  update_interval: 1s    # Frequency of NFC scans
  rf_field_enabled: true # Optional: Enable/Disable RF field
  rf_power: 15           # Optional: 0 (min) to 15 (max) power level
  supply_3v3: true       # Optional: true for 3.3V supply, false for 5V
  
  # Status binary sensor (turns off if chip health check fails)
  status:
    name: "NFC Reader Status"
    
  # RF Field strength sensor (reads A/D conversion result)
  field_strength:
    name: "NFC Field Strength"
    
  # Triggers for any tag
  on_tag:
    then:
      - logger.log:
          format: "Tag detected: %s"
          args: ['x.c_str()']
          
  on_tag_removed:
    then:
      - logger.log: "Tag removed"
```

### Configuration Variables

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `irq_pin` | [Pin](https://esphome.io/guides/configs.html#config-pin-schema) | **Required** | The interrupt pin from the ST25R. |
| `reset_pin` | [Pin](https://esphome.io/guides/configs.html#config-pin-schema) | Optional | Hardware reset pin. |
| `rf_field_enabled` | boolean | `true` | Whether to keep the RF field permanently enabled. |
| `rf_power` | integer | `15` | Transmitter driver resistance (0-15). 15 is maximum power. |
| `supply_3v3` | boolean | `true` | Set to `true` if the IC is supplied with 3.3V, `false` for 5V. |
| `status` | [Binary Sensor](https://esphome.io/components/binary_sensor/index.html) | Optional | Tracks chip health. Turns `off` if the IC identity check fails 3 times. |
| `field_strength` | [Sensor](https://esphome.io/components/sensor/index.html) | Optional | Reports the measured RF field amplitude (0-255). |
| `update_interval` | [Time](https://esphome.io/guides/configs.html#config-time) | `1s` | Interval between polling for tags. |
| `on_tag` | Trigger | Optional | Action to perform when a tag is detected. |
| `on_tag_removed` | Trigger | Optional | Action to perform when a tag is removed. |

### Binary Sensor

Track specific tags:

```yaml
binary_sensor:
  - platform: st25r
    st25r_id: my_nfc_reader
    name: "Master Key"
    uid: "00-00-00-00-00-00-00"
```

## Troubleshooting

- **Check Wiring**: Verify SPI/I2C connections and IRQ pin.
- **Strapping Pins**: On ESP32-C6, avoid using GPIO9 for CS as it is a strapping pin.
- **IRQ Pin**: Ensure the IRQ pin is configured correctly and not shared with flash interfaces.

---
Made with ❤️ for the ESPHome community
