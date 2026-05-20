#pragma once

// ISO 14443-4 S(WTX) handling loop, extracted as a free function so it can be
// host-unit-tested without dragging in the rest of the ST25R driver surface
// (SPI/I2C bus, IRQ pin, FIFO commands etc).
//
// The function is header-only and templated on the transceive callable, so the
// production code in st25r.cpp calls it inline against `this->transceive_(...)`
// and tests can plug in a scripted mock.
//
// Contract:
//
//  * `tx` is a callable with signature:
//        bool tx(const uint8_t *frame, uint8_t frame_len,
//                uint8_t *raw_resp, uint8_t &raw_len)
//    It returns true on success and writes the raw frame received from the
//    PICC (including the leading PCB) into raw_resp/raw_len.
//
//  * `block_number_io` is the caller's persistent block-number bit. The
//    function toggles it on a successful I-Block exchange — matching the
//    ISO 14443-4 PCB block-number alternation rule.
//
//  * `resp` / `resp_len` receive the I-Block payload (PCB stripped). The
//    caller is responsible for providing at least 63 bytes of space (the same
//    bound that the production code enforces).
//
//  * On S(WTX) requests (PCB & 0xF0 == 0xF0), the function ACKs with PCB=0xF2
//    plus the echoed INF (WTXM) byte and re-enters the wait. Up to 8 WTX
//    iterations are accepted before giving up — Android HCE cold-start has
//    been observed to issue 1-3 in succession on first tap.
//
//  * Any S-Block other than WTX (e.g. S(DESELECT) PCB=0xC2) → returns false.
//  * Any R-Block (PCB & 0xC0 == 0x80) → returns false.
//  * Truncated raw response (raw_len < 1, or I-Block with raw_len < 3) →
//    returns false.
//
// Why a template: avoids any function-pointer indirection cost in the
// production path. The compiler will inline the call to `tx` against
// `transceive_` exactly as before.

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace esphome {
namespace st25r {

static constexpr uint8_t kIsodepMaxWtx = 8;
static constexpr uint8_t kIsodepFrameMax = 64;

template <typename Tx>
inline bool isodep_process_loop(const uint8_t *apdu, size_t apdu_len,
                                uint8_t *resp, uint8_t &resp_len,
                                uint8_t &block_number_io,
                                Tx tx) {
  uint8_t frame[kIsodepFrameMax];
  uint8_t frame_len;
  if (apdu_len + 1 > sizeof(frame)) return false;

  // PCB: I-Block, no chaining, no DID/NAD, must-be-1 bit, block number.
  frame[0] = 0x02 | (block_number_io & 0x01);
  std::memcpy(frame + 1, apdu, apdu_len);
  frame_len = static_cast<uint8_t>(apdu_len + 1);

  uint8_t raw_resp[kIsodepFrameMax];
  uint8_t raw_len = 0;

  for (uint8_t loop = 0; loop < kIsodepMaxWtx; ++loop) {
    if (!tx(frame, frame_len, raw_resp, raw_len) || raw_len < 1) {
      return false;
    }

    const uint8_t pcb = raw_resp[0];

    // S-Block? bits 7,6 = 11.
    if ((pcb & 0xC0) == 0xC0) {
      // S(WTX) request: bits 5,4 = 11. INF byte carries WTXM (1-59).
      if ((pcb & 0x30) == 0x30 && raw_len >= 2) {
        const uint8_t wtxm = raw_resp[1];
        frame[0] = 0xF2;
        frame[1] = wtxm;
        frame_len = 2;
        continue;
      }
      // S(DESELECT) or other S-block we don't handle.
      return false;
    }

    // R-Block? bits 7,6 = 10. Shouldn't happen with our simple flow.
    if ((pcb & 0xC0) == 0x80) {
      return false;
    }

    // I-Block (bits 7,6 = 00). Valid response.
    if (raw_len < 3) {
      return false;
    }
    block_number_io ^= 1;
    resp_len = static_cast<uint8_t>(raw_len - 1);
    std::memcpy(resp, raw_resp + 1, resp_len);
    return true;
  }

  return false;
}

// Strip the trailing 2 bytes (the ISO 14443 CRC trailer) from a chip-FIFO
// response. The ST25R300's RX_CRC bit only validates the CRC; it does not
// remove it from the FIFO. Without this, every with_crc=true read leaks two
// garbage bytes that callers misinterpret as part of the payload (the ISO-DEP
// SW1 SW2 in particular).
//
// Returns true if the resulting length is still valid (>= 0). Specifically:
//  * with_crc=false → no-op, returns true, *len unchanged.
//  * with_crc=true,  *len >= 2 → *len -= 2, returns true.
//  * with_crc=true,  *len == 1 → returns false (truncated, can't strip).
//  * with_crc=true,  *len == 0 → no-op, returns true (nothing to strip,
//                                  caller will treat as "no response").
//
// Extracted into a free function purely so the strip rule can be unit-tested
// without instantiating an ST25R300.
inline bool strip_trailing_crc(uint8_t & /*ref to mutate*/ len, bool with_crc) {
  if (!with_crc) return true;
  if (len == 0) return true;
  if (len < 2) return false;
  len = static_cast<uint8_t>(len - 2);
  return true;
}

}  // namespace st25r
}  // namespace esphome
