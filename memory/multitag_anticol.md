---
name: Multi-tag anticollision implementation
description: How ISO14443A multi-tag detection works on the ST25R3916, including all bugs hit and fixed
type: reference
---

# Multi-tag anticollision on ST25R3916

## The Problem

ISO14443A anticollision is a binary tree search. When multiple tags are in the field,
they all respond simultaneously to an ANTICOL frame. Bits where they differ cause a
detectable RF collision. The reader must narrow down the UID prefix until each tag
responds alone, then SELECT it, HALT it, and repeat until no more tags remain.

---

## ST25R3916-Specific Behaviour

### FIFO stores zeros in prefix-bit positions

When you send an anticol frame with N prefix bits, the tag only transmits the
*remaining* UID bits. The chip writes those into the FIFO, but the first N bit
positions in the FIFO byte are **zero** (not the prefix bits you transmitted).

**Fix:** after `read_fifo(resp, 5)`, OR the saved prefix back in:

```cpp
uint8_t full_uid[4];
memcpy(full_uid, resp, 4);
for (int k = 0; k < anticol_prefix_full_; k++)
    full_uid[k] = anticol_prefix_[k];
if (anticol_prefix_bits_ > 0) {
    uint8_t mask = (uint8_t)((1 << anticol_prefix_bits_) - 1);
    full_uid[anticol_prefix_full_] =
        (anticol_prefix_[anticol_prefix_full_] & mask) |
        (resp[anticol_prefix_full_] & ~mask);
}
```

Without this fix the reconstructed UID has the prefix bits zeroed out, so the
SELECT fails silently (no SAK) or selects the wrong tag.

### COLLISION_DISPLAY gives absolute frame position, not UID position

`COLLISION_DISPLAY (0x20)`:
- `c_byte = (col_raw >> 4) & 0x0F` — full bytes before the collision bit
- `c_bit  = (col_raw >> 1) & 0x07` — bit within that byte

`col_pos_abs = c_byte * 8 + c_bit` counts from the start of the **TX frame**
(which begins with SEL + NVB = 2 bytes = 16 bits). Subtract 16 to get the
UID-relative collision position:

```cpp
int uid_col_pos = (int)(c_byte * 8 + c_bit) - 16;
if (uid_col_pos < 0) uid_col_pos = 0;
```

### FIFO data during a collision is unreliable

Don't try to extract the prefix from a collision response. Instead, brute-force
all `2^(col_pos+1)` prefix values. Store the collision bit position in
`anticol_col_pos_` and iterate `anticol_prefix_val_` from 0 to `(1<<(col_pos+1))-1`.

---

## Mifare Classic Quirks

### Returns to HALT after non-matching SELECT, not to IDLE

ISO14443A says a tag that doesn't match an ANTICOL/SELECT frame should go back
to IDLE. Mifare Classic goes to **HALT** instead. `REQA` only wakes IDLE tags;
`WUPA` wakes both IDLE and HALT.

**Fix:** always use WUPA (not REQA) when searching for remaining tags after
selecting one.

### Leaves READY state in ~200 ms

After waking to READY with WUPA, Mifare Classic times out and goes back to HALT
if it doesn't receive a valid ANTICOL within ~200 ms.

**Fix:** use a **20 ms** ANTICOL timeout (not the 200 ms that was originally used).
When the timeout fires and there are more prefix branches to try, send **WUPA
first**, then send the next anticol frame. This re-wakes all HALT tags for
every new branch attempt.

---

## Cascade (7-byte UID) Tags

7-byte UID tags use a cascade indicator byte `0x88` as the first byte of the CL1
response. The SAK from CL1 SELECT has bit 2 set (`sak & 0x04`), indicating CL2 is
needed. At CL2 the real UID bytes are sent.

### CL1 collision state must be saved before CL2

When a collision was detected at CL1 (e.g. two tags differ at bit 1), that
`anticol_col_pos_` / `anticol_prefix_val_` state is valid for resuming the CL1
tree after the cascade tag is fully selected. But CL2 overwrites those variables.

