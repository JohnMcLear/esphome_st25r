#pragma once

#include "esphome/components/st25r/st25r.h"
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace esphome {
namespace st25r_sim {

// A virtual NFC tag in the simulated RF field.
struct VirtualTag {
  std::vector<uint8_t> uid;  // 4 or 7 byte UID
  bool halted{false};
};

// ST25RSim — in-process software simulation of the ST25R3916 IC.
//
// Implements the five abstract transport methods from ST25R using an
// in-memory register bank and FIFO. ISO 14443-A protocol events are
// synthesised in response to the direct-command writes that the base
// class state machine issues.
//
// A Unix-domain socket server (socket_path_) lets an external process
// add and remove virtual tags at runtime:
//
//   ADD_TAG <uid_hex>\n    → acknowledge with "OK\n"
//   REMOVE_TAG <uid_hex>\n → acknowledge with "OK\n"
//   LIST\n                 → list active tags, ending with "END\n"
//
class ST25RSim : public st25r::ST25R {
 public:
  void setup() override;
  void dump_config() override;

  void set_socket_path(const std::string &path) { socket_path_ = path; }

  // Inline control API (callable from the same process / test harness).
  void add_tag(const std::vector<uint8_t> &uid);
  void remove_tag(const std::vector<uint8_t> &uid);

 protected:
  // ── ST25R transport interface ─────────────────────────────────────────────
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

  // ── Helpers ───────────────────────────────────────────────────────────────
  // Return the 4 UID bytes presented at cascade level cl (0=CL1, 1=CL2).
  std::array<uint8_t, 4> uid_at_cl_(const VirtualTag &tag, uint8_t cl) const;

  // Returns true if tag matches the given anticol prefix at cascade level cl.
  bool matches_prefix_(const VirtualTag &tag, uint8_t cl,
                       const uint8_t *prefix, uint8_t prefix_full,
                       uint8_t prefix_bits) const;

  // ── Socket server ─────────────────────────────────────────────────────────
  void start_socket_server_();
  void socket_thread_func_();
  void handle_client_(int fd);

  // ── Simulated hardware state ──────────────────────────────────────────────
  uint8_t regs_[0x40]{};        // register bank
  std::vector<uint8_t> fifo_in_;   // bytes written by firmware via write_fifo()
  std::vector<uint8_t> fifo_out_;  // bytes the firmware will read via read_fifo()

  // UID of the most-recently SELECTed tag, so we can HALT the right one.
  std::array<uint8_t, 4> last_selected_uid_{};
  uint8_t last_selected_cl_{0};
  bool last_selected_valid_{false};

  // Pending IRQ registers (cleared on read, matching real chip behaviour).
  uint8_t pending_irq_main_{0};
  uint8_t pending_irq_timer_{0};
  uint8_t collision_display_{0};

  // ── Virtual RF field ──────────────────────────────────────────────────────
  std::vector<VirtualTag> virtual_tags_;
  std::mutex tags_mutex_;

  // ── Control socket ────────────────────────────────────────────────────────
  std::string socket_path_{"/tmp/st25r_sim.sock"};
  std::thread socket_thread_;
  std::atomic<bool> running_{true};
};

}  // namespace st25r_sim
}  // namespace esphome
