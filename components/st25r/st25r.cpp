#include "st25r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/nfc/nfc_tag.h"
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

  // Enable interrupts: RX start, RX end, TX end, Error, Collision
  this->write_register(MASK_MAIN, ~(0x80 | 0x40 | 0x20 | 0x10 | 0x08)); 

  // ISO14443A settings: Enable automatic CRC
  this->write_register(ISO14443A_CONF, 0x00); 

  if (this->rf_field_enabled_) {
    this->field_on_();
  }
  delay(10);
  
  // Apply RF Power setting to TX Driver register
  // Mapping: 15 (Max) -> 0x00 (Min Resistance), 0 (Min) -> 0xF0 (Max Resistance/High Z)
  uint8_t d_res = (15 - this->rf_power_) << 4;
  this->write_register(TX_DRIVER_CONF, d_res); 

  return true;
}

void ST25R::reinitialize_() {
  this->reinitialization_attempts_++;
  ESP_LOGW(TAG, "Reinitializing ST25R (Attempt %u)...", this->reinitialization_attempts_);
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(true);
    delay(10);
    this->reset_pin_->digital_write(false);
    delay(10);
  }
  if (this->reset_()) {
    this->health_check_failures_ = 0;
    this->reinitialization_attempts_ = 0;
    ESP_LOGI(TAG, "ST25R reinitialized successfully.");
  } else {
    ESP_LOGE(TAG, "ST25R reinitialization failed.");
    if (this->reinitialization_attempts_ >= 3) {
      ESP_LOGE(TAG, "Too many reinitialization failures, marking component as FAILED.");
      this->mark_failed();
    }
  }
}

