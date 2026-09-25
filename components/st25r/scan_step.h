#pragma once

// Timeout decisions for the NFC-A scan state machine in ST25R::process_state().
//
// Header-only and free of ESPHome dependencies so the host unit tests in
// tests/unit can exercise the exact code the driver runs. Same pattern as
// st25r3916_frame.h and isodep_wtx.h.
//
// Both exist because a scan that never returns to STATE_IDLE is a silent
// failure: update() skips the scan *and* the health check while a scan is in
// flight, so the status sensor keeps its last value (healthy) while nothing is
// read. That is how a door reader sat "healthy" for days reading no tags.

#include <cstdint>

namespace esphome {
namespace st25r {

// No-response budget for one anticollision frame. The tag answers ~90 us after
// our frame ends, so 20 ms is generous even on a slow main loop.
static constexpr uint32_t kAnticolTimeoutMs = 20;

// Hard ceiling on one whole scan (WUPA through the last SELECT). Every state
// has its own timeout; this is the backstop for a path that does not, so a
// wedge costs one scan instead of the reader. The longest legitimate scan is
// the collision brute-force walking 255 prefixes at a few tens of ms each,
// which fits comfortably inside this.
static constexpr uint32_t kScanWatchdogMs = 10000;

// Bytes in a complete anticollision answer: 4 UID bytes + BCC.
static constexpr uint8_t kAnticolAnswerBytes = 5;

enum class AnticolStep : uint8_t {
  WAIT,       // nothing usable yet, still inside the budget
  TIMEOUT,    // budget spent without a usable answer
  COLLISION,  // bit collision: narrow the prefix and resend
  ANSWER,     // full UID + BCC in the FIFO
};

// What STATE_ANTICOL should do next.
//
// irq_seen:   RXE, COL or TXE has been seen since the anticol frame was sent.
// collision:  COL is among them.
// fifo_bytes: bytes in the FIFO (only read when irq_seen).
// elapsed_ms: time since the anticol frame was sent.
//
// An IRQ is not an answer. TXE fires for our own frame, and RXE can end a
// truncated or noise frame. On the ST25R300 the IRQ bits also accumulate until
// the next frame is sent, so once TXE is set it stays set. Anything short of a
// collision or a full answer therefore has to fall through to the timeout, or
// the state machine never leaves STATE_ANTICOL. A full answer is accepted even
// after the budget, so a slow main loop does not throw it away.
inline AnticolStep anticol_step(bool irq_seen, bool collision, uint8_t fifo_bytes, uint32_t elapsed_ms) {
  if (irq_seen) {
    if (collision)
      return AnticolStep::COLLISION;
    if (fifo_bytes >= kAnticolAnswerBytes)
      return AnticolStep::ANSWER;
  }
  return elapsed_ms > kAnticolTimeoutMs ? AnticolStep::TIMEOUT : AnticolStep::WAIT;
}

// True once a scan has run past kScanWatchdogMs. Unsigned subtraction keeps it
// correct across the millis() wrap at ~49.7 days.
inline bool scan_overran(uint32_t now_ms, uint32_t scan_started_ms) {
  return (uint32_t) (now_ms - scan_started_ms) > kScanWatchdogMs;
}

}  // namespace st25r
}  // namespace esphome
