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
#ifndef ST25R_NFC_TAG_UID_DEFINED
#define ST25R_NFC_TAG_UID_DEFINED
using NfcTagUid = std::vector<uint8_t>;
#endif
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

  ESP_LOGI(TAG, "ST25R initialized successfully.");
}

void ST25R::dump_config() {
  ESP_LOGCONFIG(TAG, "ST25R:");
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  ESP_LOGCONFIG(TAG, "  Update Interval: %.1fs", this->update_interval_ / 1000.0f);
}

void ST25R::update() {
  if (this->is_failed() || this->state_ != STATE_IDLE) return;

  this->write_command(ST25R_CMD_STOP_ALL);
  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);

  if (this->rf_field_enabled_) {
    this->write_register(OP_CONTROL, 0xC8);  // en + rx_en + tx_en
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
    {0x48, 0x02, "Auto-A-Max"},
    {0xC8, 0x02, "Auto-B-Max"},
    {0x68, 0x02, "Auto-AM-Max"}
  };

  uint8_t idx = (this->winner_profile_idx_ != 0xFF) ? this->winner_profile_idx_ : this->current_profile_idx_;
  const auto &p = profiles[idx % 3];

  this->write_register(RX_CONF2, p.rx_conf2);
  this->write_register(RX_CONF3, p.rx_conf3);
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->read_register(IRQ_MAIN);
  this->write_command(ST25R_CMD_TRANSMIT_WUPA);
  delay(30); // Increased to 30ms
  
  uint8_t irq_m = this->read_register(IRQ_MAIN);
  if (irq_m & (IRQ_RXE | IRQ_COL | IRQ_RXS)) {
    this->irq_status_ = irq_m | IRQ_RXS | IRQ_RXE;
    this->winner_profile_idx_ = idx % 3;
    ESP_LOGD(TAG, "Tag spotted using profile: %s", p.desc);
  } else {
    this->winner_profile_idx_ = 0xFF;
    this->current_profile_idx_ = (this->current_profile_idx_ + 1) % 3;
  }

  this->state_ = STATE_WUPA;
  this->last_state_change_ = millis();
}

bool ST25R::transceive_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  return this->transceive_ex_(data, len, resp, resp_len, true, timeout_ms);
}

