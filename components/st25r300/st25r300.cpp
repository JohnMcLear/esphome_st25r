#include "st25r300.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/components/nfc/nfc_tag.h"
#include "esphome/components/nfc/nfc_helpers.h"
#include <algorithm>
#include <cstring>

namespace esphome {
namespace st25r300 {

static const char *const TAG = "st25r300";

void ST25R300::isr(ST25R300 *arg) {
  arg->irq_triggered_ = true;
}

void ST25R300::setup() {
  ESP_LOGI(TAG, "Setting up ST25R300...");
  // ST25R300 has a RESET pin (active high = in reset).
  // Drive it low first (deassert), then assert+deassert to cycle power.
  if (this->reset_pin_ != nullptr) {
    ESP_LOGI(TAG, "Cycling reset pin...");
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);   // assert reset
    delay(10);
    this->reset_pin_->digital_write(false);  // release reset
    delay(10);
  }

  if (this->irq_pin_ != nullptr) {
    ESP_LOGI(TAG, "Configuring IRQ pin...");
    this->irq_pin_->setup();
    this->irq_pin_->attach_interrupt(ST25R300::isr, this, gpio::INTERRUPT_RISING_EDGE);
  }

  if (this->status_binary_sensor_ != nullptr) {
    this->status_binary_sensor_->publish_initial_state(false);
  }

  ESP_LOGI(TAG, "Starting reset_()...");
  if (!this->reset_()) {
    ESP_LOGE(TAG, "Failed to reset/init ST25R300");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "ST25R300 initialized successfully.");
}

void ST25R300::update() {
  if (this->is_failed() || this->state_ != STATE_IDLE) return;

  // Health check: verify IC identity on every scan cycle
  uint8_t ic_identity = this->read_register(ST25R300_REG_IC_IDENTITY);
  if ((ic_identity & ST25R300_IC_TYPE_MASK) != ST25R300_IC_TYPE_VAL) {
    ESP_LOGW(TAG, "IC identity check failed: 0x%02X", ic_identity);
    this->health_check_failures_++;
    if (this->status_binary_sensor_ != nullptr)
      this->status_binary_sensor_->publish_state(false);
    if (this->health_check_failures_ >= 3)
      this->state_ = STATE_REINITIALIZING;
    return;
  }

  this->health_check_failures_ = 0;
  if (this->status_binary_sensor_ != nullptr)
    this->status_binary_sensor_->publish_state(true);

  // Clear all IRQ registers (read-to-clear)
  this->read_register(ST25R300_REG_IRQ_STATUS1);
  this->read_register(ST25R300_REG_IRQ_STATUS2);
  this->read_register(ST25R300_REG_IRQ_STATUS3);
  this->write_command(ST25R300_CMD_CLEAR_FIFO);
  this->write_command(ST25R300_CMD_CLEAR_RX_GAIN);

  if (this->rf_field_enabled_) {
    // Ensure TX and RX are enabled
    this->write_register(ST25R300_REG_OPERATION,
                         ST25R300_OP_ALL_ON);  // en + vdddr_en + rx_en + tx_en
  }

  // Field strength reading: sense_adc register is continuously updated by the chip
  if (this->rf_field_enabled_ && this->field_strength_sensor_ != nullptr) {
    uint8_t sense = this->read_register(ST25R300_REG_SENSE_RF);
    this->field_strength_sensor_->publish_state(sense);
    ESP_LOGV(TAG, "Sense RF: %u", sense);
  }

  this->saved_anticol_valid_ = false;
  this->anticol_resume_ = false;

  // Send WUPA (7-bit short frame) via FIFO + CMD_TRANSMIT_DATA
  // ST25R300 has no dedicated WUPA/REQA command — must be done manually
  this->irq_status1_ = 0;
  this->irq_status2_ = 0;
  this->irq_triggered_ = false;
  uint8_t resp[2];
  uint8_t resp_len = 0;
  this->send_short_frame_(0x52, resp, resp_len, 10);  // 0x52 = WUPA
  ESP_LOGI(TAG, "Sent WUPA");
  delay(1);
  this->state_ = STATE_WUPA;
  this->last_state_change_ = millis();
}

// ── send_short_frame_ ─────────────────────────────────────────────────────────
// Send a 7-bit ISO14443A short frame (REQA=0x26 or WUPA=0x52) without CRC/parity.
// Does NOT wait for response; caller reads response via IRQ/state machine.
bool ST25R300::send_short_frame_(uint8_t byte7, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  // Temporarily disable CRC and parity for short frame
  this->write_register(ST25R300_REG_TX_PROTOCOL1, 0x00);  // no CRC, no parity
  // Set Rx protocol: b_rx_sof+b_rx_eof MUST be set for Manchester framing; no CRC for ATQA
  this->write_register(ST25R300_REG_RX_PROTOCOL1,
                       ST25R300_RX_PROT1_B_RX_SOF | ST25R300_RX_PROT1_B_RX_EOF |
                       ST25R300_RX_PROT1_A_RX_PAR);  // 0x38

  // Use CLEAR_FIFO (not STOP_ALL) — STOP_ALL disables the RX decoder (rx_on→0),
  // preventing ATQA reception. CLEAR_FIFO only resets FIFO without killing the RX path.
  this->write_command(ST25R300_CMD_CLEAR_FIFO);
  this->read_register(ST25R300_REG_IRQ_STATUS1);  // read-to-clear pending IRQs
  this->read_register(ST25R300_REG_IRQ_STATUS2);
  this->read_register(ST25R300_REG_IRQ_STATUS3);
  this->write_command(ST25R300_CMD_CLEAR_RX_GAIN);  // init AGC/squelch before every TX
  this->irq_triggered_ = false;

  // TX frame: 0 full bytes + 7 partial bits — MUST be written before FIFO data
  this->write_register(ST25R300_REG_TX_FRAME1, 0x00);
  this->write_register(ST25R300_REG_TX_FRAME2, 0x07);  // nbtx=7

  // Write the 7-bit byte to FIFO
  uint8_t data = byte7 & 0x7F;
  this->write_fifo(&data, 1);

  this->write_command(ST25R300_CMD_TRANSMIT_DATA);

  // Wait past ATQA window (~400µs from TX start) before reading diagnostics.
  // ATQA arrives at ~86µs after TX-end and takes ~170µs, so safe to read at 700µs.
  delayMicroseconds(700);
  uint8_t stat2_early = this->read_register(ST25R300_REG_STATUS2);
  uint8_t irq1_early  = this->read_register(ST25R300_REG_IRQ_STATUS1);
  uint8_t irq2_early  = this->read_register(ST25R300_REG_IRQ_STATUS2);
  uint8_t fifo_early  = this->read_register(ST25R300_REG_FIFO_STATUS1);
  ESP_LOGD(TAG, "ssf @700us: STAT2=0x%02X FIFO=%u IRQ1=0x%02X IRQ2=0x%02X", stat2_early, fifo_early, irq1_early, irq2_early);
  this->irq_status1_ |= irq1_early;
  this->irq_status2_ |= irq2_early;

  // Now wait out the NRT (5ms total from TX) before reading final status
  delay(10);
  uint8_t post1 = this->read_register(ST25R300_REG_IRQ_STATUS1);
  uint8_t post2 = this->read_register(ST25R300_REG_IRQ_STATUS2);
  ESP_LOGD(TAG, "ssf post10ms: IRQ1=0x%02X IRQ2=0x%02X", post1, post2);
  this->irq_status1_ |= post1;
  this->irq_status2_ |= post2;
  resp_len = 0;
  return true;
}

// ── transceive_ ───────────────────────────────────────────────────────────────
bool ST25R300::transceive_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  return this->transceive_ex_(data, len, resp, resp_len, true, timeout_ms);
}

