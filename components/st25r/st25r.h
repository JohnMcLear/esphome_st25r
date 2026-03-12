#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/automation.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/nfc/nfc.h"
#include <vector>
#include <string>
#include <set>
#include <map>

// Compatibility for ESPHome < 2025.x where nfc::NfcTagUid was not defined
#include "esphome/core/version.h"
#if ESPHOME_VERSION_CODE < VERSION_CODE(2025, 1, 0)
namespace esphome {
namespace nfc {
using NfcTagUid = std::vector<uint8_t>;
}  // namespace nfc
}  // namespace esphome
#endif

namespace esphome {
namespace st25r {

class ST25RBinarySensor;

// ST25R Register Definitions
enum ST25RRegister : uint8_t {
  IO_CONF1 = 0x00,
  IO_CONF2 = 0x01,
  OP_CONTROL = 0x02,
  MODE = 0x03,
  BIT_RATE = 0x04,
  ISO14443A_CONF = 0x05,
  RX_CONF1 = 0x0B,
  RX_CONF2 = 0x0C,
  RX_CONF3 = 0x0D,
  RX_CONF4 = 0x0E,
  MASK_MAIN = 0x16,
  IRQ_MAIN = 0x1A,
  IRQ_TIMER = 0x1B,
  IRQ_ERROR = 0x1C,
  FIFO_STATUS1 = 0x1E,
  FIFO_STATUS2 = 0x1F,
  COLLISION_DISPLAY = 0x20,
  NUM_TX_BYTES1 = 0x22,
  NUM_TX_BYTES2 = 0x23,
  AD_CONV_RESULT = 0x25,
  AAT_A = 0x26,
  AAT_B = 0x27,
  TX_DRIVER_CONF = 0x28,
  IC_IDENTITY = 0x3F,
  // Space B Registers (marked with 0x40 bit for our internal helper)
  PT_MOD = 0x69,
  AUX_MOD = 0x68,
  CORR_CONF1 = 0x4C,
  CORR_CONF2 = 0x4D,
};

// ST25R Commands
enum ST25RCommand : uint8_t {
  ST25R_CMD_SET_DEFAULT = 0xC1,
  ST25R_CMD_STOP_ALL = 0xC2,
  ST25R_CMD_CLEAR_FIFO = 0xDB,
  ST25R_CMD_TRANSMIT_WITH_CRC = 0xC4,
  ST25R_CMD_TRANSMIT_WITHOUT_CRC = 0xC5,
  ST25R_CMD_TRANSMIT_REQA = 0xC6,
  ST25R_CMD_TRANSMIT_WUPA = 0xC7,
  ST25R_CMD_FIELD_ON = 0xC8,
  ST25R_CMD_FIELD_OFF = 0xC9,
  ST25R_CMD_ANTENNA_TUNE = 0xCD,
  ST25R_CMD_MEASURE_AMPLITUDE = 0xD3,
  ST25R_CMD_ADJUST_REGULATORS = 0xD6,
  ST25R_CMD_READ_FIFO = 0x9F,
  ST25R_CMD_RC_CAL = 0xEA,
  ST25R_CMD_TEST_ACCESS = 0xFC,
};

class ST25R;

class ST25RTagTrigger : public Trigger<std::string> {
 public:
  explicit ST25RTagTrigger(ST25R *parent) : parent_(parent) {}

 protected:
  ST25R *parent_;
};

class ST25RTagScanTrigger : public Trigger<> {
 public:
  explicit ST25RTagScanTrigger(ST25R *parent) : parent_(parent) {}

 protected:
  ST25R *parent_;
};

class ST25RTagRemovedTrigger : public Trigger<std::string> {
 public:
  explicit ST25RTagRemovedTrigger(ST25R *parent) : parent_(parent) {}

 protected:
  ST25R *parent_;
};

class ST25R : public PollingComponent, public nfc::Nfcc {
 public:
  enum State {
    STATE_IDLE,
    STATE_WUPA,
    STATE_ANTICOL,
    STATE_SELECT,
    STATE_REINITIALIZING,
  };

