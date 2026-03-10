#include "st25r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/version.h"
#include "esphome/components/nfc/nfc_tag.h"
#include "esphome/components/nfc/nfc_helpers.h"
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
  }

  if (this->irq_pin_ != nullptr) {
    this->irq_pin_->setup();
    this->irq_pin_->attach_interrupt(ST25R::isr, this, gpio::INTERRUPT_RISING_EDGE);
  }

  if (!this->reset_()) {
    this->mark_failed();
    return;
  }
}

void ST25R::dump_config() {
  ESP_LOGCONFIG(TAG, "ST25R:");
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  ESP_LOGCONFIG(TAG, "  Update Interval: %.1fs", this->update_interval_ / 1000.0f);
}

void ST25R::update() {
  if (this->state_ == STATE_IDLE) {
    this->tags_this_scan_.clear();
    this->current_uid_ = "";
    this->cascade_level_ = 0;
    this->valid_bits_ = 0;
    this->collision_retries_ = 0;
    
    this->write_command(ST25R_CMD_FIELD_ON);
    delay(5);
    
    // Send WUPA
    this->write_command(ST25R_CMD_CLEAR_FIFO);
    this->read_register(IRQ_MAIN); // Clear IRQs
    this->irq_triggered_ = false;
    this->irq_status_ = 0;
    
    this->write_command(ST25R_CMD_TRANSMIT_WUPA);
    this->state_ = STATE_WUPA;
    this->last_state_change_ = millis();
  }
}

