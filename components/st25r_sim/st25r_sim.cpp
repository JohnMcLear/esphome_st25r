/*
 * ST25RSim — extended software simulation of the ST25R3916 NFC reader IC.
 *
 * Supports:
 *   • ISO 14443-A anticollision / select (4-byte and 7-byte cascade UIDs)
 *   • Multiple simultaneous virtual tags with collision tree traversal
 *   • Tag types: Mifare Classic 1K/4K, NTAG213/215/216, Mifare Ultralight
 *   • Full Mifare Classic 3-pass Crypto1 authentication (real NT→NR+AR→AT)
 *   • Mifare Classic block reads (encrypted, with CRC)
 *   • NDEF TLV in Mifare sector-0 blocks and NTAG page reads
 *   • Unix domain socket for ADD_TAG / REMOVE_TAG / SET_KEY / SET_NDEF / LIST
 *
 * Socket protocol:
 *   ADD_TAG <uid_hex> [TYPE=<name>] [KEY_A=<hex>] [KEY_B=<hex>] [NDEF=<hex>]
 *   REMOVE_TAG <uid_hex>
 *   SET_KEY <uid_hex> A|B <key_hex>
 *   SET_NDEF <uid_hex> <ndef_hex>
 *   LIST
 *
 * Type names: MIFARE_1K, MIFARE_4K, NTAG213, NTAG215, NTAG216, ULTRALIGHT
 */

#include "st25r_sim.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace esphome {
namespace st25r_sim {

static const char *const TAG = "st25r_sim";

// ── IRQ bits ──────────────────────────────────────────────────────────────────
static constexpr uint8_t IRQ_RXE = 0x10;
static constexpr uint8_t IRQ_TXE = 0x08;
static constexpr uint8_t IRQ_COL = 0x04;

// ── Register addresses ────────────────────────────────────────────────────────
static constexpr uint8_t REG_IC_IDENTITY       = 0x3F;
static constexpr uint8_t REG_IRQ_MAIN          = 0x1A;
static constexpr uint8_t REG_IRQ_TIMER         = 0x1B;
static constexpr uint8_t REG_IRQ_ERROR         = 0x1C;
static constexpr uint8_t REG_FIFO_STATUS1      = 0x1E;
static constexpr uint8_t REG_FIFO_STATUS2      = 0x1F;
static constexpr uint8_t REG_COLLISION_DISPLAY = 0x20;
static constexpr uint8_t REG_AD_CONV_RESULT    = 0x25;
static constexpr uint8_t REG_TX_DRIVER_CONF    = 0x28;

// ── Odd-parity lookup (identical to st25r.cpp) ────────────────────────────────
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

// ── Mifare CRC-A ──────────────────────────────────────────────────────────────
static uint16_t mifare_crc_a(const uint8_t *data, size_t len) {
  uint16_t crc = 0x6363;
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i] ^ (uint8_t)(crc & 0xFF);
    b ^= b << 4;
    crc = (crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ ((uint16_t)b >> 4);
  }
  return crc;
}