  void setup() override;
  void dump_config() override;
  void update() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  bool ndef_write(nfc::NdefMessage *message);
  bool clean_tag();

  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_irq_pin(InternalGPIOPin *irq_pin) { this->irq_pin_ = irq_pin; }
  void set_rf_field_enabled(bool enabled) { this->rf_field_enabled_ = enabled; }
  void set_rf_power(uint8_t power) { this->rf_power_ = power; }
  void set_supply_3v3(bool supply_3v3) { this->supply_3v3_ = supply_3v3; }
  void add_known_uid(const std::string &uid) { this->known_uids_.push_back(uid); }

  void register_on_tag_trigger(ST25RTagTrigger *trig) { this->on_tag_triggers_.push_back(trig); }
  void register_on_tag_scan_trigger(ST25RTagScanTrigger *trig) { this->on_tag_scan_triggers_.push_back(trig); }
  void register_on_tag_removed_trigger(ST25RTagRemovedTrigger *trig) {
    this->on_tag_removed_triggers_.push_back(trig);
  }
  void register_tag(ST25RBinarySensor *tag) { this->binary_sensors_.push_back(tag); }
  void set_status_binary_sensor(binary_sensor::BinarySensor *sensor) { this->status_binary_sensor_ = sensor; }
  void set_field_strength_sensor(sensor::Sensor *sensor) { this->field_strength_sensor_ = sensor; }

  bool is_tag_present() const { return !this->present_tags_.empty(); }

 protected:
  virtual uint8_t read_register(uint8_t reg) = 0;
  virtual void write_register(uint8_t reg, uint8_t value) = 0;
  virtual void write_test_register(uint8_t reg, uint8_t value) = 0;
  virtual void write_command(uint8_t command) = 0;
  virtual void write_fifo(const uint8_t *data, size_t len) = 0;
  virtual void read_fifo(uint8_t *data, size_t len) = 0;

  void write_register_b(uint8_t reg, uint8_t value);

  bool reset_();
  void field_on_();
  void finalize_scan_();
  bool wait_for_irq_(uint8_t mask, uint32_t timeout_ms);
  void reinitialize_();
  bool transceive_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms = 50);
  bool transceive_ex_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, bool with_crc, uint32_t timeout_ms = 50, bool reset_all = true);
  std::unique_ptr<nfc::NfcTag> read_tag_(nfc::NfcTagUid &uid);
  static void isr(ST25R *arg);
  
  GPIOPin *reset_pin_{nullptr};
  InternalGPIOPin *irq_pin_{nullptr};

  std::set<std::string> present_tags_;
  std::set<std::string> tags_this_scan_;
  std::map<std::string, uint8_t> tag_miss_counts_;
  std::vector<std::string> known_uids_;
  bool rf_field_enabled_{true};
  uint8_t rf_power_{15};
  bool supply_3v3_{true};
  
  volatile bool irq_triggered_{false};
  volatile uint8_t irq_status_{0};
  
  // IRQ_MAIN (0x1A) bits
  static const uint8_t IRQ_OSC     = 0x80;
  static const uint8_t IRQ_RXS     = 0x20;
  static const uint8_t IRQ_RXE     = 0x10;
  static const uint8_t IRQ_TXE     = 0x08;
  static const uint8_t IRQ_COL     = 0x04;
  
  State state_{STATE_IDLE};
  uint32_t last_state_change_{0};
  uint8_t cascade_level_{0};
  uint8_t uid_buffer_[5];
  std::string current_uid_;
  uint8_t last_amplitude_{0};

  std::vector<ST25RTagTrigger *> on_tag_triggers_;
  std::vector<ST25RTagScanTrigger *> on_tag_scan_triggers_;
  std::vector<ST25RTagRemovedTrigger *> on_tag_removed_triggers_;
  std::vector<ST25RBinarySensor *> binary_sensors_;
  binary_sensor::BinarySensor *status_binary_sensor_{nullptr};
  sensor::Sensor *field_strength_sensor_{nullptr};
};

class ST25RBinarySensor : public binary_sensor::BinarySensor {
 public:
  void set_uid(const std::vector<uint8_t> &uid) { uid_ = uid; }
  bool process(const std::string &uid);
  void on_scan_end() {
    if (!this->found_) {
      this->publish_state(false);
    }
    this->found_ = false;
  }

 protected:
  std::vector<uint8_t> uid_;
  bool found_{false};
};

}  // namespace st25r
}  // namespace esphome
