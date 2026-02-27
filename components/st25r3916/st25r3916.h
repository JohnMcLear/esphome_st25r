#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/automation.h"
#include "esphome/components/spi/spi.h"
#include <vector>
#include <string>

namespace esphome {
namespace st25r3916 {

// ST25R3916 Register Definitions
enum ST25R3916Register : uint8_t {
  IO_CONF1 = 0x00,
  IO_CONF2 = 0x01,
  OP_CONTROL = 0x02,
  MODE = 0x03,
  BIT_RATE = 0x04,
  RX_CONF1 = 0x0A,
  RX_CONF2 = 0x0B,
  RX_CONF3 = 0x0C,
  RX_CONF4 = 0x0D,
  NUM_TX_BYTES1 = 0x1E,
  NUM_TX_BYTES2 = 0x1F,
  FIFO_STATUS1 = 0x1B,
  FIFO_STATUS2 = 0x1C,
  IRQ_MAIN = 0x18,
  IC_IDENTITY = 0x3F,
};

// ST25R3916 Commands
enum ST25R3916Command : uint8_t {
  ST25R3916_CMD_SET_DEFAULT = 0xC1,
  ST25R3916_CMD_CLEAR_FIFO = 0xC2,
  ST25R3916_CMD_TRANSMIT_WITHOUT_CRC = 0xC5,
};

class ST25R3916;

class ST25R3916TagTrigger : public Trigger<std::string> {
 public:
  explicit ST25R3916TagTrigger(ST25R3916 *parent) : parent_(parent) {}

 protected:
  ST25R3916 *parent_;
};

class ST25R3916TagRemovedTrigger : public Trigger<std::string> {
 public:
  explicit ST25R3916TagRemovedTrigger(ST25R3916 *parent) : parent_(parent) {}

 protected:
  ST25R3916 *parent_;
};

class ST25R3916 : public PollingComponent,
                  public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                        spi::CLOCK_PHASE_TRAILING, spi::DATA_RATE_1MHZ> {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_irq_pin(InternalGPIOPin *irq_pin) { this->irq_pin_ = irq_pin; }

  void register_on_tag_trigger(ST25R3916TagTrigger *trig) { this->on_tag_triggers_.push_back(trig); }
  void register_on_tag_removed_trigger(ST25R3916TagRemovedTrigger *trig) {
    this->on_tag_removed_triggers_.push_back(trig);
  }

  bool is_tag_present() const { return this->tag_present_; }

 protected:
  bool reset_();
  void field_on_();
  
  uint8_t read_register_(ST25R3916Register reg);
  void write_register_(ST25R3916Register reg, uint8_t value);
  void write_command_(uint8_t command);
  void write_fifo_(const uint8_t *data, size_t len);
  void read_fifo_(uint8_t *data, size_t len);

  GPIOPin *reset_pin_{nullptr};
  InternalGPIOPin *irq_pin_{nullptr};

  bool tag_present_{false};
  std::string current_uid_;
  uint8_t bit_shift_{0};

  std::vector<ST25R3916TagTrigger *> on_tag_triggers_;
  std::vector<ST25R3916TagRemovedTrigger *> on_tag_removed_triggers_;
};

}  // namespace st25r3916
}  // namespace esphome
