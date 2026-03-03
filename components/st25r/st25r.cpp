#include "st25r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/version.h"
#include "esphome/components/nfc/nfc_tag.h"
#include "esphome/components/nfc/nfc_helpers.h"

// Compatibility for ESPHome < 2025.x where nfc::NfcTagUid was not defined
#if ESPHOME_VERSION_CODE < VERSION_CODE(2025, 1, 0)
namespace esphome {
namespace nfc {
using NfcTagUid = std::vector<uint8_t>;
}  // namespace nfc
}  // namespace esphome
#endif

namespace esphome {
namespace st25r {

static const char *const TAG = "st25r";

void ST25R::setup() {
  ESP_LOGI(TAG, "Setting up ST25R...");
  
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(10);
  }

  this->irq_pin_->setup();

  if (!this->reset_()) {
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "ST25R initialized successfully.");
}

void ST25R::update() {
  if (this->is_failed() || this->state_ != STATE_IDLE) return;

  // Clear FIFO and registers before a new scan
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);

  if (this->rf_field_enabled_) {
    this->field_on_();
  }

  this->health_check_failures_ = 0;
  if (this->status_binary_sensor_ != nullptr) {
    this->status_binary_sensor_->publish_state(true);
  }

  // Reset per-scan state
  this->tags_this_scan_.clear();
  this->cascade_level_ = 0;
  this->current_uid_ = "";
  this->valid_bits_ = 0;
  this->collision_retries_ = 0;
  memset(this->uid_buffer_, 0, 5);

  this->irq_triggered_ = false;
  this->irq_status_ = 0;
  this->irq_timer_status_ = 0;
  
  struct SearchProfile {
    uint8_t rx_conf2;
    uint8_t rx_conf3;
    const char *desc;
  } profiles[] = {
    {0x5D, 0x02, "Auto-A-3dB-Mix-Bst"}, // sel_ph_auto=1, gain=011
    {0xDD, 0x02, "Auto-B-3dB-Mix-Bst"}, // sel_ph_auto=1, gain=011
    {0x48, 0x02, "Auto-A-0dB-Mix-Bst"}, // sel_ph_auto=1, gain=000 (MAX)
    {0xC8, 0x02, "Auto-B-0dB-Mix-Bst"}, // sel_ph_auto=1, gain=000 (MAX)
    {0x6D, 0x02, "A-3dB-AM-Bst"},       // AM Demodulator
    {0xED, 0x02, "B-3dB-AM-Bst"}        // AM Demodulator
  };

  bool tag_spotted = false;
  const char *winner = "None";

  for (const auto &p : profiles) {
    this->write_register(RX_CONF2, p.rx_conf2);
    this->write_register(RX_CONF3, p.rx_conf3);
    this->write_command(ST25R_CMD_CLEAR_FIFO);
    this->read_register(IRQ_MAIN);
    this->write_command(ST25R_CMD_TRANSMIT_WUPA);
    delay(50); // MAX settle for extreme weak coupling
    
    uint8_t main_irq = this->read_register(IRQ_MAIN);
    if (main_irq & 0x40) { // RXS
      tag_spotted = true;
      winner = p.desc;
      this->irq_status_ = main_irq; // Preserve for loop()
      break;
    }
    // Also try REQA on same profile
    this->write_command(ST25R_CMD_CLEAR_FIFO);
    this->write_command(ST25R_CMD_TRANSMIT_REQA);
    delay(50);
    main_irq = this->read_register(IRQ_MAIN);
    if (main_irq & 0x40) {
      tag_spotted = true;
      winner = p.desc;
      this->irq_status_ = main_irq; // Preserve for loop()
      break;
    }
  }

  // Read back key registers to confirm final sweep state
  uint8_t final_op_ctrl = this->read_register(OP_CONTROL);
  uint8_t final_mode_reg = this->read_register(MODE);
  if (tag_spotted) {
    ESP_LOGI(TAG, "Tag spotted! Profile: %s", winner);
  }
  ESP_LOGD(TAG, "Sent WUPA sweep, OP_CONTROL=0x%02X MODE=0x%02X winner=%s", final_op_ctrl, final_mode_reg, winner);
  this->state_ = STATE_WUPA;
  this->last_state_change_ = millis();
}

