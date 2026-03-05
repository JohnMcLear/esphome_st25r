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
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(10);
  }

  this->irq_pin_->setup();
  this->irq_pin_->attach_interrupt(ST25R::isr, this, gpio::INTERRUPT_RISING_EDGE);

  if (!this->reset_()) {
    this->mark_failed();
    return;
  }

  for (const auto &uid : this->known_uids_) {
    ESP_LOGI(TAG, "Known UID configured: %s (will try direct SELECT on first ATQA)", uid.c_str());
  }

  ESP_LOGI(TAG, "ST25R initialized successfully.");
}

void ST25R::update() {
  if (this->is_failed() || this->state_ != STATE_IDLE) return;

  // Stop any ongoing activity from previous scan (also clears FIFO + IRQ)
  this->write_command(ST25R_CMD_STOP_ALL);
  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);

  if (this->rf_field_enabled_) {
    // Field stays on from reset_(); re-assert OP_CONTROL in case STOP_ALL disturbed it.
    // Do NOT call field_on_() here — its 150 ms of delays waste precious scan time.
    this->write_register(OP_CONTROL, 0xC8);  // en + rx_en + tx_en
    // Measure field amplitude for diagnostics
    this->write_command(ST25R_CMD_MEASURE_AMPLITUDE);
    delay(3);
    uint8_t amp = this->read_register(AD_CONV_RESULT);
    ESP_LOGD(TAG, "Field amplitude: %u", amp);
    if (this->field_strength_sensor_ != nullptr)
      this->field_strength_sensor_->publish_state(amp);
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
  this->current_profile_idx_ = 0;

  // Blocking phase sweep matching RANGE_MILESTONE (df09fb8):
  // Try Phase B first (bit7=1, MAX GAIN), then Phase A (bit7=0).
  // 20 ms per attempt lets the tag charge its capacitor and respond.
  this->write_register(RX_CONF3, 0x02);

  // Attempt 1: Phase B
  this->write_register(RX_CONF2, 0x88);
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->read_register(IRQ_MAIN);
  this->write_register(OP_CONTROL, 0xC8);
  this->write_command(ST25R_CMD_TRANSMIT_WUPA);
  delay(20);
  uint8_t irq_b = this->read_register(IRQ_MAIN);
  ESP_LOGD(TAG, "Phase B WUPA irq=0x%02X", irq_b);

  if (!(irq_b & 0x10)) {  // no RXE → try Phase A
    // Attempt 2: Phase A
    this->write_register(RX_CONF2, 0x08);
    this->write_command(ST25R_CMD_CLEAR_FIFO);
    this->read_register(IRQ_MAIN);
    this->write_register(OP_CONTROL, 0xC8);
    this->write_command(ST25R_CMD_TRANSMIT_WUPA);
    delay(20);
    uint8_t irq_a = this->read_register(IRQ_MAIN);
    ESP_LOGD(TAG, "Phase A WUPA irq=0x%02X", irq_a);
    this->irq_status_ |= irq_a;
    this->current_profile_idx_ = 1;
  } else {
    this->irq_status_ |= irq_b;
    this->winner_profile_idx_ = 0;  // Phase B worked
  }

  uint8_t final_op_ctrl = this->read_register(OP_CONTROL);
  uint8_t final_mode_reg = this->read_register(MODE);
  ESP_LOGD(TAG, "Sent WUPA, OP_CONTROL=0x%02X MODE=0x%02X", final_op_ctrl, final_mode_reg);
  
  this->state_ = STATE_WUPA;
  this->last_state_change_ = millis();
}

bool ST25R::transceive_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  return this->transceive_ex_(data, len, resp, resp_len, true, timeout_ms);
}

bool ST25R::transceive_no_crc_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  return this->transceive_ex_(data, len, resp, resp_len, false, timeout_ms);
}

bool ST25R::transceive_no_stop_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  return this->transceive_ex_(data, len, resp, resp_len, true, timeout_ms, false);
}

