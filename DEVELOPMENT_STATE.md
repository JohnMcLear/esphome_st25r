# ST25R3916 ESPHome — Development State

## Current State (2026-03)

The component is production-ready for ISO14443A (NFC-A) use cases.

### Working

| Feature | Status | Notes |
|---|---|---|
| ISO14443A multi-tag detection | ✅ Working | Binary tree anticollision; both 4-byte and 7-byte (cascade) UIDs |
| Mifare Classic authentication | ✅ Working | Crypto1 3-pass mutual auth; see clone card note below |
| Mifare Classic block read | ✅ Working | 16-byte reads with parity verification |
| NDEF read (Type 2 / NTAG) | ✅ Working | URL, text records |
| SPI transport | ✅ Verified | ESP32-C6 + Elechouse ST25R3916 module |
| I2C transport | Code-complete | Needs hardware verification |
| Chip health monitor | ✅ Working | IC_IDENTITY check + auto-reinitialization |
| RF field strength sensor | ✅ Working | MEASURE_AMPLITUDE command |

### Not Yet Implemented

- ISO14443B (NFC-B) — hardware supports it
- ISO15693 (NFC-V) — hardware supports it
- FeliCa (NFC-F) — hardware supports it
- Mifare Classic NDEF (needs sector traversal on top of auth)
- Write operations
- Low-power sense mode

---

## Architecture

Three-layer design:
- `components/st25r/` — abstract base (protocol logic, ISO14443A state machine, Crypto1, triggers)
- `components/st25r_spi/` — SPI transport (inherits ST25R + spi::SPIDevice)
- `components/st25r_i2c/` — I2C transport (inherits ST25R + i2c::I2CDevice)

Non-blocking state machine in `loop()` with hardware IRQ (ISR sets flag, loop polls flag):

```
STATE_IDLE → update() → WUPA → STATE_WUPA
STATE_WUPA → IRQ(RXE|COL) → anticollision → STATE_ANTICOL
STATE_WUPA → IRQ(NRE/timeout) → finalize_scan_() → STATE_IDLE
STATE_ANTICOL → IRQ(RXE): SELECT → if cascade: CL2, else HALT+WUPA → next branch
STATE_ANTICOL → IRQ(COL): narrow prefix → stay STATE_ANTICOL
```

---

## Key Bugs Found and Fixed

### Multi-tag anticollision
1. **FIFO zeros prefix bits** — must OR saved prefix back into received bytes after `read_fifo()`
2. **COLLISION_DISPLAY is absolute frame position** — subtract 16 (2 TX bytes) to get UID-relative position
3. **WUPA not REQA after HALT** — Mifare Classic returns to HALT state; REQA only wakes IDLE tags
4. **CL1 state must be saved before CL2** — CL2 anticollision overwrites `anticol_col_pos_` / `prefix_val_`
5. **RX_CONF3 is chip-version dependent** — non-B (Elechouse) uses `0xE2`; B-version (STEVAL) uses `0x00`. See below.

#### RX_CONF3 (0x0D) Detail

`RX_CONF3 = 0xE2` decodes as:
- bits[7:5] `rg1_am=7`: +5.5 dB receiver gain boost
- bit1 `lf_en=1`: **"LF signal on receiver input"** — routes receiver away from the 13.56 MHz HF path

On the non-B Elechouse module (small ferrite antenna, untuned) `lf_en=1` still worked. On the B-version STEVAL board (well-tuned 66×66 mm PCB antenna) `lf_en=1` completely blocked ATQA responses.

**The rule:** `lf_en` must be `0` for any board where the antenna is properly matched to 13.56 MHz. Use `is_b_version_` to select: `0x00` for B-version, `0xE2` for non-B Elechouse hardware. If you ever see `WUPA timeout: IRQ_MAIN=0x00 AMP≥64` (field on, no response), suspect `lf_en=1`.

### Mifare Classic / Crypto1
6. **`crypto1_bit` not `crypto1_filter` for parity** — `crypto1_filter` reads the output filter without advancing the LFSR; parity bytes must advance state, so `crypto1_bit(cs, 0, 0)` is required everywhere a parity byte is consumed or produced
7. **AR = `prng_successor(NT, 64)`** — tag PRNG advances 64 steps; encoded MSB-first over 4 bytes
8. **AT verify = `prng_successor(AR_plain, 32) ^ crypto1_word(cs, 0, 0)`**
9. **RESET_RX_GAIN (0xD5) before each transceive** — resets AGC/squelch for clean reception
10. **Clone cards** — fixed NT (e.g. always `0x009080A2`) means the card is a clone with broken PRNG; it responds to AUTH1 but silently HALTs on NR+AR. Not a driver bug.

---

## Test Hardware

### Elechouse ST25R3916 module (non-B, `ic & 0xF8 == 0x28`)
- ESP32-C6 dev board, SPI: CLK=GPIO19, MISO=GPIO10, MOSI=GPIO18, CS=GPIO6, IRQ=GPIO7
- Tags verified: NTAG/Ultralight 7-byte UID `041AA7675F6180`, Mifare Classic clone `DEA30D00`
- Uses RX_CONF3=0xE2 (lf_en=1 + gain boost)

### STEVAL-MB17149B (ST25R3916**B**, `ic & 0xF8 == 0x30`)
- ESP32-C6 dev board, same SPI pinout, STEVAL expansion board via SPI header
- Tags verified: UID `8281A441` (4-byte)
- Uses RX_CONF3=0x00 (HF path, full gain)

---

## SPI Space B Limitation

`write_register()` masks `addr & 0x3F`, so Space B registers (0x40–0x7F) cannot be written via the normal path. CORR_CONF1/2 are left at factory defaults. Do not add Space B writes to `reset_()` — they corrupt Space A registers.