// ── 9-bit parity pack/unpack (same algorithm as st25r.cpp) ───────────────────
static void sim_pack_parity(const uint8_t *in, const uint8_t *par,
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

static uint8_t sim_unpack_parity(const uint8_t *in, uint8_t *out,
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

// ── Socket helpers ────────────────────────────────────────────────────────────
static std::vector<uint8_t> hex_to_bytes(const std::string &hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    char buf[3] = {hex[i], hex[i + 1], '\0'};
    out.push_back((uint8_t)strtol(buf, nullptr, 16));
  }
  return out;
}

static uint64_t hex_to_key(const std::string &hex) {
  if (hex.size() != 12) return 0xFFFFFFFFFFFFULL;
  uint64_t v = 0;
  for (size_t i = 0; i < 12; i += 2) {
    char buf[3] = {hex[i], hex[i + 1], '\0'};
    v = (v << 8) | (uint8_t)strtol(buf, nullptr, 16);
  }
  return v;
}

static TagType parse_type(const std::string &name,
                          const std::vector<uint8_t> &uid) {
  if (name == "MIFARE_1K")   return TAG_MIFARE_CLASSIC_1K;
  if (name == "MIFARE_4K")   return TAG_MIFARE_CLASSIC_4K;
  if (name == "NTAG213")     return TAG_NTAG213;
  if (name == "NTAG215")     return TAG_NTAG215;
  if (name == "NTAG216")     return TAG_NTAG216;
  if (name == "ULTRALIGHT")  return TAG_MIFARE_ULTRALIGHT;
  if (name == "ISO15693")    return TAG_ISO15693;
  if (name == "TYPE4")       return TAG_TYPE4;
  // Auto-detect from UID length
  if (uid.size() == 8) return TAG_ISO15693;
  return (uid.size() == 4) ? TAG_MIFARE_CLASSIC_1K : TAG_NTAG213;
}

// ─────────────────────────────────────────────────────────────────────────────
// NTAG page memory initialisation
// ─────────────────────────────────────────────────────────────────────────────
// Builds a flat page_mem_ for NTAG / Ultralight tags: UID bytes at pages 0-1,
// Capability Container at page 3, NDEF TLV (if any) starting at page 4.
// This enables proper multi-page reads and WRITE (0xA2) support.
static void init_ntag_pages_(VirtualTag &tag) {
  if (tag.type != TAG_NTAG213 && tag.type != TAG_NTAG215 &&
      tag.type != TAG_NTAG216 && tag.type != TAG_MIFARE_ULTRALIGHT)
    return;

  size_t num_pages = (tag.type == TAG_NTAG216) ? 231 :
                     (tag.type == TAG_NTAG215) ? 135 : 45;  // NTAG213 / UL
  tag.page_mem_.assign(num_pages * 4, 0);

  // Pages 0-1: UID / serial bytes
  for (size_t i = 0; i < tag.uid.size() && i < 8; i++)
    tag.page_mem_[i] = tag.uid[i];

  // Page 3 (bytes 12-15): Capability Container
  tag.page_mem_[12] = 0xE1;  // NDEF magic
  tag.page_mem_[13] = 0x10;  // version 1.0
  tag.page_mem_[14] = 0x6D;  // size (NTAG213 max)
  tag.page_mem_[15] = 0x00;  // read/write access

  // Pages 4+: NDEF TLV if ndef_data is set
  if (!tag.ndef_data.empty()) {
    size_t pos = 16;  // page 4, byte 0
    size_t nd_len = tag.ndef_data.size();
    if (pos < tag.page_mem_.size()) tag.page_mem_[pos++] = 0x03;
    // 3-byte TLV length for payloads ≥255 bytes (ISO 7816-4 BER-TLV)
    if (nd_len < 255) {
      if (pos < tag.page_mem_.size()) tag.page_mem_[pos++] = (uint8_t)nd_len;
    } else {
      if (pos < tag.page_mem_.size()) tag.page_mem_[pos++] = 0xFF;
      if (pos < tag.page_mem_.size()) tag.page_mem_[pos++] = (uint8_t)((nd_len >> 8) & 0xFF);
      if (pos < tag.page_mem_.size()) tag.page_mem_[pos++] = (uint8_t)(nd_len & 0xFF);
    }
    for (size_t i = 0; i < nd_len && pos < tag.page_mem_.size(); i++)
      tag.page_mem_[pos++] = tag.ndef_data[i];
    if (pos < tag.page_mem_.size())
      tag.page_mem_[pos] = 0xFE;  // Terminator TLV
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::setup() {
  ESP_LOGI(TAG, "ST25RSim: socket %s", socket_path_.c_str());
  start_socket_server_();
  st25r::ST25R::setup();
}

void ST25RSim::dump_config() {
  ESP_LOGI(TAG, "ST25R Simulator:");
  ESP_LOGI(TAG, "  Socket: %s", socket_path_.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Inline control API
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::add_tag(const std::vector<uint8_t> &uid, TagType type) {
  std::lock_guard<std::mutex> lk(tags_mutex_);
  for (auto &t : virtual_tags_) {
    if (t.uid == uid) { t.halted = false; return; }
  }
  VirtualTag t;
  t.uid = uid;
  t.type = type;
  init_ntag_pages_(t);
  virtual_tags_.push_back(std::move(t));
  ESP_LOGI(TAG, "SIM add_tag uid_len=%u type=%u", (unsigned)uid.size(), type);
}

void ST25RSim::remove_tag(const std::vector<uint8_t> &uid) {
  std::lock_guard<std::mutex> lk(tags_mutex_);
  auto it = std::remove_if(virtual_tags_.begin(), virtual_tags_.end(),
                           [&](const VirtualTag &t) { return t.uid == uid; });
  if (it != virtual_tags_.end()) {
    virtual_tags_.erase(it, virtual_tags_.end());
    ESP_LOGI(TAG, "SIM remove_tag uid_len=%u", (unsigned)uid.size());
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport — read_register
// ─────────────────────────────────────────────────────────────────────────────

uint8_t ST25RSim::read_register(uint8_t reg) {
  switch (reg) {
    case REG_IC_IDENTITY:   return ic_identity_;
    case REG_IRQ_MAIN:      { uint8_t v = pending_irq_main_;  pending_irq_main_  = 0; return v; }
    case REG_IRQ_TIMER:     { uint8_t v = pending_irq_timer_; pending_irq_timer_ = 0; return v; }
    case REG_IRQ_ERROR:     return 0;
    case REG_FIFO_STATUS1:  return (uint8_t)std::min(fifo_out_.size(), (size_t)255);
    case REG_FIFO_STATUS2:  { uint8_t v = fifo_status2_; fifo_status2_ = 0; return v; }
    case REG_COLLISION_DISPLAY: return collision_display_;
    case REG_AD_CONV_RESULT:    return ad_conv_result_;
    default:
      if (reg & 0x40)
        return space_b_regs_[reg & 0x3F];
      return regs_[reg & 0x3F];
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport — write_register / write_fifo / read_fifo
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::write_register(uint8_t reg, uint8_t value) {
  if (reg & 0x40)
    space_b_regs_[reg & 0x3F] = value;
  else
    regs_[reg & 0x3F] = value;
}

void ST25RSim::write_fifo(const uint8_t *data, size_t len) {
  fifo_in_.insert(fifo_in_.end(), data, data + len);
}

void ST25RSim::read_fifo(uint8_t *data, size_t len) {
  size_t n = std::min(len, fifo_out_.size());
  if (n > 0) {
    memcpy(data, fifo_out_.data(), n);
    fifo_out_.erase(fifo_out_.begin(), fifo_out_.begin() + (int)n);
  }
  if (n < len) memset(data + n, 0, len - n);
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport — write_command
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::write_command(uint8_t command) {
  switch (command) {
    case 0xC1:  // SET_DEFAULT
      memset(regs_, 0, sizeof(regs_));
      memset(space_b_regs_, 0, sizeof(space_b_regs_));
      pending_irq_main_  = 0;
      pending_irq_timer_ = 0;
      fifo_in_.clear();
      fifo_out_.clear();
      fifo_status2_ = 0;
      auth_state_ = {};
      { std::lock_guard<std::mutex> lk(tags_mutex_);
        for (auto &t : virtual_tags_) t.halted = false; }
      break;

    case 0xC2:  // STOP_ALL
    case 0xC3:  // CLEAR_FIFO
      fifo_in_.clear();
      fifo_out_.clear();
      fifo_status2_ = 0;
      break;

    case 0xC7:  // TRANSMIT_WUPA
      on_wupa_();
      break;

    case 0xC5:  // TRANSMIT_WITHOUT_CRC (anticol, Mifare NR+AR, block read, NFC-V stream)
      if ((regs_[0x03] & 0x78) == 0x70) {  // MODE om=0x0E → subcarrier_stream (NFC-V)
        on_nfcv_transmit_();
      } else {
        on_anticol_();
      }
      break;

    case 0xC4:  // TRANSMIT_WITH_CRC (SELECT, HALT, Mifare auth cmd, NFC-B SENSB)
      if ((regs_[0x03] & 0xF8) == 0x90) {  // MODE om=0x02 + tr_am=1 → NFC-B
        on_nfcb_sensb_();
      } else {
        on_transmit_crc_();
      }
      break;

    // No-ops
    case 0xC8: case 0xC9: case 0xCD: case 0xCE:
    case 0xD3: case 0xD5: case 0xD6:
      break;

    case 0xDF:  // MEASURE_VDD — store vdd_raw_ so next read of AD_CONV_RESULT returns it
      ad_conv_result_ = vdd_raw_;
      break;

    default:
      ESP_LOGV(TAG, "SIM: unhandled cmd 0x%02X", command);
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::array<uint8_t, 4> ST25RSim::uid_at_cl_(const VirtualTag &tag,
                                             uint8_t cl) const {
  if (cl == 0) {
    if (tag.uid.size() == 4)
      return {tag.uid[0], tag.uid[1], tag.uid[2], tag.uid[3]};
    return {0x88, tag.uid[0], tag.uid[1], tag.uid[2]};
  }
  return {tag.uid[3], tag.uid[4], tag.uid[5], tag.uid[6]};
}

bool ST25RSim::matches_prefix_(const VirtualTag &tag, uint8_t cl,
                               const uint8_t *prefix, uint8_t prefix_full,
                               uint8_t prefix_bits) const {
  if (cl > 0 && tag.uid.size() <= 4) return false;
  if (cl > 1) return false;
  auto uid4 = uid_at_cl_(tag, cl);
  for (uint8_t i = 0; i < prefix_full; i++)
    if (uid4[i] != prefix[i]) return false;
  if (prefix_bits > 0) {
    uint8_t mask = (uint8_t)((1u << prefix_bits) - 1u);
    if ((uid4[prefix_full] & mask) != (prefix[prefix_full] & mask)) return false;
  }
  return true;
}

uint8_t ST25RSim::final_sak_for_(const VirtualTag &tag) const {
  switch (tag.type) {
    case TAG_MIFARE_CLASSIC_4K: return 0x18;
    case TAG_NTAG213:
    case TAG_NTAG215:
    case TAG_NTAG216:
    case TAG_MIFARE_ULTRALIGHT: return 0x00;
    case TAG_TYPE4:             return 0x20;  // ISO-DEP capable
    case TAG_MIFARE_CLASSIC_1K:
    default:                    return 0x08;
  }
}

// Build 16-byte Mifare Classic block data.
//   Block 0 = manufacturer / UID block (read-only dummy)
//   Blocks 1-2 = data blocks (NDEF TLV if ndef_data set)
//   Block 3 = sector trailer
//   Blocks 4+ = zeros
void ST25RSim::build_block_(const VirtualTag &tag, uint8_t block,
                            uint8_t out[16]) const {
  memset(out, 0, 16);
  if (block == 0) {
    // Manufacturer data: UID in first 4 bytes
    for (size_t i = 0; i < 4 && i < tag.uid.size(); i++)
      out[i] = tag.uid[i];
    out[4] = out[0] ^ out[1] ^ out[2] ^ out[3];  // BCC
    return;
  }
  if (block == 3) {
    // Sector trailer: keys + access bits
    // Key A = FFFFFFFFFFFF, access = FF0780, Key B = FFFFFFFFFFFF
    memset(out, 0xFF, 6);
    out[6] = 0xFF; out[7] = 0x07; out[8] = 0x80; out[9] = 0x69;
    memset(out + 10, 0xFF, 6);
    return;
  }
  if (block == 1 && !tag.ndef_data.empty()) {
    // NDEF TLV in block 1: [0x03, len, ndef_record..., 0xFE, padding]
    const auto &nd = tag.ndef_data;
    uint8_t len = (uint8_t)std::min(nd.size(), (size_t)253u);
    out[0] = 0x03;
    out[1] = len;
    for (uint8_t i = 0; i < len && i + 2u < 15u; i++)
      out[2 + i] = nd[i];
    if (2u + len < 16u) out[2 + len] = 0xFE;
    return;
  }
  if (block == 2 && !tag.ndef_data.empty() && tag.ndef_data.size() > 13u) {
    // Overflow NDEF data into block 2
    size_t offset = 13;
    for (size_t i = 0; i < 16 && offset + i < tag.ndef_data.size(); i++)
      out[i] = tag.ndef_data[offset + i];
    return;
  }
  // All other blocks: zeros (already set by memset above)
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol handler — WUPA
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::on_wupa_() {
  fifo_out_.clear();
  fifo_status2_ = 0;
  collision_display_ = 0;
  last_selected_valid_ = false;
  auth_state_ = {};  // reset any in-progress auth

  // Enforce correct AM modulation for ISO14443A.
  // TX_DRIVER_CONF bits[7:4] = am_mod — must be 0 (100% ASK / OOK).
  // am_mod != 0 (e.g. 0x70 = am_mod=7 = 12% ASK) means the carrier never
  // fully switches off during WUPA pulses, so real tags cannot demodulate the
  // command and never respond.  Simulate that behaviour here so tests catch it.
  uint8_t tx_conf = regs_[REG_TX_DRIVER_CONF & 0x3F];
  if ((tx_conf & 0xF0) != 0x00) {
    ESP_LOGW(TAG, "SIM WUPA rejected: TX_DRIVER_CONF=0x%02X has non-zero am_mod "
             "(bits[7:4]=0x%X); ISO14443A requires am_mod=0 (100%% ASK). "
             "Tags will not respond.", tx_conf, (tx_conf >> 4) & 0xF);
    // Signal NRE in both IRQ_MAIN (for base-class fast-path) and IRQ_TIMER
    // (for ST25R300 compatibility).  The base-class loop() reads IRQ_MAIN only,
    // so pending_irq_timer_ alone would cause a 100ms millis() timeout fallback.
    pending_irq_main_  = 0x01;  // IRQ_NRE bit — fast path in base-class loop()
    pending_irq_timer_ = 0x40;  // NRE in hardware timer register (ST25R300 compat)
    return;
  }

  std::lock_guard<std::mutex> lk(tags_mutex_);
  for (auto &t : virtual_tags_) t.halted = false;

  // Find first NFC-A tag (ISO15693 tags only respond to NFC-V inventory, not WUPA)
  const VirtualTag *first_nfca = nullptr;
  size_t nfca_count = 0;
  for (const auto &t : virtual_tags_) {
    if (t.type != TAG_ISO15693) {
      if (!first_nfca) first_nfca = &t;
      nfca_count++;
    }
  }

  if (first_nfca) {
    uint8_t atqa0 = 0x44;  // default (NTAG / Ultralight)
    if (first_nfca->type == TAG_MIFARE_CLASSIC_1K) atqa0 = 0x04;
    if (first_nfca->type == TAG_MIFARE_CLASSIC_4K) atqa0 = 0x02;
    fifo_out_ = {atqa0, 0x00};
    pending_irq_main_ = IRQ_RXE;
    ESP_LOGV(TAG, "SIM WUPA → ATQA 0x%02X (%u NFC-A tag(s))", atqa0,
             (unsigned)nfca_count);
  } else {
    // Signal NRE.  In "hw" mode only IRQ_TIMER is set, mirroring real ST25R3916
    // hardware (NRE appears in IRQ_TIMER bit6, never in IRQ_MAIN).  This forces
    // the firmware to take the 100ms millis() timeout path in STATE_WUPA instead
    // of the IRQ_NRE fast-path.  In default "sim" mode both registers are set so
    // the fast-path fires immediately and tests run faster.
    pending_irq_timer_ = 0x40;  // NRE in IRQ_TIMER (both modes)
    if (!nre_hw_mode_) {
      pending_irq_main_ = 0x01;  // IRQ_NRE fast-path (sim mode only)
    }
    ESP_LOGV(TAG, "SIM WUPA → no NFC-A tags (hw_mode=%d)", (int)nre_hw_mode_);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol handler — TRANSMIT_WITHOUT_CRC
// Handles: anticollision frames, Mifare auth step 2 (NR+AR), block reads.
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::on_anticol_() {
  std::vector<uint8_t> frame;
  frame.swap(fifo_in_);
  fifo_out_.clear();
  fifo_status2_ = 0;
  collision_display_ = 0;

  // ── Mifare auth step 2: NR+AR (8 bytes × 9 bits = 9 bytes packed) ────────
  if (auth_state_.pending && frame.size() == 9) {
    on_mifare_auth_response_(frame);
    return;
  }

  // ── Mifare block read: 4-byte cmd × 9 bits = 5 bytes packed ──────────────
  if (auth_state_.authenticated && frame.size() == 5) {
    on_mifare_block_read_(frame);
    return;
  }

  // ── Normal ISO 14443-A anticollision frame ────────────────────────────────
  // Clear auth state on any new anticol (new tag selection started)
  auth_state_ = {};

  if (frame.size() < 2) { pending_irq_timer_ = 0x40; return; }

  uint8_t sel      = frame[0];
  uint8_t nvb      = frame[1];
  uint8_t nvb_high = nvb >> 4;
  uint8_t nvb_low  = nvb & 0x0F;
  uint8_t prefix_full = (nvb_high >= 2) ? (nvb_high - 2) : 0;
  uint8_t prefix_bits = nvb_low;
  uint8_t cl = (sel == 0x93) ? 0 : (sel == 0x95) ? 1 : 2;

  uint8_t prefix[5]{};
  for (uint8_t i = 0; i < prefix_full && (size_t)(2 + i) < frame.size(); i++)
    prefix[i] = frame[2 + i];
  if (prefix_bits > 0 && (size_t)(2 + prefix_full) < frame.size())
    prefix[prefix_full] = frame[2 + prefix_full];

  std::vector<size_t> matches;
  {
    std::lock_guard<std::mutex> lk(tags_mutex_);
    for (size_t i = 0; i < virtual_tags_.size(); i++) {
      if (!virtual_tags_[i].halted &&
          matches_prefix_(virtual_tags_[i], cl, prefix, prefix_full, prefix_bits))
        matches.push_back(i);
    }
  }

  if (matches.empty()) {
    pending_irq_timer_ = 0x40;
    ESP_LOGV(TAG, "SIM ANTICOL CL%u → no match", cl);
    return;
  }

  if (matches.size() == 1) {
    std::lock_guard<std::mutex> lk(tags_mutex_);
    auto uid4 = uid_at_cl_(virtual_tags_[matches[0]], cl);
    for (uint8_t i = 0; i < prefix_full; i++) uid4[i] = 0;
    if (prefix_bits > 0) {
      uint8_t mask = (uint8_t)((1u << prefix_bits) - 1u);
      uid4[prefix_full] &= (uint8_t)(~mask);
    }
    uint8_t bcc = uid4[0] ^ uid4[1] ^ uid4[2] ^ uid4[3];
    fifo_out_ = {uid4[0], uid4[1], uid4[2], uid4[3], bcc};
    pending_irq_main_ = IRQ_RXE;
    ESP_LOGV(TAG, "SIM ANTICOL CL%u → single match", cl);
    return;
  }

  // Multiple tags → collision
  std::lock_guard<std::mutex> lk(tags_mutex_);
  auto uid_a = uid_at_cl_(virtual_tags_[matches[0]], cl);
  auto uid_b = uid_at_cl_(virtual_tags_[matches[1]], cl);
  int col_uid_bit = 0;
  for (int b = 0; b < 4; b++) {
    uint8_t diff = uid_a[b] ^ uid_b[b];
    if (diff) {
      for (int bit = 0; bit < 8; bit++) {
        if (diff & (1u << bit)) { col_uid_bit = b * 8 + bit; break; }
      }
      break;
    }
  }
  int abs_bit = col_uid_bit + 16;
  uint8_t c_byte = (uint8_t)(abs_bit / 8);
  uint8_t c_bit  = (uint8_t)(abs_bit % 8);
  collision_display_ = (uint8_t)((c_byte << 4) | (c_bit << 1));
  pending_irq_main_ = IRQ_COL;
  ESP_LOGV(TAG, "SIM ANTICOL CL%u → collision uid_bit=%d disp=0x%02X",
           cl, col_uid_bit, collision_display_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mifare auth step 2 — receive NR+AR, respond with AT
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::on_mifare_auth_response_(const std::vector<uint8_t> &frame) {
  // Unpack 9 bytes (72 bits) → 8 data bytes + 8 parity bits
  uint8_t data[8], parity[8];
  sim_unpack_parity(frame.data(), data, parity, 72);

  struct Crypto1State &cs = auth_state_.cs;

  // Decrypt NR (4 bytes): reader used crypto1_byte(cs, nr[i], 0) ^ nr[i]
  // is_encrypted=1 decrypts AND advances LFSR with plaintext bits ✓
  uint8_t nr_plain[4];
  for (int i = 0; i < 4; i++) {
    uint8_t ks = crypto1_byte(&cs, data[i], 1);
    nr_plain[i] = ks ^ data[i];
    crypto1_bit(&cs, 0, 0);  // advance parity with 0 (same as reader)
  }

  // Decrypt AR (4 bytes): reader used crypto1_byte(cs, 0, 0) ^ ar[i]
  // (LFSR advanced with in=0, so we must also use in=0)
  uint8_t ar_plain[4];
  for (int i = 0; i < 4; i++) {
    uint8_t ks = crypto1_byte(&cs, 0, 0);  // in=0, same as reader
    ar_plain[i] = data[4 + i] ^ ks;
    crypto1_bit(&cs, 0, 0);  // advance parity with 0
  }

  uint32_t ar_plain_u32 = ((uint32_t)ar_plain[0] << 24) |
                          ((uint32_t)ar_plain[1] << 16) |
                          ((uint32_t)ar_plain[2] <<  8) |
                           (uint32_t)ar_plain[3];

  // Verify AR == prng_successor(NT, 64) — optional; log mismatch
  uint32_t ar_expected = prng_successor(auth_state_.nt, 64);
  if (ar_plain_u32 != ar_expected) {
    ESP_LOGW(TAG, "SIM Mifare auth: AR mismatch (got %08X expected %08X)",
             ar_plain_u32, ar_expected);
    // Respond with IRQ_TXE only → firmware auth fails gracefully
    auth_state_ = {};
    pending_irq_main_ = IRQ_TXE;
    return;
  }

  // Compute AT = prng_successor(AR, 32), encrypted with remaining keystream.
  // Reader verifies: at_got == at_plain XOR crypto1_word(cs, 0, 0).
  // crypto1_word is 32 contiguous LFSR steps (no parity bits interleaved).
  // So we must NOT call crypto1_bit for parity between AT data bytes.
  uint32_t at_plain_u32 = prng_successor(ar_plain_u32, 32);
  uint8_t at_enc[4], at_par[4];
  for (int i = 0; i < 4; i++) {
    uint8_t b = (uint8_t)((at_plain_u32 >> (24 - 8 * i)) & 0xFF);
    uint8_t ks = crypto1_byte(&cs, 0, 0);  // 8 ks steps, no parity between
    at_enc[i] = ks ^ b;
    at_par[i] = ODD_PARITY[b];  // simple parity — reader doesn't verify AT par
  }

  // Pack AT (4 bytes × 9 bits = 36 bits → 5 bytes), last_bits=4
  uint8_t packed[5] = {};
  uint16_t packed_bits = 0;
  sim_pack_parity(at_enc, at_par, packed, 4, &packed_bits);
  fifo_out_.assign(packed, packed + 5);
  // FIFO_STATUS2: fifo_lb=4 → last byte has 4 valid bits
  // Formula: rx_bits = rx_bytes*8 - (8 - last_bits) → 5*8 - (8-4) = 36 ✓
  fifo_status2_ = (uint8_t)(4 << 1);

  auth_state_.pending       = false;
  auth_state_.authenticated = true;
  // auth_state_.cs is now ready for block reads

  pending_irq_main_ = IRQ_TXE | IRQ_RXE;
  ESP_LOGV(TAG, "SIM Mifare auth → AT sent (auth OK)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Mifare block read — decrypt READ command, respond with encrypted block data
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::on_mifare_block_read_(const std::vector<uint8_t> &frame) {
  // Unpack 5 bytes (36 bits) → 4 encrypted cmd bytes + 4 parity bits
  uint8_t enc_cmd[4], parity[4];
  sim_unpack_parity(frame.data(), enc_cmd, parity, 36);

  struct Crypto1State &cs = auth_state_.cs;

  // Decrypt command: reader used crypto1_byte(cs, 0, 0) ^ cmd[i]
  uint8_t cmd[4];
  for (int i = 0; i < 4; i++) {
    uint8_t ks = crypto1_byte(&cs, 0, 0);
    cmd[i] = enc_cmd[i] ^ ks;
    crypto1_bit(&cs, 0, 0);  // advance parity
  }

  if (cmd[0] != 0x30) {
    ESP_LOGW(TAG, "SIM Mifare block: unexpected cmd 0x%02X", cmd[0]);
    pending_irq_main_ = IRQ_TXE;
    return;
  }

  uint8_t block = cmd[1];
  ESP_LOGV(TAG, "SIM Mifare block read: block %u", block);

  // Build 16-byte block data
  uint8_t plain[16];
  memset(plain, 0, 16);
  {
    std::lock_guard<std::mutex> lk(tags_mutex_);
    for (const auto &t : virtual_tags_) {
      if (t.uid == last_selected_full_uid_) {
        build_block_(t, block, plain);
        break;
      }
    }
  }

  // Append CRC-A
  uint16_t crc = mifare_crc_a(plain, 16);
  uint8_t response[18];
  memcpy(response, plain, 16);
  response[16] = (uint8_t)(crc & 0xFF);
  response[17] = (uint8_t)(crc >> 8);

  // Encrypt response (18 bytes × 9 bits = 162 bits → 21 bytes packed)
  uint8_t resp_enc[18], resp_par[18];
  for (int i = 0; i < 18; i++) {
    uint8_t ks = crypto1_byte(&cs, 0, 0);
    resp_enc[i] = ks ^ response[i];
    uint8_t ks_p = crypto1_bit(&cs, 0, 0);
    resp_par[i] = ks_p ^ ODD_PARITY[response[i]];
  }

  uint8_t packed[21] = {};
  uint16_t packed_bits = 0;
  sim_pack_parity(resp_enc, resp_par, packed, 18, &packed_bits);
  fifo_out_.assign(packed, packed + 21);
  // FIFO_STATUS2: fifo_lb=2 → 21*8 - (8-2) = 162 bits ✓
  fifo_status2_ = (uint8_t)(2 << 1);

  pending_irq_main_ = IRQ_TXE | IRQ_RXE;
  ESP_LOGV(TAG, "SIM Mifare block %u → 18-byte encrypted response", block);
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol handler — TRANSMIT_WITH_CRC
// Handles: SELECT, HALT, Mifare auth command, NTAG page read.
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::on_transmit_crc_() {
  std::vector<uint8_t> frame;
  frame.swap(fifo_in_);
  fifo_out_.clear();
  fifo_status2_ = 0;

  if (frame.empty()) { pending_irq_main_ = IRQ_TXE; return; }

  uint8_t cmd = frame[0];

  // ── SELECT (93/95/97 70) ──────────────────────────────────────────────────
  if ((cmd == 0x93 || cmd == 0x95 || cmd == 0x97) &&
      frame.size() >= 2 && frame[1] == 0x70) {
    if (frame.size() < 6) { pending_irq_main_ = IRQ_TXE; return; }
    uint8_t uid4[4] = {frame[2], frame[3], frame[4], frame[5]};
    uint8_t cl = (cmd == 0x93) ? 0 : (cmd == 0x95) ? 1 : 2;

    std::lock_guard<std::mutex> lk(tags_mutex_);
    for (size_t i = 0; i < virtual_tags_.size(); i++) {
      if (virtual_tags_[i].halted) continue;
      auto uid_cl = uid_at_cl_(virtual_tags_[i], cl);
      if (uid_cl[0] == uid4[0] && uid_cl[1] == uid4[1] &&
          uid_cl[2] == uid4[2] && uid_cl[3] == uid4[3]) {
        bool needs_cascade = (cl == 0 && virtual_tags_[i].uid.size() > 4);
        uint8_t sak = needs_cascade ? 0x04 : final_sak_for_(virtual_tags_[i]);
        last_selected_uid_ = uid_cl;
        last_selected_cl_  = cl;
        last_selected_valid_ = true;
        last_selected_full_uid_ = virtual_tags_[i].uid;
        fifo_out_ = {sak};
        pending_irq_main_ = IRQ_TXE | IRQ_RXE;
        ESP_LOGV(TAG, "SIM SELECT CL%u → SAK=0x%02X", cl, sak);
        return;
      }
    }
    pending_irq_main_ = IRQ_TXE;
    return;
  }

  // ── HALT (50 00) ──────────────────────────────────────────────────────────
  if (cmd == 0x50) {
    std::lock_guard<std::mutex> lk(tags_mutex_);
    if (last_selected_valid_) {
      for (auto &t : virtual_tags_) {
        if (!t.halted) {
          auto uid_cl = uid_at_cl_(t, last_selected_cl_);
          if (uid_cl == last_selected_uid_) { t.halted = true; break; }
        }
      }
      last_selected_valid_ = false;
    } else {
      for (auto &t : virtual_tags_) {
        if (!t.halted) { t.halted = true; break; }
      }
    }
    pending_irq_main_ = 0;
    ESP_LOGV(TAG, "SIM HALT");
    return;
  }

  // ── Mifare Classic auth command (60=keyA, 61=keyB) ───────────────────────
  if ((cmd == 0x60 || cmd == 0x61) && frame.size() >= 2) {
    uint8_t block = frame[1];
    uint64_t key  = 0xFFFFFFFFFFFFULL;

    // Look up the key from the selected tag
    {
      std::lock_guard<std::mutex> lk(tags_mutex_);
      for (const auto &t : virtual_tags_) {
        if (t.uid == last_selected_full_uid_) {
          key = (cmd == 0x60) ? t.key_a : t.key_b;
          break;
        }
      }
    }

    // Build uid_u32 from the lower 4 bytes of the tag UID
    uint32_t uid_u32 = 0;
    if (last_selected_full_uid_.size() >= 4) {
      size_t off = last_selected_full_uid_.size() - 4;
      uid_u32 = ((uint32_t)last_selected_full_uid_[off]     << 24) |
                ((uint32_t)last_selected_full_uid_[off + 1] << 16) |
                ((uint32_t)last_selected_full_uid_[off + 2] <<  8) |
                 (uint32_t)last_selected_full_uid_[off + 3];
    }

    // Fixed tag nonce for determinism (a real tag would randomise this)
    uint32_t nt = 0xABCD1234;

    // Initialise Crypto1 state: key → advance with NT^UID
    struct Crypto1State cs{};
    crypto1_init(&cs, key);
    crypto1_word(&cs, nt ^ uid_u32, 0);

    auth_state_.pending    = true;
    auth_state_.authenticated = false;
    auth_state_.nt         = nt;
    auth_state_.uid_u32    = uid_u32;
    auth_state_.key        = key;
    auth_state_.cs         = cs;

    // Return NT (4 bytes MSB-first)
    fifo_out_ = {
      (uint8_t)(nt >> 24), (uint8_t)(nt >> 16),
      (uint8_t)(nt >> 8),  (uint8_t)(nt & 0xFF)
    };
    pending_irq_main_ = IRQ_TXE | IRQ_RXE;
    ESP_LOGV(TAG, "SIM Mifare auth cmd 0x%02X block %u → NT=0x%08X",
             cmd, block, nt);
    return;
  }

  // ── NTAG WRITE (A2 <page> <4 bytes>) — responds with ACK ─────────────────
  if (cmd == 0xA2 && frame.size() >= 6) {
    uint8_t page = frame[1];
    {
      std::lock_guard<std::mutex> lk(tags_mutex_);
      for (auto &t : virtual_tags_) {
        if (t.uid == last_selected_full_uid_) {
          size_t start = (size_t)page * 4;
          if (!t.page_mem_.empty() && start + 4 <= t.page_mem_.size()) {
            t.page_mem_[start + 0] = frame[2];
            t.page_mem_[start + 1] = frame[3];
            t.page_mem_[start + 2] = frame[4];
            t.page_mem_[start + 3] = frame[5];
          }
          break;
        }
      }
    }
    // ACK nibble: firmware checks (buffer[0] & 0x0F) == 0x0A
    fifo_out_ = {0x0A};
    pending_irq_main_ = IRQ_TXE | IRQ_RXE;
    ESP_LOGV(TAG, "SIM WRITE page=%u data=%02X%02X%02X%02X",
             page, frame[2], frame[3], frame[4], frame[5]);
    return;
  }

  // ── NTAG / Type-2 page read (30 <page>) ──────────────────────────────────
  if (cmd == 0x30 && frame.size() >= 2) {
    uint8_t page = frame[1];
    uint8_t response[16] = {};

    {
      std::lock_guard<std::mutex> lk(tags_mutex_);
      for (const auto &t : virtual_tags_) {
        if (t.uid == last_selected_full_uid_) {
          if (!t.page_mem_.empty()) {
            // Proper page memory (NTAG/Ultralight): return 4 pages from offset
            size_t start = (size_t)page * 4;
            for (int i = 0; i < 16; i++) {
              size_t idx = start + (size_t)i;
              response[i] = (idx < t.page_mem_.size()) ? t.page_mem_[idx] : 0;
            }
          } else {
            // Fallback for tags without page_mem_ (shouldn't happen for NTAG)
            response[0] = 0xFE;
          }
          break;
        }
      }
    }

    fifo_out_.assign(response, response + 16);
    pending_irq_main_ = IRQ_TXE | IRQ_RXE;
    ESP_LOGV(TAG, "SIM PAGE READ page=%u", page);
    return;
  }

  // ── RATS (0xE0) — ISO 14443-4 activation ─────────────────────────────────
  if (cmd == 0xE0 && frame.size() >= 2) {
    // Respond with basic ATS: TL=5, T0=0x75, TA=0x31, TB=0x02, TC=0x51
    fifo_out_ = {0x05, 0x75, 0x31, 0x02, 0x51};
    pending_irq_main_ = IRQ_TXE | IRQ_RXE;
    ESP_LOGV(TAG, "SIM RATS → ATS (TL=5)");
    return;
  }

  // ── I-Block (ISO-DEP) — PCB byte starts with 0x02 or 0x03 ──────────────
  if ((cmd & 0xC2) == 0x02 && frame.size() >= 2) {
    // I-Block with APDU. Check for SELECT NDEF app (INS=0xA4, P1=0x04)
    // Respond with 9000 (success) for known commands, 6A82 (not found) for others
    uint8_t pcb_resp = 0x02 | ((cmd ^ 0x01) & 0x01);  // toggle block number
    if (frame.size() >= 6 && frame[1] == 0x00 && frame[2] == 0xA4 && frame[3] == 0x04) {
      // SELECT by name — respond success
      fifo_out_ = {pcb_resp, 0x90, 0x00};
    } else if (frame.size() >= 6 && frame[1] == 0x00 && frame[2] == 0xA4 && frame[3] == 0x00) {
      // SELECT by FID — respond success
      fifo_out_ = {pcb_resp, 0x90, 0x00};
    } else if (frame.size() >= 5 && frame[1] == 0x00 && frame[2] == 0xB0) {
      // READ BINARY — respond with dummy data + 9000
      uint8_t le = frame[frame.size() - 1];
      if (le > 48) le = 48;
      fifo_out_.clear();
      fifo_out_.push_back(pcb_resp);
      // CC dummy: CCLEN=000F, v2.0, MLe=FF, MLc=FF, NDEF TLV
      if (frame[3] == 0x00 && frame[4] == 0x00 && le >= 15) {
        // READ CC at offset 0
        uint8_t cc[] = {0x00, 0x0F, 0x20, 0xFF, 0xFF, 0x04, 0x06, 0x00,
                        0x00, 0xE1, 0x04, 0x00, 0x20, 0x00, 0x00};
        for (uint8_t i = 0; i < 15 && i < le; i++) fifo_out_.push_back(cc[i]);
      } else {
        // Generic read — return zeros
        for (uint8_t i = 0; i < le; i++) fifo_out_.push_back(0x00);
      }
      fifo_out_.push_back(0x90);
      fifo_out_.push_back(0x00);
    } else {
      // Unknown APDU — respond 6A82 (file not found)
      fifo_out_ = {pcb_resp, 0x6A, 0x82};
    }
    pending_irq_main_ = IRQ_TXE | IRQ_RXE;
    ESP_LOGV(TAG, "SIM I-Block → response (%u bytes)", (unsigned)fifo_out_.size());
    return;
  }

  // ── Unknown / other commands ──────────────────────────────────────────────
  pending_irq_main_ = IRQ_TXE;
  ESP_LOGV(TAG, "SIM TRANSMIT_WITH_CRC: unhandled cmd=0x%02X", cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// NFC-V (ISO 15693) streaming mode handler
// ─────────────────────────────────────────────────────────────────────────────

// ISO 15693 CRC-16 CCITT (same algorithm as in st25r.cpp)
static uint16_t sim_iso15693_crc(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    uint8_t d = data[i] ^ (uint8_t)(crc & 0xFF);
    d ^= (d << 4);
    crc = (crc >> 8) ^ ((uint16_t)d << 8) ^ ((uint16_t)d << 3) ^ ((uint16_t)d >> 4);
  }
  return ~crc;
}

// Decode 1-of-4 VCD encoded FIFO data back to raw bytes
static size_t sim_decode_1of4(const uint8_t *coded, size_t coded_len, uint8_t *out, size_t out_max) {
  // Skip SOF (first byte = 0x21), read 4 coded bytes per data byte, stop at EOF (0x04)
  size_t pos = 0;
  size_t out_pos = 0;
  if (coded_len < 2 || coded[0] != 0x21) return 0;  // No SOF
  pos = 1;  // skip SOF

  while (pos + 3 < coded_len && out_pos < out_max) {
    if (coded[pos] == 0x04) break;  // EOF
    uint8_t byte = 0;
    for (int j = 0; j < 4 && pos < coded_len; j++, pos++) {
      if (coded[pos] == 0x04) goto done;
      uint8_t bits = 0;
      if (coded[pos] == 0x02) bits = 0;
      else if (coded[pos] == 0x08) bits = 1;
      else if (coded[pos] == 0x20) bits = 2;
      else if (coded[pos] == 0x80) bits = 3;
      byte |= (bits << (j * 2));
    }
    out[out_pos++] = byte;
  }
done:
  return out_pos;
}

// Manchester-encode response bytes for NFC-V streaming mode FIFO
static size_t sim_manchester_encode(const uint8_t *data, size_t len, uint8_t *out, size_t out_max) {
  // SOF: 5 bits = 10111 = 0x17 in bits[4:0] of first byte
  // Then: 2 Manchester bits per payload bit (bit 0 → 01, bit 1 → 10)
  // EOF: 10111000 pattern at byte boundary
  if (out_max < (1 + len * 2 + 2)) return 0;

  memset(out, 0, out_max);
  size_t bp = 0;  // bit position in output

  // SOF: bits 10111 (LSB first in the byte)
  out[0] = 0x17;
  bp = 5;

  // Manchester encode each data bit
  for (size_t i = 0; i < len; i++) {
    for (int b = 0; b < 8; b++) {
      int bit = (data[i] >> b) & 1;
      if (bit == 0) {
        // man=1: first bit = 1, second bit = 0 → decoded as 0
        out[bp / 8] |= (1 << (bp % 8));
        // bit at bp+1 = 0 (already 0)
      } else {
        // man=2: first bit = 0, second bit = 1 → decoded as 1
        // bit at bp+0 = 0 (already 0)
        out[(bp + 1) / 8] |= (1 << ((bp + 1) % 8));
      }
      bp += 2;
    }
  }

  // EOF: pattern 10111000 = 0xA0 in current byte + 0x03 in next byte
  // Pad to byte boundary first (should already be aligned after whole bytes)
  size_t eof_byte = bp / 8;
  out[eof_byte] |= 0xA0;  // top 3 bits of EOF pattern
  if (eof_byte + 1 < out_max)
    out[eof_byte + 1] = 0x03;  // next byte of EOF

  return eof_byte + 2;  // total output bytes
}

void ST25RSim::on_nfcv_transmit_() {
  std::vector<uint8_t> coded_frame;
  coded_frame.swap(fifo_in_);
  fifo_out_.clear();

  // Decode 1-of-4 encoded frame to get raw ISO 15693 command
  uint8_t raw[32];
  size_t raw_len = sim_decode_1of4(coded_frame.data(), coded_frame.size(), raw, sizeof(raw));

  if (raw_len < 3) {
    // Too short or decode failed — just signal TXE + NRE
    pending_irq_main_ = IRQ_TXE;
    pending_irq_timer_ = 0x40;  // NRE
    return;
  }

  // Strip CRC (last 2 bytes are CRC-16 appended by firmware)
  if (raw_len >= 3) raw_len -= 2;

  uint8_t flags = raw[0];
  uint8_t cmd = raw[1];

  ESP_LOGV(TAG, "SIM NFC-V: decoded %u bytes, flags=0x%02X cmd=0x%02X", (unsigned)raw_len, flags, cmd);

  // Check for INVENTORY command (0x01)
  if (cmd == 0x01 && (flags & 0x04)) {
    std::lock_guard<std::mutex> lk(tags_mutex_);

    // Find first ISO15693 tag that isn't halted
    const VirtualTag *v_tag = nullptr;
    for (const auto &t : virtual_tags_) {
      if (t.type == TAG_ISO15693 && !t.halted) {
        v_tag = &t;
        break;
      }
    }

    if (v_tag && v_tag->uid.size() == 8) {
      // Build response: flags(1) + DSFID(1) + UID(8) = 10 bytes, then CRC
      uint8_t resp[12];
      resp[0] = 0x00;  // flags: no error
      resp[1] = 0x00;  // DSFID
      // UID in LSB-first order (reverse of how it's stored in our UID vector which is MSB-first)
      for (int i = 0; i < 8; i++)
        resp[2 + i] = v_tag->uid[7 - i];
      // Append CRC-16
      uint16_t crc = sim_iso15693_crc(resp, 10);
      resp[10] = crc & 0xFF;
      resp[11] = (crc >> 8) & 0xFF;

      // Manchester-encode the response
      uint8_t encoded[64];
      size_t enc_len = sim_manchester_encode(resp, 12, encoded, sizeof(encoded));

      fifo_out_.assign(encoded, encoded + enc_len);
      pending_irq_main_ = IRQ_TXE | IRQ_RXE;

      ESP_LOGV(TAG, "SIM NFC-V INVENTORY → UID %02X%02X%02X%02X%02X%02X%02X%02X (enc=%u bytes)",
               v_tag->uid[0], v_tag->uid[1], v_tag->uid[2], v_tag->uid[3],
               v_tag->uid[4], v_tag->uid[5], v_tag->uid[6], v_tag->uid[7],
               (unsigned)enc_len);
    } else {
      // No ISO15693 tags — NRE
      pending_irq_main_ = IRQ_TXE;
      pending_irq_timer_ = 0x40;  // NRE
      ESP_LOGV(TAG, "SIM NFC-V INVENTORY → no ISO15693 tags");
    }
  } else {
    // Unknown NFC-V command — just TXE + NRE
    pending_irq_main_ = IRQ_TXE;
    pending_irq_timer_ = 0x40;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// NFC-B (ISO 14443B) SENSB_REQ handler
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::on_nfcb_sensb_() {
  std::vector<uint8_t> frame;
  frame.swap(fifo_in_);
  fifo_out_.clear();

  // Check for SENSB_REQ: first byte should be 0x05
  if (frame.size() < 3 || frame[0] != 0x05) {
    pending_irq_main_ = IRQ_TXE;
    pending_irq_timer_ = 0x40;  // NRE
    return;
  }

  std::lock_guard<std::mutex> lk(tags_mutex_);

  // Find first ISO14443B tag (TAG_ISO14443B type — not yet defined, so use a heuristic:
  // for now, no NFC-B virtual tags exist, so just return NRE)
  // TODO: Add TAG_ISO14443B to TagType enum when NFC-B virtual tags are needed
  pending_irq_main_ = IRQ_TXE;
  pending_irq_timer_ = 0x40;  // NRE — no NFC-B tags in sim yet
  ESP_LOGV(TAG, "SIM NFC-B SENSB_REQ → no ISO14443B tags in sim");
}

// ─────────────────────────────────────────────────────────────────────────────
// Unix-domain socket server
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::start_socket_server_() {
  socket_thread_ = std::thread([this]() { socket_thread_func_(); });
  socket_thread_.detach();
}

// Parse a socket line into tokens split by whitespace.
static std::vector<std::string> split_tokens(const std::string &line) {
  std::vector<std::string> tok;
  std::istringstream ss(line);
  std::string t;
  while (ss >> t) tok.push_back(t);
  return tok;
}

void ST25RSim::handle_client_(int fd) {
  char buf[4096];
  int n = (int)recv(fd, buf, sizeof(buf) - 1, 0);
  if (n <= 0) return;
  buf[n] = '\0';

  std::string line(buf);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
    line.pop_back();

  auto tok = split_tokens(line);
  if (tok.empty()) { send(fd, "ERROR empty\n", 12, 0); return; }

  std::string response = "OK\n";

  // ADD_TAG <uid_hex> [TYPE=<name>] [KEY_A=<hex>] [KEY_B=<hex>] [NDEF=<hex>]
  if (tok[0] == "ADD_TAG" && tok.size() >= 2) {
    auto uid = hex_to_bytes(tok[1]);
    if (uid.empty()) { send(fd, "ERROR bad UID\n", 14, 0); return; }

    TagType type = parse_type("", uid);  // auto-detect default
    uint64_t key_a = 0xFFFFFFFFFFFFULL;
    uint64_t key_b = 0xFFFFFFFFFFFFULL;
    std::vector<uint8_t> ndef_data;

    for (size_t i = 2; i < tok.size(); i++) {
      const std::string &kv = tok[i];
      auto eq = kv.find('=');
      if (eq == std::string::npos) continue;
      std::string k = kv.substr(0, eq);
      std::string v = kv.substr(eq + 1);
      if (k == "TYPE")  type  = parse_type(v, uid);
      if (k == "KEY_A") key_a = hex_to_key(v);
      if (k == "KEY_B") key_b = hex_to_key(v);
      if (k == "NDEF")  ndef_data = hex_to_bytes(v);
    }

    {
      std::lock_guard<std::mutex> lk(tags_mutex_);
      bool found = false;
      for (auto &t : virtual_tags_) {
        if (t.uid == uid) {
          t.halted = false;
          t.type   = type;
          t.key_a  = key_a;
          t.key_b  = key_b;
          if (!ndef_data.empty()) t.ndef_data = ndef_data;
          found = true;
          break;
        }
      }
      if (!found) {
        VirtualTag nt;
        nt.uid = uid; nt.type = type;
        nt.key_a = key_a; nt.key_b = key_b;
        nt.ndef_data = ndef_data;
        init_ntag_pages_(nt);
        virtual_tags_.push_back(std::move(nt));
      }
    }
    ESP_LOGI(TAG, "SIM socket: ADD_TAG uid_len=%u type=%u ndef=%u",
             (unsigned)uid.size(), type, (unsigned)ndef_data.size());

  // REMOVE_TAG <uid_hex>
  } else if (tok[0] == "REMOVE_TAG" && tok.size() >= 2) {
    auto uid = hex_to_bytes(tok[1]);
    if (uid.empty()) { send(fd, "ERROR bad UID\n", 14, 0); return; }
    remove_tag(uid);

  // SET_KEY <uid_hex> A|B <key_hex>
  } else if (tok[0] == "SET_KEY" && tok.size() >= 4) {
    auto uid = hex_to_bytes(tok[1]);
    uint64_t key = hex_to_key(tok[3]);
    std::lock_guard<std::mutex> lk(tags_mutex_);
    for (auto &t : virtual_tags_) {
      if (t.uid == uid) {
        if (tok[2] == "A") t.key_a = key;
        else               t.key_b = key;
        break;
      }
    }

  // SET_NDEF <uid_hex> <ndef_hex>
  } else if (tok[0] == "SET_NDEF" && tok.size() >= 3) {
    auto uid       = hex_to_bytes(tok[1]);
    auto ndef_data = hex_to_bytes(tok[2]);
    std::lock_guard<std::mutex> lk(tags_mutex_);
    for (auto &t : virtual_tags_) {
      if (t.uid == uid) {
        t.ndef_data = ndef_data;
        init_ntag_pages_(t);
        break;
      }
    }

  // LIST
  } else if (tok[0] == "LIST") {
    std::string out;
    {
      std::lock_guard<std::mutex> lk(tags_mutex_);
      for (const auto &t : virtual_tags_) {
        out += "TAG ";
        for (uint8_t b : t.uid) {
          char hex[3]; snprintf(hex, sizeof(hex), "%02X", b);
          out += hex;
        }
        out += t.halted ? " HALTED" : "";
        out += "\n";
      }
    }
    out += "END\n";
    send(fd, out.c_str(), out.size(), 0);
    return;

  } else if (tok[0] == "SET_IC_IDENTITY" && tok.size() >= 2) {
    char *end;
    unsigned long val = strtoul(tok[1].c_str(), &end, 16);
    ic_identity_ = (uint8_t)val;
    ESP_LOGI(TAG, "SIM socket: SET_IC_IDENTITY 0x%02X", ic_identity_);

  } else if (tok[0] == "SET_VDD" && tok.size() >= 2) {
    char *end;
    unsigned long val = strtoul(tok[1].c_str(), &end, 16);
    vdd_raw_ = (uint8_t)val;
    ESP_LOGI(TAG, "SIM socket: SET_VDD 0x%02X", vdd_raw_);

  } else if (tok[0] == "GET_REG" && tok.size() >= 2) {
    char *end;
    unsigned long addr = strtoul(tok[1].c_str(), &end, 16);
    uint8_t val = (addr & 0x40) ? space_b_regs_[addr & 0x3F] : regs_[addr & 0x3F];
    char out[16];
    snprintf(out, sizeof(out), "0x%02X\n", val);
    send(fd, out, strlen(out), 0);
    return;

  // GET_PENDING_TIMER — return pending_irq_timer_ WITHOUT clearing (for tests)
  } else if (tok[0] == "GET_PENDING_TIMER") {
    char out[16];
    snprintf(out, sizeof(out), "0x%02X\n", pending_irq_timer_);
    send(fd, out, strlen(out), 0);
    return;

  // SET_NRE_MODE hw|sim — control how NRE is signalled on WUPA with no tags
  //   hw:  only pending_irq_timer_=0x40 (real ST25R3916 behaviour)
  //   sim: also pending_irq_main_=0x01  (fast-path, default)
  } else if (tok[0] == "SET_NRE_MODE" && tok.size() >= 2) {
    if (tok[1] == "hw") {
      nre_hw_mode_ = true;
      ESP_LOGI(TAG, "SIM socket: SET_NRE_MODE hw");
    } else {
      nre_hw_mode_ = false;
      ESP_LOGI(TAG, "SIM socket: SET_NRE_MODE sim");
    }

  } else {
    response = "ERROR unknown command\n";
  }

  send(fd, response.c_str(), response.size(), 0);
}

void ST25RSim::socket_thread_func_() {
  ::unlink(socket_path_.c_str());

  int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    ESP_LOGE(TAG, "SIM: socket() failed");
    return;
  }

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  if (::bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "SIM: bind() failed on %s", socket_path_.c_str());
    ::close(server_fd);
    return;
  }

  ::listen(server_fd, 8);

  struct timeval tv{1, 0};
  setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  ESP_LOGI(TAG, "SIM: listening on %s", socket_path_.c_str());

  while (running_) {
    int client_fd = ::accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) continue;
    handle_client_(client_fd);
    ::close(client_fd);
  }

  ::close(server_fd);
  ::unlink(socket_path_.c_str());
}

}  // namespace st25r_sim
}  // namespace esphome
