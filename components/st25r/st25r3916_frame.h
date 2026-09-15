#pragma once

// ST25R3916 transceive framing helpers: the NUM_TX_BYTES encoding, the FIFO
// byte count, and the receive-side bookkeeping that ST25R::transceive_ex()
// does around them.
//
// Header-only and free of ESPHome dependencies so the host unit tests in
// tests/unit can exercise the exact code the driver runs, against a mock
// register/FIFO, without SPI or an IRQ pin. Same pattern as isodep_wtx.h.
//
// ST25R3916 only. The ST25R300 has a different register map (TX_FRAME1/2 at
// 0x34/0x35, FIFO_STATUS1/2 at 0x36/0x37) and its own transceive_ex().

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace st25r {

static constexpr uint8_t kSt25r3916RegFifoStatus1 = 0x1E;
static constexpr uint8_t kSt25r3916RegFifoStatus2 = 0x1F;

// Bytes transceive_ex() will write into a caller's response buffer. Callers
// size their buffers for this (ISO-DEP uses 64-byte frames throughout).
static constexpr size_t kSt25r3916RxCapacity = 64;

// NUM_TX_BYTES1 (0x22) / NUM_TX_BYTES2 (0x23) for a frame of whole bytes.
//
// DS12484 4.5.42/4.5.43: the byte count is one 13-bit field, ntx[12:0].
// NUM_TX_BYTES1 holds ntx[12:5]; NUM_TX_BYTES2 holds ntx[4:0] in bits 7:3 and
// the partial-bit count nbtx[2:0] below them. So the split is >> 5, not >> 8,
// which silently dropped 32 bytes for every multiple of 32 in the frame.
//
// The count excludes the CRC the chip appends on Transmit With CRC, so it is
// the same with and without CRC. The no-CRC path used to write 0 into
// NUM_TX_BYTES2, which is a length of zero for any frame under 32 bytes.
// `with_crc` stays in the signature so the call site states which command
// follows; it deliberately does not change the encoding.
inline void st25r3916_encode_num_tx(size_t n_bytes, bool /*with_crc*/, uint8_t &reg1, uint8_t &reg2) {
  reg1 = static_cast<uint8_t>((n_bytes >> 5) & 0xFF);
  reg2 = static_cast<uint8_t>((n_bytes & 0x1F) << 3);
}

// Number of bytes currently in the FIFO.
//
// DS12484 4.5.36/4.5.37: the FIFO holds 512 bytes, so the count is 10 bits.
// FIFO_STATUS1 is fifo_b[7:0]; FIFO_STATUS2 carries fifo_b[9:8] in bits 7:6,
// with fifo_ovr, fifo_lb[2:0] and np_lb below them.
inline uint16_t st25r3916_fifo_count(uint8_t status1, uint8_t status2) {
  return static_cast<uint16_t>((static_cast<uint16_t>((status2 >> 6) & 0x03) << 8) | status1);
}

// One receive poll: move whatever the FIFO holds into resp.
//
// `received` counts every byte taken out of the FIFO for this frame, including
// any that did not fit in resp. Those are still read, into a scratch buffer and
// dropped, so the FIFO empties and the next poll does not count them again.
// st25r3916_finish_rx() then sees received > capacity and fails the frame
// instead of handing back a truncated one as if it were whole.
//
// Returns the FIFO count seen, so the caller can tell that the tag is still
// talking.
template<typename ReadReg, typename ReadFifo>
inline size_t st25r3916_drain_fifo(ReadReg read_reg, ReadFifo read_fifo, uint8_t *resp, size_t capacity,
                                   size_t &received) {
  uint8_t status1 = read_reg(kSt25r3916RegFifoStatus1);
  uint8_t status2 = read_reg(kSt25r3916RegFifoStatus2);
  size_t count = st25r3916_fifo_count(status1, status2);
  if (count == 0)
    return 0;

  size_t store = received < capacity ? std::min(capacity - received, count) : 0;
  if (store > 0)
    read_fifo(resp + received, store);

  uint8_t scratch[32];
  for (size_t left = count - store; left > 0;) {
    size_t n = std::min(left, sizeof(scratch));
    read_fifo(scratch, n);
    left -= n;
  }

  received += count;
  return count;
}

// End of frame. Sets resp_len to the bytes in resp and returns true only for a
// non-empty response that fitted. `overflow`, if given, reports a frame that
// was longer than capacity.
inline bool st25r3916_finish_rx(size_t received, size_t capacity, bool /*strip_crc*/, uint8_t &resp_len,
                                bool *overflow = nullptr) {
  bool too_long = received > capacity;
  if (overflow != nullptr)
    *overflow = too_long;
  resp_len = static_cast<uint8_t>(too_long ? capacity : received);
  return !too_long && resp_len > 0;
}

}  // namespace st25r
}  // namespace esphome
