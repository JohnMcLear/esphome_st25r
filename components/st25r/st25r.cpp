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
  // Direct Command: Set Default
  this->write_command(0xC1); 
  delay(10);

  uint8_t identity = this->read_register(0x3F);
  ESP_LOGD(TAG, "GEMINI_LOG: IC Identity: 0x%02X", identity);

  // IO Configuration
  this->write_register(0x00, 0x00);
  this->write_register(0x01, 0x00);
  this->write_register(0x0A, 0x04);
  this->write_register(0x0B, 0x2D);
  this->write_register(0x2A, 0x03);

  this->write_register(0x03, 0x08);
  this->write_register(0x04, 0x00);

  this->turn_on_rf_();

  this->initialized_ = true;
  return true;
}

void ST25R::turn_on_rf_() {
  ESP_LOGD(TAG, "Turning RF field ON");
  this->write_register(0x02, 0x80);
  delay(10);
  this->write_command(0xC8);
  delay(10);
  this->write_register(0x02, 0xC0);
  this->rf_field_enabled_ = true;
}

void ST25R::turn_off_rf_() {
  ESP_LOGD(TAG, "Turning RF field OFF");
  this->write_register(0x02, 0x00);
  this->rf_field_enabled_ = false;
}

void ST25R::update() {
  if (!this->initialized_) return;

  ESP_LOGV(TAG, "GEMINI_LOG: Updating ST25R...");

  this->write_command(0xC2);
  this->read_register(0x18);
  
  uint8_t reqa = 0x26;
  this->write_fifo(&reqa, 1);
  this->write_register(0x1F, 0x80);
  this->write_register(0x1E, 0x00);
  this->write_command(0xC5);
  
  delay(20);

  uint8_t fifo_status = this->read_register(0x1B);
  if (fifo_status > 0) {
    ESP_LOGI(TAG, "GEMINI_LOG: Tag response! FIFO: %d", fifo_status);
    
    this->write_command(0xC2);
    uint8_t anti_col[] = {0x93, 0x20};
    this->write_fifo(anti_col, 2);
    this->write_register(0x1F, 0x00);
    this->write_register(0x1E, 0x02);
    this->write_command(0xC5);
    
    delay(20);
    
    fifo_status = this->read_register(0x1B);
    if (fifo_status >= 5) {
      uint8_t resp[5];
      this->read_fifo(resp, 5);
      
      // Mandatory 2-bit right shift fix
      for(int i=0; i<5; i++) resp[i] >>= 2;

      char uid_str[20];
      snprintf(uid_str, 20, "%02X-%02X-%02X-%02X", resp[0], resp[1], resp[2], resp[3]);
      ESP_LOGI(TAG, "GEMINI_LOG: Tag UID: %s", uid_str);
      
      std::vector<uint8_t> uid_vec = {resp[0], resp[1], resp[2], resp[3]};
      for (auto *trigger : this->triggers_ontag_) trigger->trigger(uid_str, nfc::NfcTag());
      for (auto *obj : this->binary_sensors_) obj->process(uid_vec);
    }
  } else {
    for (auto *obj : this->binary_sensors_) obj->on_scan_end();
  }
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
  if (data.size() != this->uid_.size()) return false;
  for (size_t i = 0; i < data.size(); i++) {
    if (data[i] != this->uid_[i]) return false;
  }
  this->publish_state(true);
  this->found_ = true;
  return true;
}

}  // namespace st25r
}  // namespace esphome
