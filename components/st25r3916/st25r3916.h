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
  // System Registers
  IO_CONF1 = 0x00,
  IO_CONF2 = 0x01,
  OP_CONTROL = 0x02,
  MODE = 0x03,
  BIT_RATE = 0x04,
  // ISO14443A/B Registers
  ISO14443A_NFC = 0x05,
  ISO14443B_1 = 0x06,
  ISO14443B_2 = 0x07,
  STREAM_MODE = 0x08,
  AUX_MOD = 0x09,
  RX_CONF1 = 0x0A,
  RX_CONF2 = 0x0B,
  RX_CONF3 = 0x0C,
  RX_CONF4 = 0x0D,
  // FIFO Registers
  MASK_RX_TIMER = 0x0E,
  NO_RESPONSE_TIMER1 = 0x0F,
  NO_RESPONSE_TIMER2 = 0x10,
  TIMER_EMV_CONTROL = 0x11,
  GPT1 = 0x12,
  GPT2 = 0x13,
  PPON2 = 0x14,
  // IRQ Registers
  IRQ_MASK_MAIN = 0x15,
  IRQ_MASK_TIMER_NFC = 0x16,
  IRQ_MASK_ERROR_WUP = 0x17,
  IRQ_MAIN = 0x18,
  IRQ_TIMER_NFC = 0x19,
  IRQ_ERROR_WUP = 0x1A,
  // FIFO Status
  FIFO_STATUS1 = 0x1B,
  FIFO_STATUS2 = 0x1C,
  COLLISION_STATUS = 0x1D,
  // Additional Registers
  NUM_TX_BYTES1 = 0x1E,
  NUM_TX_BYTES2 = 0x1F,
  NFCIP1_BIT_RATE = 0x20,
  AD_CONVERTER = 0x21,
  ANT_CAL_CONTROL = 0x22,
  ANT_CAL_TARGET = 0x23,
  ANT_CAL_RESULT = 0x24,
  AM_MOD_DEPTH_CONTROL = 0x25,
  AM_MOD_DEPTH_RESULT = 0x26,
  RFO_AM_ON_LEVEL = 0x27,
  RFO_AM_OFF_LEVEL = 0x28,
  FIELD_THRESHOLD = 0x29,
  REGULATOR_CONTROL = 0x2A,
  RSSI_RESULT = 0x2B,
  GAIN_RED_STATE = 0x2C,
  CAP_SENSOR_CONTROL = 0x2D,
  CAP_SENSOR_RESULT = 0x2E,
  AUX_DISPLAY = 0x2F,
  WUP_TIMER_CONTROL = 0x30,
  AMPLITUDE_MEASURE_CONF = 0x31,
  AMPLITUDE_MEASURE_REF = 0x32,
  AMPLITUDE_MEASURE_AA_RESULT = 0x33,
  AMPLITUDE_MEASURE_RESULT = 0x34,
  PHASE_MEASURE_CONF = 0x35,
  PHASE_MEASURE_REF = 0x36,
  PHASE_MEASURE_AA_RESULT = 0x37,
  PHASE_MEASURE_RESULT = 0x38,
  CAPACITANCE_MEASURE_CONF = 0x39,
  CAPACITANCE_MEASURE_REF = 0x3A,
  CAPACITANCE_MEASURE_AA_RESULT = 0x3B,
  CAPACITANCE_MEASURE_RESULT = 0x3C,
  IC_IDENTITY = 0x3F,
};

