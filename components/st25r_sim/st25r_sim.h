#pragma once

#include "esphome/components/st25r/st25r.h"
#include "esphome/components/st25r/crypto1.h"
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace esphome {
namespace st25r_sim {

// ── Tag type enumeration ───────────────────────────────────────────────────────
enum TagType : uint8_t {
  TAG_MIFARE_CLASSIC_1K = 0,  // 4-byte UID, SAK=0x08
  TAG_MIFARE_CLASSIC_4K,      // 4-byte UID, SAK=0x18
  TAG_NTAG213,                // 7-byte UID, SAK=0x00
  TAG_NTAG215,                // 7-byte UID, SAK=0x00
  TAG_NTAG216,                // 7-byte UID, SAK=0x00
  TAG_MIFARE_ULTRALIGHT,      // 7-byte UID, SAK=0x00
  TAG_ISO15693,               // 8-byte UID, ISO 15693 / NFC-V
};

// ── Virtual NFC tag ────────────────────────────────────────────────────────────
struct VirtualTag {
  std::vector<uint8_t> uid;
  TagType type{TAG_MIFARE_CLASSIC_1K};
  bool halted{false};
  uint64_t key_a{0xFFFFFFFFFFFFULL};  // Mifare Key A (default factory)
  uint64_t key_b{0xFFFFFFFFFFFFULL};  // Mifare Key B
  // Raw NDEF record bytes (not wrapped in TLV); used when building block data
  // or NTAG page responses. Empty = no NDEF.
  std::vector<uint8_t> ndef_data;
  // Flat page memory for NTAG / Ultralight (4 bytes per page).
  // Initialized by init_ntag_pages_(). Empty for Mifare Classic.
  std::vector<uint8_t> page_mem_;
};

// ── Mifare Classic auth state ─────────────────────────────────────────────────
// Tracks a single in-progress or completed 3-pass auth exchange.
struct AuthState {
  bool pending{false};        // we sent NT; waiting for reader NR+AR
  bool authenticated{false};  // auth complete; block reads now encrypted
  uint32_t nt{0};             // tag nonce we generated
  uint32_t uid_u32{0};        // lower 4 bytes of tag UID as uint32
  uint64_t key{0};
  struct Crypto1State cs{};   // LFSR state; advanced by each block transact.
};

// ── ST25RSim ──────────────────────────────────────────────────────────────────
// In-process simulation of the ST25R3916 NFC reader IC.
//
// Simulates ISO 14443-A anticollision/select, Mifare Classic 3-pass auth
// (full Crypto1 exchange), and NDEF page reads for NTAG / Mifare tags.
//
// Unix-domain socket control protocol:
//   ADD_TAG <uid_hex> [TYPE=<type>] [KEY_A=<hex>] [KEY_B=<hex>] [NDEF=<hex>]
//   REMOVE_TAG <uid_hex>
//   SET_KEY <uid_hex> A|B <key_hex>
//   SET_NDEF <uid_hex> <ndef_hex>
//   LIST
//   SET_IC_IDENTITY <hex>
//   GET_REG <addr_hex>
//
// Tag type names: MIFARE_1K, MIFARE_4K, NTAG213, NTAG215, NTAG216, ULTRALIGHT
// (default: MIFARE_1K for 4-byte UIDs, NTAG213 for 7-byte UIDs)
//
class ST25RSim : public st25r::ST25R {
 public:
  void setup() override;
  void dump_config() override;

  void set_socket_path(const std::string &path) { socket_path_ = path; }
  void set_ic_identity(uint8_t id) { ic_identity_ = id; }

  // Inline API (same-process).
  void add_tag(const std::vector<uint8_t> &uid,
               TagType type = TAG_MIFARE_CLASSIC_1K);
  void remove_tag(const std::vector<uint8_t> &uid);

 protected:
  // ── ST25R transport virtuals ──────────────────────────────────────────────
  uint8_t read_register(uint8_t reg) override;
  void write_register(uint8_t reg, uint8_t value) override;
  void write_command(uint8_t command) override;
  void write_fifo(const uint8_t *data, size_t len) override;
  void read_fifo(uint8_t *data, size_t len) override;

 private:
  // ── Command handlers ──────────────────────────────────────────────────────
  void on_wupa_();
  void on_anticol_();
  void on_transmit_crc_();
  void on_nfcv_transmit_();  // Handle NFC-V streaming mode transceive

  // Mifare sub-handlers (called from on_anticol_ when auth state active)
  void on_mifare_auth_response_(const std::vector<uint8_t> &frame);
  void on_mifare_block_read_(const std::vector<uint8_t> &frame);

  // ── Helpers ───────────────────────────────────────────────────────────────
  std::array<uint8_t, 4> uid_at_cl_(const VirtualTag &tag, uint8_t cl) const;
  bool matches_prefix_(const VirtualTag &tag, uint8_t cl,
                       const uint8_t *prefix, uint8_t prefix_full,
                       uint8_t prefix_bits) const;
  uint8_t final_sak_for_(const VirtualTag &tag) const;
  // Build the 16-byte block data for a given Mifare block index.
  void build_block_(const VirtualTag &tag, uint8_t block, uint8_t out[16]) const;

  // ── Socket server ─────────────────────────────────────────────────────────
  void start_socket_server_();
  void socket_thread_func_();
  void handle_client_(int fd);

  // ── Simulated hardware state ──────────────────────────────────────────────
  uint8_t regs_[0x40]{};
  uint8_t space_b_regs_[0x40]{};  // Space B registers (addr >= 0x40 → index = addr & 0x3F)
  uint8_t vdd_raw_{0x80};  // Raw VDD measurement (0x80 = ~3000mV < 3600 → sup3V=1)
  uint8_t ad_conv_result_{0x80};  // AD converter output (field strength or VDD)
  std::vector<uint8_t> fifo_in_;
  std::vector<uint8_t> fifo_out_;

  // Most-recently SELECTed tag (used to target HALT and to look up auth key).
  std::array<uint8_t, 4> last_selected_uid_{};
  uint8_t last_selected_cl_{0};
  bool last_selected_valid_{false};
  std::vector<uint8_t> last_selected_full_uid_;  // full UID of selected tag

  // Pending IRQ registers (read-to-clear).
  uint8_t pending_irq_main_{0};
  uint8_t pending_irq_timer_{0};
  uint8_t collision_display_{0};
  uint8_t fifo_status2_{0};  // bits[3:1] = fifo_lb (incomplete last byte)

  // Mifare Classic auth state machine.
  AuthState auth_state_{};

  // ── Virtual RF field ──────────────────────────────────────────────────────
  std::vector<VirtualTag> virtual_tags_;
  std::mutex tags_mutex_;

  uint8_t ic_identity_{0x28};

  // ── Control socket ────────────────────────────────────────────────────────
  std::string socket_path_{"/tmp/st25r_sim.sock"};
  std::thread socket_thread_;
  std::atomic<bool> running_{true};
};

}  // namespace st25r_sim
}  // namespace esphome