bool ST25R300::transceive_no_crc_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, uint32_t timeout_ms) {
  return this->transceive_ex_(data, len, resp, resp_len, false, timeout_ms);
}

bool ST25R300::transceive_ex_(const uint8_t *data, size_t len, uint8_t *resp, uint8_t &resp_len, bool with_crc, uint32_t timeout_ms) {
  this->write_command(ST25R300_CMD_CLEAR_FIFO);
  this->write_command(ST25R300_CMD_CLEAR_RX_GAIN);
  this->read_register(ST25R300_REG_IRQ_STATUS1);
  this->read_register(ST25R300_REG_IRQ_STATUS2);
  this->read_register(ST25R300_REG_IRQ_STATUS3);

  // TX_FRAME1/2 encode the number of bytes/bits to transmit
  this->write_register(ST25R300_REG_TX_FRAME1, (uint8_t)((len >> 5) & 0xFF));
  this->write_register(ST25R300_REG_TX_FRAME2, (uint8_t)((len & 0x1F) << 3));  // full bytes, 0 partial bits

  // Set CRC and parity mode in Tx protocol register
  if (with_crc) {
    this->write_register(ST25R300_REG_TX_PROTOCOL1,
                         ST25R300_TX_PROT1_A_TX_PAR | ST25R300_TX_PROT1_TX_CRC);  // 0x60
    this->write_register(ST25R300_REG_RX_PROTOCOL1,
                         ST25R300_RX_PROT1_B_RX_SOF | ST25R300_RX_PROT1_B_RX_EOF |
                         ST25R300_RX_PROT1_A_RX_PAR | ST25R300_RX_PROT1_RX_CRC);  // 0x3C
  } else {
    this->write_register(ST25R300_REG_TX_PROTOCOL1, 0x00);
    this->write_register(ST25R300_REG_RX_PROTOCOL1,
                         ST25R300_RX_PROT1_B_RX_SOF | ST25R300_RX_PROT1_B_RX_EOF |
                         ST25R300_RX_PROT1_A_RX_PAR);  // 0x38, no CRC
  }

  this->write_fifo(data, len);

  this->irq_triggered_ = false;
  this->write_command(ST25R300_CMD_TRANSMIT_DATA);

  uint32_t start = millis();
  resp_len = 0;
  bool tx_done = false;

  while (millis() - start < timeout_ms) {
    uint8_t irq1;
    if (this->irq_triggered_) {
      this->irq_triggered_ = false;
      irq1 = this->read_register(ST25R300_REG_IRQ_STATUS1);
    } else {
      irq1 = this->read_register(ST25R300_REG_IRQ_STATUS1);
    }

    if (irq1 & ST25R300_IRQ1_TXE) tx_done = true;

    if (tx_done) {
      uint8_t f1 = this->read_register(ST25R300_REG_FIFO_STATUS1);
      if (f1 > 0) {
        uint8_t to_read = std::min((uint8_t)(64 - resp_len), f1);
        this->read_fifo(resp + resp_len, to_read);
        resp_len += to_read;
        start = millis();
      }
      if (irq1 & ST25R300_IRQ1_RXE) {
        return resp_len > 0;
      }
    }
    delay(1);
  }
  return resp_len > 0;
}

