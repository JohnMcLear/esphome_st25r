#include "st25r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace st25r {

static const char *const TAG = "st25r";

void ST25R::setup() {
  ESP_LOGD(TAG, "Setting up ST25R...");
  if (!this->initialize_chip_()) {
    ESP_LOGE(TAG, "ST25R initialization failed!");
    this->mark_failed();
    return;
  }
  ESP_LOGD(TAG, "ST25R initialized successfully.");
}

bool ST25R::initialize_chip_() {
  // Reset chip to default state
  this->write_command(ST25R3916_CMD_SET_DEFAULT);
  delay(5);

  // Check IC Identity
  uint8_t identity = this->read_register(ST25R3916_REG_IC_IDENTITY);
  ESP_LOGD(TAG, "IC Identity: 0x%02X", identity);

  // IO Configuration
  this->write_register(ST25R3916_REG_IO_CONF1, 0x00);
  this->write_register(ST25R3916_REG_IO_CONF2, 0x00);

  // Set mode to ISO14443A
  this->write_register(ST25R3916_REG_MODE, 0x08);
  
  // Set bitrate to 106kbps
  this->write_register(ST25R3916_REG_BIT_RATE, 0x00);

  // Turn on oscillator and RF field
  this->turn_on_rf_();

  this->initialized_ = true;
  return true;
}

void ST25R::turn_on_rf_() {
  ESP_LOGD(TAG, "Turning RF field ON");
  uint8_t op_control = this->read_register(ST25R3916_REG_OP_CONTROL);
  op_control |= 0x80; // Operation control: en (oscillator enable)
  this->write_register(ST25R3916_REG_OP_CONTROL, op_control);
  
  delay(10);
  
  op_control |= 0x40; // Operation control: tx_en (RF field enable)
  this->write_register(ST25R3916_REG_OP_CONTROL, op_control);
  this->rf_field_enabled_ = true;
}

void ST25R::turn_off_rf_() {
  ESP_LOGD(TAG, "Turning RF field OFF");
  uint8_t op_control = this->read_register(ST25R3916_REG_OP_CONTROL);
  op_control &= ~0x40; // Disable RF field
  this->write_register(ST25R3916_REG_OP_CONTROL, op_control);
  this->rf_field_enabled_ = false;
}

void ST25R::update() {
  if (!this->initialized_) return;

  for (auto *obj : this->binary_sensors_)
    obj->on_scan_end();

  ESP_LOGV(TAG, "Updating ST25R...");
}

void ST25R::loop() {}

void ST25R::dump_config() {
  ESP_LOGCONFIG(TAG, "ST25R NFC Reader:");
  LOG_UPDATE_INTERVAL(this);
}

void ST25R::read_mode() { this->next_task_ = READ; }
void ST25R::clean_mode() { this->next_task_ = CLEAN; }
void ST25R::format_mode() { this->next_task_ = FORMAT; }
void ST25R::write_mode(nfc::NdefMessage *message) {
  this->next_task_ = WRITE;
  this->next_task_message_to_write_ = message;
}

bool ST25RBinarySensor::process(const std::vector<uint8_t> &data) {
  if (data.size() != this->uid_.size())
    return false;
  for (size_t i = 0; i < data.size(); i++) {
    if (data[i] != this->uid_[i])
      return false;
  }
  this->publish_state(true);
  this->found_ = true;
  return true;
}

}  // namespace st25r
}  // namespace esphome