bool ST25R::transceive_ex_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, bool with_crc, uint32_t timeout_ms, bool reset_all) {
  if (reset_all) {
    // STOP_ALL resets modem state (clears FIFO + IRQ). Re-assert OP_CONTROL so
    // the RF field stays on.
    this->write_command(ST25R_CMD_STOP_ALL);
  } else {
    // CLEAR_FIFO only: preserves the tag's ISO14443A READY state.
    this->write_command(ST25R_CMD_CLEAR_FIFO);
    this->read_register(IRQ_MAIN);  // clear IRQ pin manually
    // After receiving ATQA the modem is in RX-complete state. Give it 10ms to
    // internally transition back to TX-ready (same delay the anticol path uses).
    delay(10);
  }
  this->irq_status_ = 0;
  this->write_register(OP_CONTROL, 0xC8);

  this->write_register(NUM_TX_BYTES1, (len >> 8) & 0xFF);
  this->write_register(NUM_TX_BYTES2, (len & 0x1F) << 3);

  this->write_fifo(data, len);

  this->irq_triggered_ = false;
  this->write_register(RX_CONF1, 0x08);
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
      this->irq_status_ |= this->read_register(IRQ_MAIN);
      uint8_t timer_irq = this->read_register(IRQ_TIMER);
      if (this->irq_status_ & IRQ_TXE) tx_done = true;
      if ((timer_irq & IRQ_TIMER_NRE) && resp_len == 0) break;  // NRE: abort only if no data received yet
    }
    // Direct fallback: poll IRQ_MAIN every iteration in case ISR was missed.
    // This is essential when called from loop() on ESP32-C6 (ISR may not fire
    // reliably if FreeRTOS task switch or SPI lock briefly delays the ISR).
    {
      uint8_t polled = this->read_register(IRQ_MAIN);
      if (polled) {
        this->irq_status_ |= polled;
        if (polled & IRQ_TXE) tx_done = true;
      }
    }

    if (tx_done) {
      uint8_t f1 = this->read_register(FIFO_STATUS1);
      if (f1 > 0) {
        uint8_t to_read = std::min((uint8_t)(64 - resp_len), f1);
        this->read_fifo(resp + resp_len, to_read);
        resp_len += to_read;
        start = millis();  // reset timeout after receiving data
      }
      // Direct re-read catches RXE/COL that arrives between ISR cycles.
      // Accept COL too: weak perpendicular signals cause a "collision" before
      // completing RXE, but data bytes (e.g. SAK) may already be in the FIFO.
      uint8_t fresh = this->read_register(IRQ_MAIN);
      if (fresh) this->irq_status_ |= fresh;
      if (this->irq_status_ & (IRQ_RXE | IRQ_COL)) break;
    }
    delay(1);
  }

  return resp_len > 0;
}

