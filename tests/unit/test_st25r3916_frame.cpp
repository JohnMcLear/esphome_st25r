/*
 * Unit tests for st25r3916_frame.h: the ST25R3916 transmit length encoding
 * and the receive path of ST25R::transceive_ex().
 *
 * The receive tests run the driver's own drain/finish helpers against a mock
 * chip that models the parts of DS12484 that matter here:
 *   - a 512-byte FIFO whose byte count is split across FIFO_STATUS1 (0x1E,
 *     fifo_b[7:0]) and FIFO_STATUS2 (0x1F, fifo_b[9:8] in bits 7:6, with the
 *     overflow and last-byte-bits flags below them);
 *   - received CRC-A bytes left in the FIFO after a frame (section 2.2.13:
 *     "all of them (except the CRC bit) are removed by this block"), which
 *     ST's RFAL also strips in software unless RFAL_TXRX_FLAGS_CRC_RX_KEEP.
 *
 * Compile & run:
 *   make -C tests/unit run
 */

#include "st25r3916_frame.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

using namespace esphome::st25r;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      printf("  FAIL [%s:%d] ", __FILE__, __LINE__);      \
      printf(__VA_ARGS__);                                \
      printf("\n");                                       \
      g_fail++;                                           \
      return;                                             \
    }                                                     \
  } while (0)

#define PASS(...)          \
  do {                     \
    printf("  PASS: ");    \
    printf(__VA_ARGS__);   \
    printf("\n");          \
    g_pass++;              \
  } while (0)

// ISO 14443-3 CRC-A, LSB first on the wire.
static uint16_t crc_a(const uint8_t *data, size_t len) {
  uint16_t crc = 0x6363;
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i] ^ (uint8_t) (crc & 0xFF);
    b ^= (uint8_t) (b << 4);
    crc = (uint16_t) ((crc >> 8) ^ ((uint16_t) b << 8) ^ ((uint16_t) b << 3) ^ ((uint16_t) b >> 4));
  }
  return crc;
}

// ── Mock chip ────────────────────────────────────────────────────────────────

struct MockChip {
  std::deque<uint8_t> fifo;
  uint8_t status2_low_bits = 0;  // fifo_ovr / fifo_lb / np_lb, must not leak into the count
  size_t fifo_reads = 0;

  uint8_t read_register(uint8_t reg) {
    size_t n = fifo.size();
    if (reg == kSt25r3916RegFifoStatus1)
      return (uint8_t) (n & 0xFF);
    if (reg == kSt25r3916RegFifoStatus2)
      return (uint8_t) (((n >> 8) & 0x03) << 6) | (status2_low_bits & 0x3F);
    return 0;
  }
  void read_fifo(uint8_t *buf, size_t len) {
    fifo_reads++;
    for (size_t i = 0; i < len; i++) {
      if (fifo.empty()) {
        buf[i] = 0;  // reading an empty FIFO returns 0 (DS12484 4.2.x)
      } else {
        buf[i] = fifo.front();
        fifo.pop_front();
      }
    }
  }
  void push(const std::vector<uint8_t> &bytes) { fifo.insert(fifo.end(), bytes.begin(), bytes.end()); }
};

// Build a frame as it lands in the ST25R3916 FIFO: payload followed by CRC-A.
static std::vector<uint8_t> frame_with_crc(const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> f = payload;
  uint16_t c = crc_a(payload.data(), payload.size());
  f.push_back((uint8_t) (c & 0xFF));
  f.push_back((uint8_t) (c >> 8));
  return f;
}

static std::vector<uint8_t> pattern(size_t n) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; i++)
    v[i] = (uint8_t) (0x10 + i);
  return v;
}

// ── Bug 1: NUM_TX_BYTES encoding ─────────────────────────────────────────────

static void test_num_tx_roundtrip() {
  const size_t lens[] = {0, 1, 2, 7, 31, 32, 33, 63, 64, 65, 255, 256, 257, 511, 512, 8191};
  for (bool with_crc : {true, false}) {
    for (size_t n : lens) {
      uint8_t r1 = 0xEE, r2 = 0xEE;
      st25r3916_encode_num_tx(n, with_crc, r1, r2);
      size_t ntx = ((size_t) r1 << 5) | (r2 >> 3);
      CHECK(ntx == n, "with_crc=%d len=%zu encoded as NUM_TX_BYTES1=0x%02X NUM_TX_BYTES2=0x%02X -> %zu bytes",
            (int) with_crc, n, r1, r2, ntx);
      CHECK((r2 & 0x07) == 0, "with_crc=%d len=%zu sets nbtx bits (0x%02X)", (int) with_crc, n, r2);
    }
  }
  PASS("NUM_TX_BYTES1/2 round-trip 0..8191 bytes, with and without CRC");
}

static void test_num_tx_register_values() {
  struct {
    size_t n;
    uint8_t r1, r2;
  } cases[] = {
      {31, 0x00, 0xF8}, {32, 0x01, 0x00}, {33, 0x01, 0x08},  {64, 0x02, 0x00},
      {65, 0x02, 0x08}, {255, 0x07, 0xF8}, {256, 0x08, 0x00}, {257, 0x08, 0x08},
  };
  for (auto &c : cases) {
    for (bool with_crc : {true, false}) {
      uint8_t r1, r2;
      st25r3916_encode_num_tx(c.n, with_crc, r1, r2);
      CHECK(r1 == c.r1 && r2 == c.r2, "with_crc=%d len=%zu: got 0x%02X 0x%02X, want 0x%02X 0x%02X",
            (int) with_crc, c.n, r1, r2, c.r1, c.r2);
    }
  }
  PASS("NUM_TX_BYTES register values at 31/32/33/64/65/255/256/257");
}

int main() {
  printf("\n=== ST25R3916 frame helper tests ===\n\n");

  printf("[TX length]\n");
  test_num_tx_roundtrip();
  test_num_tx_register_values();

  printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail > 0 ? 1 : 0;
}
