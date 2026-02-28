#include "st25r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/nfc/nfc_tag.h"
#include <cinttypes>
#include <algorithm>

namespace esphome {
namespace st25r {

static const char *const TAG = "st25r";

void ST25R::isr(ST25R *arg) {
  arg->irq_triggered_ = true;
}

void ST25R::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ST25R...");
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
    delay(10);
    this->reset_pin_->digital_write(false); 
    delay(10);
  }
  
  if (this->irq_pin_ != nullptr) {
    this->irq_pin_->setup();
    this->irq_pin_->attach_interrupt(ST25R::isr, this, gpio::INTERRUPT_RISING_EDGE);
  }

  if (!this->reset_()) {
    ESP_LOGE(TAG, "Failed to reset chip");
    if (this->status_binary_sensor_ != nullptr) {
      this->status_binary_sensor_->publish_initial_state(false);
    }
    this->mark_failed();
    return;
  }
  if (this->status_binary_sensor_ != nullptr) {
    this->status_binary_sensor_->publish_initial_state(true);
  }
  ESP_LOGCONFIG(TAG, "ST25R initialized successfully.");
}

void ST25R::update() {
  if (this->is_failed() || this->state_ != STATE_IDLE) return;

  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  if ((ic_identity >> 3) != 0x05) {
    this->health_check_failures_++;
    if (this->status_binary_sensor_ != nullptr) {
      this->status_binary_sensor_->publish_state(false);
    }
    if (this->health_check_failures_ >= 3) {
      this->state_ = STATE_REINITIALIZING;
    }
    return;
  }
  
  this->health_check_failures_ = 0;
  if (this->status_binary_sensor_ != nullptr) {
    this->status_binary_sensor_->publish_state(true);
  }

  if (this->rf_field_enabled_ && this->field_strength_sensor_ != nullptr) {
    this->write_command(ST25R_CMD_MEASURE_AMPLITUDE);
    uint8_t amplitude = this->read_register(AD_CONV_RESULT);
    this->field_strength_sensor_->publish_state(amplitude);
  }

  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);
  this->write_command(ST25R_CMD_CLEAR_FIFO);

  if (this->rf_field_enabled_) {
    this->write_register(OP_CONTROL, 0xC8); 
  }

  this->irq_triggered_ = false;
  this->write_command(ST25R_CMD_TRANSMIT_WUPA);
  delay(1);
  this->state_ = STATE_WUPA;
  this->last_state_change_ = millis();
}

bool ST25R::transceive_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->read_register(IRQ_MAIN); 

  this->write_fifo(data, len);
  this->write_register(NUM_TX_BYTES1, (len >> 8) & 0xFF);
  this->write_register(NUM_TX_BYTES2, (len & 0x1F) << 3); 
  
  this->irq_triggered_ = false;
  this->write_command(ST25R_CMD_TRANSMIT_WITH_CRC);
  
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (this->irq_triggered_) {
      this->irq_triggered_ = false;
      this->irq_status_ = this->read_register(IRQ_MAIN);
      
      if (this->irq_status_ & IRQ_RXE) { 
        uint8_t f1 = this->read_register(FIFO_STATUS1);
        resp_len = (f1 > 64) ? 64 : f1;
        this->read_fifo(resp, resp_len);
        return true;
      }
      if (this->irq_status_ & IRQ_ERR) return false; 
    }
    delay(1);
  }
  return false;
}

std::unique_ptr<nfc::NfcTag> ST25R::read_tag_(std::vector<uint8_t> &uid) {
  nfc::NfcTagUid tag_uid;
  for (auto b : uid) tag_uid.push_back(b);
  uint8_t type = nfc::guess_tag_type(uid.size());
  
  if (type == nfc::TAG_TYPE_2) {
    std::vector<uint8_t> data;
    uint8_t buffer[16];
    uint8_t len;

    uint8_t read_cmd[2] = {0x30, 0x03}; 
    if (this->transceive_(read_cmd, 2, buffer, len) && len >= 16) {
      data.insert(data.end(), buffer, buffer + 16);
      
      size_t tlv_index = 0;
      bool found = false;
      for (size_t i = 0; i < 15; i++) { 
        if (data[i] == 0x03) {
          tlv_index = i;
          found = true;
          break;
        }
      }

      if (found) {
        uint8_t msg_len = data[tlv_index + 1];
        size_t msg_start_idx = tlv_index + 2;
        
        while (data.size() < (size_t)(msg_start_idx + msg_len)) {
          uint8_t next_page = data.size() / 4;
          read_cmd[1] = next_page;
          if (!this->transceive_(read_cmd, 2, buffer, len) || len < 16) break;
          data.insert(data.end(), buffer, buffer + 16);
        }
        
        if (data.size() >= (size_t)(msg_start_idx + msg_len)) {
          std::vector<uint8_t> ndef_data(data.begin() + msg_start_idx, data.begin() + msg_start_idx + msg_len);
          return make_unique<nfc::NfcTag>(tag_uid, nfc::NFC_FORUM_TYPE_2, ndef_data);
        }
      }
    }
  }

  return make_unique<nfc::NfcTag>(tag_uid);
}

