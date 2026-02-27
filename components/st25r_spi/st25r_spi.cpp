#include "st25r_spi.h"
#include "esphome/core/log.h"

namespace esphome {
namespace st25r_spi {

static const char *const TAG = "st25r_spi";

void ST25RSpi::setup() {
  ESP_LOGD(TAG, "Setting up ST25R SPI with 2-bit Shift Correction (V2)...");
  this->spi_setup();
  this->ST25R::setup();
}

void ST25RSpi::dump_config() {
  this->ST25R::dump_config();
  LOG_PIN("  CS Pin: ", this->cs_);
}

uint8_t ST25RSpi::read_register(uint8_t reg) {
  this->enable();
  // ST25R Register Read: 01xxxxxx (0x40 | reg)
  this->write_byte((uint8_t)((0x40 | (reg & 0x3F)) << 2));
  uint8_t value = this->read_byte();
  this->disable();
  return (uint8_t)(value >> 2);
}

void ST25RSpi::write_register(uint8_t reg, uint8_t value) {
  this->enable();
  // ST25R Register Write: 00xxxxxx (0x00 | reg)
  this->write_byte((uint8_t)((0x00 | (reg & 0x3F)) << 2));
  this->write_byte((uint8_t)(value << 2));
  this->disable();
}

void ST25RSpi::write_command(uint8_t command) {
  this->enable();
  // ST25R Direct Command: 11xxxxxx (0xC0 | cmd)
  // We use 0xC0 as the base for commands in the register header
  this->write_byte((uint8_t)(command << 2));
  this->disable();
}

void ST25RSpi::write_fifo(const uint8_t *data, size_t len) {
  this->enable();
  // ST25R FIFO Write: 10000000 (0x80)
  this->write_byte((uint8_t)(0x80 << 2));
  for (size_t i = 0; i < len; i++) {
    this->write_byte((uint8_t)(data[i] << 2));
  }
  this->disable();
}

void ST25RSpi::read_fifo(uint8_t *data, size_t len) {
  this->enable();
  // ST25R FIFO Read: 10111111 (0xBF)
  // Wait, 0xBF is 10111111. Let's try 0xBF << 2
  this->write_byte((uint8_t)(0xBF << 2));
  for (size_t i = 0; i < len; i++) {
    data[i] = (uint8_t)(this->read_byte() >> 2);
  }
  this->disable();
}

}  // namespace st25r_spi
}  // namespace esphome