std::unique_ptr<nfc::NfcTag> ST25R::read_tag_(std::vector<uint8_t> &uid) {
  std::vector<uint8_t> data;
  uint8_t read_cmd[2] = {0x30, 0x00};
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
      if (data[i] == 0x03) {
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
    uint8_t main_irq = this->read_register(IRQ_MAIN);
    uint8_t timer_irq = this->read_register(IRQ_TIMER);
    this->irq_status_ |= main_irq;
    this->irq_timer_status_ |= timer_irq;
  }

  uint32_t now = millis();

  switch (this->state_) {
    case STATE_IDLE: break;

    case STATE_WUPA: {
      // update() already did a blocking Phase B → Phase A sweep and accumulated
      // irq_status_. We just wait for any late-arriving IRQ bits, then act.
      if (now - this->last_state_change_ > 30) {
        ESP_LOGD(TAG, "WUPA timeout: irq_main=0x%02X irq_timer=0x%02X", this->irq_status_, this->irq_timer_status_);
        this->winner_profile_idx_ = 0xFF;
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }
      // Polling fallback: directly read IRQ_MAIN if ISR hasn't fired yet.
      // update()'s blocking reads cleared the hardware register, so also check ISR bits.
      if (!(this->irq_status_ & (IRQ_RXE | IRQ_RXS | IRQ_COL))) {
        uint8_t polled = this->read_register(IRQ_MAIN);
        if (polled) {
          this->irq_status_ |= polled;
          this->irq_timer_status_ |= this->read_register(IRQ_TIMER);
        }
      }
      // Trigger anticollision on RXE (full ATQA received), RXS (partial), or COL
      static const uint8_t ATQA_MASK = IRQ_RXE | IRQ_RXS | IRQ_COL;
      if (this->irq_status_ & ATQA_MASK) {
        this->irq_status_ &= ~ATQA_MASK;

        // Validate: a real ATQA deposits at least 1 byte in FIFO.
        // Pure noise triggers leave FIFO=0 (the AGC fired but no actual subcarrier decoded).
        uint8_t atqa_fifo = this->read_register(FIFO_STATUS1);
        if (atqa_fifo == 0) {
          ESP_LOGD(TAG, "ATQA IRQ but FIFO=0 — noise/false positive, ignoring (irq=0x%02X)", this->irq_status_);
          break;
        }
        this->winner_profile_idx_ = this->current_profile_idx_;

        ESP_LOGD(TAG, "ATQA detected (fifo=%u), known=%u present=%u", atqa_fifo, this->known_uids_.size(), this->present_tags_.size());
        // For known UIDs: ATQA reception alone is sufficient proof of presence.
        // At perpendicular orientation the anticol response collapses to 1 bit of noise —
        // not enough to match a UID. ATQA proves a tag is there; with known_uids_
        // configured we declare it present immediately without completing anticol.
        if (!this->known_uids_.empty()) {
          for (const auto &uid_str : this->known_uids_) {
            bool already_announced = (this->present_tags_.count(uid_str) ||
                                      this->tags_this_scan_.count(uid_str));
            if (!already_announced) {
              ESP_LOGI(TAG, "New tag (ATQA): %s", uid_str.c_str());
              std::vector<uint8_t> uid_bytes;
              for (size_t i = 0; i < uid_str.length(); i += 2)
                uid_bytes.push_back((uint8_t) strtol(uid_str.substr(i, 2).c_str(), nullptr, 16));
              auto nfc_tag = this->read_tag_(uid_bytes);
              for (auto *listener : this->tag_listeners_) listener->tag_on(*nfc_tag);
              for (auto *trigger : this->on_tag_triggers_) trigger->trigger(uid_str);
            }
            for (auto *obj : this->binary_sensors_) obj->process(uid_str);
            this->tags_this_scan_.insert(uid_str);
            this->tag_miss_counts_[uid_str] = 0;
          }
          this->finalize_scan_();
          this->state_ = STATE_IDLE;
          return;
        }
        // No known UIDs configured — run full anticol to discover the tag's UID.
        this->cascade_level_ = 0;
        this->current_uid_ = "";
        this->state_ = STATE_ANTICOL;
        this->last_state_change_ = now;
        this->write_command(ST25R_CMD_CLEAR_FIFO);
        this->read_register(IRQ_MAIN);
        this->write_register(OP_CONTROL, 0xC8);
        delay(10);
        this->write_register(RX_CONF3, 0xE2);
        this->write_register(RX_CONF2, 0x48);
        uint8_t anticol_pk[] = {0x93, 0x20};
        this->write_register(NUM_TX_BYTES1, 0x00);
        this->write_register(NUM_TX_BYTES2, 0x10);  // 2 bytes, 0 partial bits
        this->irq_triggered_ = false;
        this->irq_status_ = 0;
        this->write_fifo(anticol_pk, 2);
        this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
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
      // Polling fallback: keep reading IRQ_MAIN until RXE or COL.
      if (!(this->irq_status_ & (IRQ_RXE | IRQ_COL))) {
        uint8_t polled = this->read_register(IRQ_MAIN);
        if (polled) this->irq_status_ |= polled;
      }
      // Process on RXE (complete reception) or COL (partial — weak perpendicular signal).
      // With low coupling, the ST25R demodulator sees the ring's backscatter as a
      // "collision" before completing RXE. Read whatever landed in the FIFO.
      bool anticol_received = (this->irq_status_ & (IRQ_RXE | IRQ_COL)) != 0;
      if (anticol_received) {
        this->irq_status_ &= ~(IRQ_RXE | IRQ_COL);
        uint8_t f1 = this->read_register(FIFO_STATUS1);
        uint8_t col_disp = this->read_register(COLLISION_DISPLAY);
        ESP_LOGD(TAG, "ANTICOL rx: fifo=%u col_disp=0x%02X irq=0x%02X", f1, col_disp, this->irq_status_);
        // Standard NVB=0x20 anticol: ring sends {CT,B1,B2,B3,BCC} (5 bytes).
        // Accept if at least 4 bytes received; compute BCC from available bytes if needed.
        if (f1 >= 4) {
          uint8_t read_len = (f1 >= 5) ? 5 : 4;
          this->read_fifo(this->uid_buffer_, read_len);
          bool bcc_ok = true;
          if (read_len == 4) {
            this->uid_buffer_[4] = this->uid_buffer_[0] ^ this->uid_buffer_[1] ^
                                   this->uid_buffer_[2] ^ this->uid_buffer_[3];
          } else {
            uint8_t expected = this->uid_buffer_[0] ^ this->uid_buffer_[1] ^
                               this->uid_buffer_[2] ^ this->uid_buffer_[3];
            if (this->uid_buffer_[4] != expected) {
              ESP_LOGD(TAG, "ANTICOL BCC error: got 0x%02X expected 0x%02X, discarding",
                       this->uid_buffer_[4], expected);
              bcc_ok = false;
            }
          }
          if (!bcc_ok) {
            this->finalize_scan_();
            this->state_ = STATE_IDLE;
            return;
          }
          uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
          uint8_t sel_pk[] = {sel_cmds[this->cascade_level_], 0x70, this->uid_buffer_[0], this->uid_buffer_[1], this->uid_buffer_[2], this->uid_buffer_[3], this->uid_buffer_[4]};
          this->write_register(ISO14443A_CONF, 0x00);
          if (sel_pk[2] == 0x88) {
            for (int i = 3; i < 6; i++) { char buf[3]; sprintf(buf, "%02X", sel_pk[i]); this->current_uid_ += buf; }
          } else {
            for (int i = 2; i < 6; i++) { char buf[3]; sprintf(buf, "%02X", sel_pk[i]); this->current_uid_ += buf; }
          }
          delay(5);
          uint8_t sak_resp[3] = {};
          uint8_t sak_len = 0;
          ESP_LOGD(TAG, "SELECT CL%u: %02X %02X %02X %02X %02X %02X %02X", this->cascade_level_+1,
                   sel_pk[0], sel_pk[1], sel_pk[2], sel_pk[3], sel_pk[4], sel_pk[5], sel_pk[6]);
          bool select_ok = this->transceive_(sel_pk, 7, sak_resp, sak_len, 200);
          ESP_LOGD(TAG, "SELECT result: ok=%d sak_len=%u sak=0x%02X", select_ok, sak_len, sak_resp[0]);
          if (!select_ok || sak_len == 0) {
            this->write_register(RX_CONF2, 0x9D);
            delay(10);
            select_ok = this->transceive_(sel_pk, 7, sak_resp, sak_len, 250);
            ESP_LOGD(TAG, "SELECT retry: ok=%d sak_len=%u sak=0x%02X", select_ok, sak_len, sak_resp[0]);
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
            this->write_command(ST25R_CMD_STOP_ALL);
            this->write_register(RX_CONF3, 0xE2);  // keep gain boost for CL2 anticol
            uint8_t next_anticol[] = {sel_cmds[this->cascade_level_], 0x20};
            this->write_register(NUM_TX_BYTES1, 0x00);
            this->write_register(NUM_TX_BYTES2, 0x10);  // 2 bytes, 0 partial bits
            this->irq_triggered_ = false;
            this->write_fifo(next_anticol, 2);
            this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
            this->last_state_change_ = now;
          } else {
            if (this->present_tags_.find(this->current_uid_) == this->present_tags_.end()) {
              ESP_LOGI(TAG, "New tag: %s", this->current_uid_.c_str());
              std::vector<uint8_t> uid_bytes;
              for (size_t i = 0; i < this->current_uid_.length(); i += 2) { uid_bytes.push_back((uint8_t) strtol(this->current_uid_.substr(i, 2).c_str(), nullptr, 16)); }
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
        } else if (f1 > 0 && !this->known_uids_.empty()) {
          // Partial anticol response (f1 < 4 bytes): ring is perpendicular, signal too
          // weak for a full anticol frame. Match partial bytes against known UID prefixes.
          uint8_t partial[4] = {};
          this->read_fifo(partial, f1);
          ESP_LOGD(TAG, "ANTICOL partial (%u bytes): %02X %02X %02X", f1,
                   f1 > 0 ? partial[0] : 0, f1 > 1 ? partial[1] : 0, f1 > 2 ? partial[2] : 0);
          for (const auto &uid_str : this->known_uids_) {
            std::vector<uint8_t> uid;
            for (size_t i = 0; i + 1 < uid_str.size(); i += 2)
              uid.push_back((uint8_t) strtol(uid_str.substr(i, 2).c_str(), nullptr, 16));
            bool ok = false;
            if (uid.size() == 7) {
              // CL1 anticol sends {CT=0x88, uid[0], uid[1], uid[2], BCC}
              if (partial[0] != 0x88) continue;
              if (f1 >= 2 && partial[1] != uid[0]) continue;
              if (f1 >= 3 && partial[2] != uid[1]) continue;
              ok = true;
            } else if (uid.size() == 4) {
              // CL1 anticol sends {uid[0], uid[1], uid[2], uid[3], BCC}
              if (partial[0] != uid[0]) continue;
              if (f1 >= 2 && partial[1] != uid[1]) continue;
              if (f1 >= 3 && partial[2] != uid[2]) continue;
              ok = true;
            }
            if (!ok) continue;
            ESP_LOGD(TAG, "Partial anticol match: %s", uid_str.c_str());
            if (this->present_tags_.find(uid_str) == this->present_tags_.end()) {
              ESP_LOGI(TAG, "New tag (partial anticol): %s", uid_str.c_str());
              std::vector<uint8_t> uid_bytes;
              for (size_t i = 0; i < uid_str.length(); i += 2)
                uid_bytes.push_back((uint8_t) strtol(uid_str.substr(i, 2).c_str(), nullptr, 16));
              auto nfc_tag = this->read_tag_(uid_bytes);
              for (auto *listener : this->tag_listeners_) listener->tag_on(*nfc_tag);
              for (auto *trigger : this->on_tag_triggers_) trigger->trigger(uid_str);
            }
            for (auto *obj : this->binary_sensors_) obj->process(uid_str);
            this->tags_this_scan_.insert(uid_str);
            this->tag_miss_counts_[uid_str] = 0;
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
      if (this->tag_miss_counts_[uid] >= 30) {
        ESP_LOGI(TAG, "Tag removed: %s", uid.c_str());
        std::vector<uint8_t> uid_bytes;
        for (size_t i = 0; i < uid.length(); i += 2) { uid_bytes.push_back((uint8_t) strtol(uid.substr(i, 2).c_str(), nullptr, 16)); }
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
  for (const auto &uid : this->tags_this_scan_) { this->present_tags_.insert(uid); }
}

bool ST25R::reset_() {
  this->write_command(ST25R_CMD_SET_DEFAULT);
  delay(10);
  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  if ((ic_identity & 0xF8) != 0x28) return false;
  ESP_LOGI(TAG, "IC identity match: 0x%02X", ic_identity);
  this->write_register(OP_CONTROL, 0x80);  // Enable oscillator
  delay(10);
  this->write_register(IO_CONF1, 0x00);    // Differential antenna (full TX power)
  uint8_t io_conf2 = (this->supply_3v3_ ? 0x80 : 0x00) | 0x10 | 0x04;
  this->write_register(IO_CONF2, io_conf2);
  this->write_register(MODE, 0x08);         // ISO14443A initiator mode
  this->write_register(BIT_RATE, 0x00);     // fc/128 = 106 kbps
  this->write_register(ISO14443A_CONF, 0x01);  // antcl=1: anticollision enabled
  this->write_register(RX_CONF1, 0x08);
  this->write_register(RX_CONF4, 0x00);
  this->write_register(0x18, 0x00);         // MASK_ERROR: unmask all
  this->write_register(0x19, 0xFF);         // MASK_PT: mask passive target IRQs (initiator)
  this->write_register(MASK_MAIN, 0x00);    // Unmask all main IRQs
  this->write_register(0x17, 0x00);         // MASK_TIMER: unmask all timer IRQs
  uint8_t d_res = (15 - this->rf_power_) & 0x0F;
  // am_mod=7 → 12% AM modulation (ISO14443 compliant, good carrier power)
  this->write_register(TX_DRIVER_CONF, 0x70 | d_res);
  // Write RX_CONF2/3 here as initial defaults; update() overrides per scan profile.
  // NOTE: CORR_CONF1 (0x4C) and CORR_CONF2 (0x4D) are Space B registers — the SPI
  // driver masks addr & 0x3F, so writing them would corrupt RX_CONF2/RX_CONF3. Do NOT
  // write Space B registers here; let Set_Default keep their factory defaults.
  this->write_register(RX_CONF2, 0x48);  // Ph-A-Auto initial state
  this->write_register(RX_CONF3, 0x02);  // Standard RX path
  if (this->rf_field_enabled_) {
    this->field_on_();
    // Calibrate internal regulators for max TX output once field is on
    this->write_command(ST25R_CMD_ADJUST_REGULATORS);
    delay(10);
    // Sweep AAT-A/B (antenna tuning DAC, Table 74/75) to find resonance peak.
    // Default is 0x80; writing 0x00 detunes the antenna. Find the value that
    // maximises the carrier amplitude (D/A drives external tuning capacitors).
    uint8_t best_aat = 0x80;
    uint8_t best_amp = 0;
    for (int v = 0; v <= 255; v += 8) {
      this->write_register(AAT_A, (uint8_t) v);
      this->write_register(AAT_B, (uint8_t) v);
      delay(5);
      this->write_command(ST25R_CMD_MEASURE_AMPLITUDE);
      delay(3);
      uint8_t amp = this->read_register(AD_CONV_RESULT);
      ESP_LOGV(TAG, "  AAT=0x%02X amp=%u", v, amp);
      if (amp > best_amp) { best_amp = amp; best_aat = (uint8_t) v; }
    }
    this->write_register(AAT_A, best_aat);
    this->write_register(AAT_B, best_aat);
    ESP_LOGI(TAG, "AAT calibration: best=0x%02X amplitude=%u/255", best_aat, best_amp);
  }
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
  for (uint8_t b : this->uid_) { char buf[3]; sprintf(buf, "%02X", b); target_uid += buf; }
  if (uid == target_uid) { this->publish_state(true); this->found_ = true; return true; }
  return false;
}

}  // namespace st25r
}  // namespace esphome
