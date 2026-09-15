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

// Run the receive side of transceive_ex(): the FIFO is filled in `chunks`
// deliveries, with one poll after each, then end-of-receive.
struct RxResult {
  bool ok;
  uint8_t resp_len;
  bool canary_intact;
  size_t left_in_fifo;
};

static RxResult receive(const std::vector<uint8_t> &on_air, size_t chunks, bool strip_crc,
                        uint8_t *resp_out = nullptr) {
  MockChip chip;
  auto rr = [&chip](uint8_t reg) { return chip.read_register(reg); };
  auto rf = [&chip](uint8_t *b, size_t n) { chip.read_fifo(b, n); };

  // Caller buffer is exactly kSt25r3916RxCapacity, followed by a canary.
  uint8_t buf[kSt25r3916RxCapacity + 32];
  memset(buf, 0xA5, sizeof(buf));

  size_t received = 0;
  size_t per = (on_air.size() + chunks - 1) / chunks;
  size_t pos = 0;
  while (pos < on_air.size()) {
    size_t n = std::min(per, on_air.size() - pos);
    chip.push(std::vector<uint8_t>(on_air.begin() + pos, on_air.begin() + pos + n));
    pos += n;
    st25r3916_drain_fifo(rr, rf, buf, kSt25r3916RxCapacity, received);
  }
  // A last poll at IRQ_RXE, as the driver does.
  st25r3916_drain_fifo(rr, rf, buf, kSt25r3916RxCapacity, received);

  RxResult r;
  r.resp_len = 0;
  r.ok = st25r3916_finish_rx(received, kSt25r3916RxCapacity, strip_crc, r.resp_len);
  r.canary_intact = true;
  for (size_t i = kSt25r3916RxCapacity; i < sizeof(buf); i++)
    if (buf[i] != 0xA5)
      r.canary_intact = false;
  r.left_in_fifo = chip.fifo.size();
  if (resp_out)
    memcpy(resp_out, buf, kSt25r3916RxCapacity);
  return r;
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

// ── Bug 2: FIFO count and receive capacity ───────────────────────────────────

static void test_fifo_count_high_bits() {
  struct {
    uint8_t s1, s2;
    uint16_t want;
  } cases[] = {
      {0x00, 0x00, 0},   {0xFF, 0x00, 255}, {0x00, 0x40, 256}, {0x01, 0x40, 257},
      {0x00, 0x80, 512}, {0x05, 0x3F, 5},   {0x02, 0x7F, 258},
  };
  for (auto &c : cases) {
    uint16_t got = st25r3916_fifo_count(c.s1, c.s2);
    CHECK(got == c.want, "FIFO_STATUS1=0x%02X FIFO_STATUS2=0x%02X: got %u, want %u", c.s1, c.s2, got, c.want);
  }
  PASS("FIFO count uses fifo_b[9:8] from FIFO_STATUS2 and ignores its flag bits");
}

// `with_crc`: the frame arrives with CRC-A behind it in the FIFO, and the
// caller asks transceive_ex() to remove it, as Transmit With CRC does.
static std::vector<uint8_t> on_air(const std::vector<uint8_t> &payload, bool with_crc) {
  return with_crc ? frame_with_crc(payload) : payload;
}

static void test_rx_fits(size_t payload_len, size_t chunks, bool with_crc) {
  std::vector<uint8_t> payload = pattern(payload_len);
  uint8_t resp[kSt25r3916RxCapacity];
  RxResult r = receive(on_air(payload, with_crc), chunks, with_crc, resp);
  const char *crc = with_crc ? " + CRC" : "";
  CHECK(r.ok, "payload %zu%s in %zu poll(s): transceive reported failure (resp_len=%u)", payload_len, crc, chunks,
        r.resp_len);
  CHECK(r.resp_len == payload_len, "payload %zu%s in %zu poll(s): resp_len=%u", payload_len, crc, chunks,
        r.resp_len);
  CHECK(memcmp(resp, payload.data(), payload_len) == 0, "payload %zu%s: bytes differ", payload_len, crc);
  CHECK(r.canary_intact, "payload %zu%s: wrote past the %zu-byte buffer", payload_len, crc, kSt25r3916RxCapacity);
  CHECK(r.left_in_fifo == 0, "payload %zu%s: %zu bytes left in FIFO", payload_len, crc, r.left_in_fifo);
  PASS("RX payload %zu bytes%s, %zu poll(s): exact", payload_len, crc, chunks);
}

static void test_rx_too_long(size_t payload_len, size_t chunks, bool with_crc) {
  RxResult r = receive(on_air(pattern(payload_len), with_crc), chunks, with_crc);
  const char *crc = with_crc ? " + CRC" : "";
  CHECK(!r.ok, "payload %zu%s in %zu poll(s) exceeds %zu bytes but was reported as a good %u-byte response",
        payload_len, crc, chunks, kSt25r3916RxCapacity, r.resp_len);
  CHECK(r.resp_len <= kSt25r3916RxCapacity, "payload %zu%s: resp_len=%u beyond capacity", payload_len, crc,
        r.resp_len);
  CHECK(r.canary_intact, "payload %zu%s: wrote past the %zu-byte buffer", payload_len, crc, kSt25r3916RxCapacity);
  CHECK(r.left_in_fifo == 0, "payload %zu%s: %zu bytes left in FIFO", payload_len, crc, r.left_in_fifo);
  PASS("RX payload %zu bytes%s, %zu poll(s): rejected, no overrun", payload_len, crc, chunks);
}

static void test_status2_flags_do_not_inflate_count() {
  MockChip chip;
  chip.status2_low_bits = 0x3F;  // fifo_ovr, fifo_lb, np_lb all set
  chip.push(pattern(5));
  auto rr = [&chip](uint8_t reg) { return chip.read_register(reg); };
  auto rf = [&chip](uint8_t *b, size_t n) { chip.read_fifo(b, n); };
  uint8_t buf[kSt25r3916RxCapacity];
  size_t received = 0;
  st25r3916_drain_fifo(rr, rf, buf, kSt25r3916RxCapacity, received);
  CHECK(received == 5, "FIFO_STATUS2 flag bits changed the count: received=%zu", received);
  PASS("FIFO_STATUS2 flag bits do not change the byte count");
}

int main() {
  printf("\n=== ST25R3916 frame helper tests ===\n\n");

  printf("[TX length]\n");
  test_num_tx_roundtrip();
  test_num_tx_register_values();

  printf("\n[RX FIFO count / capacity]\n");
  test_fifo_count_high_bits();
  test_status2_flags_do_not_inflate_count();
  for (size_t n : {31, 32, 33, 63, 64}) {
    test_rx_fits(n, 1, false);
    test_rx_fits(n, 3, false);
  }
  for (size_t n : {65, 255, 256, 257}) {
    test_rx_too_long(n, 1, false);
    test_rx_too_long(n, 4, false);
  }

  printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail > 0 ? 1 : 0;
}
