#include "st25r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/components/nfc/nfc_tag.h"
#include "esphome/components/nfc/nfc_helpers.h"
#include <cinttypes>
#include <algorithm>

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
  if ((ic_identity >> 3) != 0x05) {
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

  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);
  this->write_command(ST25R_CMD_CLEAR_FIFO);

  if (this->rf_field_enabled_) {
    this->write_register(OP_CONTROL, 0xC8); // en=1, rx_en=1, tx_en=1
  }

  this->irq_triggered_ = false;
  this->write_command(ST25R_CMD_TRANSMIT_WUPA);
  ESP_LOGI(TAG, "Sent WUPA");
  delay(1);
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
      ESP_LOGVV(TAG, "  transceive_ex: IRQ status=%02X", this->irq_status_);
      
      if (this->irq_status_ & IRQ_TXE) {
        tx_done = true;
      }

      if (this->irq_status_ & IRQ_NRE) {
        return false; 
      }
    }

    if (tx_done) {
      uint8_t f1 = this->read_register(FIFO_STATUS1);
      if (f1 > 0) {
        uint8_t to_read = std::min((uint8_t)(64 - resp_len), f1);
        this->read_fifo(resp + resp_len, to_read);
        resp_len += to_read;
        start = millis(); // Reset timeout as we are receiving
      }
      
      if (resp_len >= 4 && (this->irq_status_ & IRQ_RXE)) {
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
  
  if (type == nfc::TAG_TYPE_MIFARE_CLASSIC) {
    ESP_LOGI(TAG, "Mifare Classic detected, but authentication is not yet implemented.");
    nfc::NfcTagUid nfc_uid(uid.begin(), uid.end());
    return make_unique<nfc::NfcTag>(nfc_uid, nfc::MIFARE_CLASSIC);
  }

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
            if (msg_len > 0) {
              return make_unique<nfc::NfcTag>(nfc_uid, nfc::NFC_FORUM_TYPE_2, ndef_data);
            } else {
              return make_unique<nfc::NfcTag>(nfc_uid, nfc::NFC_FORUM_TYPE_2);
            }
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
    ESP_LOGV(TAG, "IRQ triggered, status: 0x%02X, state: %d", this->irq_status_, this->state_);
  } else {
    this->irq_status_ = 0;
  }

  this->process_state_();
}

