#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/automation.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/nfc/nfc.h"
#include "st25r300_registers.h"
#include <map>
#include <set>
#include <vector>
#include <string>

namespace esphome {
namespace st25r300 {

class ST25R300;

class ST25R300TagTrigger : public Trigger<std::string> {
 public:
  explicit ST25R300TagTrigger(ST25R300 *parent) : parent_(parent) {}
 protected:
  ST25R300 *parent_;
};

class ST25R300TagRemovedTrigger : public Trigger<std::string> {
 public:
  explicit ST25R300TagRemovedTrigger(ST25R300 *parent) : parent_(parent) {}
 protected:
  ST25R300 *parent_;
};

template<typename... Ts> class NDEFWriteAction : public Action<Ts...> {
 public:
  void set_parent(ST25R300 *parent) { parent_ = parent; }
  void set_message(std::function<nfc::NdefMessage *(Ts...)> func) { message_func_ = func; }
  void set_format(bool format) { format_ = format; }
  void play(const Ts &...x) override;
 protected:
  ST25R300 *parent_;
  std::function<nfc::NdefMessage *(Ts...)> message_func_;
  bool format_{false};
};

class ST25R300 : public PollingComponent, public nfc::Nfcc {
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
  void process_state_();
  float get_setup_priority() const override { return setup_priority::DATA; }

  bool ndef_write(nfc::NdefMessage *message, bool format = false);
  bool clean_tag();

  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_irq_pin(InternalGPIOPin *irq_pin) { this->irq_pin_ = irq_pin; }
  void set_rf_field_enabled(bool enabled) { this->rf_field_enabled_ = enabled; }
  void set_rf_power(uint8_t power) { this->rf_power_ = power; }
  void set_supply_3v3(bool supply_3v3) { this->supply_3v3_ = supply_3v3; }
  void set_rx_gain_boost(bool boost) { this->rx_gain_boost_ = boost; }
  void set_mifare_key_a(uint64_t key) { this->mifare_key_a_ = key; }
  void set_mifare_key_b(uint64_t key) { this->mifare_key_b_ = key; }

  void register_on_tag_trigger(ST25R300TagTrigger *trig) { this->on_tag_triggers_.push_back(trig); }
  void register_on_tag_removed_trigger(ST25R300TagRemovedTrigger *trig) {
    this->on_tag_removed_triggers_.push_back(trig);
  }
  void set_status_binary_sensor(binary_sensor::BinarySensor *sensor) { this->status_binary_sensor_ = sensor; }
  void set_field_strength_sensor(sensor::Sensor *sensor) { this->field_strength_sensor_ = sensor; }

  bool is_tag_present() const { return !this->present_tags_.empty(); }

 protected:
  // Hardware abstraction — implemented by st25r300_spi
  virtual uint8_t read_register(uint8_t reg) = 0;
  virtual void write_register(uint8_t reg, uint8_t value) = 0;
  virtual void write_command(uint8_t command) = 0;
  virtual void write_fifo(const uint8_t *data, size_t len) = 0;
  virtual void read_fifo(uint8_t *data, size_t len) = 0;

  bool reset_();
  void reinitialize_();
  void finalize_scan_();
  void apply_anticol_prefix_();
  void send_anticol_frame_();
  bool transceive_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms = 150);
  bool transceive_no_crc_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms = 150);
  bool transceive_ex_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, bool with_crc, uint32_t timeout_ms = 150);
  // Send WUPA/REQA 7-bit short frame (ST25R300 has no dedicated command for this)
  bool send_short_frame_(uint8_t byte7, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms = 10);

  std::unique_ptr<nfc::NfcTag> read_tag_(std::vector<uint8_t> &uid);
  static void isr(ST25R300 *arg);

  GPIOPin *reset_pin_{nullptr};
  InternalGPIOPin *irq_pin_{nullptr};

  bool rf_field_enabled_{true};
  uint8_t rf_power_{15};
  bool supply_3v3_{true};
  bool rx_gain_boost_{false};
  uint64_t mifare_key_a_{0xFFFFFFFFFFFFULL};
  uint64_t mifare_key_b_{0xFFFFFFFFFFFFULL};
  uint8_t health_check_failures_{0};
  volatile bool irq_triggered_{false};
  volatile uint8_t irq_status1_{0};
  uint8_t irq_status2_{0};

  // Multi-tag tracking
  std::map<std::string, uint8_t> present_tags_;   // UID → consecutive miss count
  std::set<std::string> tags_this_scan_;           // UIDs found in current scan cycle
  std::map<std::string, std::unique_ptr<nfc::NfcTag>> tags_data_;

  State state_{STATE_IDLE};
  uint32_t last_state_change_{0};
  uint8_t cascade_level_{0};
  std::string current_uid_;

  // Anticollision loop state
  uint8_t anticol_prefix_[5]{};
  uint8_t anticol_prefix_full_;
  uint8_t anticol_prefix_bits_;
  uint8_t anticol_col_pos_{0};
  uint8_t anticol_prefix_val_{0};

  // Multi-tag tree traversal: saved CL1 collision state for resuming after cascade CL2
  uint8_t saved_col_pos_{0};
  uint8_t saved_prefix_val_{0};
  bool saved_anticol_valid_{false};
  bool anticol_resume_{false};

  std::vector<ST25R300TagTrigger *> on_tag_triggers_;
  std::vector<ST25R300TagRemovedTrigger *> on_tag_removed_triggers_;
  binary_sensor::BinarySensor *status_binary_sensor_{nullptr};
  sensor::Sensor *field_strength_sensor_{nullptr};
};

template<typename... Ts> void NDEFWriteAction<Ts...>::play(const Ts &...x) {
  auto *message = this->message_func_(x...);
  if (message != nullptr) {
    this->parent_->ndef_write(message, this->format_);
  }
}

template<typename... Ts> class CleanTagAction : public Action<Ts...> {
 public:
  void set_parent(ST25R300 *parent) { parent_ = parent; }
  void play(const Ts &...x) override { this->parent_->clean_tag(); }
 protected:
  ST25R300 *parent_;
};

}  // namespace st25r300
}  // namespace esphome
