#include "st25r.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/components/nfc/nfc_tag.h"
#include "esphome/components/nfc/nfc_helpers.h"
#include "esphome/components/nfc/ndef_message.h"
#include <cinttypes>
#include <algorithm>
#include <cstring>

/*
 * Mifare Classic support.
 *
 * Protocol flow adapted from mf1.c — MIT licence:
 *   https://github.com/suut/rfal-mifare-classic/blob/master/mf1/mf1.c
 *
 * ST25R3916 9-bit parity interleaving (mf1_encode/decode_parity_st25r3916):
 *   Each byte is stored as 9 bits in the FIFO: 8 data bits then 1 parity bit.
 *   CRC and parity are both handled manually; ISO14443A_CONF bits no_tx_par
 *   (bit6) and no_rx_par (bit7) must be set before transmitting/receiving.
 */

// ── Mifare CRC-A ────────────────────────────────────────────────────────────
// Inline from mf1.h (MIT, suut/rfal-mifare-classic)
static uint16_t mifare_crc_a(const uint8_t *data, size_t len) {
  uint16_t crc = 0x6363;
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i] ^ (uint8_t)(crc & 0xFF);
    b ^= b << 4;
    crc = (crc >> 8) ^ ((uint16_t) b << 8) ^ ((uint16_t) b << 3) ^ ((uint16_t) b >> 4);
  }
  return crc;
}

// ── Odd parity lookup ────────────────────────────────────────────────────────
static const uint8_t ODD_PARITY[256] = {
  1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
  0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
  0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
  1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
  0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
  1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
  1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
  0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
};

// ── 9-bit parity pack/unpack ─────────────────────────────────────────────────
// Each byte → 9 bits in the buffer: data bits 0..7 then parity bit.
// Adapted from mf1_encode/decode_parity_st25r3916 (MIT, suut/rfal-mifare-classic)

static void mifare_pack_parity(const uint8_t *in, const uint8_t *par,
                                uint8_t *out, uint8_t nbytes,
                                uint16_t *out_bits) {
  uint16_t total = 9u * nbytes;
  memset(out, 0, (total + 7u) / 8u);
  for (uint8_t i = 0; i < nbytes; i++) {
    for (uint8_t j = 0; j < 8; j++) {
      uint32_t p = j + 9u * i;
      out[p / 8] |= (uint8_t)(((in[i] >> j) & 1u) << (p % 8));
    }
    uint32_t p = 8u + 9u * i;
    out[p / 8] |= (uint8_t)((par[i] & 1u) << (p % 8));
  }
  *out_bits = total;
}

static uint8_t mifare_unpack_parity(const uint8_t *in, uint8_t *out,
                                     uint8_t *par, uint16_t in_bits) {
  uint8_t nbytes = (uint8_t)(in_bits / 9u);
  memset(out, 0, nbytes);
  memset(par, 0, nbytes);
  for (uint8_t i = 0; i < nbytes; i++) {
    for (uint8_t j = 0; j < 8; j++) {
      uint32_t p = j + 9u * i;
      out[i] |= (uint8_t)(((in[p / 8] >> (p % 8)) & 1u) << j);
    }
    uint32_t p = 8u + 9u * i;
    par[i] = (in[p / 8] >> (p % 8)) & 1u;
  }
  return nbytes;
}

namespace esphome {
namespace st25r {

static const char *const TAG = "st25r";
// NFC Forum Type 4 Tag string (mirrors nfc::NFC_FORUM_TYPE_2 naming convention;
// not yet in ESPHome's nfc.h so we define it here).
static const char *const NFC_FORUM_TYPE_4 = "NFC Forum Type 4";

// ── BER-TLV helpers ───────────────────────────────────────────────────────────
// Used to parse Google Smart Tap 2.0 and Apple VAS responses.
// Placed in an anonymous namespace so they are local to this translation unit.
namespace {

// Read a BER-TLV length field at *pos; advance *pos past the length bytes.
// Returns the decoded length, or 0 for unsupported indefinite-length encoding.
size_t ber_read_len(const uint8_t *data, size_t data_len, size_t *pos) {
  if (*pos >= data_len) return 0;
  size_t l = data[(*pos)++];
  if (l <= 0x7F) return l;       // short-form: length encoded directly
  if (l == 0x80) return 0;       // indefinite length — not used in Smart Tap / VAS
  size_t nb = l & 0x7F;          // number of following length bytes
  l = 0;
  for (; nb > 0 && *pos < data_len; nb--)
    l = (l << 8) | data[(*pos)++];
  return l;
}

// Find the first TLV entry with a 1-byte tag within [data, data+data_len).
// Sets *val_len to the value's length and returns a pointer to the value bytes.
// Returns nullptr if the tag is not found or the TLV is malformed.
const uint8_t *ber_find(const uint8_t *data, size_t data_len,
                        uint8_t tag, size_t *val_len) {
  size_t i = 0;
  while (i < data_len) {
    uint8_t t = data[i++];
    size_t l = ber_read_len(data, data_len, &i);
    if (i + l > data_len) break;
    if (t == tag) { *val_len = l; return &data[i]; }
    i += l;
  }
  return nullptr;
}

}  // anonymous namespace

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

  if (this->status_binary_sensor_ != nullptr) {
    this->status_binary_sensor_->publish_initial_state(false);
  }
  ESP_LOGI(TAG, "Starting reset_()...");
  if (!this->reset_()) {
    ESP_LOGE(TAG, "Failed to reset chip");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "ST25R initialized successfully.");
}

void ST25R::update() {
  if (this->is_failed() || this->state_ != STATE_IDLE) return;

  uint8_t ic_identity = this->read_register(IC_IDENTITY);
  uint8_t chip_type_upd = ic_identity & 0xF8;
  if (chip_type_upd != 0x28 && chip_type_upd != 0x30) {
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

  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->write_command(ST25R_CMD_RESET_RX_GAIN);  // reset AGC/squelch to initial state per datasheet

  if (this->rf_field_enabled_) {
    this->write_register(OP_CONTROL, 0xC8); // en=1, rx_en=1, tx_en=1
  }

  if (this->rf_field_enabled_ && this->field_strength_sensor_ != nullptr) {
    this->write_command(ST25R_CMD_MEASURE_AMPLITUDE);
    uint8_t amplitude = this->read_register(AD_CONV_RESULT);
    this->field_strength_sensor_->publish_state(amplitude);
  }

  // RX_CONF3: 0xE2 = rg1_am=7 (+5.5dB AM boost) + lf_en=1 — needed for non-B Elechouse module
  //           0xFE = rg1_am=7 + rg1_pm=7 (+5.5dB PM boost too) + lf_en=1 — maximum sensitivity, for weak coupling
  // B-version (ST25R3916B): lf_en=1 routes receiver away from HF 13.56MHz NFC path → use 0x00 instead
  uint8_t rx_conf3;
  if (this->is_b_version_) {
    rx_conf3 = 0x00;
  } else {
    rx_conf3 = this->rx_gain_boost_ ? 0xFE : 0xE2;
  }
  this->write_register(RX_CONF3, rx_conf3);

  this->saved_anticol_valid_ = false;
  this->anticol_resume_ = false;

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
  this->write_command(ST25R_CMD_RESET_RX_GAIN);  // reset AGC/squelch per datasheet transceive sequence
  // Clear ALL IRQ registers so IRQ pin goes low — required for ISR rising-edge to fire
  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);

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
    uint8_t irq;
    if (this->irq_triggered_) {
      this->irq_triggered_ = false;
      irq = this->read_register(IRQ_MAIN);
    } else {
      // Fallback: poll directly in case ISR missed the rising edge (pin was already high)
      irq = this->read_register(IRQ_MAIN);
    }
    this->irq_status_ = irq;

    if (irq & IRQ_TXE) tx_done = true;

    if (tx_done) {
      uint8_t f1 = this->read_register(FIFO_STATUS1);
      if (f1 > 0) {
        uint8_t to_read = std::min((uint8_t)(64 - resp_len), f1);
        this->read_fifo(resp + resp_len, to_read);
        resp_len += to_read;
        start = millis();
      }
      if (irq & IRQ_RXE) {
        return resp_len > 0;
      }
    }
    delay(1);
  }
  return resp_len > 0;
}