bool ST25R::transceive_ex_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, bool with_crc, uint32_t timeout_ms, bool reset_all) {
  if (reset_all) {
    this->write_command(ST25R_CMD_STOP_ALL);
  } else {
    this->write_command(ST25R_CMD_CLEAR_FIFO);
    this->read_register(IRQ_MAIN);
    delay(10);
  }
  this->irq_status_ = 0;
  this->write_register(OP_CONTROL, 0xC8);

  uint16_t tx_bits = len * 8;
  this->write_register(NUM_TX_BYTES1, (tx_bits >> 5) & 0xFF);
  this->write_register(NUM_TX_BYTES2, (tx_bits & 0x1F) << 3);

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
    uint8_t polled = this->read_register(IRQ_MAIN);
    if (polled) {
      this->irq_status_ |= polled;
      if (polled & IRQ_TXE) tx_done = true;
    }

    if (tx_done) {
      uint8_t f1 = this->read_register(FIFO_STATUS1);
      if (f1 > 0) {
        uint8_t to_read = std::min((uint8_t)(64 - resp_len), f1);
        this->read_fifo(resp + resp_len, to_read);
        resp_len += to_read;
        start = millis();
      }
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
          return std::make_unique<nfc::NfcTag>(nfc_uid, nfc::NFC_FORUM_TYPE_2, ndef_data);
        }
      }
    }
  }

  nfc::NfcTagUid nfc_uid(uid.begin(), uid.end());
  return std::make_unique<nfc::NfcTag>(nfc_uid);
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
      if (now - this->last_state_change_ > 40) {
        this->winner_profile_idx_ = 0xFF;
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }
      if (!(this->irq_status_ & (IRQ_RXE | IRQ_RXS | IRQ_COL))) {
        uint8_t polled = this->read_register(IRQ_MAIN);
        if (polled) {
          this->irq_status_ |= polled;
          this->irq_timer_status_ |= this->read_register(IRQ_TIMER);
        }
      }
      static const uint8_t ATQA_MASK = IRQ_RXE | IRQ_RXS | IRQ_COL;
      if (this->irq_status_ & ATQA_MASK) {
        this->irq_status_ &= ~ATQA_MASK;
        uint8_t atqa_fifo = this->read_register(FIFO_STATUS1);
        if (atqa_fifo == 0) break;
        this->winner_profile_idx_ = this->current_profile_idx_;

        if (!this->known_uids_.empty()) {
          for (const auto &uid_str : this->known_uids_) {
            bool already_announced = (this->present_tags_.count(uid_str) ||
                                      this->tags_this_scan_.count(uid_str));
            if (!already_announced) {
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
        this->cascade_level_ = 0;
        this->current_uid_ = "";
        this->state_ = STATE_ANTICOL;
        this->last_state_change_ = now;
        this->write_command(ST25R_CMD_CLEAR_FIFO);
        this->read_register(IRQ_MAIN);
        this->write_register(OP_CONTROL, 0xC8);
        delay(10); // Settle delay
        this->write_register(RX_CONF3, 0xE2);
        this->write_register(RX_CONF2, 0x48);
        uint8_t anticol_pk[] = {0x93, 0x20};
        uint16_t tx_bits = 16;
        this->write_register(NUM_TX_BYTES1, (tx_bits >> 5) & 0xFF);
        this->write_register(NUM_TX_BYTES2, (tx_bits & 0x1F) << 3);
        this->irq_triggered_ = false;
        this->irq_status_ = 0;
        this->write_fifo(anticol_pk, 2);
        this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
      }
      break;
    }

    case STATE_ANTICOL: {
      if (now - this->last_state_change_ > 500) {
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }

      if (this->irq_status_ & (IRQ_RXS | IRQ_RXE | IRQ_COL)) {
        uint32_t wait_start = millis();
        uint8_t total_read = 0;
        int repolls = 0;
        
        while (total_read < 4 && repolls < 3) {
          uint8_t f1_val = this->read_register(FIFO_STATUS1);
          if (f1_val > 0) {
            uint8_t to_read = std::min((uint8_t)(5 - total_read), f1_val);
            this->read_fifo(this->uid_buffer_ + total_read, to_read);
            total_read += to_read;
            wait_start = millis();
          } else if (millis() - wait_start > 30) {
            this->write_command(ST25R_CMD_CLEAR_FIFO);
            uint8_t cl[] = {0x93, 0x20}; 
            this->write_fifo(cl, 2);
            this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
            repolls++;
            wait_start = millis();
          }
          delay(1);
        }

        if (total_read >= 4) {
          this->irq_status_ &= ~(IRQ_RXS | IRQ_RXE | IRQ_COL);
          
          if (total_read == 4) {
            this->uid_buffer_[4] = this->uid_buffer_[0] ^ this->uid_buffer_[1] ^
                                   this->uid_buffer_[2] ^ this->uid_buffer_[3];
          }
          
          uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
          uint8_t sel_pk[7] = {sel_cmds[this->cascade_level_], 0x70,
                               this->uid_buffer_[0], this->uid_buffer_[1],
                               this->uid_buffer_[2], this->uid_buffer_[3], this->uid_buffer_[4]};

          if (sel_pk[2] == 0x88) {
            for (int i = 3; i < 6; i++) { char buf[3]; sprintf(buf, "%02X", sel_pk[i]); this->current_uid_ += buf; }
          } else {
            for (int i = 2; i < 6; i++) { char buf[3]; sprintf(buf, "%02X", sel_pk[i]); this->current_uid_ += buf; }
          }

          uint8_t sak_resp[3] = {};
          uint8_t sak_len = 0;
          if (!this->transceive_(sel_pk, 7, sak_resp, sak_len, 50) || sak_len == 0) {
            this->finalize_scan_();
            this->state_ = STATE_IDLE;
            return;
          }
          uint8_t sak = sak_resp[0];

          if (sak & 0x04) {
            this->cascade_level_++;
            this->write_command(ST25R_CMD_STOP_ALL);
            this->write_register(RX_CONF3, 0xE2);
            uint8_t next_anticol[] = {sel_cmds[this->cascade_level_], 0x20};
            uint16_t next_tx_bits = 16;
            this->write_register(NUM_TX_BYTES1, (next_tx_bits >> 5) & 0xFF);
            this->write_register(NUM_TX_BYTES2, (next_tx_bits & 0x1F) << 3);
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
              for (auto *trigger : this->on_tag_triggers_) trigger->trigger(this->current_uid_);
            }
            for (auto *obj : this->binary_sensors_) obj->process(this->current_uid_);
            this->tags_this_scan_.insert(this->current_uid_);
            this->tag_miss_counts_[this->current_uid_] = 0;
            this->finalize_scan_();
            this->state_ = STATE_IDLE;
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

void ST25R::write_register_b(uint8_t reg, uint8_t value) {
  uint8_t io_conf1 = this->read_register(IO_CONF1);
  this->write_register(IO_CONF1, io_conf1 | 0x40);
  this->write_register(reg & 0x3F, value);
  this->write_register(IO_CONF1, io_conf1);
}

bool ST25R::reset_() {
  this->write_command(ST25R_CMD_SET_DEFAULT);
  delay(10);
  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  uint8_t chip_type = ic_identity & 0xF8;
  if (chip_type != 0x28 && chip_type != 0x30) return false;
  this->is_b_version_ = (chip_type == 0x30);
  ESP_LOGI(TAG, "IC identity match: 0x%02X (ST25R3916%s)", ic_identity, this->is_b_version_ ? "B" : "");
  
  this->write_register(OP_CONTROL, 0x80);
  delay(10);
  
  if (this->is_b_version_) {
    this->write_command(ST25R_CMD_RC_CAL);
    delay(10);
  }

  this->write_register(IO_CONF1, 0x00);
  uint8_t io_conf2 = (this->supply_3v3_ ? 0x80 : 0x00) | 0x10 | 0x04;
  this->write_register(IO_CONF2, io_conf2);
  this->write_register(MODE, 0x08);
  this->write_register(BIT_RATE, 0x00);
  this->write_register(ISO14443A_CONF, 0x01);
  this->write_register(RX_CONF1, 0x08);
  this->write_register(RX_CONF4, 0x00);
  this->write_register(0x18, 0x00);
  this->write_register(0x19, 0xFF);
  this->write_register(MASK_MAIN, 0x00);
  this->write_register(0x17, 0x00);
  uint8_t d_res = (15 - this->rf_power_) & 0x0F;
  this->write_register(TX_DRIVER_CONF, 0x70 | d_res);
  this->write_register(RX_CONF2, 0x48);
  this->write_register(RX_CONF3, 0x02);
  
  if (this->rf_field_enabled_) {
    this->field_on_();
    this->write_command(ST25R_CMD_ADJUST_REGULATORS);
    delay(10);
    
    if (this->aat_enabled_) {
      uint8_t best_aat = 0x80;
      uint8_t best_amp = 0;
      for (int v = 0; v <= 255; v += 8) {
        this->write_register(AAT_A, (uint8_t) v);
        this->write_register(AAT_B, (uint8_t) v);
        delay(5);
        this->write_command(ST25R_CMD_MEASURE_AMPLITUDE);
        delay(3);
        uint8_t amp = this->read_register(AD_CONV_RESULT);
        if (amp > best_amp) { best_amp = amp; best_aat = (uint8_t) v; }
      }
      this->write_register(AAT_A, best_aat);
      this->write_register(AAT_B, best_aat);
      ESP_LOGI(TAG, "AAT calibration: best=0x%02X amplitude=%u/255", best_aat, best_amp);
    }
  }
  delay(50);
  return true;
}

void ST25R::reinitialize_() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(true);
    delay(10);
    this->reset_pin_->digital_write(false);
    delay(10);
  }
  this->reset_();
}

void ST25R::field_on_() {
  this->write_register(OP_CONTROL, 0x88);
  delay(20);
  this->write_command(ST25R_CMD_FIELD_ON);
  delay(20);
  this->write_register(OP_CONTROL, 0xC8);
  delay(10);
}

bool ST25RBinarySensor::process(const std::string &uid) {
  std::string target_uid = "";
  for (uint8_t b : this->uid_) { char buf[3]; sprintf(buf, "%02X", b); target_uid += buf; }
  if (uid == target_uid) { this->publish_state(true); this->found_ = true; return true; }
  return false;
}

}  // namespace st25r
}  // namespace esphome
