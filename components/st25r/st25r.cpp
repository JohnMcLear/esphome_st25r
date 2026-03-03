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
  
  // RX_CONF2 0x1F: AGC enabled, full-period, reset algorithm (recommended for ISO14443A)
  this->write_register(RX_CONF2, 0x1F);
  // RX_CONF3 0xE0: rg1_am=7 (+5.5 dB boost), lf_en=0 (HF path for 13.56 MHz)
  this->write_register(RX_CONF3, 0xE0);
  
  this->write_command(ST25R_CMD_STOP_ALL);   // Stop + clear FIFO + clear IRQ before WUPA
  this->write_command(ST25R_CMD_TRANSMIT_WUPA);

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

bool ST25R::transceive_ex_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, bool with_crc, uint32_t timeout_ms) {
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->read_register(IRQ_MAIN);

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
      if (timer_irq & IRQ_TIMER_NRE) break;
    }

    if (tx_done) {
      uint8_t f1 = this->read_register(FIFO_STATUS1);
      if (f1 > 0) {
        uint8_t to_read = std::min((uint8_t)(64 - resp_len), f1);
        this->read_fifo(resp + resp_len, to_read);
        resp_len += to_read;
        start = millis();
      }
      uint8_t main_irq = this->read_register(IRQ_MAIN);
      if (main_irq & 0x10) break; // RXE
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
      if (now - this->last_state_change_ > 30) {
        // ISO14443A tFDT max ≈ 9.4 ms; 30 ms is ample. Shrinking from 300 ms keeps scan
        // rate high so we catch momentary coupling windows with perpendicular tags.
        ESP_LOGD(TAG, "WUPA timeout: irq_main=0x%02X irq_timer=0x%02X", this->irq_status_, this->irq_timer_status_);
        this->finalize_scan_();
        this->state_ = STATE_IDLE;
        return;
      }
      if (this->irq_status_ & 0x20) { // RXS
        if (this->irq_status_ & 0x10) { // RXE
          this->irq_status_ &= ~0x30;
          this->cascade_level_ = 0;
          this->current_uid_ = "";
          this->state_ = STATE_ANTICOL;
          this->last_state_change_ = now;
          // Datasheet 4.4.4: Clear FIFO → set NUM_TX_BYTES → write FIFO → transmit command
          this->write_command(ST25R_CMD_STOP_ALL);  // Stop rx, clear FIFO+IRQ
          uint8_t anticol_pk[] = {0x93, 0x20};
          this->write_register(NUM_TX_BYTES1, 0x00);
          this->write_register(NUM_TX_BYTES2, 0x10);  // 2 bytes, 0 partial bits
          this->irq_triggered_ = false;
          this->write_fifo(anticol_pk, 2);            // Load FIFO before transmit
          this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
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
      if (this->irq_status_ & 0x10) {
        this->irq_status_ &= ~0x10;
        uint8_t f1 = this->read_register(FIFO_STATUS1);
        if (f1 >= 5) {
          this->read_fifo(this->uid_buffer_, 5);
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
            this->write_command(ST25R_CMD_STOP_ALL);
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
      if (this->tag_miss_counts_[uid] >= 3) {
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
  // am_mod=5 → 10% modulation (ISO14443 minimum), maximises carrier field strength
  this->write_register(TX_DRIVER_CONF, 0x50 | d_res);
  // Write RX_CONF2/3 LAST so nothing overwrites them.
  // NOTE: CORR_CONF1 (0x4C) and CORR_CONF2 (0x4D) are Space B registers — the SPI
  // driver masks addr & 0x3F, so writing them would corrupt RX_CONF2/RX_CONF3. Do NOT
  // write Space B registers here; let Set_Default keep their factory defaults.
  // RX_CONF2 0x1F: AGC enabled (agc_en=1), full-period (agc_m=1), reset algorithm
  //                (agc_alg=1, recommended for ISO14443A with short SOF)
  this->write_register(RX_CONF2, 0x1F);
  // RX_CONF3 0xE0: rg1_am=7 (+5.5dB AM gain boost), rg1_pm=0 (full PM), lf_en=0 (HF)
  this->write_register(RX_CONF3, 0xE0);
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