// ── read_tag_ ─────────────────────────────────────────────────────────────────
std::unique_ptr<nfc::NfcTag> ST25R300::read_tag_(std::vector<uint8_t> &uid) {
  uint8_t type = nfc::guess_tag_type(uid.size());
  ESP_LOGI(TAG, "read_tag_: UID length=%zu, guessed type=%d", uid.size(), type);

  if (type == nfc::TAG_TYPE_2) {
    uint8_t buffer[16];
    uint8_t len;
    std::vector<uint8_t> data;

    uint8_t read_cmd[2] = {0x30, 0x00};
    if (this->transceive_(read_cmd, 2, buffer, len) && len >= 16) {
      data.insert(data.end(), buffer, buffer + 16);

      size_t tlv_index = 0;
      bool found = false;
      bool terminator_found = false;

      for (size_t i = 0; i < 16; i++) {
        if (data[i] == 0x03) { tlv_index = i; found = true; break; }
        if (data[i] == 0xFE) { terminator_found = true; break; }
      }

      if (!found && !terminator_found) {
        for (uint8_t p = 4; p < 16; p += 4) {
          delay(10);
          read_cmd[1] = p;
          if (this->transceive_(read_cmd, 2, buffer, len) && len >= 16) {
            data.insert(data.end(), buffer, buffer + 16);
            for (size_t i = data.size() - 16; i < data.size(); i++) {
              if (data[i] == 0x03) { tlv_index = i; found = true; break; }
              if (data[i] == 0xFE) { terminator_found = true; break; }
            }
          }
          if (found || terminator_found) break;
        }
      }

      if (found) {
        // Ensure we have enough bytes to read the full TLV length field
        while (data.size() <= tlv_index + 3) {
          read_cmd[1] = (uint8_t)(data.size() / 4);
          delay(10);
          if (!this->transceive_(read_cmd, 2, buffer, len) || len < 16) break;
          data.insert(data.end(), buffer, buffer + 16);
        }
        if (tlv_index + 1 < data.size()) {
          size_t msg_len;
          size_t msg_start;
          if (data[tlv_index + 1] == 0xFF && tlv_index + 3 < data.size()) {
            msg_len = ((size_t) data[tlv_index + 2] << 8) | data[tlv_index + 3];
            msg_start = tlv_index + 4;
          } else {
            msg_len = data[tlv_index + 1];
            msg_start = tlv_index + 2;
          }
          while (data.size() < msg_start + msg_len) {
            read_cmd[1] = (uint8_t)(data.size() / 4);
            delay(10);
            if (!this->transceive_(read_cmd, 2, buffer, len) || len < 16) break;
            data.insert(data.end(), buffer, buffer + 16);
          }
          if (data.size() >= msg_start + msg_len) {
            std::vector<uint8_t> ndef_data(data.begin() + (int) msg_start,
                                           data.begin() + (int) msg_start + (int) msg_len);
            nfc::NfcTagUid nfc_uid(uid.begin(), uid.end());
            if (msg_len > 0) {
              return make_unique<nfc::NfcTag>(nfc_uid, nfc::NFC_FORUM_TYPE_2, ndef_data);
            }
            return make_unique<nfc::NfcTag>(nfc_uid, nfc::NFC_FORUM_TYPE_2);
          }
        }
      }
    } else {
      ESP_LOGW(TAG, "Failed to read page 0, len=%d", len);
    }
  }

  nfc::NfcTagUid nfc_uid(uid.begin(), uid.end());
  return make_unique<nfc::NfcTag>(nfc_uid);
}

// ── loop ──────────────────────────────────────────────────────────────────────
void ST25R300::loop() {
  if (this->is_failed()) return;

  if (this->irq_triggered_) {
    this->irq_triggered_ = false;
    uint8_t irq1 = this->read_register(ST25R300_REG_IRQ_STATUS1);
    uint8_t irq2 = this->read_register(ST25R300_REG_IRQ_STATUS2);
    this->irq_status1_ |= irq1;
    this->irq_status2_ |= irq2;
    ESP_LOGV(TAG, "IRQ triggered, IRQ1=0x%02X IRQ2=0x%02X state=%d", irq1, irq2, this->state_);
  } else if (this->state_ == STATE_WUPA || this->state_ == STATE_ANTICOL) {
    this->irq_status1_ |= this->read_register(ST25R300_REG_IRQ_STATUS1);
    this->irq_status2_ |= this->read_register(ST25R300_REG_IRQ_STATUS2);
    if (this->irq_status1_ != 0 || this->irq_status2_ != 0) {
      ESP_LOGV(TAG, "IRQ polled, IRQ1=0x%02X IRQ2=0x%02X state=%d", this->irq_status1_, this->irq_status2_, this->state_);
    }
  } else {
    this->irq_status1_ = 0;
    this->irq_status2_ = 0;
  }

  this->process_state_();
}

