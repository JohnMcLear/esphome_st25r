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

### Binary Sensor

Track specific tags:

```yaml
binary_sensor:
  - platform: st25r
    st25r_id: my_st25r_reader
    name: "Master Key"
    uid: "04-1A-A7-67-5F-61-80"
```

## Troubleshooting

- **Check Wiring**: Verify SPI/I2C connections and IRQ pin.
- **Strapping Pins**: On ESP32-C6, avoid using GPIO9 for CS as it is a strapping pin.
- **IRQ Pin**: Ensure the IRQ pin is configured correctly and not shared with flash interfaces.

---
Made with ❤️ for the ESPHome community
