# Bill of Materials - ST25R3916B Custom Reader

| Reference | Value | Description | JLCPCB Part # | Notes |
| :--- | :--- | :--- | :--- | :--- |
| U1 | ST25R3916B | NFC Reader IC (VFQFPN-32) | [C17315217](https://jlcpcb.com/partdetail/STMicroelectronics-ST25R3916BAQET/C17315217) | Core IC |
| L1, L2 | 270nH | EMC Filter Inductor (0603) | C1034 | LQW18ANR27J80D |
| C1, C2 | 220pF | EMC Filter Capacitor (0603) | C1600 | NP0 |
| C3, C4 | 120pF | Series Matching Cap (0603) | C1591 | NP0 |
| C5, C6 | 180pF | Parallel Matching Cap (0603) | C1598 | NP0 |
| R1, R2 | 2.2 Ohm | Damping Resistor (0603) | C22935 | |
| X1 | 27.12MHz | Crystal (3.2x2.5mm) | C112441 | |
| C31, C32 | 12pF | Crystal Load Caps (0603) | C1547 | |
| U3 | 3.3V LDO | LDO Regulator (SOT-23-5) | C161 | TLV70233 or similar |
| C15-C30 | 2.2uF / 10nF | Decoupling Caps | C1602 / C1514 | Multiples required |
| CN1 | 7-pin 1.25mm | Connector | C161944 | |

## Matching Network Guidelines
The values provided are based on the **ST25R3916 Reference Design (MB1414)** for a standard integrated PCB antenna (~1uH).
For optimal performance with the ST25R3916B, ensure:
1. Differential symmetry in layout.
2. Solid ground plane on bottom layer.
3. Keep RFI input traces as short as possible.
4. Use NP0/C0G capacitors for the RF path.
