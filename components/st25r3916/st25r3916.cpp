#include "st25r3916.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cinttypes>

namespace esphome {
namespace st25r3916 {

static const char *const TAG = "st25r3916";

void ST25R3916::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ST25R3916...");
  this->spi_setup();

  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
    delay(10);
  }
  this->irq_pin_->setup();

  if (!this->reset_()) {
    ESP_LOGE(TAG, "Failed to reset chip");
    this->mark_failed();
    return;
  }
  ESP_LOGCONFIG(TAG, "ST25R3916 initialized successfully.");
}

bool ST25R3916::reset_() {
  this->write_command_(ST25R3916_CMD_SET_DEFAULT);
  delay(10);

  uint8_t ic_identity = this->read_register_(IC_IDENTITY);
  if (ic_identity == 0x2A) {
    this->bit_shift_ = 2;
    ic_identity >>= 2;
  }

  if (ic_identity != 0x05 && ic_identity != 0x0A) {
    ESP_LOGE(TAG, "Chip not responding correctly (ID: 0x%02X)", ic_identity);
    return false;
  }

  this->write_register_(IO_CONF1, 0x00);
  this->write_register_(IO_CONF2, 0x00);
  this->write_register_(MODE, 0x08); 
  this->write_register_(BIT_RATE, 0x00); 

  this->field_on_();
  return true;
}

void ST25R3916::update() {
  if (this->is_failed()) return;

  // ISO14443A Poll using built-in REQA command
  this->write_command_(ST25R3916_CMD_CLEAR_FIFO);
  
  // Clear any pending IRQs
  this->read_register_(ST25R3916Register::IRQ_MAIN);
  
  // Command 0xC6 is CMD_TRANSMIT_REQA
  this->write_command_(0xC6); 
  delay(20);

  uint8_t fifo_status = this->read_register_(FIFO_STATUS1);
  if (fifo_status > 0) {
    ESP_LOGI(TAG, "Tag detected! Reading UID...");
    
    // Simplistic UID read for verification
    uint8_t resp[10];
    this->read_fifo_(resp, 10);
    
    char uid_str[30];
    snprintf(uid_str, 30, "%02X-%02X-%02X-%02X", resp[0], resp[1], resp[2], resp[3]);
    ESP_LOGI(TAG, "New Tag Found: %s", uid_str);
    
    if (this->current_uid_ != uid_str) {
      this->current_uid_ = uid_str;
      this->tag_present_ = true;
      for (auto *trigger : this->on_tag_triggers_) {
        trigger->trigger(uid_str);
      }
    }
  } else {
    this->tag_present_ = false;
    this->current_uid_ = "";
  }
}

uint8_t ST25R3916::read_register_(ST25R3916Register reg) {
  this->enable();
  this->write_byte((uint8_t)((0x40 | (reg & 0x3F)) << this->bit_shift_));
  uint8_t value = this->read_byte();
  this->disable();
  return value >> this->bit_shift_;
}

void ST25R3916::write_register_(ST25R3916Register reg, uint8_t value) {
  this->enable();
  this->write_byte((uint8_t)((0x00 | (reg & 0x3F)) << this->bit_shift_));
  this->write_byte((uint8_t)(value << this->bit_shift_));
  this->disable();
}

void ST25R3916::write_command_(uint8_t command) {
  this->enable();
  this->write_byte((uint8_t)(command << this->bit_shift_));
  this->disable();
}

void ST25R3916::write_fifo_(const uint8_t *data, size_t len) {
  this->enable();
  this->write_byte((uint8_t)(0x80 << this->bit_shift_));
  for (size_t i = 0; i < len; i++) {
    this->write_byte((uint8_t)(data[i] << this->bit_shift_));
  }
  this->disable();
}

void ST25R3916::read_fifo_(uint8_t *data, size_t len) {
  this->enable();
  this->write_byte((uint8_t)(0xBF << this->bit_shift_));
  for (size_t i = 0; i < len; i++) {
    data[i] = (uint8_t)(this->read_byte() >> this->bit_shift_);
  }
  this->disable();
}

void ST25R3916::field_on_() {
  this->write_register_(OP_CONTROL, 0x80); // OSC ON
  delay(10);
  this->write_command_(0xC8); // INITIAL_FIELD_ON
  delay(10);
  this->write_register_(OP_CONTROL, 0xC0); // OSC + TX ON
}

void ST25R3916::dump_config() {
  ESP_LOGCONFIG(TAG, "ST25R3916:");
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace st25r3916
}  // namespace esphome