**Fix:** before entering CL2, save the CL1 state:

```cpp
if (cascade_level_ == 0) {
    saved_col_pos_   = anticol_col_pos_;
    saved_prefix_val_ = anticol_prefix_val_;
    saved_anticol_valid_ = (anticol_col_pos_ > 0 || anticol_prefix_bits_ > 0);
}
```

After fully selecting the cascade tag and sending HALT, restore the saved state
and advance `prefix_val` by 1 to continue the CL1 tree:

```cpp
if (saved_anticol_valid_) {
    anticol_col_pos_   = saved_col_pos_;
    anticol_prefix_val_ = saved_prefix_val_ + 1;
    saved_anticol_valid_ = false;
}
```

---

## Full Multi-tag Scan Flow

```
update():
  clear saved state
  WUPA → STATE_WUPA

STATE_WUPA:
  on IRQ_RXE|IRQ_COL:
    if anticol_resume_: use saved prefix (don't reset to 0)
    else: reset prefix state to 0
    anticol_resume_ = false
    send_anticol_frame_() → STATE_ANTICOL

  on timeout (100ms):
    finalize_scan_() → STATE_IDLE

STATE_ANTICOL:
  on timeout (20ms):
    if more prefix branches remain:
      prefix_val++, apply_anticol_prefix_()
      STOP_ALL, clear IRQ, WUPA       ← re-wake HALT tags
      anticol_resume_ = true → STATE_WUPA
    else:
      finalize_scan_() → STATE_IDLE

  on clean response (f1 >= 5):
    reconstruct UID (OR prefix bits back in)
    send SELECT (transceive_ with CRC)
    if SAK & 0x04 (cascade):
      save CL1 state if cascade_level_==0
      cascade_level_++, reset prefix state
      send_anticol_frame_() → STATE_ANTICOL
    else (fully selected):
      add to tags_this_scan_
      send HALT (no response expected, raw write + 10ms delay)
      restore saved CL1 state or use current anticol state
      advance prefix_val + 1
      if prefix_val > max_val: finalize → STATE_IDLE
      STOP_ALL, clear IRQ
      anticol_resume_ = true, WUPA → STATE_WUPA

  on collision:
    read COLLISION_DISPLAY, compute uid_col_pos
    drain FIFO garbage
    anticol_col_pos_ = uid_col_pos, prefix_val_ = 0
    apply_anticol_prefix_(), send_anticol_frame_()
```

---

## NVB Encoding for Anticol Frames

```
NVB high nibble = 2 + anticol_prefix_full_   (SEL + NVB + complete prefix bytes)
NVB low nibble  = anticol_prefix_bits_        (partial bits in last byte, 0 = none)

NUM_TX_BYTES1 = ntx_n >> 5         where ntx_n = 2 + anticol_prefix_full_
NUM_TX_BYTES2 = ((ntx_n & 0x1F) << 3) | (anticol_prefix_bits_ & 0x07)
```

The partial byte (if `anticol_prefix_bits_ > 0`) goes into the FIFO but is **not**
counted in the NVB high nibble.

---

## RX_CONF3 = 0xE2 Required

`RX_CONF3` must be `0xE2` (bit 1 = 1) before sending WUPA. This bit enables
anticollision reception on this hardware. Set it in `reset_()` to `0x00`, then
write `0xE2` in `update()` just before sending WUPA. Without it the chip returns
no anticol response at all.

---

## Key Verified Working UIDs

| Tag | UID | Type | Notes |
|-----|-----|------|-------|
| NFC ring | `041AA7675F6180` | NFC Forum Type 2, 7-byte cascade | CL1=0x88 04 1A A7, CL2=67 5F 61 80 |
| Mifare Classic | `DEA30D00` | Mifare Classic 1K, 4-byte | SAK=0x08, returns to HALT after non-match |

Both detected every scan cycle, zero spurious removal events across 580+ cycles.