// ── transceive_mifare_ ───────────────────────────────────────────────────────
// Send/receive with manual CRC and parity (ISO14443A_CONF no_tx_par + no_rx_par).
// data/parity: len plaintext bytes + precomputed parity bits.
// resp/resp_parity: decoded response bytes and their parity bits.
// Adapted from mf1_send_receive_raw (MIT, suut/rfal-mifare-classic).
bool ST25R::transceive_mifare_(const uint8_t *data, const uint8_t *parity,
                                uint8_t len,
                                uint8_t *resp, uint8_t *resp_parity,
                                uint8_t &resp_len,
                                uint32_t timeout_ms) {
  // Max encoded size: ceil(9*64/8) = 72 bytes
  uint8_t encoded[72];
  uint16_t tx_bits = 0;
  mifare_pack_parity(data, parity, encoded, len, &tx_bits);

  uint8_t ntx_n   = (uint8_t)(tx_bits >> 3);
  uint8_t ntx_b   = (uint8_t)(tx_bits & 7);
  uint8_t fifo_bytes = (uint8_t)((tx_bits + 7) / 8);

  // Set manual parity mode: no_tx_par (bit6) + no_rx_par (bit7)
  this->write_register(ISO14443A_CONF, 0xC0);
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->write_command(ST25R_CMD_RESET_RX_GAIN);
  this->read_register(IRQ_MAIN);
  this->read_register(IRQ_TIMER);
  this->read_register(IRQ_ERROR);
  this->irq_triggered_ = false;

  this->write_register(NUM_TX_BYTES1, ntx_n >> 5);
  this->write_register(NUM_TX_BYTES2, (uint8_t)(((ntx_n & 0x1F) << 3) | (ntx_b & 7)));
  this->write_fifo(encoded, fifo_bytes);
  this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);

  // Wait for RXE (end of receive)
  uint32_t start = millis();
  resp_len = 0;
  bool tx_done = false;
  while (millis() - start < timeout_ms) {
    uint8_t irq = this->read_register(IRQ_MAIN);
    if (irq & IRQ_TXE) tx_done = true;
    if (tx_done) {
      uint8_t f1 = this->read_register(FIFO_STATUS1);
      if (irq & IRQ_RXE) {
        // Read all FIFO bytes; FIFO holds 9*n bits packed
        uint8_t rx_fifo[72] = {};
        uint8_t rx_bytes = std::min(f1, (uint8_t) 72);
        if (rx_bytes > 0)
          this->read_fifo(rx_fifo, rx_bytes);

        // We need to know how many bits arrived; use FIFO_STATUS2 fifo_lb
        uint8_t fs2 = this->read_register(FIFO_STATUS2);
        uint8_t last_bits = (fs2 >> 1) & 0x07;  // fifo_lb: bits in last byte (0 = full byte)
        uint16_t rx_bits = (uint16_t)(rx_bytes * 8) - (last_bits ? (uint8_t)(8 - last_bits) : 0);

        resp_len = mifare_unpack_parity(rx_fifo, resp, resp_parity, rx_bits);

        this->write_register(ISO14443A_CONF, 0x00);
        return resp_len > 0;
      }
    }
    delay(1);
  }

  this->write_register(ISO14443A_CONF, 0x00);
  return false;
}

// ── mifare_authenticate_ ─────────────────────────────────────────────────────
// Three-pass mutual authentication per ISO 14443-3 / NXP AN10609.
// Protocol flow from mf1_authenticate() (MIT, suut/rfal-mifare-classic).
bool ST25R::mifare_authenticate_(uint8_t block, bool key_b, uint64_t key,
                                  const uint8_t *uid, uint8_t uid_len,
                                  struct Crypto1State *cs) {
  // ── Step 1: send AUTHENT command (plain text, with CRC) ──────────────────
  uint8_t auth_cmd[2] = {(uint8_t)(key_b ? 0x61 : 0x60), block};
  uint8_t nt_raw[4] = {};
  uint8_t nt_len = 0;
  if (!this->transceive_(auth_cmd, 2, nt_raw, nt_len, 20) || nt_len < 4) {
    ESP_LOGW(TAG, "Mifare auth: no NT from tag (block %u)", block);
    return false;
  }

  // ── Step 2: Crypto1 challenge-response ───────────────────────────────────
  uint8_t uid_offset = (uid_len > 4) ? (uint8_t)(uid_len - 4) : 0;
  uint32_t uid_u32 = ((uint32_t) uid[uid_offset]     << 24) |
                     ((uint32_t) uid[uid_offset + 1] << 16) |
                     ((uint32_t) uid[uid_offset + 2] <<  8) |
                      (uint32_t) uid[uid_offset + 3];
  uint32_t nt = ((uint32_t) nt_raw[0] << 24) | ((uint32_t) nt_raw[1] << 16) |
                ((uint32_t) nt_raw[2] <<  8) |  (uint32_t) nt_raw[3];
  ESP_LOGD(TAG, "Mifare auth: NT=%08X UID=%08X", nt, uid_u32);

  crypto1_init(cs, key);
  crypto1_word(cs, nt ^ uid_u32, 0);

  // Choose a fixed nr (reader nonce); any value works for normal auth
  const uint8_t nr[4] = {0x12, 0x34, 0x56, 0x78};
  uint8_t nr_ar[8], nr_ar_par[8];

  // Encrypt NR: feed plaintext NR into LFSR (is_encrypted=0), advance parity bit with crypto1_bit
  for (int i = 0; i < 4; i++) {
    nr_ar[i]     = crypto1_byte(cs, nr[i], 0) ^ nr[i];
    nr_ar_par[i] = crypto1_bit(cs, 0, 0) ^ ODD_PARITY[nr[i]];
  }

  // AR = 4 bytes of prng_successor(NT, 64) MSB-first
  uint32_t ar_plain = prng_successor(nt, 64);
  for (int i = 0; i < 4; i++) {
    uint8_t b = (uint8_t)((ar_plain >> (24 - 8 * i)) & 0xFF);
    nr_ar[4 + i]     = crypto1_byte(cs, 0, 0) ^ b;
    nr_ar_par[4 + i] = crypto1_bit(cs, 0, 0) ^ ODD_PARITY[b];
  }

  // ── Step 3: send nr+ar (encrypted, manual parity, no CRC) ────────────────
  uint8_t at[4] = {}, at_par[4] = {};
  uint8_t at_len = 0;
  if (!this->transceive_mifare_(nr_ar, nr_ar_par, 8, at, at_par, at_len) || at_len < 4) {
    ESP_LOGW(TAG, "Mifare auth: no AT from tag (block %u)", block);
    return false;
  }

  // ── Step 4: verify tag answer ─────────────────────────────────────────────
  uint32_t at_expected = prng_successor(ar_plain, 32) ^ crypto1_word(cs, 0, 0);
  uint32_t at_got = ((uint32_t) at[0] << 24) | ((uint32_t) at[1] << 16) |
                    ((uint32_t) at[2] <<  8) |  (uint32_t) at[3];
  if (at_got != at_expected) {
    ESP_LOGW(TAG, "Mifare auth: AT mismatch (got %08" PRIx32 " expected %08" PRIx32 ")", at_got, at_expected);
    return false;
  }

  ESP_LOGI(TAG, "Mifare auth OK (block %u, key %s)", block, key_b ? "B" : "A");
  return true;
}

// ── mifare_read_block_ ───────────────────────────────────────────────────────
// Read one 16-byte block after a successful mifare_authenticate_().
// Protocol flow from mf1_send_receive_encrypted (MIT, suut/rfal-mifare-classic).
bool ST25R::mifare_read_block_(uint8_t block, uint8_t *data,
                                struct Crypto1State *cs) {
  // Build: READ(0x30) + block + CRC_A — then encrypt all 4 bytes + CRC
  uint8_t cmd[4];
  cmd[0] = 0x30;
  cmd[1] = block;
  uint16_t crc = mifare_crc_a(cmd, 2);
  cmd[2] = (uint8_t)(crc & 0xFF);
  cmd[3] = (uint8_t)(crc >> 8);

  uint8_t enc[4], enc_par[4];
  for (int i = 0; i < 4; i++) {
    enc[i]     = crypto1_byte(cs, 0, 0) ^ cmd[i];
    enc_par[i] = (uint8_t)(crypto1_bit(cs, 0, 0) ^ ODD_PARITY[cmd[i]]);
  }

  // Response: 16 data bytes + 2 CRC bytes = 18 bytes, all encrypted
  uint8_t rx_enc[18] = {}, rx_par[18] = {};
  uint8_t rx_len = 0;
  if (!this->transceive_mifare_(enc, enc_par, 4, rx_enc, rx_par, rx_len) || rx_len < 18) {
    ESP_LOGW(TAG, "Mifare read block %u failed (got %u bytes)", block, rx_len);
    return false;
  }

  // Decrypt and verify parity + CRC
  uint8_t plain[18];
  for (int i = 0; i < 18; i++) {
    plain[i] = crypto1_byte(cs, 0, 0) ^ rx_enc[i];
    uint8_t exp_par = (uint8_t)(crypto1_bit(cs, 0, 0) ^ ODD_PARITY[plain[i]]);
    if (rx_par[i] != exp_par) {
      ESP_LOGW(TAG, "Mifare read block %u: parity error at byte %d", block, i);
      return false;
    }
  }
  uint16_t rx_crc = mifare_crc_a(plain, 16);
  if ((rx_crc & 0xFF) != plain[16] || (rx_crc >> 8) != plain[17]) {
    ESP_LOGW(TAG, "Mifare read block %u: CRC error", block);
    return false;
  }

  memcpy(data, plain, 16);
  return true;
}