void ST25R::loop() {
  uint32_t now = millis();

  switch (this->state_) {
    case STATE_IDLE:
      break;

    case STATE_WUPA: {
      if (now - this->last_state_change_ > 50) {
        // No tag responded to WUPA
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }

      if (this->irq_status_ & (IRQ_RXS | IRQ_RXE)) {
        // Tag found!
        ESP_LOGD(TAG, "Tag detected via WUPA");
        this->irq_status_ = 0;
        this->cascade_level_ = 0;
        this->valid_bits_ = 0;
        this->current_uid_ = "";
        
        // Start Anticol CL1
        uint8_t anticol_pk[] = {0x93, 0x20};
        this->write_command(ST25R_CMD_CLEAR_FIFO);
        this->write_register(NUM_TX_BYTES1, 0x00);
        this->write_register(NUM_TX_BYTES2, 0x10);  // 2 bytes, 0 partial bits
        this->irq_triggered_ = false;
        this->irq_status_ = 0;
        this->write_fifo(anticol_pk, 2);
        this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
        this->state_ = STATE_ANTICOL;
        this->last_state_change_ = now;
      }
      break;
    }

    case STATE_ANTICOL: {
      if (now - this->last_state_change_ > 500) {
        ESP_LOGD(TAG, "ANTICOL timeout: irq_main=0x%02X", this->irq_status_);
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }

      if (this->irq_status_ & (IRQ_RXS | IRQ_RXE | IRQ_COL)) {
        uint32_t wait_start = millis();
        uint8_t total_read = 0;
        int repolls = 0;
        
        while (millis() - wait_start < 100 && total_read < 4 && repolls < 3) {
          uint8_t f1 = this->read_register(FIFO_STATUS1);
          if (f1 > 0) {
            uint8_t to_read = std::min((uint8_t)(5 - total_read), f1);
            this->read_fifo(this->uid_buffer_ + total_read, to_read);
            total_read += to_read;
            wait_start = millis(); // Reset wait on data
          } else if (millis() - wait_start > 30) {
            // Re-poll on weak RXS hit
            this->write_command(ST25R_CMD_CLEAR_FIFO);
            uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
            uint8_t cl[] = {sel_cmds[this->cascade_level_], 0x20};
            this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
            this->write_fifo(cl, 2);
            repolls++;
            wait_start = millis();
          }
          delay(1);
        }

        if (total_read >= 4) {
          uint8_t current = this->irq_status_;
          bool was_col = (current & IRQ_COL) != 0;
          this->irq_status_ = current & ~(IRQ_RXS | IRQ_RXE | IRQ_COL);
          
          if (total_read == 4) {
            this->uid_buffer_[4] = this->uid_buffer_[0] ^ this->uid_buffer_[1] ^
                                   this->uid_buffer_[2] ^ this->uid_buffer_[3];
          }
          
          if (was_col) {
            uint8_t col_raw = this->read_register(COLLISION_DISPLAY);
            uint8_t col_byte = (col_raw >> 4) & 0x0F;
            uint8_t col_bit  = (col_raw >> 1) & 0x07;
            uint8_t col_pos_abs = col_byte * 8 + col_bit;
            
            ESP_LOGD(TAG, "Collision at bit %u", col_pos_abs);
            // Just retry with current bits for now
            this->valid_bits_ = col_pos_abs;
            uint8_t nvb = ((this->valid_bits_ / 8 + 2) << 4) | (this->valid_bits_ % 8);
            uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
            uint8_t tx_bytes = (this->valid_bits_ + 7) / 8;
            uint8_t tx_buf[7] = {sel_cmds[this->cascade_level_], nvb};
            memcpy(tx_buf + 2, this->uid_buffer_, tx_bytes);

            uint16_t tx_bits = 16 + this->valid_bits_;
            uint8_t ntx = (tx_bits >> 3) & 0x1F;
            uint8_t nbtx = tx_bits & 0x07;

            this->write_command(ST25R_CMD_CLEAR_FIFO);
            this->read_register(IRQ_MAIN);
            this->read_register(IRQ_TIMER);
            this->irq_triggered_ = false;
            this->write_fifo(tx_buf, 2 + tx_bytes);

            this->write_register(NUM_TX_BYTES1, (tx_bits >> 8) & 0xFF);
            this->write_register(NUM_TX_BYTES2, (ntx << 3) | nbtx);
            this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
            this->last_state_change_ = millis();
            return;
          }

          // No collision — full UID received at this cascade level.
          uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
          uint8_t bcc = this->uid_buffer_[0] ^ this->uid_buffer_[1] ^ this->uid_buffer_[2] ^ this->uid_buffer_[3];
          uint8_t sel_pk[7] = {sel_cmds[this->cascade_level_], 0x70,
                               this->uid_buffer_[0], this->uid_buffer_[1],
                               this->uid_buffer_[2], this->uid_buffer_[3], bcc};
          
          // Accumulate UID bytes — skip cascade tag byte 0x88
          if (this->uid_buffer_[0] == 0x88) {
            for (int i = 1; i < 4; i++) {
              char buf[3]; sprintf(buf, "%02X", this->uid_buffer_[i]); this->current_uid_ += buf;
            }
          } else {
            for (int i = 0; i < 4; i++) {
              char buf[3]; sprintf(buf, "%02X", this->uid_buffer_[i]); this->current_uid_ += buf;
            }
          }

          uint8_t sak_resp[3] = {};
          uint8_t sak_len = 0;
          if (!this->transceive_(sel_pk, 7, sak_resp, sak_len, 100) || sak_len == 0) {
            ESP_LOGD(TAG, "SELECT failed (CL%u)", this->cascade_level_ + 1);
            this->finalize_scan_();
            this->state_ = STATE_IDLE;
            return;
          }
          uint8_t sak = sak_resp[0];
          ESP_LOGD(TAG, "SAK: 0x%02X (cascade=%s)", sak, (sak & 0x04) ? "yes" : "no");

          if (sak & 0x04) {
            this->cascade_level_++;
            if (this->cascade_level_ > 2) {
              ESP_LOGE(TAG, "Too many cascade levels");
              this->finalize_scan_();
              this->state_ = STATE_IDLE;
              return;
            }
            // Next cascade level
            uint8_t next_anticol[] = {sel_cmds[this->cascade_level_], 0x20};
            this->write_command(ST25R_CMD_CLEAR_FIFO);
            this->write_register(NUM_TX_BYTES1, 0x00);
            this->write_register(NUM_TX_BYTES2, 0x10);
            this->irq_triggered_ = false;
            this->irq_status_ = 0;
            this->write_fifo(next_anticol, 2);
            this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
            this->last_state_change_ = now;
          } else {
            // Tag selected!
            if (this->present_tags_.find(this->current_uid_) == this->present_tags_.end()) {
              ESP_LOGI(TAG, "New tag: %s", this->current_uid_.c_str());
              std::vector<uint8_t> uid_bytes;
              for (size_t i = 0; i < this->current_uid_.length(); i += 2) { 
                uid_bytes.push_back((uint8_t) strtol(this->current_uid_.substr(i, 2).c_str(), nullptr, 16)); 
              }
              auto nfc_tag = this->read_tag_(uid_bytes);
              // For now just triggers
              for (auto *trigger : this->on_tag_triggers_) trigger->trigger(this->current_uid_);
            }
            for (auto *obj : this->binary_sensors_) obj->process(this->current_uid_);
            this->tags_this_scan_.insert(this->current_uid_);
            this->tag_miss_counts_[this->current_uid_] = 0;
            
            this->finalize_scan_();
            this->state_ = STATE_IDLE;
          }
        } else if (total_read > 0 && !this->known_uids_.empty()) {
          // Partial anticol response match against known UIDs
          for (const auto &uid_str : this->known_uids_) {
            std::vector<uint8_t> uid;
            for (size_t i = 0; i + 1 < uid_str.size(); i += 2)
              uid.push_back((uint8_t) strtol(uid_str.substr(i, 2).c_str(), nullptr, 16));
            bool ok = false;
            if (uid.size() == 7) {
              if (this->uid_buffer_[0] != 0x88) continue;
              if (total_read >= 2 && this->uid_buffer_[1] != uid[0]) continue;
              if (total_read >= 3 && this->uid_buffer_[2] != uid[1]) continue;
              ok = true;
            } else if (uid.size() == 4) {
              if (this->uid_buffer_[0] != uid[0]) continue;
              if (total_read >= 2 && this->uid_buffer_[1] != uid[1]) continue;
              if (total_read >= 3 && this->uid_buffer_[2] != uid[2]) continue;
              ok = true;
            }
            if (!ok) continue;
            ESP_LOGD(TAG, "Partial anticol match: %s", uid_str.c_str());
            this->tags_this_scan_.insert(uid_str);
            this->tag_miss_counts_[uid_str] = 0;
            for (auto *obj : this->binary_sensors_) obj->process(uid_str);
            this->finalize_scan_();
            this->state_ = STATE_IDLE;
            return;
          }
        }
      }
      break;
    }

    case STATE_REINITIALIZING: this->reinitialize_(); this->state_ = STATE_IDLE; break;
  }
}

