#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/hal.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/nfc/nfc_tag.h"
#include "esphome/components/nfc/nfc.h"
#include "esphome/components/nfc/automation.h"

#include "st25r_registers.h"

#include <vector>

namespace esphome {
namespace st25r {

class ST25RBinarySensor;

class ST25R : public PollingComponent {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;
  void loop() override;

  void set_rf_field_enabled(bool enabled) { this->rf_field_enabled_ = enabled; }
  void set_max_failed_checks(uint8_t n) { this->max_failed_checks_ = n; }
  void set_auto_reset(bool v) { this->auto_reset_ = v; }

  void register_tag(ST25RBinarySensor *tag) { this->binary_sensors_.push_back(tag); }
  void register_ontag_trigger(nfc::NfcOnTagTrigger *trig) { this->triggers_ontag_.push_back(trig); }
  void register_ontagremoved_trigger(nfc::NfcOnTagTrigger *trig) { this->triggers_ontagremoved_.push_back(trig); }

  void add_on_finished_write_callback(std::function<void()> callback) {
    this->on_finished_write_callback_.add(std::move(callback));
  }

  bool is_writing() { return this->next_task_ != READ; };

  void read_mode();
  void clean_mode();
  void format_mode();
  void write_mode(nfc::NdefMessage *message);

 protected:
  virtual uint8_t read_register(uint8_t reg) = 0;
  virtual void write_register(uint8_t reg, uint8_t value) = 0;
  virtual void write_command(uint8_t command) = 0;
  virtual void write_fifo(const uint8_t *data, size_t len) = 0;
  virtual void read_fifo(uint8_t *data, size_t len) = 0;

  bool initialize_chip_();
  void turn_off_rf_();
  void turn_on_rf_();

  std::vector<ST25RBinarySensor *> binary_sensors_;
  std::vector<nfc::NfcOnTagTrigger *> triggers_ontag_;
  std::vector<nfc::NfcOnTagTrigger *> triggers_ontagremoved_;
  
  std::vector<uint8_t> current_uid_;
  nfc::NdefMessage *next_task_message_to_write_{nullptr};

  enum NfcTask {
    READ = 0,
    CLEAN,
    FORMAT,
    WRITE,
  } next_task_{READ};

  uint8_t consecutive_failures_{0};
  uint8_t max_failed_checks_{3};
  bool auto_reset_{true};
  bool rf_field_enabled_{false};
  bool initialized_{false};

  CallbackManager<void()> on_finished_write_callback_;
};

class ST25RBinarySensor : public binary_sensor::BinarySensor {
 public:
  void set_uid(const std::vector<uint8_t> &uid) { uid_ = uid; }
  bool process(const std::vector<uint8_t> &data);
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

class ST25ROnFinishedWriteTrigger : public Trigger<> {
 public:
  explicit ST25ROnFinishedWriteTrigger(ST25R *parent) {
    parent->add_on_finished_write_callback([this]() { this->trigger(); });
  }
};

template<typename... Ts> class ST25RIsWritingCondition : public Condition<Ts...>, public Parented<ST25R> {
 public:
  bool check(const Ts &...x) override { return this->parent_->is_writing(); }
};

}  // namespace st25r
}  // namespace esphome