// ── iso_dep_activate_ ────────────────────────────────────────────────────────
// Sends RATS (Request for Answer To Select) to activate an ISO 14443-4
// (ISO-DEP / T=CL) session.  FSDI=5 → max frame 64 bytes.
// Returns true and fills ats/ats_len on success.
bool ST25R::iso_dep_activate_(uint8_t *ats, uint8_t &ats_len) {
  // RATS: E0 | (FSDI << 4) | CID — FSDI=5 (64 B), CID=0 (no card identifier)
  const uint8_t rats[2] = {0xE0, 0x50};
  this->iso_dep_block_num_ = 0;
  ats_len = 0;
  if (!this->transceive_(rats, 2, ats, ats_len, 100)) {
    ESP_LOGD(TAG, "ISO-DEP: RATS timed out (no ATS)");
    return false;
  }
  ESP_LOGD(TAG, "ISO-DEP: ATS received (%u bytes)", ats_len);
  return ats_len >= 1;
}

// ── iso_dep_transceive_ ──────────────────────────────────────────────────────
// Sends an APDU wrapped in an ISO 14443-4 I-block (PCB | INF | CRC).
// Strips the PCB byte from the response before returning.
// Block number toggles automatically (0 → 1 → 0 → …).
bool ST25R::iso_dep_transceive_(const uint8_t *apdu, uint8_t apdu_len,
                                 uint8_t *resp, uint8_t &resp_len,
                                 uint32_t timeout_ms) {
  // I-block PCB: bits 7-6 = 00, bit 5 = 0 (no chaining), bit 4 = 0 (no CID),
  //              bit 3 = 0 (no NAD), bit 2 = 1 (required), bit 1 = 0 (RFU),
  //              bit 0 = block number.
  uint8_t pcb = (uint8_t)(0x02 | (this->iso_dep_block_num_ & 0x01));
  this->iso_dep_block_num_ ^= 0x01;

  // Build the frame: PCB + APDU.  Max = 1 + 61 = 62 bytes (FSDI=5 → FSD=64,
  // minus 2 bytes CRC appended by the chip gives 62 bytes payload capacity).
  uint8_t frame[66];
  frame[0] = pcb;
  if (apdu_len > 0)
    memcpy(frame + 1, apdu, apdu_len);

  uint8_t raw[64];
  uint8_t raw_len = 0;
  if (!this->transceive_(frame, (size_t)(1 + apdu_len), raw, raw_len, timeout_ms)) {
    return false;
  }

  // Strip the PCB byte; return only the APDU response (INF).
  if (raw_len < 1)
    return false;
  resp_len = (uint8_t)(raw_len - 1);
  if (resp_len > 0)
    memcpy(resp, raw + 1, resp_len);
  return true;
}

// ── iso_dep_deselect_ ────────────────────────────────────────────────────────
// Sends an S(DESELECT) supervisory block to cleanly close the ISO-DEP session
// and put the PICC back into HALT state.
void ST25R::iso_dep_deselect_() {
  const uint8_t deselect = 0xC2;  // S(DESELECT) PCB, no CID
  uint8_t resp[4];
  uint8_t resp_len = 0;
  // Best-effort — ignore errors; the HALT command that follows will finish the job.
  this->transceive_(&deselect, 1, resp, resp_len, 50);
}

// ── iso_dep_smart_tap_ ───────────────────────────────────────────────────────
// Reads a stable credential from a Google Wallet pass via Google Smart Tap 2.0.
//
// HOW IT WORKS:
//   The pass owner creates a Google Wallet generic pass (free Google Wallet API
//   issuer account) with Smart Tap enabled and a person-specific string as the
//   smartTapRedemptionValue (e.g. "alice@home").  The pass is configured to NOT
//   require reader authentication, so the redemption value is returned in
//   cleartext.  When the phone is tapped, the reader:
//     1. SELECTs the Smart Tap 2.0 application (AID "OSE.GST")
//     2. Sends NEGOTIATE without a reader public key — this signals no-encryption
//     3. Sends GET SMART TAP DATA
//     4. Parses BER-TLV: E2 → E3 → tag 0x82 = redemption value
//
// WHAT FAILS (returns false):
//   • Phone has no Smart-Tap-enabled Google Wallet pass (SW=6A82 on SELECT)
//   • Pass requires merchant authentication  (SW≠90 on NEGOTIATE)
//   • Pass response is encrypted (0x82 absent in cleartext E2>E3)
//
// Passes that require reader authentication need the operator to register with
// Google as a certified Smart Tap reader — out of scope for this DIY component.
bool ST25R::iso_dep_smart_tap_(std::string &token) {
  token.clear();
  uint8_t resp[64];
  uint8_t resp_len;

  // ── Step 1: SELECT OSE.GST ────────────────────────────────────────────────
  // AID = "OSE.GST" = 4F 53 45 2E 47 53 54
  const uint8_t sel_gst[13] = {
    0x00, 0xA4, 0x04, 0x00,
    0x07, 0x4F, 0x53, 0x45, 0x2E, 0x47, 0x53, 0x54,
    0x00
  };
  resp_len = 0;
  if (!this->iso_dep_transceive_(sel_gst, sizeof(sel_gst), resp, resp_len, 300))
    return false;
  if (resp_len < 2 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGV(TAG, "Smart Tap: no pass (SW=%02X%02X)",
             resp_len >= 2 ? resp[resp_len - 2] : 0,
             resp_len >= 1 ? resp[resp_len - 1] : 0);
    return false;
  }
  ESP_LOGD(TAG, "Smart Tap: SELECT OSE.GST OK");

  // ── Step 2: NEGOTIATE SECURE SESSIONS (no reader public key = cleartext) ──
  // TLV structure: E0 (merchant data) → 81 (version 2.0) + 82 (merchant ID,
  // 8 zero bytes = anonymous) + 83 (nonce, 8 bytes).  Omitting tag 0x84
  // (reader public key) requests cleartext response for no-auth passes.
  // Total APDU data: E0 18 | 81 02 01 02 | 82 08 00*8 | 83 08 [nonce]
  //   = 2 + 4 + 10 + 10 = 26 bytes → Lc = 0x1A
  uint8_t neg[31];
  neg[0]  = 0x80; neg[1]  = 0x50;  // CLA INS
  neg[2]  = 0x01; neg[3]  = 0x00;  // P1 = 1 merchant, P2 = 0
  neg[4]  = 0x1A;                   // Lc = 26 bytes
  neg[5]  = 0xE0; neg[6]  = 0x18;  // E0 merchant-data TLV: 24 bytes follow
  neg[7]  = 0x81; neg[8]  = 0x02; neg[9]  = 0x01; neg[10] = 0x02;  // version 2.0
  neg[11] = 0x82; neg[12] = 0x08;  // merchant ID TLV (8 zero bytes = anonymous)
  memset(neg + 13, 0x00, 8);
  neg[21] = 0x83; neg[22] = 0x08;  // nonce TLV (8 bytes)
  // Use ESPHome's random_uint32() for unpredictable nonces.
  for (int i = 0; i < 2; i++) {
    uint32_t r = random_uint32();
    neg[23 + i * 4 + 0] = (uint8_t)(r >> 24);
    neg[23 + i * 4 + 1] = (uint8_t)(r >> 16);
    neg[23 + i * 4 + 2] = (uint8_t)(r >>  8);
    neg[23 + i * 4 + 3] = (uint8_t)(r);
  }
  resp_len = 0;
  if (!this->iso_dep_transceive_(neg, sizeof(neg), resp, resp_len, 500))
    return false;
  if (resp_len < 2 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGD(TAG, "Smart Tap: NEGOTIATE SW=%02X%02X (pass may require merchant auth)",
             resp_len >= 2 ? resp[resp_len - 2] : 0,
             resp_len >= 1 ? resp[resp_len - 1] : 0);
    return false;
  }

  // ── Step 3: GET SMART TAP DATA ────────────────────────────────────────────
  const uint8_t get_data[4] = {0x80, 0xCA, 0x01, 0x00};
  resp_len = 0;
  if (!this->iso_dep_transceive_(get_data, sizeof(get_data), resp, resp_len, 500))
    return false;
  if (resp_len < 2 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGD(TAG, "Smart Tap: GET DATA SW=%02X%02X",
             resp_len >= 2 ? resp[resp_len - 2] : 0,
             resp_len >= 1 ? resp[resp_len - 1] : 0);
    return false;
  }

  // ── Step 4: Parse BER-TLV response: E2 → E3 → 0x82 (redemption value) ───
  const size_t data_len = (size_t)(resp_len - 2);  // strip SW bytes
  size_t e2_len = 0, e3_len = 0, v82_len = 0;
  const uint8_t *e2 = ber_find(resp, data_len, 0xE2, &e2_len);
  if (!e2) { ESP_LOGD(TAG, "Smart Tap: E2 missing in response"); return false; }
  const uint8_t *e3 = ber_find(e2, e2_len, 0xE3, &e3_len);
  if (!e3) { ESP_LOGD(TAG, "Smart Tap: E3 missing in E2"); return false; }
  const uint8_t *v82 = ber_find(e3, e3_len, 0x82, &v82_len);
  if (!v82 || v82_len == 0) {
    ESP_LOGD(TAG, "Smart Tap: redemption value (0x82) absent in E3 — pass is encrypted");
    return false;
  }
  token.assign(reinterpret_cast<const char *>(v82), v82_len);
  ESP_LOGI(TAG, "Smart Tap: redemption value = %s", token.c_str());
  return true;
}