// ── process_state_ ────────────────────────────────────────────────────────────
void ST25R300::process_state_() {
  switch (this->state_) {
    case STATE_IDLE:
      break;

    case STATE_WUPA: {
      if (this->irq_status1_ & (ST25R300_IRQ1_RXE | ST25R300_IRQ1_COL)) {
        ESP_LOGD(TAG, "WUPA got response: IRQ1=0x%02X IRQ2=0x%02X", this->irq_status1_, this->irq_status2_);
        this->cascade_level_ = 0;
        this->current_uid_ = "";
        if (!this->anticol_resume_) {
          this->anticol_prefix_full_ = 0;
          this->anticol_prefix_bits_ = 0;
          this->anticol_col_pos_ = 0;
          this->anticol_prefix_val_ = 0;
        }
        this->anticol_resume_ = false;
        this->send_anticol_frame_();
        this->state_ = STATE_ANTICOL;
        this->last_state_change_ = millis();
      } else if (this->irq_status2_ & ST25R300_IRQ2_NRE) {
        // NRT expired: no tag response — fast path to idle
        uint8_t rssi1   = this->read_register(ST25R300_REG_RSSI1);
        uint8_t rssi2   = this->read_register(ST25R300_REG_RSSI2);
        uint8_t stat2   = this->read_register(ST25R300_REG_STATUS2);
        uint8_t sstatus2 = this->read_register(ST25R300_REG_STATIC_STATUS2);
        uint8_t sstatus3 = this->read_register(ST25R300_REG_STATIC_STATUS3);
        ESP_LOGD(TAG, "WUPA NRE (no tag): IRQ1=0x%02X RSSI=%02X/%02X STAT2=0x%02X SS2=0x%02X SS3=0x%02X",
                 this->irq_status1_, rssi1, rssi2, stat2, sstatus2, sstatus3);
        this->irq_status1_ = 0;
        this->irq_status2_ = 0;
        this->state_ = STATE_IDLE;
        this->finalize_scan_();
      } else if (millis() - this->last_state_change_ > 100) {
        uint8_t irq3 = this->read_register(ST25R300_REG_IRQ_STATUS3);
        uint8_t fifo = this->read_register(ST25R300_REG_FIFO_STATUS1);
        uint8_t op = this->read_register(ST25R300_REG_OPERATION);
        uint8_t stat2 = this->read_register(ST25R300_REG_STATUS2);
        uint8_t sstatus2 = this->read_register(ST25R300_REG_STATIC_STATUS2);
        uint8_t sstatus3 = this->read_register(ST25R300_REG_STATIC_STATUS3);
        ESP_LOGD(TAG, "WUPA timeout: IRQ1=0x%02X IRQ2=0x%02X IRQ3=0x%02X FIFO=%u OP=0x%02X STAT2=0x%02X SS2=0x%02X SS3=0x%02X",
                 this->irq_status1_, this->irq_status2_, irq3, fifo, op, stat2, sstatus2, sstatus3);
        this->irq_status1_ = 0;
        this->irq_status2_ = 0;
        this->state_ = STATE_IDLE;
        this->finalize_scan_();
      }
      break;
    }

    case STATE_ANTICOL: {
      if (millis() - this->last_state_change_ > 20) {
        uint8_t max_prefix_val = (1 << (this->anticol_col_pos_ + 1)) - 1;
        if (this->anticol_col_pos_ > 0 && this->anticol_prefix_val_ < max_prefix_val) {
          this->anticol_prefix_val_++;
          this->apply_anticol_prefix_();

          // Send WUPA before each new prefix attempt
          this->write_command(ST25R300_CMD_CLEAR_FIFO);
          this->read_register(ST25R300_REG_IRQ_STATUS1);
          this->read_register(ST25R300_REG_IRQ_STATUS2);
          this->read_register(ST25R300_REG_IRQ_STATUS3);
          this->irq_triggered_ = false;
          this->irq_status1_ = 0;  // fresh receive window
          this->irq_status2_ = 0;
          this->anticol_resume_ = true;

          uint8_t dummy[2];
          uint8_t dummy_len = 0;
          this->send_short_frame_(0x52, dummy, dummy_len, 10);  // WUPA
          this->state_ = STATE_WUPA;
          this->last_state_change_ = millis();
          return;
        }
        this->state_ = STATE_IDLE;
        this->finalize_scan_();
        return;
      }

      if (this->irq_status1_ != 0 &&
          (this->irq_status1_ & (ST25R300_IRQ1_RXE | ST25R300_IRQ1_COL | ST25R300_IRQ1_TXE))) {
        uint8_t saved_irq = this->irq_status1_;
        this->irq_status1_ = 0;
        this->irq_status2_ = 0;  // consume; send_anticol_frame_ / send_short_frame_ will repopulate
        delay(5);
        uint8_t f1 = this->read_register(ST25R300_REG_FIFO_STATUS1);
        bool has_collision = (saved_irq & ST25R300_IRQ1_COL) != 0;

        if (has_collision) {
          uint8_t col_raw = this->read_register(ST25R300_REG_COLLISION_DISPLAY);
          uint8_t c_byte = (col_raw >> 4) & 0x0F;
          uint8_t c_bit  = (col_raw >> 1) & 0x07;
          int uid_col_pos = (int)(c_byte * 8 + c_bit) - 16;
          if (uid_col_pos < 0) uid_col_pos = 0;
          if (f1 > 0) { uint8_t tmp[8]; this->read_fifo(tmp, std::min(f1, (uint8_t) 8)); }

          this->anticol_col_pos_ = uid_col_pos;
          this->anticol_prefix_val_ = 0;
          this->apply_anticol_prefix_();
          this->send_anticol_frame_();
          this->last_state_change_ = millis();

        } else if (f1 >= 5) {
          // Clean anticol response — full UID level received
          uint8_t resp[5];
          this->read_fifo(resp, 5);

          // Reconstruct full UID by OR-ing prefix bits back in
          uint8_t full_uid[4];
          memcpy(full_uid, resp, 4);
          for (int k = 0; k < (int) this->anticol_prefix_full_; k++) {
            full_uid[k] = this->anticol_prefix_[k];
          }
          if (this->anticol_prefix_bits_ > 0) {
            uint8_t mask = (uint8_t)((1 << this->anticol_prefix_bits_) - 1);
            full_uid[this->anticol_prefix_full_] =
                (this->anticol_prefix_[this->anticol_prefix_full_] & mask) |
                (resp[this->anticol_prefix_full_] & (uint8_t)(~mask));
          }
          uint8_t bcc = full_uid[0] ^ full_uid[1] ^ full_uid[2] ^ full_uid[3];

          uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
          uint8_t sel_pk[7] = {sel_cmds[this->cascade_level_], 0x70,
                               full_uid[0], full_uid[1], full_uid[2], full_uid[3], bcc};

          ESP_LOGV(TAG, "SELECT CL%d: UID=%02X%02X%02X%02X BCC=%02X tagBCC=%02X",
                   this->cascade_level_ + 1,
                   full_uid[0], full_uid[1], full_uid[2], full_uid[3], bcc, resp[4]);

          if (full_uid[0] == 0x88) {
            for (int i = 1; i < 4; i++) {
              char buf[3]; sprintf(buf, "%02X", full_uid[i]); this->current_uid_ += buf;
            }
          } else {
            for (int i = 0; i < 4; i++) {
              char buf[3]; sprintf(buf, "%02X", full_uid[i]); this->current_uid_ += buf;
            }
          }

          // Restore normal Rx protocol (clear antcl) for SELECT (uses CRC)
          this->write_register(ST25R300_REG_RX_PROTOCOL1,
                               ST25R300_RX_PROT1_B_RX_SOF | ST25R300_RX_PROT1_B_RX_EOF |
                               ST25R300_RX_PROT1_A_RX_PAR | ST25R300_RX_PROT1_RX_CRC);  // 0x3C

          uint8_t sak_buf[3];
          uint8_t sak_len = 0;
          if (!this->transceive_(sel_pk, 7, sak_buf, sak_len) || sak_len == 0) {
            ESP_LOGW(TAG, "SELECT failed (no SAK), sak_len=%d", sak_len);
            this->state_ = STATE_IDLE;
            this->finalize_scan_();
            return;
          }
          uint8_t sak = sak_buf[0];

          if (sak & 0x04) {  // Cascade bit — need another anticollision level
            if (this->cascade_level_ == 0) {
              this->saved_col_pos_ = this->anticol_col_pos_;
              this->saved_prefix_val_ = this->anticol_prefix_val_;
              this->saved_anticol_valid_ = (this->anticol_col_pos_ > 0 || this->anticol_prefix_bits_ > 0);
            }
            this->cascade_level_++;
            if (this->cascade_level_ > 2) {
              ESP_LOGE(TAG, "Too many cascade levels");
              this->state_ = STATE_IDLE;
              this->finalize_scan_();
              return;
            }
            this->anticol_prefix_full_ = 0;
            this->anticol_prefix_bits_ = 0;
            this->anticol_col_pos_ = 0;
            this->anticol_prefix_val_ = 0;
            this->send_anticol_frame_();
            this->state_ = STATE_ANTICOL;
            this->last_state_change_ = millis();
          } else {
            // Tag fully selected
            size_t uid_bytes_len = this->current_uid_.length() / 2;
            if (uid_bytes_len != 4 && uid_bytes_len != 7) {
              ESP_LOGW(TAG, "Discarding invalid UID len=%zu (%s)", uid_bytes_len, this->current_uid_.c_str());
              this->state_ = STATE_IDLE;
              this->finalize_scan_();
              return;
            }

            ESP_LOGI(TAG, "Tag selected: %s", this->current_uid_.c_str());

            if (!this->present_tags_.count(this->current_uid_)) {
              std::vector<uint8_t> uid_bytes;
              for (size_t i = 0; i < this->current_uid_.length(); i += 2)
                uid_bytes.push_back((uint8_t) strtol(this->current_uid_.substr(i, 2).c_str(), nullptr, 16));
              this->tags_data_[this->current_uid_] = this->read_tag_(uid_bytes);
            }

            this->tags_this_scan_.insert(this->current_uid_);

            // HALT: [0x50, 0x00] + CRC; tag has no response
            {
              uint8_t halt_cmd[2] = {0x50, 0x00};
              this->write_command(ST25R300_CMD_CLEAR_FIFO);
              this->read_register(ST25R300_REG_IRQ_STATUS1);
              this->read_register(ST25R300_REG_IRQ_STATUS2);
              this->read_register(ST25R300_REG_IRQ_STATUS3);
              // TX_FRAME must be written before FIFO data
              this->write_register(ST25R300_REG_TX_FRAME1, 0x00);
              this->write_register(ST25R300_REG_TX_FRAME2, 0x10);   // 2 full bytes
              this->write_register(ST25R300_REG_TX_PROTOCOL1,
                                   ST25R300_TX_PROT1_A_TX_PAR | ST25R300_TX_PROT1_TX_CRC);
              this->write_fifo(halt_cmd, 2);
              this->write_command(ST25R300_CMD_TRANSMIT_DATA);
              delay(10);
            }

            // Determine collision tree resume state
            uint8_t resume_col_pos;
            uint8_t resume_prefix_val;
            bool can_resume;
            if (this->saved_anticol_valid_) {
              resume_col_pos = this->saved_col_pos_;
              resume_prefix_val = this->saved_prefix_val_;
              can_resume = true;
              this->saved_anticol_valid_ = false;
            } else {
              resume_col_pos = this->anticol_col_pos_;
              resume_prefix_val = this->anticol_prefix_val_;
              can_resume = (this->anticol_col_pos_ > 0 || this->anticol_prefix_bits_ > 0);
            }

            this->write_command(ST25R300_CMD_CLEAR_FIFO);
            this->read_register(ST25R300_REG_IRQ_STATUS1);
            this->read_register(ST25R300_REG_IRQ_STATUS2);
            this->read_register(ST25R300_REG_IRQ_STATUS3);
            this->irq_triggered_ = false;
            this->irq_status1_ = 0;
            this->irq_status2_ = 0;  // fresh receive window for resume WUPA

            if (can_resume) {
              this->cascade_level_ = 0;
              this->current_uid_ = "";
              this->anticol_col_pos_ = resume_col_pos;
              this->anticol_prefix_val_ = resume_prefix_val + 1;
              this->apply_anticol_prefix_();

              uint8_t max_val = (1 << (resume_col_pos + 1)) - 1;
              if (this->anticol_prefix_val_ > max_val) {
                this->state_ = STATE_IDLE;
                this->finalize_scan_();
                return;
              }
              this->anticol_resume_ = true;
              uint8_t dummy[2];
              uint8_t dummy_len = 0;
              this->send_short_frame_(0x52, dummy, dummy_len, 10);  // WUPA
            } else {
              this->state_ = STATE_IDLE;
              this->finalize_scan_();
              return;
            }
            this->state_ = STATE_WUPA;
            this->last_state_change_ = millis();
          }
        }
      }
      break;
    }

    case STATE_REINITIALIZING:
      this->reinitialize_();
      this->state_ = STATE_IDLE;
      break;

    default:
      break;
  }
}

