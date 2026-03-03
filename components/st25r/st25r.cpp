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

void ST25R::isr(ST25R *arg) {
  arg->irq_triggered_ = true;
}

void ST25R::setup() {
  ESP_LOGI(TAG, "Setting up ST25R...");
  if (this->reset_pin_ != nullptr) {
    ESP_LOGI(TAG, "Resetting ST25R via pin...");
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
    delay(10);
    this->reset_pin_->digital_write(false); 
    delay(10);
  }
  
  if (this->irq_pin_ != nullptr) {
    ESP_LOGI(TAG, "Configuring IRQ pin...");
    this->irq_pin_->setup();
    this->irq_pin_->attach_interrupt(ST25R::isr, this, gpio::INTERRUPT_RISING_EDGE);
  }

  ESP_LOGI(TAG, "Starting reset_()...");
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
  ESP_LOGI(TAG, "ST25R initialized successfully.");
}

void ST25R::update() {
  if (this->is_failed() || this->state_ != STATE_IDLE) return;

  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  if ((ic_identity & 0xF8) != 0x28) {
    ESP_LOGW(TAG, "IC identity check failed: 0x%02X", ic_identity);
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

  // Stop any ongoing chip operation, clear IRQ registers and FIFO before a new scan
  this->write_command(ST25R_CMD_STOP_ALL);
  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);
  this->write_command(ST25R_CMD_CLEAR_FIFO);

  if (this->rf_field_enabled_) {
    this->write_register(OP_CONTROL, 0xC8); // en=1, rx_en=1, tx_en=1
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
  this->write_command(ST25R_CMD_TRANSMIT_WUPA);
  // Read back key registers immediately after WUPA to confirm chip state
  uint8_t op_ctrl = this->read_register(OP_CONTROL);
  uint8_t mode_reg = this->read_register(MODE);
  ESP_LOGD(TAG, "Sent WUPA, OP_CONTROL=0x%02X MODE=0x%02X", op_ctrl, mode_reg);
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
  this->write_command(ST25R_CMD_STOP_ALL);
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
  if (with_crc) {
    this->write_command(ST25R_CMD_TRANSMIT_WITH_CRC);
    ESP_LOGV(TAG, "  transceive_: Transmitting %d bytes with CRC: %02X %02X", len, data[0], data[1]);
  } else {
    this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
    ESP_LOGV(TAG, "  transceive_: Transmitting %d bytes without CRC: %02X %02X", len, data[0], data[1]);
  }
  
  uint32_t start = millis();
  resp_len = 0;
  bool tx_done = false;

  while (millis() - start < timeout_ms) {
    if (this->irq_triggered_) {
      this->irq_triggered_ = false;
      this->irq_status_ = this->read_register(IRQ_MAIN);
      uint8_t timer_irq = this->read_register(IRQ_TIMER);

      if (this->irq_status_ & IRQ_TXE) {
        tx_done = true;
      }
      if (timer_irq & IRQ_TIMER_NRE) {
        // No-response timer expired — tag did not reply (expected for HALT)
        return false;
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

      if (resp_len >= 1 && (this->irq_status_ & IRQ_RXE)) {
        return true;
      }
    }
    delay(1);
  }
  return resp_len > 0;
}

std::unique_ptr<nfc::NfcTag> ST25R::read_tag_(std::vector<uint8_t> &uid) {
  uint8_t type = nfc::guess_tag_type(uid.size());
  ESP_LOGD(TAG, "Guessed tag type: %d for UID length: %d", type, uid.size());
  
  if (type == nfc::TAG_TYPE_2) {
    std::vector<uint8_t> data;
    uint8_t buffer[16];
    uint8_t len;

    uint8_t read_cmd[2] = {0x30, 0x00}; 
    if (this->transceive_(read_cmd, 2, buffer, len) && len >= 16) {
      ESP_LOGD(TAG, "  Read page 0-3 success");
      data.insert(data.end(), buffer, buffer + 16); // Only keep data, skip CRC
      
      size_t tlv_index = 0;
      bool found = false;
      bool terminator_found = false;

      for (size_t i = 0; i < 16; i++) { 
        if (data[i] == 0x03) {
          tlv_index = i;
          found = true;
          ESP_LOGD(TAG, "  Found NDEF TLV at index %d", i);
          break;
        }
        if (data[i] == 0xFE) {
          terminator_found = true;
          break;
        }
      }

      if (!found && !terminator_found) {
        for (uint8_t p = 4; p < 16; p += 4) {
          delay(10);
          read_cmd[1] = p;
          if (this->transceive_(read_cmd, 2, buffer, len) && len >= 16) {
            data.insert(data.end(), buffer, buffer + 16);
            for (size_t i = data.size() - 16; i < data.size(); i++) {
              if (data[i] == 0x03) {
                tlv_index = i;
                found = true;
                ESP_LOGD(TAG, "  Found NDEF TLV at index %d", i);
                break;
              }
              if (data[i] == 0xFE) {
                terminator_found = true;
                ESP_LOGD(TAG, "  Found Terminator TLV (0xFE) at index %d", i);
                break;
              }
            }
          }
          if (found || terminator_found) break;
        }
      }

      if (found) {
        if (tlv_index + 1 >= data.size()) {
           delay(10);
           read_cmd[1] = data.size() / 4;
           if (this->transceive_(read_cmd, 2, buffer, len) && len >= 16) {
             data.insert(data.end(), buffer, buffer + 16);
           }
        }

        if (tlv_index + 1 < data.size()) {
          uint8_t msg_len = data[tlv_index + 1];
          size_t msg_start_idx = tlv_index + 2;
          ESP_LOGD(TAG, "  NDEF message length: %d", msg_len);
          
          while (data.size() < (size_t)(msg_start_idx + msg_len)) {
            uint8_t next_page = data.size() / 4;
            read_cmd[1] = next_page;
            delay(10);
            if (!this->transceive_(read_cmd, 2, buffer, len) || len < 16) {
              ESP_LOGW(TAG, "  Failed to read page %d during NDEF fetch", next_page);
              break;
            }
            data.insert(data.end(), buffer, buffer + 16);
          }
          
          if (data.size() >= (size_t)(msg_start_idx + msg_len)) {
            std::vector<uint8_t> ndef_data(data.begin() + msg_start_idx, data.begin() + msg_start_idx + msg_len);
            ESP_LOGI(TAG, "  Successfully read NDEF message of %d bytes", msg_len);
            nfc::NfcTagUid nfc_uid(uid.begin(), uid.end());
            return make_unique<nfc::NfcTag>(nfc_uid, nfc::NFC_FORUM_TYPE_2, ndef_data);
          }
        }
      } else {
        ESP_LOGD(TAG, "  No NDEF TLV (0x03) found in searched pages");
      }
    } else {
      ESP_LOGW(TAG, "  Failed to read page 0, len=%d", len);
    }
  }

  nfc::NfcTagUid nfc_uid(uid.begin(), uid.end());
  return make_unique<nfc::NfcTag>(nfc_uid);
}

void ST25R::loop() {
  if (this->is_failed()) return;

  if (this->irq_triggered_) {
    this->irq_triggered_ = false;
    this->irq_status_ = this->read_register(IRQ_MAIN);
    this->irq_timer_status_ = this->read_register(IRQ_TIMER);
    ESP_LOGV(TAG, "IRQ: main=0x%02X timer=0x%02X state=%d", this->irq_status_, this->irq_timer_status_, this->state_);
  } else {
    this->irq_status_ = 0;
    this->irq_timer_status_ = 0;
  }

  switch (this->state_) {
    case STATE_IDLE:
      break;

    case STATE_WUPA: {
      if (millis() - this->last_state_change_ > 200) {
        // Timeout: no response to WUPA/REQA — scan complete
        ESP_LOGD(TAG, "STATE_WUPA timeout, scan complete (%u tags found)", this->tags_this_scan_.size());
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }

      // Use ISR-based status if available, otherwise poll directly.
      // Direct polling is a fallback for missed edges (IRQ already HIGH when WUPA TXE fires).
      uint8_t irq_m = this->irq_status_;
      uint8_t irq_t = this->irq_timer_status_;
      if (irq_m == 0 && irq_t == 0) {
        irq_m = this->read_register(IRQ_MAIN);
        irq_t = this->read_register(IRQ_TIMER);
      }
      if (irq_m != 0 || irq_t != 0) {
        ESP_LOGD(TAG, "  WUPA IRQ: main=0x%02X timer=0x%02X", irq_m, irq_t);
      }

      if (irq_m & (IRQ_RXE | IRQ_COL)) {
        // ATQA received — one or more tags present, start anticollision
        ESP_LOGD(TAG, "ATQA received (IRQ: 0x%02X), ANTICOL CL%u", irq_m, this->cascade_level_ + 1);
        // Reset per-tag anticollision state
        memset(this->uid_buffer_, 0, 5);
        this->valid_bits_ = 0;
        this->collision_retries_ = 0;

        uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
        uint8_t cl[] = {sel_cmds[this->cascade_level_], 0x20};
        this->write_command(ST25R_CMD_CLEAR_FIFO);
        this->read_register(IRQ_MAIN);
        this->read_register(IRQ_TIMER);
        this->irq_triggered_ = false;
        this->write_fifo(cl, 2);
        this->write_register(NUM_TX_BYTES1, 0x00);
        this->write_register(NUM_TX_BYTES2, 0x10);  // 2 full bytes
        this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);

        this->state_ = STATE_ANTICOL;
        this->last_state_change_ = millis();
      } else if (irq_t & IRQ_TIMER_NRE) {
        // No response — no more unhalted tags in the field
        ESP_LOGD(TAG, "NRE in STATE_WUPA, scan complete (%u tags)", this->tags_this_scan_.size());
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
      }
      break;
    }

    case STATE_ANTICOL: {
      if (millis() - this->last_state_change_ > 500) {
        ESP_LOGD(TAG, "ANTICOL timeout");
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }

      if (this->irq_status_ & IRQ_TXE) {
        ESP_LOGD(TAG, "ANTICOL TXE (retry transmitted, waiting for response)");
      }

      if (this->irq_status_ & (IRQ_RXE | IRQ_COL)) {
        uint8_t f1 = this->read_register(FIFO_STATUS1);
        uint8_t f2 = this->read_register(FIFO_STATUS2);
        // fifo_lb: number of bits NOT valid in the last FIFO byte (0 = all 8 valid)
        uint8_t fifo_lb = (f2 >> 1) & 0x07;

        uint8_t col_pos_abs = 0xFF;
        if (this->irq_status_ & IRQ_COL) {
          // COLLISION_DISPLAY (0x20): bits[7:4]=c_byte (full bytes before collision),
          // bits[3:1]=c_bit (bits before collision in that byte), bit[0]=c_pb (parity collision)
          uint8_t col_raw = this->read_register(COLLISION_DISPLAY);
          uint8_t col_byte = (col_raw >> 4) & 0x0F;
          uint8_t col_bit  = (col_raw >> 1) & 0x07;
          col_pos_abs = col_byte * 8 + col_bit;
          ESP_LOGD(TAG, "Collision at byte=%u bit=%u (abs=%u) FIFO_STATUS1=%u FIFO_STATUS2=0x%02X fifo_lb=%u",
                   col_byte, col_bit, col_pos_abs, f1, f2, fifo_lb);
        }

        uint8_t resp[16] = {};
        uint8_t bytes_to_read = std::min((uint8_t) 16, f1);
        if (bytes_to_read > 0) {
          this->read_fifo(resp, bytes_to_read);
        }

        uint8_t partial_bits = this->valid_bits_ % 8;
        uint8_t byte_start = this->valid_bits_ / 8;

        if (partial_bits == 0) {
          // Byte-aligned: simple copy into uid_buffer_
          for (uint8_t i = 0; i < bytes_to_read; i++) {
            if (byte_start + i < 5) {
              this->uid_buffer_[byte_start + i] = resp[i];
            }
          }
        } else {
          // Non-byte-aligned: merge received bits into uid_buffer_ at bit offset partial_bits.
          // The chip stores received bits from bit 0; we shift them to align with partial_bits.
          uint8_t mask = (1 << partial_bits) - 1;
          if (byte_start < 5) {
            this->uid_buffer_[byte_start] = (this->uid_buffer_[byte_start] & mask) | (resp[0] << partial_bits);
          }
          for (uint8_t i = 1; i < bytes_to_read; i++) {
            if (byte_start + i < 5) {
              this->uid_buffer_[byte_start + i] = (resp[i - 1] >> (8 - partial_bits)) | (resp[i] << partial_bits);
            }
          }
          // Carry the top bits of the last resp byte into the next uid_buffer_ slot
          if (byte_start + bytes_to_read < 5 && bytes_to_read > 0) {
            this->uid_buffer_[byte_start + bytes_to_read] = resp[bytes_to_read - 1] >> (8 - partial_bits);
          }
        }

        ESP_LOGD(TAG, "uid_buffer_: %02X %02X %02X %02X %02X (valid_bits=%u f1=%u fifo_lb=%u)",
                 this->uid_buffer_[0], this->uid_buffer_[1], this->uid_buffer_[2],
                 this->uid_buffer_[3], this->uid_buffer_[4], this->valid_bits_, f1, fifo_lb);

        if (this->irq_status_ & IRQ_COL) {
          // Collision: narrow the prefix by forcing the collision bit to 0.
          // COLLISION_DISPLAY bit position is from the START OF THE FRAME, which
          // includes 16 bits of SEL+NVB transmitted by the reader. Subtract 16
          // to convert to a UID-relative bit position.
          if (col_pos_abs >= 16) {
            uint8_t uid_col_pos = col_pos_abs - 16;
            uint8_t col_byte_idx = uid_col_pos / 8;
            uint8_t col_bit_idx  = uid_col_pos % 8;
            ESP_LOGD(TAG, "  UID collision at byte=%u bit=%u (uid_pos=%u)", col_byte_idx, col_bit_idx, uid_col_pos);
            this->uid_buffer_[col_byte_idx] &= ~(1 << col_bit_idx);
            this->valid_bits_ = uid_col_pos + 1;
          }

          uint8_t nvb = ((this->valid_bits_ / 8 + 2) << 4) | (this->valid_bits_ % 8);
          uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
          uint8_t tx_bytes = (this->valid_bits_ + 7) / 8;
          uint8_t tx_buf[7] = {sel_cmds[this->cascade_level_], nvb};
          memcpy(tx_buf + 2, this->uid_buffer_, tx_bytes);

          uint16_t tx_bits = 16 + this->valid_bits_;
          uint8_t ntx = (tx_bits >> 3) & 0x1F;
          uint8_t nbtx = tx_bits & 0x07;
          ESP_LOGD(TAG, "ANTICOL retry NVB=0x%02X valid_bits=%u tx_bits=%u ntx=%u nbtx=%u buf: %02X %02X %02X %02X %02X %02X",
                   nvb, this->valid_bits_, tx_bits, ntx, nbtx,
                   tx_buf[0], tx_buf[1], tx_buf[2],
                   tx_bytes > 1 ? tx_buf[3] : 0,
                   tx_bytes > 2 ? tx_buf[4] : 0,
                   tx_bytes > 3 ? tx_buf[5] : 0);

          this->write_command(ST25R_CMD_CLEAR_FIFO);
          this->read_register(IRQ_MAIN);
          this->read_register(IRQ_TIMER);
          this->irq_triggered_ = false;
          this->write_fifo(tx_buf, 2 + tx_bytes);

          // NUM_TX_BYTES: ntx[4:0] in bits[7:3], nbtx[2:0] in bits[2:0]
          this->write_register(NUM_TX_BYTES1, (tx_bits >> 8) & 0xFF);
          this->write_register(NUM_TX_BYTES2, (ntx << 3) | nbtx);
          this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
          this->last_state_change_ = millis();
          return;
        }

        // No collision — full UID received at this cascade level.
        // Execute SELECT synchronously via transceive_() to avoid IRQ edge-miss.
        uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
        uint8_t bcc = this->uid_buffer_[0] ^ this->uid_buffer_[1] ^ this->uid_buffer_[2] ^ this->uid_buffer_[3];
        uint8_t sel_pk[7] = {sel_cmds[this->cascade_level_], 0x70,
                             this->uid_buffer_[0], this->uid_buffer_[1],
                             this->uid_buffer_[2], this->uid_buffer_[3], bcc};
        ESP_LOGD(TAG, "  BCC=0x%02X selecting CL%u: %02X%02X%02X%02X",
                 bcc, this->cascade_level_ + 1, sel_pk[2], sel_pk[3], sel_pk[4], sel_pk[5]);

        // Accumulate UID bytes — skip cascade tag byte 0x88
        if (sel_pk[2] == 0x88) {
          for (int i = 3; i < 6; i++) {
            char buf[3]; sprintf(buf, "%02X", sel_pk[i]); this->current_uid_ += buf;
          }
        } else {
          for (int i = 2; i < 6; i++) {
            char buf[3]; sprintf(buf, "%02X", sel_pk[i]); this->current_uid_ += buf;
          }
        }

        uint8_t sak_resp[3] = {};
        uint8_t sak_len = 0;
        if (!this->transceive_(sel_pk, 7, sak_resp, sak_len, 50) || sak_len == 0) {
          ESP_LOGD(TAG, "SELECT failed (CL%u)", this->cascade_level_ + 1);
          this->finalize_scan_();
          this->state_ = STATE_IDLE;
          return;
        }
        uint8_t sak = sak_resp[0];
        ESP_LOGD(TAG, "SAK: 0x%02X (cascade=%s)", sak, (sak & 0x04) ? "yes" : "no");

        if (sak & 0x04) {
          // Cascade bit — UID continues at next level; stay in STATE_ANTICOL
          this->cascade_level_++;
          if (this->cascade_level_ > 2) {
            ESP_LOGE(TAG, "Too many cascade levels");
            this->finalize_scan_();
            this->state_ = STATE_IDLE;
            return;
          }
          memset(this->uid_buffer_, 0, 5);
          this->valid_bits_ = 0;
          uint8_t cl[] = {sel_cmds[this->cascade_level_], 0x20};
          ESP_LOGD(TAG, "Cascade → ANTICOL CL%u", this->cascade_level_ + 1);
          this->write_command(ST25R_CMD_CLEAR_FIFO);
          this->read_register(IRQ_MAIN);
          this->read_register(IRQ_TIMER);
          this->irq_triggered_ = false;
          this->write_fifo(cl, 2);
          this->write_register(NUM_TX_BYTES1, 0x00);
          this->write_register(NUM_TX_BYTES2, 0x10);  // 2 full bytes
          this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
          this->last_state_change_ = millis();
          // STATE stays STATE_ANTICOL
        } else {
          // Tag fully selected
          bool already_this_scan = this->tags_this_scan_.count(this->current_uid_) != 0;
          if (!already_this_scan) {
            bool is_new = this->present_tags_.count(this->current_uid_) == 0;
            std::vector<uint8_t> uid_bytes;
            for (size_t i = 0; i < this->current_uid_.length(); i += 2) {
              uid_bytes.push_back((uint8_t) strtol(this->current_uid_.substr(i, 2).c_str(), nullptr, 16));
            }
            if (is_new) {
              ESP_LOGI(TAG, "New tag: %s", this->current_uid_.c_str());
              delay(5);
              auto nfc_tag = this->read_tag_(uid_bytes);
              if (nfc_tag->has_ndef_message()) {
                auto &msg = nfc_tag->get_ndef_message();
                for (auto &rec : msg->get_records()) {
                  ESP_LOGI(TAG, "  NDEF type=%s payload=%s", rec->get_type().c_str(), rec->get_payload().c_str());
                }
              }
              for (auto *listener : this->tag_listeners_) listener->tag_on(*nfc_tag);
              for (auto *trigger : this->on_tag_triggers_) trigger->trigger(this->current_uid_);
            } else {
              ESP_LOGD(TAG, "Known tag still present: %s", this->current_uid_.c_str());
            }
            for (auto *obj : this->binary_sensors_) obj->process(this->current_uid_);
            this->tags_this_scan_.insert(this->current_uid_);
            this->tag_miss_counts_[this->current_uid_] = 0;
          } else {
            ESP_LOGD(TAG, "Tag %s already processed this scan", this->current_uid_.c_str());
          }

          // HALT this tag so it won't respond to REQA (only WUPA wakes halted tags)
          {
            uint8_t halt_cmd[] = {0x50, 0x00};
            uint8_t halt_resp[4];
            uint8_t halt_resp_len = 0;
            this->transceive_(halt_cmd, 2, halt_resp, halt_resp_len, 30);
          }

          // Reset anticollision state for next tag
          this->cascade_level_ = 0;
          this->current_uid_ = "";
          this->valid_bits_ = 0;
          this->collision_retries_ = 0;
          memset(this->uid_buffer_, 0, 5);

          // Send REQA to discover any remaining non-halted tags
          this->write_command(ST25R_CMD_STOP_ALL);
          this->write_command(ST25R_CMD_CLEAR_FIFO);
          this->read_register(IRQ_MAIN);
          this->read_register(IRQ_TIMER);
          this->irq_triggered_ = false;
          this->irq_status_ = 0;
          this->irq_timer_status_ = 0;
          this->write_command(ST25R_CMD_TRANSMIT_REQA);
          ESP_LOGD(TAG, "Sent REQA to find more tags");
          this->state_ = STATE_WUPA;
          this->last_state_change_ = millis();
        }
      } else if (this->irq_timer_status_ & IRQ_TIMER_NRE) {
        ESP_LOGD(TAG, "NRE in ANTICOL — tag vanished");
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
      }
      break;
    }

    case STATE_SELECT: {
      // SELECT is now handled inline in STATE_ANTICOL via transceive_().
      // This state should never be entered; reset if somehow reached.
      this->finalize_scan_();
      this->state_ = STATE_IDLE;
      break;
    }

    case STATE_REINITIALIZING:
      this->reinitialize_();
      this->state_ = STATE_IDLE;
      break;
  }
}

void ST25R::finalize_scan_() {
  // Notify binary sensors that the scan is complete
  for (auto *obj : this->binary_sensors_) obj->on_scan_end();

  // Determine which previously-present tags were not seen this scan
  std::vector<std::string> to_remove;
  for (const auto &uid : this->present_tags_) {
    if (this->tags_this_scan_.count(uid) == 0) {
      auto &misses = this->tag_miss_counts_[uid];
      misses++;
      if (misses >= 3) {
        to_remove.push_back(uid);
      }
    } else {
      this->tag_miss_counts_[uid] = 0;
    }
  }

  // Fire removal events for confirmed-gone tags
  for (const auto &uid : to_remove) {
    ESP_LOGI(TAG, "Tag removed: %s", uid.c_str());
    std::vector<uint8_t> uid_bytes;
    for (size_t i = 0; i < uid.length(); i += 2) {
      uid_bytes.push_back((uint8_t) strtol(uid.substr(i, 2).c_str(), nullptr, 16));
    }
    nfc::NfcTagUid nfc_uid(uid_bytes.begin(), uid_bytes.end());
    nfc::NfcTag nfc_tag(nfc_uid);
    for (auto *listener : this->tag_listeners_) listener->tag_off(nfc_tag);
    for (auto *trigger : this->on_tag_removed_triggers_) trigger->trigger(uid);
    this->present_tags_.erase(uid);
    this->tag_miss_counts_.erase(uid);
  }

  // Add any newly detected tags to the confirmed-present set
  for (const auto &uid : this->tags_this_scan_) {
    this->present_tags_.insert(uid);
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
  if ((ic_identity & 0xF8) != 0x28) return false;
  ESP_LOGI(TAG, "IC identity match: 0x%02X", ic_identity);

  this->write_register(OP_CONTROL, 0x80); // en=1: Ready mode (enable oscillator and regulators)
  delay(10); // Wait for oscillator to stabilize

  this->write_register(IO_CONF1, 0x00);  // single=0: differential antenna driving (full power)
  // Enable AAT (bit 4) and set supply voltage bit
  uint8_t io_conf2 = (this->supply_3v3_ ? 0x80 : 0x00) | 0x10; 
  this->write_register(IO_CONF2, io_conf2); 
  this->write_register(MODE, 0x08); 
  this->write_register(BIT_RATE, 0x00); 
  this->write_register(0x09, 0x00);     // AUX: Enable Correlator (dis_corr=0)
  this->write_register(RX_CONF1, 0x08); // ISO14443A 106kbps optimized Rx
  this->write_register(RX_CONF2, 0x2D); // Mixer demodulator
  this->write_register(RX_CONF3, 0x00); // 0 dB (Full gain), no boost
  this->write_register(RX_CONF4, 0x00); 
  this->write_register(0x2C, 0x80);     // ANT_TUNE_A: Default tuning from sample
  this->write_register(0x2D, 0x40);     // ANT_TUNE_B: Default tuning from sample
  this->write_register(MASK_MAIN, 0x00); // Unmask all main IRQs
  this->write_register(0x17, 0x00);     // Unmask all timer/NFC IRQs (IRQ_TIMER_NRE etc.)
  this->write_register(ISO14443A_CONF, 0x01); // antcl=1: Enable anticollision framing

  // TX_DRIVER_CONF (0x28): bits[7:4]=am_mod (12% = 7), bits[3:0]=d_res (driver resistance)
  // ISO 14443-3 requires minimum 10% AM modulation; default am_mod=7 = 12%.
  uint8_t d_res = (15 - this->rf_power_) & 0x0F;
  this->write_register(TX_DRIVER_CONF, 0x70 | d_res);

  if (this->rf_field_enabled_) this->field_on_();
  delay(10);

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
  this->write_register(OP_CONTROL, 0x88); // en=1, tx_en=1
  delay(10);
  this->write_command(ST25R_CMD_FIELD_ON);
  delay(10);
  this->write_register(OP_CONTROL, 0xC8); // en=1, rx_en=1, tx_en=1
  this->write_command(ST25R_CMD_ADJUST_REGULATORS);
}

bool ST25R::ndef_write(nfc::NdefMessage *message) {
  std::vector<uint8_t> ndef_data = message->encode();
  std::vector<uint8_t> payload;
  
  // Build TLV structure
  payload.push_back(0x03); // NDEF TLV
  if (ndef_data.size() < 255) {
    payload.push_back((uint8_t)ndef_data.size());
  } else {
    payload.push_back(0xFF);
    payload.push_back((uint8_t)((ndef_data.size() >> 8) & 0xFF));
    payload.push_back((uint8_t)(ndef_data.size() & 0xFF));
  }
  payload.insert(payload.end(), ndef_data.begin(), ndef_data.end());
  payload.push_back(0xFE); // Terminator TLV

  // Pad to 4-byte pages
  while (payload.size() % 4 != 0) payload.push_back(0);

  ESP_LOGD(TAG, "Writing NDEF message, total size with TLVs: %d", payload.size());

  for (size_t i = 0; i < payload.size(); i += 4) {
    uint8_t page = 4 + (i / 4);
    uint8_t write_cmd[6] = {0xA2, page, payload[i], payload[i+1], payload[i+2], payload[i+3]};
    uint8_t buffer[16];
    uint8_t len;
    
    delay(10);
    if (!this->transceive_(write_cmd, 6, buffer, len) || (len > 0 && buffer[0] != 0x0A)) {
      ESP_LOGE(TAG, "NDEF write failed at page %d", page);
      return false;
    }
  }
  ESP_LOGI(TAG, "NDEF write successful!");
  return true;
}

bool ST25R::clean_tag() {
  uint8_t buffer[16];
  uint8_t len;
  
  // Format Capability Container (Page 3)
  uint8_t cc_cmd[6] = {0xA2, 0x03, 0xE1, 0x10, 0x6D, 0x00};
  if (!this->transceive_(cc_cmd, 6, buffer, len)) return false;
  
  // Clear first data page with empty NDEF TLV (Page 4)
  uint8_t empty_ndef[6] = {0xA2, 0x04, 0x03, 0x00, 0xFE, 0x00};
  return this->transceive_(empty_ndef, 6, buffer, len);
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