// ── iso_dep_apple_vas_ ───────────────────────────────────────────────────────
// Reads a stable credential from an Apple Wallet pass via Apple VAS (Value
// Added Services) protocol, URL-based mode.
//
// HOW IT WORKS:
//   The pass owner creates an Apple Wallet pass (.pkpass) with an NFC section
//   that uses URL-based VAS.  The pass URL serves as the stable credential.
//   The reader SELECTs the VAS AID ("OSE.VAS") and sends GET VAS DATA without
//   a reader public key, requesting plaintext.  For passes configured without
//   mandatory reader authentication the phone returns the pass URL in a TLV.
//
// WHAT FAILS (returns false):
//   • No Apple Wallet pass with VAS present  (SW=6A82 on SELECT)
//   • Pass requires merchant authentication  (SW≠90 on GET VAS DATA)
//   • VAS response contains no URL tag (0x82) in cleartext
//
// Apple VAS passes need an Apple Developer account ($99/year) or a compatible
// third-party pass issuer.  The pass URL scheme must be registered with Apple.
bool ST25R::iso_dep_apple_vas_(std::string &token) {
  token.clear();
  uint8_t resp[64];
  uint8_t resp_len;

  // ── Step 1: SELECT OSE.VAS ────────────────────────────────────────────────
  // AID = "OSE.VAS" = 4F 53 45 2E 56 41 53
  const uint8_t sel_vas[13] = {
    0x00, 0xA4, 0x04, 0x00,
    0x07, 0x4F, 0x53, 0x45, 0x2E, 0x56, 0x41, 0x53,
    0x00
  };
  resp_len = 0;
  if (!this->iso_dep_transceive_(sel_vas, sizeof(sel_vas), resp, resp_len, 300))
    return false;
  if (resp_len < 2 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGV(TAG, "Apple VAS: no pass (SW=%02X%02X)",
             resp_len >= 2 ? resp[resp_len - 2] : 0,
             resp_len >= 1 ? resp[resp_len - 1] : 0);
    return false;
  }
  ESP_LOGD(TAG, "Apple VAS: SELECT OSE.VAS OK");

  // ── Step 2: GET VAS DATA (URL-mode, no reader public key = cleartext) ─────
  // TLV data: 81 00 (empty URL filter = return all URL-based passes) +
  //           82 10 [16-byte nonce].
  // Omitting tag 0x83 (reader ephemeral public key) requests plaintext.
  uint8_t get_vas[26];
  get_vas[0]  = 0x80; get_vas[1]  = 0xCA;  // CLA INS
  get_vas[2]  = 0x01; get_vas[3]  = 0x00;  // P1 = get VAS data, P2 = 0
  get_vas[4]  = 0x14;                       // Lc = 20
  get_vas[5]  = 0x81; get_vas[6]  = 0x00;  // pass type filter: empty = all URL passes
  get_vas[7]  = 0x82; get_vas[8]  = 0x10;  // nonce TLV (16 bytes)
  // Use ESPHome's random_uint32() for unpredictable nonces.
  for (int i = 0; i < 4; i++) {
    uint32_t r = random_uint32();
    get_vas[9 + i * 4 + 0] = (uint8_t)(r >> 24);
    get_vas[9 + i * 4 + 1] = (uint8_t)(r >> 16);
    get_vas[9 + i * 4 + 2] = (uint8_t)(r >>  8);
    get_vas[9 + i * 4 + 3] = (uint8_t)(r);
  }
  get_vas[25] = 0x00;                       // Le = 0 (return all)
  resp_len = 0;
  // Send with Le byte — pass 26 bytes total including Le
  if (!this->iso_dep_transceive_(get_vas, sizeof(get_vas), resp, resp_len, 500))
    return false;
  if (resp_len < 2 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGD(TAG, "Apple VAS: GET VAS DATA SW=%02X%02X",
             resp_len >= 2 ? resp[resp_len - 2] : 0,
             resp_len >= 1 ? resp[resp_len - 1] : 0);
    return false;
  }

  // ── Step 3: Parse BER-TLV response: E2 → 0x82 (pass URL / data) ─────────
  const size_t data_len = (size_t)(resp_len - 2);
  size_t e2_len = 0, v82_len = 0;
  const uint8_t *e2 = ber_find(resp, data_len, 0xE2, &e2_len);
  if (!e2) { ESP_LOGD(TAG, "Apple VAS: E2 missing in response"); return false; }
  const uint8_t *v82 = ber_find(e2, e2_len, 0x82, &v82_len);
  if (!v82 || v82_len == 0) {
    ESP_LOGD(TAG, "Apple VAS: pass URL (0x82) absent — pass may be encrypted");
    return false;
  }
  token.assign(reinterpret_cast<const char *>(v82), v82_len);
  ESP_LOGI(TAG, "Apple VAS: pass URL = %s", token.c_str());
  return true;
}