bool ST25R::transceive_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  return this->transceive_ex_(data, len, resp, resp_len, true, timeout_ms);
}

bool ST25R::transceive_no_crc_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  return this->transceive_ex_(data, len, resp, resp_len, false, timeout_ms);
}

bool ST25R::transceive_ex_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, bool with_crc, uint32_t timeout_ms) {
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->read_register(IRQ_MAIN);

  this->write_register(NUM_TX_BYTES1, (len >> 8) & 0xFF);
  if (with_crc) {
    this->write_register(NUM_TX_BYTES2, (len & 0x1F) << 3); 
  } else {
    this->write_register(NUM_TX_BYTES2, 0x00); // Whole bytes
  }
  
  this->write_fifo(data, len);
  
  this->irq_triggered_ = false;
  this->write_register(RX_CONF1, 0x08); // Always expect_crc=0, ch_en=1
  if (with_crc) {
    this->write_command(ST25R_CMD_TRANSMIT_WITH_CRC);
  } else {
    this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
  }
  
  uint32_t start = millis();
  resp_len = 0;
  bool tx_done = false;

  while (millis() - start < timeout_ms) {
    if (this->irq_triggered_) {
      this->irq_triggered_ = false;
      this->irq_status_ = this->read_register(IRQ_MAIN);
      uint8_t timer_irq = this->read_register(IRQ_TIMER);
      uint8_t error_irq = this->read_register(IRQ_ERROR);

      if (this->irq_status_ & IRQ_TXE) {
        tx_done = true;
      }
      if (this->irq_status_ & 0x08) { // ERROR bit
        ESP_LOGV(TAG, "  transceive_ex_: IRQ_ERROR=0x%02X", error_irq);
      }
      if (timer_irq & IRQ_TIMER_NRE) {
        break;
      }
    }

    if (tx_done) {
      uint8_t f1 = this->read_register(FIFO_STATUS1);
      if (f1 > 0) {
        uint8_t to_read = std::min((uint8_t)(64 - resp_len), f1);
        this->read_fifo(resp + resp_len, to_read);
        resp_len += to_read;
        start = millis(); // reset timeout while receiving
      }
      
      uint8_t main_irq = this->read_register(IRQ_MAIN);
      if (main_irq & 0x20) break; // RXE
    }
    delay(1);
  }

  return resp_len > 0;
}

std::unique_ptr<nfc::NfcTag> ST25R::read_tag_(std::vector<uint8_t> &uid) {
  uint8_t type = 2; // Default to Type 2
  ESP_LOGD(TAG, "Guessed tag type: %d for UID length: %d", type, (int)uid.size());

  std::vector<uint8_t> data;
  uint8_t read_cmd[2] = {0x30, 0x00}; // READ page 0
  uint8_t buffer[20];
  uint8_t len;

  for (uint8_t p = 0; p < 16; p += 4) {
    delay(10);
    read_cmd[1] = p;
    if (this->transceive_(read_cmd, 2, buffer, len) && len >= 16) {
      data.insert(data.end(), buffer, buffer + 16);
    }
  }

  if (data.size() >= 16) {
    size_t msg_start_idx = 0;
    for (size_t i = 0; i < data.size() - 2; i++) {
      if (data[i] == 0x03) { // NDEF TLV
        msg_start_idx = i + 2;
        uint8_t msg_len = data[i + 1];
        if (data.size() >= (size_t)(msg_start_idx + msg_len)) {
          std::vector<uint8_t> ndef_data(data.begin() + msg_start_idx, data.begin() + msg_start_idx + msg_len);
          nfc::NfcTagUid nfc_uid(uid.begin(), uid.end());
          return make_unique<nfc::NfcTag>(nfc_uid, nfc::NFC_FORUM_TYPE_2, ndef_data);
        }
      }
    }
  }

  nfc::NfcTagUid nfc_uid(uid.begin(), uid.end());
  return make_unique<nfc::NfcTag>(nfc_uid);
}