void ST25R::process_state_() {
  switch (this->state_) {
    case STATE_IDLE:
      break;

    case STATE_WUPA: {
      if (this->irq_status_ & (IRQ_RXE | IRQ_COL)) {
          this->cascade_level_ = 0;
          this->current_uid_ = "";
          
          uint8_t cl[] = {0x93, 0x20};
          this->write_command(ST25R_CMD_CLEAR_FIFO);
          this->read_register(IRQ_MAIN); // Clear any pending IRQs
          this->irq_triggered_ = false;
          this->write_fifo(cl, 2);
          this->write_register(NUM_TX_BYTES1, 0x00);
          this->write_register(NUM_TX_BYTES2, 0x10); 
          this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
          
          this->state_ = STATE_ANTICOL;
          this->last_state_change_ = millis();
      } else if (millis() - this->last_state_change_ > 100) {
          this->state_ = STATE_IDLE;
          this->process_tag_removed_(false);
      }
      break;
    }

    case STATE_ANTICOL: {
      if (millis() - this->last_state_change_ > 200) { 
        ESP_LOGD(TAG, "ANTICOL timeout");
        this->state_ = STATE_IDLE;
        this->process_tag_removed_(false);
        return;
      }

      if (this->irq_status_ != 0) {
        if (this->irq_status_ & (IRQ_RXE | IRQ_COL)) {
          delay(10); // Give chip time to populate FIFO
          uint8_t f1 = this->read_register(FIFO_STATUS1);
          if (f1 > 0) {
            uint8_t resp[16];
            uint8_t bytes_to_read = std::min((uint8_t)16, f1);
            this->read_fifo(resp, bytes_to_read);

            ESP_LOGV(TAG, "  ANTICOL raw: %s", format_hex(resp, bytes_to_read).c_str());

            if (bytes_to_read < 5) {
               ESP_LOGD(TAG, "ANTICOL too short: %u", bytes_to_read);
               this->state_ = STATE_IDLE;
               this->process_tag_removed_(false);
               return;
            }

            uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
            uint8_t sel_pk[7] = {sel_cmds[this->cascade_level_], 0x70, resp[0], resp[1], resp[2], resp[3], resp[4]};

            // Construct UID from response
            if (resp[0] == 0x88) { // Cascade Tag
                ESP_LOGV(TAG, "  Cascade Tag detected");
                for(int i=1; i<4; i++) {
                    char buf[3];
                    sprintf(buf, "%02X", resp[i]);
                    this->current_uid_ += buf;
                }
            } else {
                for(int i=0; i<4; i++) {
                    char buf[3];
                    sprintf(buf, "%02X", resp[i]);
                    this->current_uid_ += buf;
                }
            }

            ESP_LOGD(TAG, "Sending SELECT level %u (UID so far: %s, BCC: 0x%02X)", 
                      this->cascade_level_, this->current_uid_.c_str(), resp[4]);

            this->write_command(ST25R_CMD_CLEAR_FIFO);
            this->read_register(IRQ_MAIN); // Clear any pending IRQs
            this->irq_triggered_ = false;
            this->write_fifo(sel_pk, 7);
            this->write_register(NUM_TX_BYTES1, 0x00);
            this->write_register(NUM_TX_BYTES2, 0x38); // 7 bytes
            this->write_command(ST25R_CMD_TRANSMIT_WITH_CRC);

            this->state_ = STATE_SELECT;
            this->last_state_change_ = millis();
          }
        } else if (this->irq_status_ & IRQ_NRE) {
            this->state_ = STATE_IDLE;
            this->process_tag_removed_(false);
        }
      }
      break;
    }

    case STATE_SELECT: {
      if (millis() - this->last_state_change_ > 200) { 
        ESP_LOGD(TAG, "SELECT timeout");
        this->state_ = STATE_IDLE;
        this->process_tag_removed_(false);
        return;
      }

      if (this->irq_status_ != 0) {
        if (this->irq_status_ & (IRQ_RXE | IRQ_COL | IRQ_NRE)) {
          delay(5); // Small delay for FIFO to populate in I2C mode
          uint8_t f1 = this->read_register(FIFO_STATUS1);
          if (f1 > 0) {
            uint8_t buffer[16];
            uint8_t to_read = std::min((uint8_t)16, f1);
            this->read_fifo(buffer, to_read);
            uint8_t sak = buffer[0];
            ESP_LOGD(TAG, "SELECT SAK: 0x%02X (fifo_len=%d)", sak, f1);
            
            this->write_command(ST25R_CMD_CLEAR_FIFO);

            if (sak & 0x04) { // Cascade bit set
              this->cascade_level_++;
              if (this->cascade_level_ > 2) {
                  ESP_LOGE(TAG, "Too many cascade levels");
                  this->state_ = STATE_IDLE;
                  this->process_tag_removed_(false);
                  return;
              }
              uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
              ESP_LOGD(TAG, "Continuing to ANTICOLLISION level %u", this->cascade_level_);
              this->write_command(ST25R_CMD_CLEAR_FIFO);
              this->read_register(IRQ_MAIN); // Clear any pending IRQs
              uint8_t cl[] = {sel_cmds[this->cascade_level_], 0x20};
              this->irq_triggered_ = false;
              this->write_fifo(cl, 2);
              this->write_register(NUM_TX_BYTES1, 0x00);
              this->write_register(NUM_TX_BYTES2, 0x10); 
              this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
              
              this->state_ = STATE_ANTICOL;
              this->last_state_change_ = millis();
            } else {
              // Tag fully selected!
              ESP_LOGI(TAG, "Tag fully selected: %s", this->current_uid_.c_str());
              
              if (!this->tag_present_ || this->tag_present_uid_ != this->current_uid_) {
                this->tag_present_ = true;
                this->tag_present_uid_ = this->current_uid_;

                for (auto *trigger : this->on_tag_triggers_) {
                  trigger->trigger(this->current_uid_);
                }
              }
              for (auto *obj : this->binary_sensors_) obj->process(this->current_uid_);
              
              // Power cycle field to reset tags (reliable way to ensure they respond to next WUPA)
              this->write_command(ST25R_CMD_FIELD_OFF);
              delay(50);
              this->field_on_();
              
              this->state_ = STATE_IDLE;
              this->process_tag_removed_(true);
            }
          }
        } else if (this->irq_status_ & IRQ_NRE) {
            ESP_LOGV(TAG, "SELECT NRE, returning to IDLE");
            this->state_ = STATE_IDLE;
            this->process_tag_removed_(false);
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
    if (this->missed_updates_ >= 10) {
      ESP_LOGI(TAG, "Tag Removed: %s", this->tag_present_uid_.c_str());

      std::vector<uint8_t> uid_bytes;
      for (size_t i = 0; i < this->tag_present_uid_.length(); i += 2) {
        std::string byteString = this->tag_present_uid_.substr(i, 2);
        uint8_t byte = (uint8_t) strtol(byteString.c_str(), nullptr, 16);
        uid_bytes.push_back(byte);
      }
      nfc::NfcTagUid nfc_uid(uid_bytes.begin(), uid_bytes.end());
      nfc::NfcTag nfc_tag(nfc_uid);
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
  ESP_LOGV(TAG, "  reset_: Sending SET_DEFAULT");
  this->write_command(ST25R_CMD_SET_DEFAULT);
  delay(10);

  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  ESP_LOGD(TAG, "  reset_: IC identity read: 0x%02X", ic_identity);
  if ((ic_identity >> 3) != 0x05) {
    ESP_LOGE(TAG, "  reset_: IC identity mismatch! Expected 0x28 (shifted), got 0x%02X", ic_identity >> 3);
    return false;
  }
  ESP_LOGI(TAG, "IC identity match: 0x%02X", ic_identity);

  ESP_LOGV(TAG, "  reset_: Enabling Ready mode");
  this->write_register(OP_CONTROL, 0x80); // en=1: Ready mode (enable oscillator and regulators)
  delay(10); // Wait for oscillator to stabilize

  ESP_LOGV(TAG, "  reset_: Configuring registers");
  this->write_register(IO_CONF1, 0x00);  // single=0: differential antenna driving (full power)
  this->write_register(IO_CONF2, this->supply_3v3_ ? 0x80 : 0x00); 
  this->write_register(MODE, 0x08); 
  this->write_register(BIT_RATE, 0x00); 
  this->write_register(RX_CONF1, 0x00); 
  this->write_register(RX_CONF2, 0x6C); // Enable AGC during complete receive period
  this->write_register(RX_CONF3, 0x00); // 0 dB (Full gain), no boost
  this->write_register(MASK_MAIN, 0x00); // Enable all interrupts
  this->write_register(ISO14443A_CONF, 0x00); 

  uint8_t d_res = (15 - this->rf_power_) & 0x0F; 
  this->write_register(TX_DRIVER_CONF, d_res); 

  if (this->rf_field_enabled_) {
    ESP_LOGV(TAG, "  reset_: Enabling RF field");
    this->field_on_();
  }
  delay(10);

  ESP_LOGV(TAG, "  reset_: Complete");
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

bool ST25R::ndef_write(nfc::NdefMessage *message, bool format) {
  uint8_t buffer[16];
  uint8_t len;

  if (format) {
    ESP_LOGD(TAG, "Formatting tag (NTAG215 CC)...");
    uint8_t cc_cmd[6] = {0xA2, 0x03, 0xE1, 0x10, 0x3E, 0x00};
    bool cc_success = false;
    for (uint8_t i = 0; i < 3; i++) {
      delay(20);
      if (this->transceive_(cc_cmd, 6, buffer, len) && (len > 0 && (buffer[0] & 0x0F) == 0x0A)) {
        cc_success = true;
        break;
      }
    }
    if (!cc_success) {
      ESP_LOGE(TAG, "Failed to write CC page during format");
      return false;
    }
    delay(50);
  }

  if (message == nullptr) {
    // Just formatting/cleaning
    uint8_t empty_ndef[6] = {0xA2, 0x04, 0x03, 0x00, 0xFE, 0x00};
    bool empty_success = false;
    for (uint8_t i = 0; i < 3; i++) {
      delay(20);
      if (this->transceive_(empty_ndef, 6, buffer, len) && (len > 0 && (buffer[0] & 0x0F) == 0x0A)) {
        empty_success = true;
        break;
      }
    }
    return empty_success;
  }

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
    bool success = false;
    
    for (uint8_t retry = 0; retry < 3; retry++) {
      delay(20);
      if (this->transceive_(write_cmd, 6, buffer, len) && (len > 0 && (buffer[0] & 0x0F) == 0x0A)) {
        success = true;
        break;
      }
      ESP_LOGW(TAG, "NDEF write retry %d for page %d (resp_len=%d, byte0=%02X)", retry + 1, page, len, len > 0 ? buffer[0] : 0);
    }

    if (!success) {
      ESP_LOGE(TAG, "NDEF write failed at page %d after retries", page);
      return false;
    }
  }
  ESP_LOGI(TAG, "NDEF write successful!");
  return true;
}

bool ST25R::clean_tag() {
  return this->ndef_write(nullptr, true);
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
