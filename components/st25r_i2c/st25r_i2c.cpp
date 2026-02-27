#include "st25r_i2c.h"
#include "esphome/core/log.h"

namespace esphome {
namespace st25r_i2c {

static const char *const TAG = "st25r_i2c";

void ST25RI2C::setup() {
  ESP_LOGD(TAG, "Setting up ST25R I2C...");
  this->ST25R::setup();
}

void ST25RI2C::dump_config() {
  this->ST25R::dump_config();
  LOG_I2C_DEVICE(this);
}

uint8_t ST25RI2C::read_register(uint8_t reg) {
  uint8_t value = 0;
  if (this->write(&reg, 1) != i2c::ERROR_OK) {
    return 0;
  }
  this->read(&value, 1);
  return value;
}

void ST25RI2C::write_register(uint8_t reg, uint8_t value) {
  uint8_t data[2] = {reg, value};
  this->write(data, 2);
}

void ST25RI2C::write_command(uint8_t command) {
  // Direct commands in I2C are sent by setting bits 7 and 6 to 1
  uint8_t cmd = 0xC0 | (command & 0x3F);
  this->write(&cmd, 1);
}

void ST25RI2C::write_fifo(const uint8_t *data, size_t len) {
  // To be implemented: FIFO access in I2C
}

void ST25RI2C::read_fifo(uint8_t *data, size_t len) {
  // To be implemented: FIFO access in I2C
}

}  // namespace st25r_i2c
}  // namespace esphome