void ST25R::loop() {
  if (this->is_failed()) return;

  if (this->irq_triggered_) {
    this->irq_triggered_ = false;
    this->irq_status_ |= this->read_register(IRQ_MAIN);
    this->irq_timer_status_ |= this->read_register(IRQ_TIMER);
  }

  uint32_t now = millis();

  switch (this->state_) {
    case STATE_IDLE:
      break;

    case STATE_WUPA: {
      if (now - this->last_state_change_ > 200) {
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }

      if (this->irq_status_ & 0x40) { // RXS
        if (this->irq_status_ & 0x20) { // RXE
          this->cascade_level_ = 0;
          this->current_uid_ = "";
          this->state_ = STATE_ANTICOL;
          this->last_state_change_ = now;
          this->write_command(ST25R_CMD_CLEAR_FIFO);
          uint8_t anticol_pk[] = {0x93, 0x20};
          this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
          this->write_fifo(anticol_pk, 2);
        }
      }
      break;
    }

    case STATE_ANTICOL: {
      if (now - this->last_state_change_ > 500) {
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }

      if (this->irq_status_ & 0x20) { // RXE
        uint8_t f1 = this->read_register(FIFO_STATUS1);
        if (f1 >= 5) {
          this->read_fifo(this->uid_buffer_, 5);
          uint8_t bcc = this->uid_buffer_[0] ^ this->uid_buffer_[1] ^ this->uid_buffer_[2] ^ this->uid_buffer_[3];
          
          uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
          uint8_t sel_pk[] = {sel_cmds[this->cascade_level_], 0x70,
                              this->uid_buffer_[0], this->uid_buffer_[1],
                              this->uid_buffer_[2], this->uid_buffer_[3],
                              this->uid_buffer_[4]};
          
          this->write_register(ISO14443A_CONF, 0x00);
          
          if (sel_pk[2] == 0x88) {
            for (int i = 3; i < 6; i++) {
              char buf[3]; sprintf(buf, "%02X", sel_pk[i]); this->current_uid_ += buf;
            }
          } else {
            for (int i = 2; i < 6; i++) {
              char buf[3]; sprintf(buf, "%02X", sel_pk[i]); this->current_uid_ += buf;
            }
          }

          delay(5);
          uint8_t sak_resp[3] = {};
          uint8_t sak_len = 0;
          
          bool select_ok = this->transceive_(sel_pk, 7, sak_resp, sak_len, 200);
          if (!select_ok || sak_len == 0) {
            this->write_register(RX_CONF2, 0x9D);
            delay(10);
            select_ok = this->transceive_(sel_pk, 7, sak_resp, sak_len, 250);
          }
          
          if (!select_ok || sak_len == 0) {
            this->write_register(RX_CONF2, 0x1D);
            this->write_register(ISO14443A_CONF, 0x01);
            this->finalize_scan_();
            this->state_ = STATE_IDLE;
            return;
          }
          this->write_register(RX_CONF2, 0x1D);
          this->write_register(ISO14443A_CONF, 0x01);
          
          uint8_t sak = sak_resp[0];
          if (sak & 0x04) {
            this->cascade_level_++;
            if (this->cascade_level_ > 2) {
              this->finalize_scan_();
              this->state_ = STATE_IDLE;
              return;
            }
            this->write_command(ST25R_CMD_CLEAR_FIFO);
            uint8_t next_anticol[] = {sel_cmds[this->cascade_level_], 0x20};
            this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
            this->write_fifo(next_anticol, 2);
            this->last_state_change_ = now;
          } else {
            if (this->present_tags_.find(this->current_uid_) == this->present_tags_.end()) {
              ESP_LOGI(TAG, "New tag: %s", this->current_uid_.c_str());
              std::vector<uint8_t> uid_bytes;
              for (size_t i = 0; i < this->current_uid_.length(); i += 2) {
                uid_bytes.push_back((uint8_t) strtol(this->current_uid_.substr(i, 2).c_str(), nullptr, 16));
              }
              auto nfc_tag = this->read_tag_(uid_bytes);
              for (auto *listener : this->tag_listeners_) listener->tag_on(*nfc_tag);
              for (auto *trigger : this->on_tag_triggers_) trigger->trigger(this->current_uid_);
            }
            for (auto *obj : this->binary_sensors_) obj->process(this->current_uid_);
            this->tags_this_scan_.insert(this->current_uid_);
            this->tag_miss_counts_[this->current_uid_] = 0;
            
            this->write_command(ST25R_CMD_CLEAR_FIFO);
            this->write_command(ST25R_CMD_TRANSMIT_REQA);
            this->state_ = STATE_WUPA;
            this->last_state_change_ = now;
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

  this->irq_status_ = 0;
  this->irq_timer_status_ = 0;
}

void ST25R::finalize_scan_() {
  for (auto it = this->present_tags_.begin(); it != this->present_tags_.end(); ) {
    const std::string &uid = *it;
    if (this->tags_this_scan_.find(uid) == this->tags_this_scan_.end()) {
      this->tag_miss_counts_[uid]++;
      if (this->tag_miss_counts_[uid] >= 3) {
        ESP_LOGI(TAG, "Tag removed: %s", uid.c_str());
        std::vector<uint8_t> uid_bytes;
        for (size_t i = 0; i < uid.length(); i += 2) {
          uid_bytes.push_back((uint8_t) strtol(uid.substr(i, 2).c_str(), nullptr, 16));
        }
        nfc::NfcTagUid nfc_uid(uid_bytes.begin(), uid_bytes.end());
        nfc::NfcTag nfc_tag(nfc_uid);
        for (auto *listener : this->tag_listeners_) listener->tag_off(nfc_tag);
        for (auto *trigger : this->on_tag_removed_triggers_) trigger->trigger(uid);
        it = this->present_tags_.erase(it);
        this->tag_miss_counts_.erase(uid);
        continue;
      }
    }
    ++it;
  }
  for (const auto &uid : this->tags_this_scan_) {
    this->present_tags_.insert(uid);
  }
}

bool ST25R::reset_() {
  this->write_command(ST25R_CMD_SET_DEFAULT);
  delay(10);

  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  if ((ic_identity & 0xF8) != 0x28) return false;
  ESP_LOGI(TAG, "IC identity match: 0x%02X", ic_identity);

  this->write_register(OP_CONTROL, 0x80); 
  delay(10);

  this->write_register(IO_CONF1, 0x00);
  uint8_t io_conf2 = (this->supply_3v3_ ? 0x80 : 0x00) | 0x10 | 0x04; 
  this->write_register(IO_CONF2, io_conf2); 
  this->write_register(MODE, 0x08); 
  this->write_register(BIT_RATE, 0x00); 
  this->write_register(0x09, 0x00);
  this->write_register(RX_CONF1, 0x08);
  this->write_register(RX_CONF2, 0x1D);
  this->write_register(RX_CONF3, 0x02);
  this->write_register(RX_CONF4, 0x01);
  this->write_register(0x68, 0x01);
  this->write_register(CORR_CONF1, 0x51);
  this->write_register(CORR_CONF2, 0x00); 
  this->write_register(0x2C, 0x80);
  this->write_register(0x2D, 0x40);
  this->write_register(FIELD_THRESHOLD_ACTV, 0x00);
  this->write_register(FIELD_THRESHOLD_DEACTV, 0x00);
  this->write_register(0x18, 0x00);
  this->write_register(0x19, 0xFF);
  this->write_register(MASK_MAIN, 0x00);
  this->write_register(0x17, 0x00);
  this->write_register(0x05, 0x01);

  uint8_t d_res = (15 - this->rf_power_) & 0x0F;
  this->write_register(TX_DRIVER_CONF, 0x70 | d_res);

  if (this->rf_field_enabled_) this->field_on_();
  delay(50);

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
  this->write_register(OP_CONTROL, 0x88);
  delay(20);
  this->write_command(ST25R_CMD_FIELD_ON);
  delay(20);
  this->write_register(OP_CONTROL, 0xC8);
  delay(10);
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