// ── read_nfc_type4_ndef_ ─────────────────────────────────────────────────────
// Reads an NDEF message from an NFC Forum Type 4 Tag over an active ISO-DEP
// session.  Protocol per NFC Forum T4T Technical Specification V2.0:
//   1. SELECT NDEF Application (AID D2 76 00 00 85 01 01)
//   2. SELECT CC File (E1 03)
//   3. READ BINARY CC (15 bytes)  — parse NDEF File ID and max size
//   4. SELECT NDEF File
//   5. READ BINARY first 2 bytes  — get NDEF message length
//   6. READ BINARY NDEF data      — in chunks of ≤59 bytes
bool ST25R::read_nfc_type4_ndef_(std::vector<uint8_t> &ndef_data) {
  uint8_t resp[64];
  uint8_t resp_len;

  // ── Step 1: SELECT NDEF Application ────────────────────────────────────────
  // AID: D2 76 00 00 85 01 01 (NFC Forum NDEF Application AID V2.0)
  const uint8_t select_ndef_app[] = {
    0x00, 0xA4, 0x04, 0x00,              // CLA INS P1 P2
    0x07,                                // Lc = 7
    0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01,  // AID
    0x00                                 // Le (return FCI if any)
  };
  resp_len = 0;
  if (!this->iso_dep_transceive_(select_ndef_app, sizeof(select_ndef_app), resp, resp_len)) {
    ESP_LOGD(TAG, "ISO-DEP: SELECT NDEF App timeout");
    return false;
  }
  if (resp_len < 2 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGD(TAG, "ISO-DEP: SELECT NDEF App failed SW=%02X%02X",
             resp_len >= 2 ? resp[resp_len - 2] : 0,
             resp_len >= 1 ? resp[resp_len - 1] : 0);
    return false;
  }

  // ── Step 2: SELECT CC File (File ID = E1 03) ────────────────────────────────
  const uint8_t select_cc[] = {
    0x00, 0xA4, 0x00, 0x0C,  // CLA INS P1(by ID) P2(no response data)
    0x02, 0xE1, 0x03          // Lc=2, File ID
  };
  resp_len = 0;
  if (!this->iso_dep_transceive_(select_cc, sizeof(select_cc), resp, resp_len)) {
    ESP_LOGD(TAG, "ISO-DEP: SELECT CC file timeout");
    return false;
  }
  if (resp_len < 2 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGD(TAG, "ISO-DEP: SELECT CC file failed SW=%02X%02X",
             resp_len >= 2 ? resp[resp_len - 2] : 0,
             resp_len >= 1 ? resp[resp_len - 1] : 0);
    return false;
  }

  // ── Step 3: READ BINARY CC (15 bytes) ──────────────────────────────────────
  const uint8_t read_cc[] = {0x00, 0xB0, 0x00, 0x00, 0x0F};  // offset=0, Le=15
  resp_len = 0;
  if (!this->iso_dep_transceive_(read_cc, sizeof(read_cc), resp, resp_len)) {
    ESP_LOGD(TAG, "ISO-DEP: READ CC timeout");
    return false;
  }
  // Expect 15 CC bytes + 2 SW bytes = 17 bytes total
  if (resp_len < 17 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGD(TAG, "ISO-DEP: READ CC failed (resp_len=%u)", resp_len);
    return false;
  }

  // Parse CC: byte[7]=NDEF TLV tag (0x04), byte[8]=TLV length (≥6),
  //           bytes[9-10]=NDEF File ID, bytes[11-12]=max NDEF size.
  if (resp[7] != 0x04 || resp[8] < 0x06) {
    ESP_LOGD(TAG, "ISO-DEP: CC NDEF File Control TLV not found (T=%02X L=%02X)", resp[7], resp[8]);
    return false;
  }
  const uint8_t ndef_file_id[2] = {resp[9], resp[10]};
  const uint16_t ndef_max_size = (uint16_t)(resp[11] << 8 | resp[12]);

  // ── Step 4: SELECT NDEF File ────────────────────────────────────────────────
  uint8_t select_ndef[7] = {
    0x00, 0xA4, 0x00, 0x0C, 0x02,
    ndef_file_id[0], ndef_file_id[1]
  };
  resp_len = 0;
  if (!this->iso_dep_transceive_(select_ndef, sizeof(select_ndef), resp, resp_len)) {
    ESP_LOGD(TAG, "ISO-DEP: SELECT NDEF file timeout");
    return false;
  }
  if (resp_len < 2 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGD(TAG, "ISO-DEP: SELECT NDEF file failed SW=%02X%02X",
             resp_len >= 2 ? resp[resp_len - 2] : 0,
             resp_len >= 1 ? resp[resp_len - 1] : 0);
    return false;
  }

  // ── Step 5: READ BINARY — first 2 bytes = NDEF message length ──────────────
  const uint8_t read_nlen[] = {0x00, 0xB0, 0x00, 0x00, 0x02};
  resp_len = 0;
  if (!this->iso_dep_transceive_(read_nlen, sizeof(read_nlen), resp, resp_len)) {
    ESP_LOGD(TAG, "ISO-DEP: READ NDEF length timeout");
    return false;
  }
  if (resp_len < 4 || resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
    ESP_LOGD(TAG, "ISO-DEP: READ NDEF length failed (resp_len=%u)", resp_len);
    return false;
  }
  const uint16_t ndef_len = (uint16_t)(resp[0] << 8 | resp[1]);
  if (ndef_len == 0 || ndef_len > ndef_max_size) {
    ESP_LOGD(TAG, "ISO-DEP: NDEF length invalid (len=%u max=%u)", ndef_len, ndef_max_size);
    return false;
  }

  // ── Step 6: READ BINARY — NDEF data in chunks ──────────────────────────────
  // Max bytes per chunk: FSD(64) - PCB(1) - CRC(2) - SW(2) = 59 bytes of data.
  ndef_data.clear();
  ndef_data.reserve(ndef_len);
  uint16_t offset = 2;       // skip the 2-byte NDEF length field
  uint16_t remaining = ndef_len;
  while (remaining > 0) {
    const uint8_t chunk = (uint8_t)((remaining > 59) ? 59 : remaining);
    uint8_t read_cmd[5] = {
      0x00, 0xB0,
      (uint8_t)((offset >> 8) & 0x7F),  // P1: high offset bits (bit7 must be 0)
      (uint8_t)(offset & 0xFF),          // P2: low offset byte
      chunk                              // Le
    };
    resp_len = 0;
    if (!this->iso_dep_transceive_(read_cmd, sizeof(read_cmd), resp, resp_len)) {
      ESP_LOGD(TAG, "ISO-DEP: READ NDEF data timeout (offset=%u)", offset);
      return false;
    }
    if (resp_len < (uint8_t)(chunk + 2) ||
        resp[resp_len - 2] != 0x90 || resp[resp_len - 1] != 0x00) {
      ESP_LOGD(TAG, "ISO-DEP: READ NDEF data failed (offset=%u resp_len=%u)", offset, resp_len);
      return false;
    }
    ndef_data.insert(ndef_data.end(), resp, resp + chunk);
    offset += chunk;
    remaining -= chunk;
  }

  ESP_LOGI(TAG, "ISO-DEP: NDEF message read successfully (%u bytes)", ndef_len);
  return true;
}