// ── finalize_scan_ ────────────────────────────────────────────────────────────
void ST25R300::finalize_scan_() {
  std::vector<std::string> to_remove;
  for (auto &kv : this->present_tags_) {
    if (this->tags_this_scan_.count(kv.first)) {
      kv.second = 0;
    } else {
      kv.second++;
      if (kv.second >= 3) to_remove.push_back(kv.first);
    }
  }
  for (const auto &uid : to_remove) {
    ESP_LOGI(TAG, "Tag Removed: %s", uid.c_str());
    std::vector<uint8_t> uid_bytes;
    for (size_t i = 0; i < uid.length(); i += 2)
      uid_bytes.push_back((uint8_t) strtol(uid.substr(i, 2).c_str(), nullptr, 16));
    nfc::NfcTagUid nfc_uid(uid_bytes.begin(), uid_bytes.end());
    nfc::NfcTag nfc_tag(nfc_uid);
    for (auto *listener : this->tag_listeners_)
      listener->tag_off(nfc_tag);
    for (auto *trigger : this->on_tag_removed_triggers_)
      trigger->trigger(uid);
    this->tags_data_.erase(uid);
    this->present_tags_.erase(uid);
  }

  for (const auto &uid : this->tags_this_scan_) {
    if (!this->present_tags_.count(uid)) {
      this->present_tags_[uid] = 0;
      for (auto *trigger : this->on_tag_triggers_)
        trigger->trigger(uid);
      if (this->tags_data_.count(uid) && this->tags_data_[uid]) {
        for (auto *listener : this->tag_listeners_)
          listener->tag_on(*this->tags_data_[uid]);
      }
    }
  }

  this->tags_this_scan_.clear();
}

