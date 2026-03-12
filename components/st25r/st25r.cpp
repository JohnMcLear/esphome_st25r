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
    this->reset_pin_->digital_write(false);
    delay(10);
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
  if (this->state_ != STATE_IDLE) return;

  this->tags_this_scan_.clear();
  
  // Send WUPA blocking
  this->write_command(ST25R_CMD_STOP_ALL);
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->read_register(IRQ_MAIN);
  this->irq_triggered_ = false;
  this->irq_status_ = 0;
  
  this->write_command(ST25R_CMD_TRANSMIT_WUPA);
  delay(30); 
  uint8_t irq = this->read_register(IRQ_MAIN);
  if (irq & (IRQ_RXS | IRQ_RXE | IRQ_COL)) {
    ESP_LOGD(TAG, "Tag detected via WUPA (irq=0x%02X)", irq);
    
    // BRUTE FORCE ANTICOL
    this->current_uid_ = "";
    uint8_t sel_cmds[] = {0x93, 0x95, 0x97};

    for (uint8_t cl = 0; cl < 3; cl++) {
      this->write_command(ST25R_CMD_STOP_ALL);
      this->write_command(ST25R_CMD_CLEAR_FIFO);
      this->read_register(IRQ_MAIN);
      delay(10); 
      
      uint8_t anticol_pk[] = {sel_cmds[cl], 0x20};
      this->write_register(NUM_TX_BYTES1, 0x00);
      this->write_register(NUM_TX_BYTES2, 0x10); 
      this->write_fifo(anticol_pk, 2);
      this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);
      
      delay(50); 
      
      uint8_t f1 = this->read_register(FIFO_STATUS1);
      if (f1 > 0) {
        uint8_t resp[10] = {0};
        uint8_t to_read = std::min((uint8_t)10, f1);
        this->read_fifo(resp, to_read);

        if (resp[0] == 0x88) {
          for (int i = 1; i < 4; i++) {
            char buf[3]; sprintf(buf, "%02X", resp[i]); this->current_uid_ += buf;
          }
        } else {
          for (int i = 0; i < 4; i++) {
            char buf[3]; sprintf(buf, "%02X", resp[i]); this->current_uid_ += buf;
          }
        }

        // Try SELECT
        uint8_t bcc = resp[0] ^ resp[1] ^ resp[2] ^ resp[3];
        uint8_t sel_pk[7] = {sel_cmds[cl], 0x70, resp[0], resp[1], resp[2], resp[3], bcc};
        uint8_t sak_resp[10] = {0};
        uint8_t sak_len = 0;
        if (this->transceive_ex_(sel_pk, 7, sak_resp, sak_len, true, 100, true) && sak_len > 0) {
          uint8_t sak = sak_resp[0];
          if (!(sak & 0x04)) {
            if (!this->present_tags_.count(this->current_uid_)) {
              ESP_LOGI(TAG, "New tag detected: %s", this->current_uid_.c_str());
              for (auto *trigger : this->on_tag_triggers_) trigger->trigger(this->current_uid_);
            }
            for (auto *obj : this->binary_sensors_) obj->process(this->current_uid_);
            this->tags_this_scan_.insert(this->current_uid_);
            this->tag_miss_counts_[this->current_uid_] = 0;
            break; 
          }
        } else break;
      } else break;
    }
  }
  
  this->finalize_scan_();
}

void ST25R::loop() {
  if (this->irq_triggered_) {
    uint8_t dummy = this->read_register(IRQ_MAIN);
    (void)dummy;
    this->irq_triggered_ = false;
  }
}

void ST25R::finalize_scan_() {
  this->irq_status_ = 0;
  for (auto it = this->present_tags_.begin(); it != this->present_tags_.end(); ) {
    const std::string &uid = *it;
    if (this->tags_this_scan_.find(uid) == this->tags_this_scan_.end()) {
      this->tag_miss_counts_[uid]++;
      if (this->tag_miss_counts_[uid] >= 5) {
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
  if ((ic_identity & 0xF8) != 0x28) return false;
  ESP_LOGI(TAG, "IC identity match: 0x%02X", ic_identity);
  this->write_test_register(0x04, 0x10);
  this->write_register(OP_CONTROL, 0x80);
  delay(10);
  
  this->write_register(IO_CONF1, 0x00);
  this->write_register_b(PT_MOD, 0x51); 
  this->write_register_b(AUX_MOD, 0x10); 
  this->write_register(TX_DRIVER_CONF, 0x70);
  
  this->write_register(IO_CONF2, 0x14);
  this->write_register(MODE, 0x08);
  this->write_register(BIT_RATE, 0x00);
  this->write_register(ISO14443A_CONF, 0x01);
  this->write_register(RX_CONF4, 0x00);
  this->write_register(0x18, 0x00);
  this->write_register(0x19, 0xFF);
  this->write_register(MASK_MAIN, 0x00);
  this->write_register(0x17, 0x00);
  
  this->write_register(RX_CONF1, 0x08);
  this->write_register(RX_CONF2, 0x48);
  
  // FINAL OPTIMIZED HIGH SENSITIVITY
  this->write_register(RX_CONF3, 0xE2); 
  this->write_register_b(0x4C, 0x40); // Squelch Level 4
  this->write_register_b(0x4D, 0x40); 
  
  if (this->rf_field_enabled_) {
    this->field_on_();
    this->write_register(IO_CONF2, 0x54);
    this->write_command(ST25R_CMD_ADJUST_REGULATORS);
    delay(10);
  }
  return true;
}

void ST25R::reinitialize_() {
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

bool ST25R::wait_for_irq_(uint8_t mask, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (this->irq_triggered_) {
      uint8_t main = this->read_register(IRQ_MAIN);
      if (main) this->irq_status_ |= main;
      this->irq_triggered_ = false;
      if (main & mask) return true;
    }
    if (this->irq_status_ & mask) return true;
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
  this->irq_status_ = 0;
  this->irq_triggered_ = false;
  this->write_register(NUM_TX_BYTES1, (len >> 8) & 0xFF);
  this->write_register(NUM_TX_BYTES2, (len & 0xFF) << 3);
  this->write_fifo(data, len);
  this->write_command(with_crc ? ST25R_CMD_TRANSMIT_WITH_CRC : ST25R_CMD_TRANSMIT_WITHOUT_CRC);
  if (!this->wait_for_irq_(IRQ_TXE, 50)) return false;
  this->irq_status_ &= ~IRQ_TXE;
  if (!this->wait_for_irq_(IRQ_RXS | IRQ_RXE | IRQ_COL, timeout_ms)) return false;
  uint8_t f1 = this->read_register(FIFO_STATUS1);
  if (f1 > 0) {
    uint8_t to_read = std::min((uint8_t)(64), f1);
    this->read_fifo(resp, to_read);
    resp_len = to_read;
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