void ST25R::update() {
  if (this->is_failed() || this->state_ != STATE_IDLE) return;

  // Health Check: Verify chip communication
  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  if ((ic_identity >> 3) != 0x05) {
    this->health_check_failures_++;
    ESP_LOGW(TAG, "Health check failed (%u/3). IC Identity: 0x%02X", this->health_check_failures_, ic_identity);
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

  // Field Strength Measurement
  if (this->rf_field_enabled_ && this->field_strength_sensor_ != nullptr) {
    this->write_command(ST25R_CMD_MEASURE_AMPLITUDE);
    // Result will be read in the next cycle or after a short delay
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

  this->write_command(ST25R_CMD_TRANSMIT_WUPA);
  this->state_ = STATE_WUPA;
  this->last_state_change_ = millis();
}

void ST25R::loop() {
  if (this->is_failed()) return;

  switch (this->state_) {
    case STATE_IDLE:
      this->process_tag_removed_();
      break;

    case STATE_WUPA: {
      if (millis() - this->last_state_change_ > 50) { // Timeout
        ESP_LOGV(TAG, "WUPA Timeout");
        this->state_ = STATE_IDLE;
        return;
      }

      bool irq = false;
      if (this->irq_pin_ != nullptr) {
        irq = this->irq_pin_->digital_read();
      } else {
        irq = (this->read_register(IRQ_MAIN) & 0xF8) != 0;
      }

      if (irq) {
        uint8_t main_irq = this->read_register(IRQ_MAIN);
        ESP_LOGV(TAG, "WUPA IRQ received: 0x%02X", main_irq);
        uint8_t f1 = this->read_register(FIFO_STATUS1);
        if (f1 > 0) {
          ESP_LOGV(TAG, "Tag detected via WUPA, switching to READ_UID");
          this->cascade_level_ = 0;
          this->current_uid_ = "";
          this->state_ = STATE_READ_UID;
          this->last_state_change_ = millis();
          // Start first cascade
          this->write_command(ST25R_CMD_CLEAR_FIFO);
          uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
          uint8_t cl[] = {sel_cmds[0], 0x20};
          this->write_fifo(cl, 2);
          this->write_register(NUM_TX_BYTES1, 0x00);
          this->write_register(NUM_TX_BYTES2, 0x10); 
          this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
        } else {
          ESP_LOGV(TAG, "WUPA IRQ but empty FIFO");
          this->state_ = STATE_IDLE;
          this->process_tag_removed_();
        }
      }
      break;
    }

    case STATE_READ_UID: {
      if (millis() - this->last_state_change_ > 100) { // Timeout
        ESP_LOGV(TAG, "READ_UID Timeout at cascade %u", this->cascade_level_);
        this->state_ = STATE_IDLE;
        return;
      }

      bool irq = false;
      if (this->irq_pin_ != nullptr) {
        irq = this->irq_pin_->digital_read();
      } else {
        irq = (this->read_register(IRQ_MAIN) & 0xF8) != 0;
      }

      if (irq) {
        uint8_t main_irq = this->read_register(IRQ_MAIN);
        ESP_LOGV(TAG, "READ_UID IRQ received: 0x%02X at cascade %u", main_irq, this->cascade_level_);
        
        if ((main_irq & 0x08) || main_irq == 0x28) { // Collision or Partial RX
          uint8_t col_reg = 0;
          if (main_irq & 0x08) {
            col_reg = this->read_register(COLLISION_DISPLAY);
            ESP_LOGW(TAG, "Collision detected at bit %u of cascade %u. Resolving...", col_reg >> 4, this->cascade_level_);
          } else {
            ESP_LOGW(TAG, "Partial response (0x28) at cascade %u. Treating as collision.", this->cascade_level_);
          }
          // Resolve: retry cascade
          this->write_command(ST25R_CMD_CLEAR_FIFO);
          uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
          uint8_t cl[] = {sel_cmds[this->cascade_level_], 0x20};
          this->write_fifo(cl, 2);
          this->write_register(NUM_TX_BYTES1, 0x00);
          this->write_register(NUM_TX_BYTES2, 0x10); 
          this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
          this->last_state_change_ = millis();
          return;
        }

        uint8_t f1 = this->read_register(FIFO_STATUS1);
        if (f1 < 5) {
          this->state_ = STATE_IDLE;
          this->process_tag_removed_();
          return;
        }

        uint8_t resp[16];
        size_t len = (f1 > 16) ? 16 : f1;
        this->read_fifo(resp, len);

        if (resp[0] == 0x88) {
          for (int i = 1; i < 4; i++) {
            char buf[3];
            sprintf(buf, "%02X", resp[i]);
            this->current_uid_ += buf;
          }
          // SELECT this level
          this->write_command(ST25R_CMD_CLEAR_FIFO);
          uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
          uint8_t sel_pk[7] = {sel_cmds[this->cascade_level_], 0x70, resp[0], resp[1], resp[2], resp[3], resp[4]};
          this->write_fifo(sel_pk, 7);
          this->write_register(NUM_TX_BYTES1, 0x00);
          this->write_register(NUM_TX_BYTES2, 0x38); 
          this->write_command(ST25R_CMD_TRANSMIT_WITH_CRC);
          
          this->cascade_level_++;
          if (this->cascade_level_ >= 3) { // Should not happen for standard cards
            this->state_ = STATE_IDLE;
            return;
          }
          
          // Wait for SELECT IRQ then start next level
          delay(5); // Small delay for SELECT to finish - still better than full block
          this->write_command(ST25R_CMD_CLEAR_FIFO);
          uint8_t next_cl[] = {sel_cmds[this->cascade_level_], 0x20};
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
          
          // Tag found!
          this->missed_updates_ = 0;
          if (!this->tag_present_ || this->tag_present_uid_ != this->current_uid_) {
            ESP_LOGI(TAG, "New Tag Detected: %s", this->current_uid_.c_str());
            this->tag_present_ = true;
            this->tag_present_uid_ = this->current_uid_;

            std::vector<uint8_t> uid_bytes;
            for (size_t i = 0; i < this->current_uid_.length(); i += 2) {
              std::string byteString = this->current_uid_.substr(i, 2);
              uint8_t byte = (uint8_t) strtol(byteString.c_str(), nullptr, 16);
              uid_bytes.push_back(byte);
            }
            nfc::NfcTag nfc_tag(uid_bytes);
            for (auto *listener : this->tag_listeners_) {
              listener->tag_on(nfc_tag);
            }

            for (auto *trigger : this->on_tag_triggers_) {
              trigger->trigger(this->current_uid_);
            }
          }
          for (auto *obj : this->binary_sensors_) obj->process(this->current_uid_);
          this->state_ = STATE_IDLE;
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

void ST25R::process_tag_removed_() {
  for (auto *obj : this->binary_sensors_) obj->on_scan_end();

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
      nfc::NfcTag nfc_tag(uid_bytes);
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
    if (this->irq_pin_ != nullptr) {
      if (this->irq_pin_->digital_read()) return true;
    } else {
      uint8_t irq = this->read_register(IRQ_MAIN);
      if (irq > 0) {
        ESP_LOGV(TAG, "IRQ Seen: 0x%02X (mask: 0x%02X)", irq, mask);
        if (irq & mask) return true;
      }
    }
    delay(1);
  }
  return false;
}

void ST25R::field_on_() {
  ESP_LOGD(TAG, "Turning field ON...");
  this->write_register(OP_CONTROL, 0x80); 
  delay(10);
  this->write_command(ST25R_CMD_FIELD_ON);
  delay(10);
  this->write_register(OP_CONTROL, 0xC8); // en=1, rf_en=1, rx_en=1, tx_en=1
  uint8_t op = this->read_register(OP_CONTROL);
  ESP_LOGD(TAG, "Field ON status - OP_CONTROL: 0x%02X", op);
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