// ── apply_anticol_prefix_ ─────────────────────────────────────────────────────
void ST25R300::apply_anticol_prefix_() {
  int total_bits = this->anticol_col_pos_ + 1;
  this->anticol_prefix_full_ = total_bits >> 3;
  this->anticol_prefix_bits_ = total_bits & 7;
  memset(this->anticol_prefix_, 0, sizeof(this->anticol_prefix_));
  for (int i = 0; i < total_bits; i++) {
    int byte_idx = i >> 3;
    int bit_idx  = i & 7;
    if ((this->anticol_prefix_val_ >> i) & 1)
      this->anticol_prefix_[byte_idx] |= (1 << bit_idx);
  }
}

// ── send_anticol_frame_ ───────────────────────────────────────────────────────
void ST25R300::send_anticol_frame_() {
  uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
  uint8_t sel = sel_cmds[this->cascade_level_];

  uint8_t nvb_high = 2 + this->anticol_prefix_full_;
  uint8_t nvb = (nvb_high << 4) | this->anticol_prefix_bits_;

  uint8_t frame[7];
  frame[0] = sel;
  frame[1] = nvb;
  uint8_t frame_len = 2;
  for (int i = 0; i < this->anticol_prefix_full_; i++)
    frame[frame_len++] = this->anticol_prefix_[i];
  if (this->anticol_prefix_bits_ > 0)
    frame[frame_len++] = this->anticol_prefix_[this->anticol_prefix_full_];

  uint8_t ntx_n = 2 + this->anticol_prefix_full_;
  uint8_t ntx_b = this->anticol_prefix_bits_;

  // Enable anticollision mode; keep b_rx_sof+b_rx_eof+a_rx_par for correct 8-bit byte recovery.
  // a_rx_par MUST be set: without it, parity bits from 9-bit NFC-A bytes are packed into FIFO
  // alongside data bits, garbling the UID. a_rx_par strips parity before FIFO write.
  this->write_register(ST25R300_REG_RX_PROTOCOL1,
                       ST25R300_RX_PROT1_B_RX_SOF | ST25R300_RX_PROT1_B_RX_EOF |
                       ST25R300_RX_PROT1_A_RX_PAR | ST25R300_RX_PROT1_ANTCL);  // 0x39

  // Tx protocol: no CRC for anticollision frame
  this->write_register(ST25R300_REG_TX_PROTOCOL1, ST25R300_TX_PROT1_A_TX_PAR);  // parity, no CRC

  this->write_command(ST25R300_CMD_STOP_ALL);  // reset receiver state + clear FIFO + clear IRQs
  this->read_register(ST25R300_REG_IRQ_STATUS1);
  this->read_register(ST25R300_REG_IRQ_STATUS2);
  this->read_register(ST25R300_REG_IRQ_STATUS3);
  this->write_command(ST25R300_CMD_CLEAR_RX_GAIN);  // init AGC/squelch before every TX
  this->irq_triggered_ = false;
  this->irq_status1_ = 0;
  this->irq_status2_ = 0;  // clear accumulated bits; fresh receive window starts
  // TX_FRAME must be written before FIFO data (per datasheet section 5.16.6)
  this->write_register(ST25R300_REG_TX_FRAME1, ntx_n >> 5);
  this->write_register(ST25R300_REG_TX_FRAME2, (uint8_t)(((ntx_n & 0x1F) << 3) | (ntx_b & 0x07)));
  this->write_fifo(frame, frame_len);
  this->write_command(ST25R300_CMD_TRANSMIT_DATA);
}