void ST25R::loop() {
  if (this->is_failed()) return;

  switch (this->state_) {
    case STATE_IDLE:
      break;

    case STATE_WUPA: {
      if (millis() - this->last_state_change_ > 50) { 
        this->state_ = STATE_IDLE;
        this->process_tag_removed_(false);
        return;
      }

      if (this->irq_triggered_) {
        this->irq_triggered_ = false;
        this->irq_status_ = this->read_register(IRQ_MAIN);
        
        if (this->irq_status_ & (IRQ_RXE | IRQ_TXE)) {
          uint8_t f1 = this->read_register(FIFO_STATUS1);
          if (f1 > 0) {
            this->cascade_level_ = 0;
            this->current_uid_ = "";
            this->state_ = STATE_READ_UID;
            this->last_state_change_ = millis();
            this->write_command(ST25R_CMD_CLEAR_FIFO);
            uint8_t cl[] = {0x93, 0x20};
            this->irq_triggered_ = false;
            this->write_fifo(cl, 2);
            this->write_register(NUM_TX_BYTES1, 0x00);
            this->write_register(NUM_TX_BYTES2, 0x10); 
            this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
          } else {
            this->state_ = STATE_IDLE;
            this->process_tag_removed_(false);
          }
        }
      }
      break;
    }

    case STATE_READ_UID: {
      if (millis() - this->last_state_change_ > 500) { 
        this->state_ = STATE_IDLE;
        this->process_tag_removed_(false);
        return;
      }

      if (this->irq_triggered_) {
        this->irq_triggered_ = false;
        this->irq_status_ = this->read_register(IRQ_MAIN);
        
        if (this->irq_status_ & (IRQ_RXE | IRQ_TXE)) {
          uint8_t f1 = this->read_register(FIFO_STATUS1);
          
          if (f1 == 0) {
            this->state_ = STATE_IDLE;
            this->process_tag_removed_(false);
            return;
          }

          uint8_t resp[16];
          uint8_t actual_len = (f1 > 16) ? 16 : f1;
          this->read_fifo(resp, actual_len);
          
          if (resp[0] == 0x88) {
            for (int i = 1; i < 4; i++) {
              char buf[3];
              sprintf(buf, "%02X", resp[i]);
              this->current_uid_ += buf;
            }
            uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
            uint8_t sel_pk[7] = {sel_cmds[this->cascade_level_], 0x70, resp[0], resp[1], resp[2], resp[3], resp[4]};
            this->write_command(ST25R_CMD_CLEAR_FIFO);
            this->irq_triggered_ = false;
            this->write_fifo(sel_pk, 7);
            this->write_register(NUM_TX_BYTES1, 0x00);
            this->write_register(NUM_TX_BYTES2, 0x38); 
            this->write_command(ST25R_CMD_TRANSMIT_WITH_CRC);
            
            this->cascade_level_++;
            delay(20); 
            this->read_register(IRQ_MAIN); 
            this->write_command(ST25R_CMD_CLEAR_FIFO);
            uint8_t next_cl[] = {sel_cmds[this->cascade_level_], 0x20};
            this->irq_triggered_ = false;
            this->write_fifo(next_cl, 2);
            this->write_register(NUM_TX_BYTES1, 0x00);
            this->write_register(NUM_TX_BYTES2, 0x10); 
            this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
            this->last_state_change_ = millis();
          } else {
            for (int i = 0; i < 4; i++) {
              char buf[3];
              sprintf(buf, "%02X", resp[i]);
              this->current_uid_ += buf;
            }
            
            std::vector<uint8_t> uid_bytes;
            for (size_t i = 0; i < this->current_uid_.length(); i += 2) {
              std::string byteString = this->current_uid_.substr(i, 2);
              uint8_t byte = (uint8_t) strtol(byteString.c_str(), nullptr, 16);
              uid_bytes.push_back(byte);
            }
            
            auto nfc_tag = this->read_tag_(uid_bytes);
            if (nfc_tag->has_ndef_message()) {
              auto &message = nfc_tag->get_ndef_message();
              for (auto &record : message->get_records()) {
                ESP_LOGI(TAG, "  NDEF Record type: %s", record->get_type().c_str());
                ESP_LOGI(TAG, "  NDEF Payload: %s", record->get_payload().c_str());
              }
            }

            if (!this->tag_present_ || this->tag_present_uid_ != this->current_uid_) {
              this->tag_present_ = true;
              this->tag_present_uid_ = this->current_uid_;

              for (auto *listener : this->tag_listeners_) {
                listener->tag_on(*nfc_tag);
              }

              for (auto *trigger : this->on_tag_triggers_) {
                trigger->trigger(this->current_uid_);
              }
            }
            for (auto *obj : this->binary_sensors_) obj->process(this->current_uid_);
            this->state_ = STATE_IDLE;
            this->process_tag_removed_(true);
          }
        }
      }
      break;
    }

    case STATE_REINITIALIZING:
      this->reinitialize_();
      this->state_ = STATE_IDLE;
      break;
  }
}