void ST25R::finalize_scan_() {
  this->irq_status_ = 0;
  this->irq_timer_status_ = 0;
  for (auto it = this->present_tags_.begin(); it != this->present_tags_.end(); ) {
    const std::string &uid = *it;
    if (this->tags_this_scan_.find(uid) == this->tags_this_scan_.end()) {
      this->tag_miss_counts_[uid]++;
      if (this->tag_miss_counts_[uid] >= 10) {
        ESP_LOGI(TAG, "Tag removed: %s", uid.c_str());
        for (auto *trigger : this->on_tag_removed_triggers_) trigger->trigger(uid);
        it = this->present_tags_.erase(it);
        this->tag_miss_counts_.erase(uid);
        continue;
      }
    }
    ++it;
  }
  for (const auto &uid : this->tags_this_scan_) { this->present_tags_.insert(uid); }
}

bool ST25R::reset_() {
  this->write_command(ST25R_CMD_SET_DEFAULT);
  delay(10);
  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  if ((ic_identity & 0xF8) != 0x28) return false;
  ESP_LOGI(TAG, "IC identity match: 0x%02X", ic_identity);

  // Apply overheat protection fix for ST25R3916 (non-B)
  this->write_test_register(0x04, 0x10);

  this->write_register(OP_CONTROL, 0x80);  // Enable oscillator
  delay(10);
  
  this->write_register(IO_CONF1, 0x00);
  this->write_register(IO_CONF2, 0x00);
  
  // Set Gain
  this->write_register(RX_CONF1, 0x08);
  this->write_register(RX_CONF2, 0x1D);
  this->write_register(RX_CONF3, 0x00);
  this->write_register(RX_CONF4, 0x00);
  
  return true;
}

void ST25R::reinitialize_() {
  ESP_LOGW(TAG, "Reinitializing chip...");
  this->reset_();
}

bool ST25R::wait_for_irq_(uint8_t mask, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (this->irq_triggered_) {
      uint8_t main = this->read_register(IRQ_MAIN);
      this->irq_status_ |= main;
      this->irq_triggered_ = false;
      if (main & mask) return true;
    }
    delay(1);
  }
  return false;
}

bool ST25R::transceive_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  return this->transceive_ex_(data, len, resp, resp_len, true, timeout_ms, true);
}

bool ST25R::transceive_ex_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, bool with_crc, uint32_t timeout_ms, bool reset_all) {
  this->write_command(ST25R_CMD_STOP_ALL);
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->irq_status_ = 0;
  this->irq_triggered_ = false;
  
  this->write_register(NUM_TX_BYTES1, (len >> 8) & 0xFF);
  this->write_register(NUM_TX_BYTES2, (len & 0xFF) << 3);
  
  this->write_fifo(data, len);
  this->write_command(with_crc ? ST25R_CMD_TRANSMIT_WITH_CRC : ST25R_CMD_TRANSMIT_WITHOUT_CRC);
  
  if (!this->wait_for_irq_(IRQ_TXE, 50)) return false;
  this->irq_status_ &= ~IRQ_TXE;
  
  if (!this->wait_for_irq_(IRQ_RXS | IRQ_RXE, timeout_ms)) return false;
  
  uint8_t f1 = this->read_register(FIFO_STATUS1);
  if (f1 > 0) {
    this->read_fifo(resp, f1);
    resp_len = f1;
    return true;
  }
  return false;
}

std::unique_ptr<nfc::NfcTag> ST25R::read_tag_(std::vector<uint8_t> &uid) {
  return std::make_unique<nfc::NfcTag>(uid);
}

void ST25R::isr(ST25R *arg) {
  arg->irq_triggered_ = true;
}

bool ST25RBinarySensor::process(const std::string &uid) {
  std::string target_uid = "";
  for (uint8_t b : this->uid_) { char buf[3]; sprintf(buf, "%02X", b); target_uid += buf; }
  if (uid == target_uid) { this->publish_state(true); this->found_ = true; return true; }
  return false;
}

}  // namespace st25r
}  // namespace esphome