// ── reset_ ────────────────────────────────────────────────────────────────────
bool ST25R300::reset_() {
  ESP_LOGV(TAG, "  reset_: Sending SET_DEFAULT");
  this->write_command(ST25R300_CMD_SET_DEFAULT);
  delay(10);

  // Verify IC identity
  uint8_t ic_identity = this->read_register(ST25R300_REG_IC_IDENTITY);
  ESP_LOGD(TAG, "  reset_: IC identity read: 0x%02X", ic_identity);
  if ((ic_identity & ST25R300_IC_TYPE_MASK) != ST25R300_IC_TYPE_VAL) {
    ESP_LOGE(TAG, "  reset_: IC identity mismatch! Expected 0xB0, got 0x%02X (chip_type=0x%02X)",
             ic_identity, ic_identity & ST25R300_IC_TYPE_MASK);
    return false;
  }
  ESP_LOGI(TAG, "IC identity match: 0x%02X (ST25R300 rev %u)", ic_identity, ic_identity & 0x07);

  // Step 1: Enable oscillator → enter Ready mode
  ESP_LOGV(TAG, "  reset_: Enabling oscillator (en=1)");
  this->write_register(ST25R300_REG_OPERATION, ST25R300_OP_EN);  // bit3=1
  delay(5);

  // Wait for oscillator stable (I_osc in IRQ status 3, bit 0)
  uint32_t osc_start = millis();
  while (millis() - osc_start < 50) {
    uint8_t irq3 = this->read_register(ST25R300_REG_IRQ_STATUS3);
    if (irq3 & ST25R300_IRQ3_OSC) break;
    delay(1);
  }
  ESP_LOGV(TAG, "  reset_: Oscillator stable");

  // Step 2: Enable VDDDR regulator, wait ≥10µs
  this->write_register(ST25R300_REG_OPERATION, ST25R300_OP_EN | ST25R300_OP_VDDDR_EN);
  delay(1);

  // Step 3: Enable TX + RX
  this->write_register(ST25R300_REG_OPERATION, ST25R300_OP_ALL_ON);

  // Step 4: Adjust regulators
  this->write_command(ST25R300_CMD_ADJUST_REGS);
  delay(5);

  // Step 5: Configure for NFC-A / ISO14443A
  // General config: differential antenna (single=0), no RFO2 switch
  this->write_register(ST25R300_REG_GENERAL_CONF, 0x00);

  // Protocol register 1: om=0x1 (NFC-A reader/initiator)
  this->write_register(ST25R300_REG_PROTOCOL1, 0x01);

  // Tx protocol: a_tx_par=1 (bit6), tx_crc=1 (bit5), tr_am=0 (OOK), p_len=0
  this->write_register(ST25R300_REG_TX_PROTOCOL1,
                       ST25R300_TX_PROT1_A_TX_PAR | ST25R300_TX_PROT1_TX_CRC);  // 0x60

  // Rx protocol: b_rx_sof+b_rx_eof MUST be set (factory default=0x3C); antcl=0
  this->write_register(ST25R300_REG_RX_PROTOCOL1,
                       ST25R300_RX_PROT1_B_RX_SOF | ST25R300_RX_PROT1_B_RX_EOF |
                       ST25R300_RX_PROT1_A_RX_PAR | ST25R300_RX_PROT1_RX_CRC);  // 0x3C

  // RX path analog and correlator configuration tuned for NFC-A 106kbps reception.
  // Values derived from RFAL ST25R500 analogConfigTbl for Poll NFC-A Rx 106kbps.
  // The ST25R300 and ST25R500 share the same register map at these addresses.

  // 0x09 RX Path Ana 1: dig_clk_dly=7 (recommended), hpf_ctrl=3 (80kHz HPF), gain_boost optional
  uint8_t rx_ana1 = 0x73 | (this->rx_gain_boost_ ? 0x04 : 0x00);  // gain_boost = bit2
  this->write_register(ST25R300_REG_RX_ANALOG1, rx_ana1);

  // 0x0A RX Path Ana 2: afe_gain_rw=2 (6dB start reduction, per RFAL), afe_gain_td=2
  // Note: afe_gain_rw≠0 requires dis_agc_noise_meas=1 in CORR5 (0x12)
  this->write_register(0x0A, 0x22);

  this->write_register(0x0B, 0x85);  // RX Path Ana 3: ook thresholds (factory default)
  this->write_register(0x0D, 0xCC);  // RX Digital: agc_en=1, lpf_coef=4, hpf_coef=3 (factory default)

  // Correlator settings tuned for NFC-A 106kbps (from RFAL analogConfigTbl)
  this->write_register(0x0E, 0xF8);  // CORR1: iir_coef2=F, iir_coef1=8 (RFAL; factory=0xC1)
  this->write_register(0x0F, 0x2E);  // CORR2: squelch_thr=2, agc_thr=E (RFAL; factory=0x5A)
  this->write_register(0x10, 0x0F);  // CORR3: start_wait=15 (RFAL; factory=7) — longer wait prevents false subc_start
  this->write_register(0x11, 0x88);  // CORR4: coll_lvl=8, data_lvl=8 (RFAL; factory=0xAA) — lower threshold
  this->write_register(0x12, 0x32);  // CORR5: dis_soft_sq=1, dis_agc_noise_meas=1, dec_f=2 (required for afe_gain_rw≠0)
  this->write_register(0x13, 0x20);  // CORR6: init_noise_lvl=2 (RFAL; factory=0x30)

  // TX driver: d_res controls power (0=max, 15=min); keep am_mod field at 0x7 in TX_MOD1
  uint8_t d_res = (15 - this->rf_power_) & 0x0F;
  this->write_register(ST25R300_REG_TX_DRIVER_CONF, d_res);    // d_res[3:0]
  this->write_register(ST25R300_REG_TX_MOD1, 0x70);             // am_mod=7 (20%)

  // Unmask all IRQs
  this->write_register(ST25R300_REG_IRQ_MASK1, 0x00);
  this->write_register(ST25R300_REG_IRQ_MASK2, 0x00);
  this->write_register(ST25R300_REG_IRQ_MASK3, 0x00);

  // NRT: auto-starts after CMD_TRANSMIT_DATA when non-zero; fires I_nre on timeout.
  // 1059 steps × 64/fc (4.72µs/step) ≈ 5ms — enough window to capture ATQA (arrives ~86µs, lasts ~165µs)
  this->write_register(ST25R300_REG_NRT_CONF1, 0x00);   // nrt_step=0 → 64/fc = 4.72µs per step
  this->write_register(ST25R300_REG_NRT_CONF2, 0x04);   // nrt[15:8] = 0x04
  this->write_register(ST25R300_REG_NRT_CONF3, 0x23);   // nrt[7:0]  = 0x23 → 0x0423 = 1059 steps = 5ms

  if (this->rf_field_enabled_) {
    ESP_LOGV(TAG, "  reset_: Enabling RF field");
    // Enable TX first, then trigger RF collision avoidance field on sequence
    this->write_register(ST25R300_REG_OPERATION, ST25R300_OP_EN | ST25R300_OP_VDDDR_EN | ST25R300_OP_TX_EN);
    delay(5);
    this->write_command(ST25R300_CMD_FIELD_ON);
    delay(10);
    this->write_register(ST25R300_REG_OPERATION, ST25R300_OP_ALL_ON);
  }
  delay(10);

  ESP_LOGV(TAG, "  reset_: Complete");
  return true;
}

