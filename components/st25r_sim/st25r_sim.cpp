/*
 * ST25RSim — software simulation of the ST25R3916 NFC reader IC.
 *
 * Simulates the ISO 14443-A anticollision/select protocol in-process so that
 * the base ST25R state machine can be exercised without physical hardware.
 *
 * A Unix-domain socket server accepts ADD_TAG / REMOVE_TAG / LIST commands
 * from an external test runner, enabling tag-present, tag-removed, and
 * multi-tag scenarios to be scripted from CI.
 *
 * Protocol simulation
 * ───────────────────
 * 4-byte UIDs (TAG_TYPE_MIFARE_CLASSIC path):
 *   Auth cmd 0x60 receives IRQ_TXE but no IRQ_RXE; transceive_() times out
 *   after 20 ms and returns false.  Auth fails gracefully; on_tag still fires.
 *
 * 7-byte UIDs (TAG_TYPE_2 / NTAG path):
 *   Two-level cascade SELECT (CL1 SAK=0x04, CL2 SAK=0x08).  Page read 0x30
 *   returns 16 bytes beginning with 0xFE (terminator TLV) so NDEF parsing
 *   stops without further pages.
 *
 * HALT (0x50 0x00) marks the tag as halted; WUPA re-wakes all.
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
#include <fcntl.h>

namespace esphome {
namespace st25r_sim {

static const char *const TAG = "st25r_sim";

// ── IRQ_MAIN bit constants (mirrors st25r.h) ─────────────────────────────────
static constexpr uint8_t IRQ_RXE = 0x10;  // end of receive
static constexpr uint8_t IRQ_TXE = 0x08;  // end of transmission
static constexpr uint8_t IRQ_COL = 0x04;  // bit collision

// ── Register addresses ────────────────────────────────────────────────────────
static constexpr uint8_t REG_IC_IDENTITY       = 0x3F;
static constexpr uint8_t REG_IRQ_MAIN          = 0x1A;
static constexpr uint8_t REG_IRQ_TIMER         = 0x1B;
static constexpr uint8_t REG_IRQ_ERROR         = 0x1C;
static constexpr uint8_t REG_FIFO_STATUS1      = 0x1E;
static constexpr uint8_t REG_FIFO_STATUS2      = 0x1F;
static constexpr uint8_t REG_COLLISION_DISPLAY = 0x20;
static constexpr uint8_t REG_AD_CONV_RESULT    = 0x25;

// IC identity: reset_() requires (ic >> 3) == 0x05; 0x28 satisfies this.
static constexpr uint8_t IC_IDENTITY_VALUE = 0x28;

// ATQA bytes (ISO 14443-A, bit-frame anticollision, single-size UID).
static const uint8_t ATQA[2] = {0x44, 0x00};

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
  ESP_LOGI(TAG, "  Socket path: %s", socket_path_.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Inline control API (same-process)
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::add_tag(const std::vector<uint8_t> &uid) {
  std::lock_guard<std::mutex> lk(tags_mutex_);
  for (auto &t : virtual_tags_) {
    if (t.uid == uid) { t.halted = false; return; }
  }
  virtual_tags_.push_back({uid, false});
  ESP_LOGI(TAG, "SIM add_tag: uid_len=%u", (unsigned) uid.size());
}

void ST25RSim::remove_tag(const std::vector<uint8_t> &uid) {
  std::lock_guard<std::mutex> lk(tags_mutex_);
  auto it = std::remove_if(virtual_tags_.begin(), virtual_tags_.end(),
                           [&](const VirtualTag &t) { return t.uid == uid; });
  if (it != virtual_tags_.end()) {
    virtual_tags_.erase(it, virtual_tags_.end());
    ESP_LOGI(TAG, "SIM remove_tag: uid_len=%u", (unsigned) uid.size());
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport interface — read_register
// ─────────────────────────────────────────────────────────────────────────────

uint8_t ST25RSim::read_register(uint8_t reg) {
  switch (reg) {
    case REG_IC_IDENTITY:   return IC_IDENTITY_VALUE;
    case REG_IRQ_MAIN:      { uint8_t v = pending_irq_main_;  pending_irq_main_  = 0; return v; }
    case REG_IRQ_TIMER:     { uint8_t v = pending_irq_timer_; pending_irq_timer_ = 0; return v; }
    case REG_IRQ_ERROR:     return 0;
    case REG_FIFO_STATUS1:  return (uint8_t) std::min(fifo_out_.size(), (size_t) 255);
    case REG_FIFO_STATUS2:  return 0;  // fifo_lb=0 → last byte is full
    case REG_COLLISION_DISPLAY: return collision_display_;
    case REG_AD_CONV_RESULT:    return 0x80;  // mid-range amplitude
    default:                return regs_[reg & 0x3F];
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport interface — write_register
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::write_register(uint8_t reg, uint8_t value) {
  regs_[reg & 0x3F] = value;
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport interface — write_fifo / read_fifo
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::write_fifo(const uint8_t *data, size_t len) {
  fifo_in_.insert(fifo_in_.end(), data, data + len);
}

void ST25RSim::read_fifo(uint8_t *data, size_t len) {
  size_t n = std::min(len, fifo_out_.size());
  if (n > 0) {
    memcpy(data, fifo_out_.data(), n);
    fifo_out_.erase(fifo_out_.begin(), fifo_out_.begin() + (int) n);
  }
  if (n < len) memset(data + n, 0, len - n);
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport interface — write_command
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::write_command(uint8_t command) {
  switch (command) {
    case 0xC1:  // SET_DEFAULT
      memset(regs_, 0, sizeof(regs_));
      pending_irq_main_  = 0;
      pending_irq_timer_ = 0;
      fifo_in_.clear();
      fifo_out_.clear();
      { std::lock_guard<std::mutex> lk(tags_mutex_);
        for (auto &t : virtual_tags_) t.halted = false; }
      break;

    case 0xC2:  // STOP_ALL
    case 0xC3:  // CLEAR_FIFO
      fifo_in_.clear();
      fifo_out_.clear();
      break;

    case 0xC7:  // TRANSMIT_WUPA
      on_wupa_();
      break;

    case 0xC5:  // TRANSMIT_WITHOUT_CRC  (anticol frame)
      on_anticol_();
      break;

    case 0xC4:  // TRANSMIT_WITH_CRC  (SELECT / HALT / data reads)
      on_transmit_crc_();
      break;

    // No-ops in simulation
    case 0xC8:  // FIELD_ON
    case 0xC9:  // FIELD_OFF / NFC response field ON
    case 0xCD:  // GO_TO_SENSE
    case 0xCE:  // GO_TO_SLEEP
    case 0xD3:  // MEASURE_AMPLITUDE (AD_CONV_RESULT handled in read_register)
    case 0xD5:  // RESET_RX_GAIN
    case 0xD6:  // ADJUST_REGULATORS
      break;

    default:
      ESP_LOGV(TAG, "SIM: unhandled command 0x%02X", command);
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol handlers
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::on_wupa_() {
  fifo_out_.clear();
  collision_display_ = 0;
  last_selected_valid_ = false;

  std::lock_guard<std::mutex> lk(tags_mutex_);
  // WUPA wakes all halted tags (broadcast wake-up).
  for (auto &t : virtual_tags_) t.halted = false;

  if (!virtual_tags_.empty()) {
    fifo_out_.assign(ATQA, ATQA + 2);
    pending_irq_main_ = IRQ_RXE;
    ESP_LOGV(TAG, "SIM WUPA → ATQA (%u tag(s))", (unsigned) virtual_tags_.size());
  } else {
    pending_irq_timer_ = 0x40;  // NRE: no-response timer expired
    ESP_LOGV(TAG, "SIM WUPA → no tags");
  }
}

// Return the four UID bytes for this cascade level.
//   4-byte UID, CL1: uid[0..3]
//   7-byte UID, CL1: 0x88, uid[0], uid[1], uid[2]  (cascade byte)
//   7-byte UID, CL2: uid[3], uid[4], uid[5], uid[6]
std::array<uint8_t, 4> ST25RSim::uid_at_cl_(const VirtualTag &tag, uint8_t cl) const {
  if (cl == 0) {
    if (tag.uid.size() == 4) {
      return {tag.uid[0], tag.uid[1], tag.uid[2], tag.uid[3]};
    }
    return {0x88, tag.uid[0], tag.uid[1], tag.uid[2]};
  }
  // cl == 1, 7-byte UID
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

void ST25RSim::on_anticol_() {
  // fifo_in_ holds: [SEL, NVB, prefix_bytes...]
  // Capture and clear before any early return.
  std::vector<uint8_t> frame;
  frame.swap(fifo_in_);
  fifo_out_.clear();
  collision_display_ = 0;

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

  // Collect matching non-halted tags
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
    // Zero prefix bits so firmware can OR them back (real chip behaviour).
    for (uint8_t i = 0; i < prefix_full; i++) uid4[i] = 0;
    if (prefix_bits > 0) {
      uint8_t mask = (uint8_t)((1u << prefix_bits) - 1u);
      uid4[prefix_full] &= (uint8_t)(~mask);
    }
    uint8_t bcc = uid4[0] ^ uid4[1] ^ uid4[2] ^ uid4[3];
    fifo_out_ = {uid4[0], uid4[1], uid4[2], uid4[3], bcc};
    pending_irq_main_ = IRQ_RXE;
    ESP_LOGV(TAG, "SIM ANTICOL CL%u → single match %02X %02X %02X %02X",
             cl, uid4[0], uid4[1], uid4[2], uid4[3]);
    return;
  }

  // Multiple tags → signal collision at the first differing bit.
  std::lock_guard<std::mutex> lk(tags_mutex_);
  auto uid_a = uid_at_cl_(virtual_tags_[matches[0]], cl);
  auto uid_b = uid_at_cl_(virtual_tags_[matches[1]], cl);
  int col_uid_bit = 0;
  bool col_found = false;
  for (int byte_i = 0; byte_i < 4 && !col_found; byte_i++) {
    uint8_t diff = uid_a[byte_i] ^ uid_b[byte_i];
    if (diff) {
      for (int bit_i = 0; bit_i < 8; bit_i++) {
        if (diff & (1u << bit_i)) {
          col_uid_bit = byte_i * 8 + bit_i;
          col_found = true;
          break;
        }
      }
    }
  }
  // COLLISION_DISPLAY encodes position from start of TX frame (SEL+NVB = 16 bits).
  int abs_bit = col_uid_bit + 16;
  uint8_t c_byte = (uint8_t)(abs_bit / 8);
  uint8_t c_bit  = (uint8_t)(abs_bit % 8);
  collision_display_ = (uint8_t)((c_byte << 4) | (c_bit << 1));
  pending_irq_main_ = IRQ_COL;
  ESP_LOGV(TAG, "SIM ANTICOL CL%u → collision uid_bit=%d col_disp=0x%02X (found=%d)",
           cl, col_uid_bit, collision_display_, (int) col_found);
}

void ST25RSim::on_transmit_crc_() {
  // fifo_in_ holds the complete frame the firmware wrote via write_fifo().
  // Capture it before clearing.
  std::vector<uint8_t> frame;
  frame.swap(fifo_in_);
  fifo_out_.clear();

  if (frame.empty()) { pending_irq_main_ = IRQ_TXE; return; }

  uint8_t cmd = frame[0];

  // ── SELECT (93 70 / 95 70 / 97 70) ──────────────────────────────────────
  if ((cmd == 0x93 || cmd == 0x95 || cmd == 0x97) &&
      frame.size() >= 2 && frame[1] == 0x70) {
    // frame: [SEL, 0x70, uid0, uid1, uid2, uid3, bcc]  (CRC appended by chip)
    if (frame.size() < 6) { pending_irq_main_ = IRQ_TXE; return; }
    uint8_t uid4[4] = {frame[2], frame[3], frame[4], frame[5]};
    uint8_t cl = (cmd == 0x93) ? 0 : (cmd == 0x95) ? 1 : 2;

    std::lock_guard<std::mutex> lk(tags_mutex_);
    for (size_t i = 0; i < virtual_tags_.size(); i++) {
      if (virtual_tags_[i].halted) continue;
      auto uid_cl = uid_at_cl_(virtual_tags_[i], cl);
      if (uid_cl[0] == uid4[0] && uid_cl[1] == uid4[1] &&
          uid_cl[2] == uid4[2] && uid_cl[3] == uid4[3]) {
        // SAK: cascade bit (bit2) set when tag has more CL levels.
        bool needs_cascade = (cl == 0 && virtual_tags_[i].uid.size() > 4);
        uint8_t sak = needs_cascade ? 0x04 : 0x08;
        // Remember the selected tag so HALT can target the right one.
        last_selected_uid_ = uid_cl;
        last_selected_cl_ = cl;
        last_selected_valid_ = true;
        fifo_out_ = {sak};
        pending_irq_main_ = IRQ_TXE | IRQ_RXE;
        ESP_LOGV(TAG, "SIM SELECT CL%u → SAK=0x%02X", cl, sak);
        return;
      }
    }
    // No matching tag (stale SELECT after removal).
    pending_irq_main_ = IRQ_TXE;
    return;
  }

  // ── HALT (50 00) ──────────────────────────────────────────────────────────
  if (cmd == 0x50) {
    std::lock_guard<std::mutex> lk(tags_mutex_);
    if (last_selected_valid_) {
      // Halt the tag that was most recently SELECTed.
      for (auto &t : virtual_tags_) {
        if (!t.halted) {
          auto uid_cl = uid_at_cl_(t, last_selected_cl_);
          if (uid_cl == last_selected_uid_) { t.halted = true; break; }
        }
      }
      last_selected_valid_ = false;
    } else {
      // Fallback: halt the first non-halted tag.
      for (auto &t : virtual_tags_) {
        if (!t.halted) { t.halted = true; break; }
      }
    }
    // No response expected; firmware does delay(10) without polling.
    pending_irq_main_ = 0;
    ESP_LOGV(TAG, "SIM HALT → tag halted");
    return;
  }

  // ── Mifare auth (60 / 61) ─────────────────────────────────────────────────
  // Return IRQ_TXE but no IRQ_RXE; transceive_() will time out after 20 ms.
  // Auth fails gracefully; on_tag still fires.
  if (cmd == 0x60 || cmd == 0x61) {
    pending_irq_main_ = IRQ_TXE;
    ESP_LOGV(TAG, "SIM MIFARE AUTH → no response (will timeout)");
    return;
  }

  // ── NTAG page read (30 <page>) ────────────────────────────────────────────
  if (cmd == 0x30) {
    // Return 16 bytes starting with TLV terminator (0xFE) so NDEF parsing
    // stops without further reads.
    fifo_out_.resize(16, 0x00);
    fifo_out_[0] = 0xFE;
    pending_irq_main_ = IRQ_TXE | IRQ_RXE;
    ESP_LOGV(TAG, "SIM PAGE READ 0x30 → 16 bytes (terminator)");
    return;
  }

  // ── Unknown / other commands ──────────────────────────────────────────────
  // Return IRQ_TXE with empty FIFO so transceive_ times out cleanly.
  pending_irq_main_ = IRQ_TXE;
  ESP_LOGV(TAG, "SIM TRANSMIT_WITH_CRC: unhandled cmd=0x%02X", cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Unix-domain socket server
// ─────────────────────────────────────────────────────────────────────────────

void ST25RSim::start_socket_server_() {
  socket_thread_ = std::thread([this]() { socket_thread_func_(); });
  socket_thread_.detach();
}

// Parse a hex string like "DEADBEEF" into bytes.
static std::vector<uint8_t> hex_to_bytes(const std::string &hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    char buf[3] = {hex[i], hex[i + 1], '\0'};
    out.push_back((uint8_t) strtol(buf, nullptr, 16));
  }
  return out;
}

void ST25RSim::handle_client_(int fd) {
  char buf[256];
  int n = (int) recv(fd, buf, sizeof(buf) - 1, 0);
  if (n <= 0) return;
  buf[n] = '\0';

  // Strip newline
  std::string line(buf);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
    line.pop_back();

  std::string response = "OK\n";

  if (line.substr(0, 8) == "ADD_TAG ") {
    auto uid = hex_to_bytes(line.substr(8));
    if (!uid.empty()) add_tag(uid);
    else response = "ERROR bad UID\n";

  } else if (line.substr(0, 11) == "REMOVE_TAG ") {
    auto uid = hex_to_bytes(line.substr(11));
    if (!uid.empty()) remove_tag(uid);
    else response = "ERROR bad UID\n";

  } else if (line == "LIST") {
    std::string out;
    {
      std::lock_guard<std::mutex> lk(tags_mutex_);
      for (const auto &t : virtual_tags_) {
        out += "TAG ";
        for (uint8_t b : t.uid) {
          char hex[3]; snprintf(hex, sizeof(hex), "%02X", b);
          out += hex;
        }
        out += t.halted ? " HALTED\n" : "\n";
      }
    }
    out += "END\n";
    send(fd, out.c_str(), out.size(), 0);
    return;

  } else {
    response = "ERROR unknown command\n";
  }

  send(fd, response.c_str(), response.size(), 0);
}

void ST25RSim::socket_thread_func_() {
  // Remove stale socket file.
  ::unlink(socket_path_.c_str());

  int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    ESP_LOGE(TAG, "SIM: socket() failed");
    return;
  }

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  if (::bind(server_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "SIM: bind() failed on %s", socket_path_.c_str());
    ::close(server_fd);
    return;
  }

  ::listen(server_fd, 8);

  // Set non-blocking with 1 s accept timeout so we can check running_.
  struct timeval tv{1, 0};
  setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  ESP_LOGI(TAG, "SIM: socket server listening on %s", socket_path_.c_str());

  while (running_) {
    int client_fd = ::accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) continue;  // timeout or signal — loop
    handle_client_(client_fd);
    ::close(client_fd);
  }

  ::close(server_fd);
  ::unlink(socket_path_.c_str());
}

}  // namespace st25r_sim
}  // namespace esphome