// ST25R3916 Commands
enum ST25R3916Command : uint8_t {
  CMD_SET_DEFAULT = 0xC1,
  CMD_CLEAR_FIFO = 0xC2,
  CMD_TRANSMIT_WITH_CRC = 0xC4,
  CMD_TRANSMIT_WITHOUT_CRC = 0xC5,
  CMD_TRANSMIT_REQA = 0xC6,
  CMD_TRANSMIT_WUPA = 0xC7,
  CMD_NFC_INITIAL_FIELD_ON = 0xC8,
  CMD_NFC_RESPONSE_FIELD_ON = 0xC9,
  CMD_GOTO_SENSE = 0xCD,
  CMD_GOTO_SLEEP = 0xCE,
  CMD_MASK_RECEIVE_DATA = 0xD0,
  CMD_UNMASK_RECEIVE_DATA = 0xD1,
  CMD_AM_MOD_STATE_CHANGE = 0xD2,
  CMD_MEASURE_AMPLITUDE = 0xD3,
  CMD_RESET_RXGAIN = 0xD5,
  CMD_ADJUST_REGULATORS = 0xD6,
  CMD_CALIBRATE_DRIVER_TIMING = 0xD8,
  CMD_MEASURE_PHASE = 0xD9,
  CMD_CLEAR_RSSI = 0xDA,
  CMD_TRANSPARENT_MODE = 0xDC,
  CMD_CALIBRATE_C_SENSOR = 0xDD,
  CMD_MEASURE_CAPACITANCE = 0xDE,
  CMD_MEASURE_VDD = 0xDF,
  CMD_START_GP_TIMER = 0xE0,
  CMD_START_WUP_TIMER = 0xE1,
  CMD_START_MASK_RECEIVE_TIMER = 0xE2,
  CMD_START_NO_RESPONSE_TIMER = 0xE3,
  CMD_TEST_CLEARA = 0xFA,
  CMD_TEST_CLEARB = 0xFB,
  CMD_TEST_ACCESS = 0xFC,
  CMD_LOAD_PPONA = 0xFD,
  CMD_LOAD_PPONB = 0xFE,
  CMD_STOP = 0xFF,
};

// IRQ Masks
enum ST25R3916IRQ : uint32_t {
  IRQ_OSC = (1 << 0),
  IRQ_FWL = (1 << 1),
  IRQ_RXS = (1 << 2),
  IRQ_RXE = (1 << 3),
  IRQ_TXE = (1 << 4),
  IRQ_COL = (1 << 5),
  IRQ_NRE = (1 << 9),
  IRQ_GPE = (1 << 10),
  IRQ_CRC = (1 << 11),
  IRQ_PAR = (1 << 12),
  IRQ_ERR1 = (1 << 13),
  IRQ_ERR2 = (1 << 14),
  IRQ_WU_F = (1 << 19),
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
                                        spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
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

  // Public API methods
  bool is_tag_present() const { return this->tag_present_; }
  std::string get_current_uid() const { return this->current_uid_; }

 protected:
  // Hardware control
  bool reset_chip_();
  bool init_chip_();
  bool configure_chip_();

  // Register operations
  void write_register_(ST25R3916Register reg, uint8_t value);
  uint8_t read_register_(ST25R3916Register reg);
  void modify_register_(ST25R3916Register reg, uint8_t mask, uint8_t value);
  void read_registers_(ST25R3916Register start_reg, uint8_t *data, size_t length);

  // Command operations
  void send_command_(ST25R3916Command cmd);

  // FIFO operations
  void write_fifo_(const uint8_t *data, size_t length);
  size_t read_fifo_(uint8_t *data, size_t max_length);
  void clear_fifo_();

  // IRQ handling
  uint32_t read_irq_status_();
  void clear_irq_();
  void wait_for_irq_(uint32_t mask, uint32_t timeout_ms);

  // ISO14443A protocol operations
  bool iso14443a_wakeup_();
  bool iso14443a_reqa_(uint8_t *atqa);
  bool iso14443a_anticollision_(uint8_t cascade_level, uint8_t *uid, uint8_t *uid_length);
  bool iso14443a_select_(uint8_t cascade_level, const uint8_t *uid);
  bool iso14443a_read_uid_(std::string &uid_string);

  // Field control
  void field_on_();
  void field_off_();

  // Tag detection
  void check_for_tag_();

  // Hardware pins
  GPIOPin *reset_pin_{nullptr};
  InternalGPIOPin *irq_pin_{nullptr};

  // State tracking
  bool tag_present_{false};
  std::string current_uid_;
  uint32_t last_tag_detection_time_{0};

  // Triggers
  std::vector<ST25R3916TagTrigger *> on_tag_triggers_;
  std::vector<ST25R3916TagRemovedTrigger *> on_tag_removed_triggers_;

  // Configuration
  static constexpr uint32_t TAG_REMOVAL_TIMEOUT_MS = 1000;
  static constexpr uint8_t MAX_RETRIES = 3;
};

}  // namespace st25r3916
}  // namespace esphome
