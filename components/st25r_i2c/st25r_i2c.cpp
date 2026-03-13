#include "st25r_i2c.h"
#include "esphome/core/log.h"

namespace esphome {
namespace st25r_i2c {

static const char *const TAG = "st25r_i2c";

void ST25RI2c::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ST25R I2C...");
  
  // Wake up chip - send a dummy byte and ignore the result
  uint8_t dummy = 0x00;
  this->i2c::I2CDevice::write(&dummy, 1);
  delay(10);
  
  st25r::ST25R::setup();
}

void ST25RI2c::dump_config() {
  st25r::ST25R::dump_config();
  LOG_I2C_DEVICE(this);
}

uint8_t ST25RI2c::read_register(uint8_t reg) {
  uint8_t value = 0;
  uint8_t addr = 0x40 | (reg & 0x3F);
  if (!this->i2c::I2CDevice::read_bytes(addr, &value, 1)) {
    return 0;
  }
  return value;
}

void ST25RI2c::write_register(uint8_t reg, uint8_t value) {
  uint8_t addr = 0x00 | (reg & 0x3F);
  this->i2c::I2CDevice::write_bytes(addr, &value, 1);
}

void ST25RI2c::write_command(uint8_t command) {
  this->i2c::I2CDevice::write(&command, 1);
}

void ST25RI2c::write_fifo(const uint8_t *data, size_t len) {
  this->i2c::I2CDevice::write_bytes(0x80, data, len);
}

void ST25RI2c::read_fifo(uint8_t *data, size_t len) {
  this->i2c::I2CDevice::read_bytes(0xBF, data, len);
}

}  // namespace st25r_i2c
}  // namespace esphome