// ── reinitialize_ ─────────────────────────────────────────────────────────────
void ST25R300::reinitialize_() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(true);
    delay(10);
    this->reset_pin_->digital_write(false);
    delay(10);
  }
  if (this->reset_()) {
    this->health_check_failures_ = 0;
  } else {
    this->mark_failed();
  }
}

// ── NDEF write ─────────────────────────────────────────────────────────────────
bool ST25R300::ndef_write(nfc::NdefMessage *message, bool format) {
  uint8_t buffer[16];
  uint8_t len;

  if (format) {
    ESP_LOGD(TAG, "Formatting tag (NTAG CC)...");
    uint8_t cc_cmd[6] = {0xA2, 0x03, 0xE1, 0x10, 0x3E, 0x00};
    bool cc_success = false;
    for (uint8_t i = 0; i < 3; i++) {
      delay(20);
      if (this->transceive_(cc_cmd, 6, buffer, len) && len > 0 && (buffer[0] & 0x0F) == 0x0A) {
        cc_success = true; break;
      }
    }
    if (!cc_success) {
      ESP_LOGE(TAG, "Failed to write CC page during format");
      return false;
    }
    delay(50);
  }

  if (message == nullptr) {
    uint8_t empty_ndef[6] = {0xA2, 0x04, 0x03, 0x00, 0xFE, 0x00};
    for (uint8_t i = 0; i < 3; i++) {
      delay(20);
      if (this->transceive_(empty_ndef, 6, buffer, len) && len > 0 && (buffer[0] & 0x0F) == 0x0A)
        return true;
    }
    return false;
  }

  std::vector<uint8_t> ndef_data = message->encode();
  std::vector<uint8_t> payload;
  payload.push_back(0x03);
  if (ndef_data.size() < 255) {
    payload.push_back((uint8_t) ndef_data.size());
  } else {
    payload.push_back(0xFF);
    payload.push_back((uint8_t)((ndef_data.size() >> 8) & 0xFF));
    payload.push_back((uint8_t)(ndef_data.size() & 0xFF));
  }
  payload.insert(payload.end(), ndef_data.begin(), ndef_data.end());
  payload.push_back(0xFE);
  while (payload.size() % 4 != 0) payload.push_back(0);

  for (size_t i = 0; i < payload.size(); i += 4) {
    uint8_t page = (uint8_t)(4 + (i / 4));
    uint8_t write_cmd[6] = {0xA2, page, payload[i], payload[i+1], payload[i+2], payload[i+3]};
    bool success = false;
    for (uint8_t retry = 0; retry < 3; retry++) {
      delay(20);
      if (this->transceive_(write_cmd, 6, buffer, len) && len > 0 && (buffer[0] & 0x0F) == 0x0A) {
        success = true; break;
      }
    }
    if (!success) {
      ESP_LOGE(TAG, "NDEF write failed at page %d", page);
      return false;
    }
  }
  ESP_LOGI(TAG, "NDEF write successful!");
  return true;
}

bool ST25R300::clean_tag() {
  return this->ndef_write(nullptr, true);
}

void ST25R300::dump_config() {
  ESP_LOGCONFIG(TAG, "ST25R300:");
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  ESP_LOGCONFIG(TAG, "  RF Power: %u", this->rf_power_);
  ESP_LOGCONFIG(TAG, "  RF Field Enabled: %s", YESNO(this->rf_field_enabled_));
  LOG_UPDATE_INTERVAL(this);
  uint8_t ic_id = this->read_register(ST25R300_REG_IC_IDENTITY);
  ESP_LOGCONFIG(TAG, "  IC Identity (live): 0x%02X (chip_type=0x%02X)", ic_id, ic_id & ST25R300_IC_TYPE_MASK);
}

}  // namespace st25r300
}  // namespace esphome
