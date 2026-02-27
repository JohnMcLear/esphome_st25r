#include "st25r_i2c.h"
#include "esphome/core/log.h"

namespace esphome {
namespace st25r_i2c {

static const char *const TAG = "st25r_i2c";

void ST25RI2c::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ST25R I2C...");
  st25r::ST25R::setup();
}

void ST25RI2c::dump_config() {
  st25r::ST25R::dump_config();
  LOG_I2C_DEVICE(this);
}

uint8_t ST25RI2c::read_register(uint8_t reg) {
  uint8_t value;
  this->i2c::I2CDevice::read_register(reg, &value, 1);
  return value;
}

void ST25RI2c::write_register(uint8_t reg, uint8_t value) {
  this->i2c::I2CDevice::write_register(reg, &value, 1);
}

void ST25RI2c::write_command(uint8_t command) {
  this->i2c::I2CDevice::write(&command, 1);
}

void ST25RI2c::write_fifo(const uint8_t *data, size_t len) {
  // FIFO load command is 0x80
  this->i2c::I2CDevice::write_register(0x80, data, len);
}

void ST25RI2c::read_fifo(uint8_t *data, size_t len) {
  // FIFO read command is 0x9F
  this->i2c::I2CDevice::read_register(0x9F, data, len);
}

}  // namespace st25r_i2c
}  // namespace esphome
