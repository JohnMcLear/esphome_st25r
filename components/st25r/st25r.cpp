#include "st25r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cinttypes>
#include <algorithm>

namespace esphome {
namespace st25r {

static const char *const TAG = "st25r";

void ST25R::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ST25R...");

  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true); 
    delay(10);
    this->reset_pin_->digital_write(false); 
    delay(10);
  }
  this->irq_pin_->setup();

  if (!this->reset_()) {
    ESP_LOGE(TAG, "Failed to reset chip");
    this->mark_failed();
    return;
  }
  ESP_LOGCONFIG(TAG, "ST25R initialized successfully.");
}

bool ST25R::reset_() {
  this->write_command(ST25R_CMD_SET_DEFAULT);
  delay(10);

  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  ESP_LOGD(TAG, "IC Identity: 0x%02X", ic_identity);

  if ((ic_identity >> 3) != 0x05) {
    ESP_LOGE(TAG, "Chip not responding correctly (ID: 0x%02X)", ic_identity);
    return false;
  }

  this->write_register(IO_CONF1, 0x80); 
  this->write_register(IO_CONF2, 0x80); 
  this->write_register(MODE, 0x08); 
  this->write_register(BIT_RATE, 0x00); 
  this->write_register(RX_CONF1, 0x00); 
  this->write_register(RX_CONF2, 0x68); 

  this->field_on_();
  delay(10);
  
  this->write_register(0x28, 0xF0); 

  return true;
}

void ST25R::update() {
  if (this->is_failed()) return;

  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);
  this->write_command(ST25R_CMD_CLEAR_FIFO);

  this->write_register(OP_CONTROL, 0xC8); 
  delay(2);

  this->write_command(ST25R_CMD_TRANSMIT_WUPA);
  
  bool tag_found = false;
  for (int i = 0; i < 10; i++) {
    if (this->irq_pin_->digital_read()) {
      tag_found = true;
      break;
    }
    delay(1);
  }

  if (tag_found) {
    this->read_register(IRQ_MAIN);
    uint8_t f1 = this->read_register(FIFO_STATUS1);
    
    if (f1 > 0) { 
      std::string uid = this->read_uid_();
      if (!uid.empty()) {
        this->missed_updates_ = 0;
        if (!this->tag_present_ || this->current_uid_ != uid) {
          ESP_LOGI(TAG, "New Tag Detected: %s", uid.c_str());
          this->tag_present_ = true;
          this->current_uid_ = uid;
          for (auto *trigger : this->on_tag_triggers_) {
            trigger->trigger(uid);
          }
        }
        for (auto *obj : this->binary_sensors_) obj->process(uid);
        return;
      }
    }
  }

  for (auto *obj : this->binary_sensors_) obj->on_scan_end();

  if (this->tag_present_) {
    this->missed_updates_++;
    if (this->missed_updates_ >= 2) {
      ESP_LOGI(TAG, "Tag Removed: %s", this->current_uid_.c_str());
      for (auto *trigger : this->on_tag_removed_triggers_) {
        trigger->trigger(this->current_uid_);
      }
      this->tag_present_ = false;
      this->current_uid_ = "";
      this->missed_updates_ = 0;
    }
  }
}

std::string ST25R::read_uid_() {
  std::string full_uid = "";
  uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
  
  for (int cascade = 0; cascade < 3; cascade++) {
    this->write_command(ST25R_CMD_CLEAR_FIFO);
    this->read_register(IRQ_MAIN);

    uint8_t cl[] = {sel_cmds[cascade], 0x20};
    this->write_fifo(cl, 2);
    this->write_register(NUM_TX_BYTES1, 0x00);
    this->write_register(NUM_TX_BYTES2, 0x10); 
    this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
    
    bool irq_fired = false;
    for (int i = 0; i < 20; i++) {
      if (this->irq_pin_->digital_read()) { irq_fired = true; break; }
      delay(1);
    }
    if (!irq_fired) break;

    this->read_register(IRQ_MAIN);
    uint8_t f1 = this->read_register(FIFO_STATUS1);
    if (f1 < 5) break;

    uint8_t resp[5];
    this->read_fifo(resp, 5);

    if (resp[0] == 0x88) {
      for(int i=1; i<4; i++) {
        char buf[3];
        sprintf(buf, "%02X", resp[i]);
        full_uid += buf;
      }
      this->write_command(ST25R_CMD_CLEAR_FIFO);
      uint8_t sel_pk[7] = {sel_cmds[cascade], 0x70, resp[0], resp[1], resp[2], resp[3], resp[4]};
      this->write_fifo(sel_pk, 7);
      this->write_register(NUM_TX_BYTES1, 0x00);
      this->write_register(NUM_TX_BYTES2, 0x38); 
      this->write_command(ST25R_CMD_TRANSMIT_WITH_CRC);
      for (int i = 0; i < 10; i++) { if (this->irq_pin_->digital_read()) break; delay(1); }
      this->read_register(IRQ_MAIN);
    } else {
      for(int i=0; i<4; i++) {
        char buf[3];
        sprintf(buf, "%02X", resp[i]);
        full_uid += buf;
      }
      return full_uid;
    }
  }
  return full_uid;
}

void ST25R::field_on_() {
  ESP_LOGD(TAG, "Turning field ON...");
  this->write_register(OP_CONTROL, 0x80); 
  delay(10);
  this->write_command(ST25R_CMD_FIELD_ON);
  delay(10);
  this->write_register(OP_CONTROL, 0xC0); 
}

void ST25R::dump_config() {
  ESP_LOGCONFIG(TAG, "ST25R:");
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  LOG_UPDATE_INTERVAL(this);
}

bool ST25RBinarySensor::process(const std::string &uid) {
  std::string target_uid = "";
  for (uint8_t b : this->uid_) {
    char buf[3];
    sprintf(buf, "%02X", b);
    target_uid += buf;
  }

  if (uid == target_uid) {
    this->publish_state(true);
    this->found_ = true;
    return true;
  }
  return false;
}

}  // namespace st25r
}  // namespace esphome