std::unique_ptr<nfc::NfcTag> ST25R::read_tag_(std::vector<uint8_t> &uid, uint8_t sak) {
  nfc::NfcTagUid nfc_uid(uid.begin(), uid.end());

  // ── ISO-DEP (NFC Forum Type 4 / ISO 14443-4) ─────────────────────────────
  // SAK bit 5 (0x20) = T=CL: device supports ISO-DEP.  This covers ISO 14443-4
  // smart-cards, DESFire, Java Card devices, and phones running a dedicated
  // Android HCE app that registers the NFC Forum T4T AID (D2 76 00 00 85 01 01).
  //
  // NOTE: standard Apple Wallet, Google Pay, and most payment apps use proprietary
  // protocols (Apple VAS / Google Smart Tap / EMVCo) and will NOT respond to the
  // NFC Forum T4T NDEF AID.  They will activate ISO-DEP (respond to RATS) but
  // return SW=6A82 (Application Not Found) for the SELECT below.  The reader
  // handles that gracefully — iso_dep_token_ stays empty, caller falls back to UID.
  //
  // For a phone to serve NDEF to this reader the user must install a dedicated
  // Android HCE app and pre-configure a credential string in that app.  See
  // examples/example-wallet.yaml for setup instructions.
  if (sak & 0x20) {
    ESP_LOGI(TAG, "ISO-DEP device detected (SAK=0x%02X), activating ISO 14443-4", sak);
    uint8_t ats[64];
    uint8_t ats_len = 0;
    if (this->iso_dep_activate_(ats, ats_len)) {
      std::vector<uint8_t> ndef_data;
      if (this->read_nfc_type4_ndef_(ndef_data) && !ndef_data.empty()) {
        // Build the NfcTag with parsed NDEF records so we can inspect the content.
        auto tag = make_unique<nfc::NfcTag>(nfc_uid, NFC_FORUM_TYPE_4, ndef_data);

        // Derive a stable, human-readable token from the NDEF records.
        // Priority order:
        //   1. Home Assistant tag UUID — extracted from the HA NDEF URL record
        //      (https://www.home-assistant.io/tag/<UUID>).  The token is just
        //      the UUID (e.g. "abc12345-0000-1234-abcd-ef1234567890").
        //   2. First-record payload — URI or plain-text string from the first NDEF
        //      record.  Covers Android HCE apps configured with a URI or text record.
        //   3. Raw-bytes hex fallback — used when the NDEF records have no
        //      printable payload (e.g. custom binary external records).
        this->iso_dep_token_.clear();

        if (nfc::has_ha_tag_ndef(*tag)) {
          // NDEF contains the HA tag URL — use just the UUID portion.
          this->iso_dep_token_ = nfc::get_ha_tag_ndef(*tag);
          ESP_LOGI(TAG, "ISO-DEP: HA tag UUID: %s", this->iso_dep_token_.c_str());
        } else if (tag->has_ndef_message()) {
          const auto &records = tag->get_ndef_message()->get_records();
          if (!records.empty()) {
            // get_payload() returns the full URI for URI records and the text
            // string for Text records — either is human-readable and stable.
            this->iso_dep_token_ = records[0]->get_payload();
            if (!this->iso_dep_token_.empty()) {
              ESP_LOGI(TAG, "ISO-DEP: NDEF payload token: %s", this->iso_dep_token_.c_str());
            }
          }
        }

        // Hex fallback: guarantees a non-empty token when NDEF data exists but
        // no printable payload could be extracted from the parsed records.
        if (this->iso_dep_token_.empty()) {
          this->iso_dep_token_.resize(ndef_data.size() * 2);
          for (size_t i = 0; i < ndef_data.size(); i++) {
            snprintf(&this->iso_dep_token_[i * 2], 3, "%02X", ndef_data[i]);
          }
          ESP_LOGI(TAG, "ISO-DEP: hex token: %.40s%s",
                   this->iso_dep_token_.c_str(),
                   this->iso_dep_token_.size() > 40 ? "..." : "");
        }

        this->iso_dep_deselect_();
        return tag;
      }

      // NFC Forum T4T NDEF not available.  The ISO-DEP session is still active —
      // try the wallet protocols in order before giving up.
      //
      // Order: Google Smart Tap → Apple VAS → fall back to UID.
      // The ISO-DEP block number continues toggling correctly across all attempts
      // because iso_dep_transceive_() always increments it, and each new SELECT
      // starts a fresh application context within the same ISO-DEP session.

      // ── Google Wallet (Smart Tap 2.0) ──────────────────────────────────────
      std::string wallet_token;
      if (this->iso_dep_smart_tap_(wallet_token) && !wallet_token.empty()) {
        this->iso_dep_token_ = wallet_token;
        this->iso_dep_deselect_();
        return make_unique<nfc::NfcTag>(nfc_uid, NFC_FORUM_TYPE_4);
      }

      // ── Apple Wallet (VAS) ─────────────────────────────────────────────────
      if (this->iso_dep_apple_vas_(wallet_token) && !wallet_token.empty()) {
        this->iso_dep_token_ = wallet_token;
        this->iso_dep_deselect_();
        return make_unique<nfc::NfcTag>(nfc_uid, NFC_FORUM_TYPE_4);
      }

      // No wallet credential readable — device either has payment-only passes
      // (require merchant auth) or is a DESFire/smart-card with custom apps.
      ESP_LOGD(TAG, "ISO-DEP: no T4T NDEF, Smart Tap, or VAS credential available");
      this->iso_dep_deselect_();
    } else {
      ESP_LOGW(TAG, "ISO-DEP: activation failed despite T=CL SAK");
    }
    // No stable NDEF token — iso_dep_token_ stays empty; caller falls back to UID.
    return make_unique<nfc::NfcTag>(nfc_uid, NFC_FORUM_TYPE_4);
  }

  uint8_t type = nfc::guess_tag_type(uid.size());
  ESP_LOGI(TAG, "read_tag_: UID length=%zu, guessed type=%d", uid.size(), type);

  if (type == nfc::TAG_TYPE_MIFARE_CLASSIC) {
    ESP_LOGI(TAG, "Mifare Classic detected - attempting authentication");
    struct Crypto1State cs = {};
    bool auth_ok = this->mifare_authenticate_(0, false, this->mifare_key_a_,
                                              uid.data(), (uint8_t) uid.size(), &cs);
    if (!auth_ok) {
      ESP_LOGW(TAG, "Mifare Classic: sector 0 auth failed (wrong key or clone card)");
      return make_unique<nfc::NfcTag>(nfc_uid, nfc::MIFARE_CLASSIC);
    }

    ESP_LOGI(TAG, "Mifare Classic: Auth successful, reading blocks 1 and 2");
    // Read blocks 1 and 2 (block 0 is manufacturer data; block 3 is sector trailer)
    uint8_t block1[16] = {}, block2[16] = {};
    bool b1 = this->mifare_read_block_(1, block1, &cs);
    bool b2 = b1 && this->mifare_read_block_(2, block2, &cs);

    if (!b1) {
      ESP_LOGW(TAG, "Mifare Classic: block read failed");
      return make_unique<nfc::NfcTag>(nfc_uid, nfc::MIFARE_CLASSIC);
    }

    ESP_LOGI(TAG, "Block 1: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
             block1[0],block1[1],block1[2],block1[3],block1[4],block1[5],block1[6],block1[7],
             block1[8],block1[9],block1[10],block1[11],block1[12],block1[13],block1[14],block1[15]);
    ESP_LOGI(TAG, "Block 2: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
             block2[0],block2[1],block2[2],block2[3],block2[4],block2[5],block2[6],block2[7],
             block2[8],block2[9],block2[10],block2[11],block2[12],block2[13],block2[14],block2[15]);

    // Look for NFC Forum Type 2 NDEF TLV (0x03) in the data area
    // On Mifare Classic the NDEF data starts at block 1 byte 0 when
    // the card is formatted as NFC Forum Type 2 / Mifare Classic NDEF.
    std::vector<uint8_t> raw;
    raw.insert(raw.end(), block1, block1 + 16);
    if (b2) raw.insert(raw.end(), block2, block2 + 16);

    size_t idx = 0;
    while (idx < raw.size()) {
      uint8_t tlv = raw[idx++];
      if (tlv == 0xFE) break;      // terminator
      if (tlv == 0x00) continue;   // null
      if (idx >= raw.size()) break;
      uint8_t tlen = raw[idx++];
      if (tlv == 0x03 && tlen > 0 && (idx + tlen) <= raw.size()) {
        std::vector<uint8_t> ndef_data(raw.begin() + (int) idx, raw.begin() + (int) idx + tlen);
        ESP_LOGI(TAG, "Mifare Classic: NDEF found (%u bytes)", tlen);
        return make_unique<nfc::NfcTag>(nfc_uid, nfc::MIFARE_CLASSIC, ndef_data);
      }
      idx += tlen;
    }

    ESP_LOGD(TAG, "Mifare Classic: no NDEF TLV in sector 0 data blocks");
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
          ESP_LOGD(TAG, "  Found NDEF TLV at index %zu", i);
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
                ESP_LOGD(TAG, "  Found NDEF TLV at index %zu", i);
                break;
              }
              if (data[i] == 0xFE) {
                terminator_found = true;
                ESP_LOGD(TAG, "  Found Terminator TLV (0xFE) at index %zu", i);
                break;
              }
            }
          }
          if (found || terminator_found) break;
        }
      }

      if (found) {
        // Ensure we have enough bytes to read the full TLV length field
        // (3-byte length needs tlv_index + 3 to be valid)
        while (data.size() <= tlv_index + 3) {
          read_cmd[1] = (uint8_t)(data.size() / 4);
          delay(10);
          if (!this->transceive_(read_cmd, 2, buffer, len) || len < 16) break;
          data.insert(data.end(), buffer, buffer + 16);
        }

        if (tlv_index + 1 < data.size()) {
          size_t msg_len;
          size_t msg_start_idx;
          if (data[tlv_index + 1] == 0xFF && tlv_index + 3 < data.size()) {
            // 3-byte TLV length (BER-TLV extended form) for payloads ≥255 bytes
            msg_len = ((size_t) data[tlv_index + 2] << 8) | data[tlv_index + 3];
            msg_start_idx = tlv_index + 4;
          } else {
            msg_len = data[tlv_index + 1];
            msg_start_idx = tlv_index + 2;
          }
          ESP_LOGD(TAG, "  NDEF message length: %zu", msg_len);

          while (data.size() < msg_start_idx + msg_len) {
            uint8_t next_page = (uint8_t)(data.size() / 4);
            read_cmd[1] = next_page;
            delay(10);
            if (!this->transceive_(read_cmd, 2, buffer, len) || len < 16) {
              ESP_LOGW(TAG, "  Failed to read page %d during NDEF fetch", next_page);
              break;
            }
            data.insert(data.end(), buffer, buffer + 16);
          }

          if (data.size() >= msg_start_idx + msg_len) {
            std::vector<uint8_t> ndef_data(data.begin() + (int) msg_start_idx,
                                           data.begin() + (int) msg_start_idx + (int) msg_len);
            ESP_LOGI(TAG, "  Successfully read NDEF message of %zu bytes", msg_len);
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

  return make_unique<nfc::NfcTag>(nfc_uid);
}

void ST25R::loop() {
  if (this->is_failed()) return;

  if (this->irq_triggered_) {
    this->irq_triggered_ = false;
    this->irq_status_ = this->read_register(IRQ_MAIN);
    ESP_LOGV(TAG, "IRQ triggered, status: 0x%02X, state: %d", this->irq_status_, this->state_);
  } else if (this->state_ == STATE_WUPA || this->state_ == STATE_ANTICOL || this->state_ == STATE_SELECT) {
    // Fallback polling — ISR rising edge may not fire if IRQ pin was already high
    this->irq_status_ = this->read_register(IRQ_MAIN);
    if (this->irq_status_ != 0) {
      ESP_LOGV(TAG, "IRQ polled, status: 0x%02X, state: %d", this->irq_status_, this->state_);
    }
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
          if (!this->anticol_resume_) {
            // Fresh scan: start anticol from beginning
            this->anticol_prefix_full_ = 0;
            this->anticol_prefix_bits_ = 0;
            this->anticol_col_pos_ = 0;
            this->anticol_prefix_val_ = 0;
          }
          // anticol_resume_ is cleared inside: use saved prefix for this one anticol round
          this->anticol_resume_ = false;

          this->send_anticol_frame_();
          this->state_ = STATE_ANTICOL;
          this->last_state_change_ = millis();
      } else if (millis() - this->last_state_change_ > 100) {
          uint8_t irq_t = this->read_register(IRQ_TIMER);
          uint8_t irq_e = this->read_register(IRQ_ERROR);
          this->write_command(ST25R_CMD_MEASURE_AMPLITUDE);
          uint8_t amp = this->read_register(AD_CONV_RESULT);
          uint8_t op = this->read_register(OP_CONTROL);
          ESP_LOGD(TAG, "WUPA timeout: IRQ_MAIN=0x%02X IRQ_TIMER=0x%02X IRQ_ERR=0x%02X FIFO=%u AMP=%u OP=0x%02X",
                   this->irq_status_, irq_t, irq_e, this->read_register(FIFO_STATUS1), amp, op);
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
          // Send WUPA before each new prefix attempt — some cards (e.g. Mifare Classic) leave
          // the READY state quickly after responding to an anticol they don't match.
          this->write_command(ST25R_CMD_CLEAR_FIFO);
          this->read_register(IRQ_MAIN);
          this->read_register(IRQ_TIMER);
          this->read_register(IRQ_ERROR);
          this->irq_triggered_ = false;
          this->anticol_resume_ = true;
          this->write_command(ST25R_CMD_TRANSMIT_WUPA);
          this->state_ = STATE_WUPA;
          this->last_state_change_ = millis();
          return;
        }
        this->state_ = STATE_IDLE;
        this->finalize_scan_();
        return;
      }

      if (this->irq_status_ != 0 && (this->irq_status_ & (IRQ_RXE | IRQ_COL | IRQ_TXE))) {
        delay(5);
        uint8_t f1 = this->read_register(FIFO_STATUS1);
        bool has_collision = (this->irq_status_ & IRQ_COL) != 0;

        if (has_collision) {
          // Read collision position from COLLISION_DISPLAY (0x20)
          uint8_t col_raw = this->read_register(COLLISION_DISPLAY);
          uint8_t c_byte = (col_raw >> 4) & 0x0F;
          uint8_t c_bit  = (col_raw >> 1) & 0x07;
          // col_pos_abs is from start of TX frame (SEL + NVB = 2 bytes = 16 bits)
          int uid_col_pos = (int)(c_byte * 8 + c_bit) - 16;
          if (uid_col_pos < 0) uid_col_pos = 0;
          // Drain any garbage FIFO bytes
          if (f1 > 0) { uint8_t tmp[8]; this->read_fifo(tmp, std::min(f1, (uint8_t)8)); }

          // FIFO bytes during collision are unreliable — brute-force all 2^(col_pos+1) prefixes
          this->anticol_col_pos_ = uid_col_pos;
          this->anticol_prefix_val_ = 0;
          this->apply_anticol_prefix_();

          this->send_anticol_frame_();
          this->last_state_change_ = millis();

        } else if (f1 >= 5) {
          // Clean response — full UID received
          uint8_t resp[5];
          this->read_fifo(resp, 5);

          // ST25R3916 FIFO behaviour with prefix bits:
          // The tag only sends the bits NOT covered by the prefix. The FIFO stores the
          // tag's response bits with zeros in the first anticol_prefix_bits_ positions.
          // Reconstruct the full UID by OR-ing the prefix bits back in.
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

          if (full_uid[0] == 0x88) {
            for (int i = 1; i < 4; i++) {
              char buf[3]; sprintf(buf, "%02X", full_uid[i]); this->current_uid_ += buf;
            }
          } else {
            for (int i = 0; i < 4; i++) {
              char buf[3]; sprintf(buf, "%02X", full_uid[i]); this->current_uid_ += buf;
            }
          }

          this->write_register(ISO14443A_CONF, 0x00);  // clear antcl — SELECT uses CRC
          uint8_t sak_buf[3];
          uint8_t sak_len = 0;
          if (!this->transceive_(sel_pk, 7, sak_buf, sak_len) || sak_len == 0) {
            ESP_LOGW(TAG, "SELECT failed (no SAK)");
            this->state_ = STATE_IDLE;
            this->finalize_scan_();
            return;
          }
          uint8_t sak = sak_buf[0];

          if (sak & 0x04) {  // Cascade bit — need another anticollision level
            // Save CL1 collision state before overwriting for CL2
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
            // Tag fully selected — validate UID length (must be 4 or 7 bytes; 3-byte = CL1 glitch)
            size_t uid_bytes_len = this->current_uid_.length() / 2;
            if (uid_bytes_len != 4 && uid_bytes_len != 7) {
              ESP_LOGW(TAG, "Discarding invalid UID len=%zu (%s)", uid_bytes_len, this->current_uid_.c_str());
              this->state_ = STATE_IDLE;
              this->finalize_scan_();
              return;
            }

            ESP_LOGI(TAG, "Tag selected: %s SAK=0x%02X", this->current_uid_.c_str(), sak);

            // For ISO-DEP devices (phones, smart-cards), always call read_tag_() so
            // we can activate ISO 14443-4 and read the NDEF token that serves as a
            // stable identifier even when the phone's UID is randomised each tap.
            // For other devices only read on first detection.
            bool is_iso_dep = (sak & 0x20) != 0;
            std::string tag_key = this->current_uid_;

            if (is_iso_dep || !this->present_tags_.count(this->current_uid_)) {
              std::vector<uint8_t> uid_bytes;
              for (size_t i = 0; i < this->current_uid_.length(); i += 2)
                uid_bytes.push_back((uint8_t) strtol(this->current_uid_.substr(i, 2).c_str(), nullptr, 16));
              this->iso_dep_token_.clear();
              auto tag_data = this->read_tag_(uid_bytes, sak);
              // If read_tag_() derived a stable NDEF token (ISO-DEP path), use it as
              // the tracking key so the same phone is not treated as a new tag on
              // every scan despite having a different random UID each time.
              if (!this->iso_dep_token_.empty()) {
                tag_key = this->iso_dep_token_;
                this->iso_dep_token_.clear();
              }
              this->tags_data_[tag_key] = std::move(tag_data);
            }

            this->tags_this_scan_.insert(tag_key);

            // HALT: send [0x50, 0x00] + CRC; tag has no response. Don't use
            // transceive_() here — it blocks 150ms waiting for a non-existent SAK.
            {
              uint8_t halt_cmd[2] = {0x50, 0x00};
              this->write_command(ST25R_CMD_CLEAR_FIFO);
              this->read_register(IRQ_MAIN);
              this->read_register(IRQ_TIMER);
              this->read_register(IRQ_ERROR);
              this->write_fifo(halt_cmd, 2);
              this->write_register(NUM_TX_BYTES1, 0x00);
              this->write_register(NUM_TX_BYTES2, 0x10);  // 2 bytes
              this->write_command(ST25R_CMD_TRANSMIT_WITH_CRC);
              delay(10);  // wait for HALT frame to be transmitted (~2ms for 4 bytes)
            }

            // Determine the CL1 collision state so we can resume the multi-tag tree traversal.
            // If we went through cascade (CL2), restore the saved CL1 state.
            // Otherwise use the current CL1 state directly.
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

            // STOP_ALL to reset chip RX state
            this->write_command(ST25R_CMD_CLEAR_FIFO);
            this->read_register(IRQ_MAIN);
            this->read_register(IRQ_TIMER);
            this->read_register(IRQ_ERROR);
            this->irq_triggered_ = false;

            if (can_resume) {
              // Advance to the next branch in the collision tree
              this->cascade_level_ = 0;
              this->current_uid_ = "";
              this->anticol_col_pos_ = resume_col_pos;
              this->anticol_prefix_val_ = resume_prefix_val + 1;
              this->apply_anticol_prefix_();

              uint8_t max_val = (1 << (resume_col_pos + 1)) - 1;
              if (this->anticol_prefix_val_ > max_val) {
                // All branches at this collision level exhausted — done
                this->state_ = STATE_IDLE;
                this->finalize_scan_();
                return;
              }
              // Send WUPA (not REQA) so all tags — including those in HALT — wake up.
              // Some cards (e.g. Mifare Classic) return to HALT after a non-matching SELECT,
              // so REQA would not wake them.
              this->anticol_resume_ = true;
              this->write_command(ST25R_CMD_TRANSMIT_WUPA);
            } else {
              // No prior collision: this was the only tag — scan complete
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
  }
}

void ST25R::finalize_scan_() {
  ESP_LOGD(TAG, "finalize_scan_: this_scan=%zu present=%zu", this->tags_this_scan_.size(), this->present_tags_.size());
  // Increment miss counters for tags not seen this scan; fire on_tag_removed when threshold reached
  std::vector<std::string> to_remove;
  for (auto &kv : this->present_tags_) {
    if (this->tags_this_scan_.count(kv.first)) {
      kv.second = 0;  // seen this scan — reset miss counter
    } else {
      kv.second++;
      if (kv.second >= this->miss_threshold_) {
        to_remove.push_back(kv.first);
      }
    }
  }
  for (const auto &uid : to_remove) {
    ESP_LOGI(TAG, "Tag Removed: %s", uid.c_str());

    std::vector<uint8_t> uid_bytes;
    for (size_t i = 0; i < uid.length(); i += 2) {
      uid_bytes.push_back((uint8_t) strtol(uid.substr(i, 2).c_str(), nullptr, 16));
    }
    nfc::NfcTagUid nfc_uid(uid_bytes.begin(), uid_bytes.end());
    nfc::NfcTag nfc_tag(nfc_uid);
    for (auto *listener : this->tag_listeners_) {
      listener->tag_off(nfc_tag);
    }
    for (auto *trigger : this->on_tag_removed_triggers_) {
      trigger->trigger(uid);
    }
    this->tags_data_.erase(uid);
    this->present_tags_.erase(uid);
  }

  // Fire on_tag for newly seen UIDs
  for (const auto &uid : this->tags_this_scan_) {
    if (!this->present_tags_.count(uid)) {
      ESP_LOGD(TAG, "finalize_scan_: NEW tag %s, firing %zu on_tag triggers", uid.c_str(), this->on_tag_triggers_.size());
      this->present_tags_[uid] = 0;
      for (auto *trigger : this->on_tag_triggers_) {
        trigger->trigger(uid);
      }
      // Fire tag_on for NFC listeners (e.g. ndef_write action)
      if (this->tags_data_.count(uid) && this->tags_data_[uid]) {
        for (auto *listener : this->tag_listeners_) {
          listener->tag_on(*this->tags_data_[uid]);
        }
      }
    }
  }

  // Update binary sensors
  for (auto *obj : this->binary_sensors_) {
    for (const auto &uid : this->tags_this_scan_) {
      obj->process(uid);
    }
    obj->on_scan_end();
  }

  this->tags_this_scan_.clear();
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
  uint8_t chip_type = ic_identity & 0xF8;
  if (chip_type != 0x28 && chip_type != 0x30) {
    ESP_LOGE(TAG, "  reset_: IC identity mismatch! Expected 0x28/0x30, got 0x%02X", chip_type);
    return false;
  }
  bool is_b_version = (chip_type == 0x30);
  this->is_b_version_ = is_b_version;
  ESP_LOGI(TAG, "IC identity match: 0x%02X (ST25R3916%s)", ic_identity, is_b_version ? "B" : "");

  ESP_LOGV(TAG, "  reset_: Enabling Ready mode");
  this->write_register(OP_CONTROL, 0x80); // en=1: Ready mode (enable oscillator and regulators)
  delay(10); // Wait for oscillator to stabilize

  ESP_LOGV(TAG, "  reset_: Configuring registers");
  this->write_register(IO_CONF1, 0x00);  // single=0: differential antenna driving (full power)
  this->write_register(IO_CONF2, this->supply_3v3_ ? 0x80 : 0x00); 
  this->write_register(MODE, 0x08); 
  this->write_register(BIT_RATE, 0x00); 
  this->write_register(RX_CONF1, 0x00); 
  this->write_register(RX_CONF2, 0x6C); // AGC enabled during complete receive period
  this->write_register(RX_CONF3, 0x00); // 0 dB (Full gain), no boost
  this->write_register(MASK_MAIN, 0x00);   // unmask all main IRQs
  this->write_register(MASK_TIMER, 0x00);  // unmask all timer IRQs (NRE etc)
  this->write_register(ISO14443A_CONF, 0x00);

  uint8_t d_res = (15 - this->rf_power_) & 0x0F;
  // am_mod (bits[7:4]) MUST be 0 for ISO14443A — 100% ASK (OOK) required; tags cannot demodulate REQA/WUPA with partial AM.
  // Do NOT set am_mod=7 (0x70|d_res): that is for ISO14443B (type B uses ~10% ASK). Applies equally to non-B and B chip variants.
  this->write_register(TX_DRIVER_CONF, d_res);
  // ANT_TUNE_A/B (0x26/0x27): DAC outputs to external varicap capacitors for antenna resonance tuning.
  // V_AAT = (0.044 + 0.868 * value / 255) * VDD_A; 0x80 = mid-range (chip default).
  // Increase to raise varicap voltage → reduce capacitance → shift resonance higher.
  // Decrease to lower voltage → increase capacitance → shift resonance lower.
  this->write_register(ANT_TUNE_A, this->ant_tune_a_);
  this->write_register(ANT_TUNE_B, this->ant_tune_b_);

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

void ST25R::apply_anticol_prefix_() {
  // Decode anticol_prefix_val_ (bit N..0) into prefix arrays
  // anticol_col_pos_ = N: prefix covers bits 0..N (N+1 bits total)
  // bit position i of prefix = (anticol_prefix_val_ >> i) & 1
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

void ST25R::send_anticol_frame_() {
  uint8_t sel_cmds[] = {0x93, 0x95, 0x97};
  uint8_t sel = sel_cmds[this->cascade_level_];

  // NVB: high nibble = complete bytes in frame (SEL + NVB + complete UID prefix bytes only)
  //      low nibble  = partial bits (0 = full bytes only)
  // NOTE: partial byte is NOT counted in high nibble — it goes into FIFO but NVB only counts complete bytes
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

  // NUM_TX_BYTES: N full bytes + B partial bits (B>0 means one extra partial byte is in FIFO)
  // N = SEL + NVB + complete UID prefix bytes only (NOT counting the partial byte)
  uint8_t ntx_n = 2 + this->anticol_prefix_full_;
  uint8_t ntx_b = this->anticol_prefix_bits_;

  this->write_register(ISO14443A_CONF, 0x01);  // antcl=1
  this->write_command(ST25R_CMD_CLEAR_FIFO);
  this->read_register(IRQ_MAIN);    // clear all IRQ registers so IRQ pin goes low
  this->read_register(IRQ_TIMER);   // IRQ pin stays high until ALL pending bits are cleared
  this->read_register(IRQ_ERROR);
  this->irq_triggered_ = false;
  this->write_fifo(frame, frame_len);
  this->write_register(NUM_TX_BYTES1, ntx_n >> 5);
  this->write_register(NUM_TX_BYTES2, ((ntx_n & 0x1F) << 3) | (ntx_b & 0x07));
  this->write_command(ST25R_CMD_TRANSMIT_WITHOUT_CRC);

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

  ESP_LOGD(TAG, "Writing NDEF message, total size with TLVs: %zu", payload.size());

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
  uint8_t ic_id = this->read_register(IC_IDENTITY);
  ESP_LOGCONFIG(TAG, "  IC Identity (live read): 0x%02X (chip_type=0x%02X)", ic_id, ic_id & 0xF8);
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
