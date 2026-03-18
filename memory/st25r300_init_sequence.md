---
name: ST25R300 working init sequence
description: Exact register writes for ST25R300 NFC-A ISO14443A operation, verified on X-NUCLEO-NFC12A1
type: reference
---

# ST25R300 Working Init Sequence (NFC-A / ISO14443A)

Verified on X-NUCLEO-NFC12A1 board with ESP32-C6, detecting NTAG 7-byte UIDs.
Commit: 589dbde, branch: refine-st25r-technical-docs.

## Step-by-Step

```
1. Send CMD_SET_DEFAULT (0x61)       — reset to power-up state
   delay(10ms)

2. Read REG_IC_IDENTITY (0x3F)       — verify (val & 0xF8) == 0xB0
   ST25R300 rev 1 reads 0xB1

3. Write REG_OPERATION (0x00) = 0x08 — en=1 (oscillator on)
   delay(5ms)

4. Poll REG_IRQ_STATUS3 (0x3E) bit0  — wait for I_osc (oscillator stable)

5. Write REG_OPERATION (0x00) = 0x18 — en=1, vdddr_en=1
   delay(1ms)

6. Write REG_OPERATION (0x00) = 0x78 — en=1, vdddr_en=1, rx_en=1, tx_en=1

7. Send CMD_ADJUST_REGS (0x69)       — calibrate internal regulators
   delay(5ms)

8. Write REG_GENERAL_CONF (0x01) = 0x00  — differential antenna, no RFO2

9. Write REG_PROTOCOL1 (0x14) = 0x01    — om=1 (NFC-A reader mode)

10. Write REG_TX_PROTOCOL1 (0x15) = 0x60 — a_tx_par=1, tx_crc=1

11. Write REG_RX_PROTOCOL1 (0x17) = 0x3C — b_rx_sof|b_rx_eof|a_rx_par|rx_crc
```

## RX Path Analog — CRITICAL RFAL Settings

**These must be set. Factory defaults do NOT work for NFC-A reception on ST25R300.**

```
0x09 = 0x73  — RX Ana1: dig_clk_dly=7, hpf_ctrl=3 (80kHz HPF)
               (+0x04 if rx_gain_boost enabled)
0x0A = 0x22  — RX Ana2: afe_gain_rw=2 (6dB reduction), afe_gain_td=2
               NOTE: afe_gain_rw≠0 REQUIRES dis_agc_noise_meas=1 in 0x12
0x0B = 0x85  — RX Ana3: ook thresholds (factory default, keep)
0x0D = 0xCC  — RX Digital: agc_en=1, lpf_coef=4, hpf_coef=3 (factory default, keep)
```

## Correlator — CRITICAL RFAL Settings

**Source: RFAL ST25R500 analogConfigTbl, Poll NFC-A Rx 106kbps.**
**ST25R300 and ST25R500 share the same register map at 0x0E-0x13.**

```
0x0E = 0xF8  — CORR1: iir_coef2=F, iir_coef1=8       (factory=0xC1)
0x0F = 0x2E  — CORR2: squelch_thr=2, agc_thr=E        (factory=0x5A)
0x10 = 0x0F  — CORR3: start_wait=15 (prevents false I_subc_start from TX transient)
0x11 = 0x88  — CORR4: coll_lvl=8, data_lvl=8          (factory=0xAA)
0x12 = 0x32  — CORR5: dis_soft_sq=1, dis_agc_noise_meas=1, dec_f=2
               (dis_agc_noise_meas=1 REQUIRED when 0x0A afe_gain_rw≠0)
0x13 = 0x20  — CORR6: init_noise_lvl=2                (factory=0x30)
```

Without these: I_subc_start fires every cycle but I_rxs/I_rxe never fire → no ATQA.

## TX Driver and Modulation

```
0x03 = d_res         — TX Driver: d_res = (15 - rf_power) & 0x0F; am_mod field = 0
0x04 = 0x70          — TX Mod1: am_mod=7 (20% AM modulation index)
```

**am_mod in 0x03 MUST stay 0 for ISO14443A.** am_mod=7 is for ISO14443B.
Only set am_mod via REG_TX_MOD1 (0x04).

## IRQ Masks and NRT

```
0x39 = 0x00  — IRQ_MASK1: unmask all
0x3A = 0x00  — IRQ_MASK2: unmask all
0x3B = 0x00  — IRQ_MASK3: unmask all

0x22 = 0x00  — NRT_CONF1: nrt_step=0 (4.72µs per step), nrt_emv=0 (auto-start)
0x23 = 0x04  — NRT_CONF2: nrt[15:8]
0x24 = 0x23  — NRT_CONF3: nrt[7:0]  → 0x0423 = 1059 steps = ~5ms timeout
```

## RF Field On

```
Write REG_OPERATION = 0x58  (en | vdddr_en | tx_en, rx_en off)
delay(5ms)
Send CMD_FIELD_ON (0x6F)    (RF collision avoidance)
delay(10ms)
Write REG_OPERATION = 0x78  (all on)
delay(10ms)
```

## Key IRQ Bits for NFC-A Scanning

| Register | Bit | Mask | Meaning |
|----------|-----|------|---------|
| IRQ_STATUS1 (0x3C) | 7 | 0x80 | I_subc_start — subcarrier detected (Manchester SOF) |
| IRQ_STATUS1 (0x3C) | 3 | 0x08 | I_rxe — end of receive |
| IRQ_STATUS1 (0x3C) | 2 | 0x04 | I_rxs — start of receive |
| IRQ_STATUS1 (0x3C) | 1 | 0x02 | I_txe — end of transmission |
| IRQ_STATUS1 (0x3C) | 6 | 0x40 | I_col — bit collision in anticollision |
| IRQ_STATUS2 (0x3D) | 6 | 0x40 | I_nre — no-response timer expired (no tag present) |

Normal ATQA-received pattern: IRQ1=0x8E (I_subc_start | I_rxe | I_rxs | I_txe)
Normal no-tag pattern: IRQ2=0x40 (I_nre only)
