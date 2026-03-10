# Bill of Materials - ST25R3916B Custom Reader

| Reference | Value | Description | JLCPCB Part # | Notes |
| :--- | :--- | :--- | :--- | :--- |
| U1 | ST25R3916B | NFC Reader IC (VFQFPN-32) | [C17315217](https://jlcpcb.com/partdetail/STMicroelectronics-ST25R3916BAQET/C17315217) | Core IC |
| L1, L2 | 27nH | Series EMC Inductor (0603) | C148128 | Murata LQW18AN27NG00D — **not 270nH** |
| C1, C2 | 220pF | EMC Shunt Cap RFO (0603) | C1600 | C0G/NP0 |
| C3, C4 | 120pF | Series Matching Cap (0603) | C1591 | C0G/NP0 |
| C5, C6 | 180pF | RFI Shunt Cap (0603) | C1598 | C0G/NP0 |
| R1, R2 | 2.2Ω | Damping Resistor (0603) | C22939 | |
| X1 | 27.12MHz | Crystal (3.2×2.5mm, 4-pad) | C112441 | |
| C31, C32 | 10pF | Crystal Load Caps (0603) | C1634 | C0G, 50V |
| U3 | 3.3V LDO | LDO Regulator (SOT-23-5) | C7955 | TLV70233 or C7955 equivalent |
| C15, C16 | 100nF | VDD_IO / VDD_D bypass (0603) | C14663 | |
| C17, C18 | 2.2uF | VDD_IO / VDD_D bulk (0603) | C1602 | |
| C19, C20 | 10nF | AGDC / VDD_TX bypass (0603) | C1514 | |
| CN1 | 7-pin 2.54mm | SPI/Power header (THT) | — | Hand solder after delivery |

## Component Notes

- **L1/L2 (27nH)**: Acts as EMC filter at 13.56 MHz — series impedance ~2.3Ω. Reference board (Elechouse cardboard) confirmed 27nH. Earlier 270nH value was 10× too high (~23Ω) and would significantly reduce antenna drive.
- **R1/R2 (2.2Ω)**: Damping resistors; reference uses 1.5Ω (1892906). Both are acceptable first-prototype values.
- **C31/C32 (10pF)**: Crystal load capacitors, matched to NX3225SA-27.12M spec. Earlier 12pF was slightly high.
- **C22939**: Basic JLCPCB part for 2.2Ω 0603 (replaces incorrect C22935 which is 1MΩ).

## Matching Network

```
RFO1 ──L1(27nH)──┬──C3(120pF)──R1(2.2Ω)──ANT1
                C1(220pF)
                └── (NODE_A) ──C5(180pF)──RFI1

RFO2 ──L2(27nH)──┬──C4(120pF)──R2(2.2Ω)──ANT2
                C2(220pF)
                └── (NODE_B) ──C6(180pF)──RFI2
```

Values tuned for ~1 µH 2-turn 40×40mm PCB antenna. Use C0G/NP0 capacitors for RF path.
