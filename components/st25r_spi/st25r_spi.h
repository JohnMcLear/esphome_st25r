#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "../st25r/st25r.h"

namespace esphome {
namespace st25r_spi {

class ST25RSpi : public st25r::ST25R, public spi::SPIDevice {
 public:
  void setup() override;
  void dump_config() override;

 protected:
  uint8_t read_register(uint8_t reg) override;
  void write_register(uint8_t reg, uint8_t value) override;
  void write_command(uint8_t command) override;
  void write_fifo(const uint8_t *data, size_t len) override;
  void read_fifo(uint8_t *data, size_t len) override;
};

}  // namespace st25r_spi
}  // namespace esphome
