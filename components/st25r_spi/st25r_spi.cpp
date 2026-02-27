#include "st25r_spi.h"
#include "esphome/core/log.h"

namespace esphome {
namespace st25r_spi {

static const char *const TAG = "st25r_spi";

void ST25RSpi::setup() {
  ESP_LOGD(TAG, "Setting up ST25R SPI...");
  this->spi_setup();
  this->ST25R::setup();
}

void ST25RSpi::dump_config() {
  this->ST25R::dump_config();
  LOG_PIN("  CS Pin: ", this->cs_);
}

uint8_t ST25RSpi::read_register(uint8_t reg) {
  this->enable();
  this->write_byte(0x40 | (reg & 0x3F));
  uint8_t value = this->read_byte();
  this->disable();
  return value;
}

void ST25RSpi::write_register(uint8_t reg, uint8_t value) {
  this->enable();
  this->write_byte(0x00 | (reg & 0x3F));
  this->write_byte(value);
  this->disable();
}

void ST25RSpi::write_command(uint8_t command) {
  this->enable();
  this->write_byte(0xC0 | (command & 0x3F));
  this->disable();
}

void ST25RSpi::write_fifo(const uint8_t *data, size_t len) {
  this->enable();
  this->write_byte(0x80);
  this->write_array(data, len);
  this->disable();
}

void ST25RSpi::read_fifo(uint8_t *data, size_t len) {
  this->enable();
  this->write_byte(0xBF);
  for (size_t i = 0; i < len; i++) {
    data[i] = this->read_byte();
  }
  this->disable();
}

}  // namespace st25r_spi
}  // namespace esphome