void ST25R::process_tag_removed_(bool found) {
  for (auto *obj : this->binary_sensors_) obj->on_scan_end();

  if (found) {
    this->missed_updates_ = 0;
    return;
  }

  if (this->tag_present_) {
    this->missed_updates_++;
    if (this->missed_updates_ >= 3) {
      ESP_LOGI(TAG, "Tag Removed: %s", this->tag_present_uid_.c_str());

      std::vector<uint8_t> uid_bytes;
      for (size_t i = 0; i < this->tag_present_uid_.length(); i += 2) {
        std::string byteString = this->tag_present_uid_.substr(i, 2);
        uint8_t byte = (uint8_t) strtol(byteString.c_str(), nullptr, 16);
        uid_bytes.push_back(byte);
      }
      nfc::NfcTagUid tag_uid;
      for (auto b : uid_bytes) tag_uid.push_back(b);
      nfc::NfcTag nfc_tag(tag_uid);
      for (auto *listener : this->tag_listeners_) {
        listener->tag_off(nfc_tag);
      }

      for (auto *trigger : this->on_tag_removed_triggers_) {
        trigger->trigger(this->tag_present_uid_);
      }
      this->tag_present_ = false;
      this->tag_present_uid_ = "";
      this->missed_updates_ = 0;
    }
  }
}

bool ST25R::wait_for_irq_(uint8_t mask, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (this->irq_triggered_) {
       return true;
    }
    delay(1);
  }
  return false;
}

bool ST25R::reset_() {
  this->write_command(ST25R_CMD_SET_DEFAULT);
  delay(10);

  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  if ((ic_identity >> 3) != 0x05) return false;

  this->write_register(IO_CONF1, 0x80); 
  this->write_register(IO_CONF2, 0x80); 
  this->write_register(MODE, 0x08); 
  this->write_register(BIT_RATE, 0x00); 
  this->write_register(RX_CONF1, 0x00); 
  this->write_register(RX_CONF2, 0x68); 
  this->write_register(0x09, 0x01); 
  this->write_register(0x0A, 0x10); 
  this->write_register(MASK_MAIN, 0x07); 
  this->write_register(ISO14443A_CONF, 0x00); 

  if (this->rf_field_enabled_) this->field_on_();
  delay(10);
  
  uint8_t d_res = (15 - this->rf_power_) << 4;
  this->write_register(TX_DRIVER_CONF, d_res); 

  return true;
}

void ST25R::reinitialize_() {
  this->reinitialization_attempts_++;
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(true);
    delay(10);
    this->reset_pin_->digital_write(false);
    delay(10);
  }
  if (this->reset_()) {
    this->health_check_failures_ = 0;
    this->reinitialization_attempts_ = 0;
  } else {
    if (this->reinitialization_attempts_ >= 3) this->mark_failed();
  }
}

void ST25R::field_on_() {
  this->write_register(OP_CONTROL, 0x80); 
  delay(10);
  this->write_command(ST25R_CMD_FIELD_ON);
  delay(10);
  this->write_register(OP_CONTROL, 0xC8); 
}

void ST25R::dump_config() {
  ESP_LOGCONFIG(TAG, "ST25R:");
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  ESP_LOGCONFIG(TAG, "  RF Power: %u", this->rf_power_);
  ESP_LOGCONFIG(TAG, "  RF Field Enabled: %s", YESNO(this->rf_field_enabled_));
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
