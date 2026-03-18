---
name: ST25R300 RX_PROTOCOL1 usage guide
description: Which RX_PROTOCOL1 bits to set for each phase of ISO14443A (WUPA, anticol, SELECT/data)
type: reference
---

# ST25R300 RX_PROTOCOL1 (0x17) — Per-Phase Settings

This register must be set differently for each phase of ISO14443A. Wrong bits → silent failure
(tag ignores you, or FIFO data is garbled). Factory default is 0x3C.

## Bit Map

| Bit | Mask | Name        | Meaning                                              |
|-----|------|-------------|------------------------------------------------------|
|  5  | 0x20 | b_rx_sof    | Expect SOF from PICC — MUST be set or I_rxs never fires |
|  4  | 0x10 | b_rx_eof    | Expect EOF from PICC — MUST be set for normal frames |
|  3  | 0x08 | a_rx_par    | Strip ISO14443A parity bits before writing to FIFO   |
|  2  | 0x04 | rx_crc      | Verify and strip RX CRC                              |
|  0  | 0x01 | antcl       | Anticollision mode (partial-byte receive, collision det) |

---

## Phase-by-Phase Settings

### 1. WUPA / REQA short frame (send_short_frame_)
```
RX_PROTOCOL1 = 0x38  (b_rx_sof | b_rx_eof | a_rx_par)
TX_PROTOCOL1 = 0x00  (no CRC, no parity — 7-bit short frame)
```
- antcl=0: ATQA is a normal 2-byte frame, not anticollision
- rx_crc=0: ATQA has no CRC
- a_rx_par=1: parity stripping on (ATQA bytes have parity bits)
- **Do NOT use STOP_ALL before WUPA TX** — it kills rx_on, preventing ATQA reception.
  Use CLEAR_FIFO only.

### 2. Anticollision frame (send_anticol_frame_)
```
RX_PROTOCOL1 = 0x39  (b_rx_sof | b_rx_eof | a_rx_par | antcl)
TX_PROTOCOL1 = 0x40  (a_tx_par only — no CRC for anticol request)
```
- antcl=1: enables partial-byte reception and collision detection
- a_rx_par=1: **CRITICAL** — without this, 9-bit NFC-A bytes (8 data + 1 parity) are
  stored raw in FIFO. Parity bits mix into data bytes, garbling the UID → BCC mismatch →
  tag ignores SELECT. Symptom: computed BCC ≠ tagBCC (the 5th FIFO byte).
- rx_crc=0: anticol response has no CRC
- STOP_ALL is safe here (TX restarts RX automatically after TX end)

### 3. SELECT and all subsequent data exchanges (transceive_ex_)
```
RX_PROTOCOL1 = 0x3C  (b_rx_sof | b_rx_eof | a_rx_par | rx_crc)
TX_PROTOCOL1 = 0x60  (a_tx_par | tx_crc)
```
- antcl=0: normal full-byte framing
- rx_crc=1: chip verifies and strips the 2-byte CRC from SAK, READ responses, etc.
- a_rx_par=1: parity stripping on
- FIFO contains payload only (no CRC bytes, no parity bytes)

---

## Common Mistakes

| Mistake | Symptom | Fix |
|---------|---------|-----|
| antcl=1 without a_rx_par=1 in anticol phase | BCC mismatch every cycle, SELECT always fails | Add 0x08 (a_rx_par) |
| b_rx_sof=0 or b_rx_eof=0 | I_rxs never fires, FIFO always empty | Set 0x20 \| 0x10 |
| rx_crc=1 during anticol | Anticol response treated as CRC-protected, likely rejected | Clear 0x04 |
| STOP_ALL before WUPA TX | rx_on goes to 0, ATQA missed | Use CLEAR_FIFO only |
| CLEAR_FIFO (not STOP_ALL) before anticol TX | Receiver left in unknown state | Use STOP_ALL |

---

## Summary Table

| Phase         | RX_PROTOCOL1 | TX_PROTOCOL1 | Pre-TX command |
|---------------|-------------|-------------|----------------|
| WUPA/REQA     | 0x38        | 0x00        | CLEAR_FIFO     |
| Anticol frame | 0x39        | 0x40        | STOP_ALL       |
| SELECT / data | 0x3C        | 0x60        | CLEAR_FIFO     |
