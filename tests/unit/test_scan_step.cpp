/*
 * Unit tests for scan_step.h: the STATE_ANTICOL decision and the whole-scan
 * watchdog used by ST25R::process_state().
 *
 * The regression under test: a tag answers WUPA, then does not answer the
 * anticollision frame (moved away, or the WUPA "answer" was noise). The only
 * IRQ seen is TXE for our own frame. On the ST25R300 that bit stays set until
 * the next frame, and the old code treated any IRQ as a response and only
 * checked its timeout when no IRQ was set, so it waited in STATE_ANTICOL
 * forever. update() skips while a scan is in flight, so the health check
 * stopped too and the reader reported healthy while reading nothing.
 *
 * Compile & run:
 *   make -C tests/unit run
 */

#include "scan_step.h"

#include <cstdint>
#include <cstdio>

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

static const char *name(AnticolStep s) {
  switch (s) {
    case AnticolStep::WAIT:
      return "WAIT";
    case AnticolStep::TIMEOUT:
      return "TIMEOUT";
    case AnticolStep::COLLISION:
      return "COLLISION";
    case AnticolStep::ANSWER:
      return "ANSWER";
  }
  return "?";
}

// The garage wedge: sticky TXE, empty FIFO, and the loop keeps coming back.
// Replays what the loop sees over 1 s of iterations; it must leave ANTICOL.
static void test_sticky_txe_without_answer_times_out() {
  AnticolStep last = AnticolStep::WAIT;
  uint32_t t;
  for (t = 0; t <= 1000; t += 5) {
    last = anticol_step(/*irq_seen=*/true, /*collision=*/false, /*fifo_bytes=*/0, t);
    if (last != AnticolStep::WAIT)
      break;
  }
  CHECK(last == AnticolStep::TIMEOUT, "sticky TXE, empty FIFO: got %s after %u ms, want TIMEOUT", name(last), t);
  CHECK(t <= kAnticolTimeoutMs + 5, "timed out at %u ms, want by %u ms", t, kAnticolTimeoutMs + 5);
  PASS("sticky TXE with no answer times out at %u ms", t);
}

// RXE for a truncated/noise frame (fewer than UID+BCC bytes) is not an answer.
static void test_short_frame_times_out() {
  for (uint8_t n = 0; n < kAnticolAnswerBytes; n++) {
    AnticolStep s = anticol_step(true, false, n, kAnticolTimeoutMs + 1);
    CHECK(s == AnticolStep::TIMEOUT, "%u-byte frame after budget: got %s, want TIMEOUT", n, name(s));
  }
  PASS("0..4 byte frames time out");
}

static void test_short_frame_waits_inside_budget() {
  AnticolStep s = anticol_step(true, false, 2, kAnticolTimeoutMs);
  CHECK(s == AnticolStep::WAIT, "2-byte frame inside budget: got %s, want WAIT", name(s));
  PASS("short frame inside budget waits");
}

static void test_no_irq_waits_then_times_out() {
  AnticolStep s = anticol_step(false, false, 0, 3);
  CHECK(s == AnticolStep::WAIT, "no IRQ at 3 ms: got %s, want WAIT", name(s));
  s = anticol_step(false, false, 0, kAnticolTimeoutMs + 1);
  CHECK(s == AnticolStep::TIMEOUT, "no IRQ after budget: got %s, want TIMEOUT", name(s));
  PASS("no IRQ waits, then times out");
}

// 9ce68af: a full answer that is already in the FIFO must be taken even when
// the main loop was slower than the budget.
static void test_full_answer_accepted_after_budget() {
  AnticolStep s = anticol_step(true, false, 5, 250);
  CHECK(s == AnticolStep::ANSWER, "full answer at 250 ms: got %s, want ANSWER", name(s));
  s = anticol_step(true, false, 5, 1);
  CHECK(s == AnticolStep::ANSWER, "full answer at 1 ms: got %s, want ANSWER", name(s));
  PASS("full answer accepted inside and after budget");
}

// An IRQ must still be required: bytes left in the FIFO with no IRQ are not
// taken as an answer (they are cleared before the frame is sent anyway).
static void test_fifo_without_irq_is_not_an_answer() {
  AnticolStep s = anticol_step(false, false, 5, 1);
  CHECK(s == AnticolStep::WAIT, "FIFO bytes, no IRQ: got %s, want WAIT", name(s));
  PASS("FIFO bytes without an IRQ are not an answer");
}

static void test_collision_wins() {
  AnticolStep s = anticol_step(true, true, 5, 1);
  CHECK(s == AnticolStep::COLLISION, "collision with bytes: got %s, want COLLISION", name(s));
  s = anticol_step(true, true, 0, 500);
  CHECK(s == AnticolStep::COLLISION, "collision after budget: got %s, want COLLISION", name(s));
  PASS("collision takes precedence");
}

static void test_scan_watchdog() {
  CHECK(!scan_overran(1000, 1000), "0 ms elapsed reported as overrun");
  CHECK(!scan_overran(1000 + kScanWatchdogMs, 1000), "exactly the limit reported as overrun");
  CHECK(scan_overran(1001 + kScanWatchdogMs, 1000), "limit + 1 ms not reported as overrun");
  PASS("watchdog trips just past kScanWatchdogMs");
}

// millis() wraps after ~49.7 days; a scan straddling the wrap must not trip.
static void test_scan_watchdog_across_wrap() {
  uint32_t start = 0xFFFFFF00u;
  CHECK(!scan_overran(start + 500u, start), "500 ms across the wrap reported as overrun");
  CHECK(scan_overran(start + kScanWatchdogMs + 1u, start), "overrun across the wrap missed");
  PASS("watchdog correct across millis() wrap");
}

int main() {
  printf("=== scan_step unit tests ===\n");

  printf("\n[anticol_step]\n");
  test_sticky_txe_without_answer_times_out();
  test_short_frame_times_out();
  test_short_frame_waits_inside_budget();
  test_no_irq_waits_then_times_out();
  test_full_answer_accepted_after_budget();
  test_fifo_without_irq_is_not_an_answer();
  test_collision_wins();

  printf("\n[scan watchdog]\n");
  test_scan_watchdog();
  test_scan_watchdog_across_wrap();

  printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail > 0 ? 1 : 0;
}
