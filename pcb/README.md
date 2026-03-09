# ST25R3916B Custom PCB Design

Custom 40×40mm ST25R3916B NFC Reader PCB with integrated 2-turn antenna, designed for JLCPCB fabrication **and assembly** (PCBA).

## Files

| File | Purpose |
|------|---------|
| `generate_kicad.py` | Python script that generates all output files |
| `ST25R3916B_Custom.kicad_pcb` | KiCad 7 PCB layout (generated) |
| `ST25R3916B_Custom.kicad_sch` | KiCad schematic (reference only) |
| `gerbers/` | Gerber + drill files (generated) |
| `gerbers.zip` | Gerbers zipped for JLCPCB upload |
| `jlcpcb_bom.csv` | Bill of Materials (JLCPCB PCBA format) |
| `jlcpcb_cpl.csv` | Component Placement List (JLCPCB PCBA format) |
| `BOM.md` | Human-readable BOM with part notes |

## Regenerating Files

```bash
python3 pcb/generate_kicad.py
kicad-cli pcb export gerbers --output pcb/gerbers/ \
  --layers "F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,Edge.Cuts" \
  pcb/ST25R3916B_Custom.kicad_pcb
kicad-cli pcb export drill --output pcb/gerbers/ pcb/ST25R3916B_Custom.kicad_pcb
cd pcb && zip -j gerbers.zip gerbers/*
```

## Ordering from JLCPCB (PCB + Assembly)

### Step 1 — PCB Fabrication
1. Go to [jlcpcb.com](https://jlcpcb.com) → **Order Now**
2. Upload `pcb/gerbers.zip`
3. Confirm board parameters:
   - Layers: **2**
   - Dimensions: **40 × 40 mm**
   - PCB Thickness: **1.6 mm**
   - Copper Weight: **1 oz**
   - Surface Finish: **HASL (lead-free)** or ENIG (recommended for QFN soldering)
4. Enable **SMT Assembly** → select **Assemble top side**

### Step 2 — PCBA (Assembly)
1. Upload `pcb/jlcpcb_bom.csv` when prompted for BOM
2. Upload `pcb/jlcpcb_cpl.csv` when prompted for CPL
3. Review the component placement preview — all SMD parts should be placed correctly
4. **Note**: `CN1` (7-pin 2.54mm through-hole header) is not assembled by JLCPCB. Solder it manually after delivery.

### Connector Pinout (CN1 — J1, 7-pin 2.54mm)

| Pin | Signal | ESP32-C6 GPIO |
|-----|--------|--------------|
| 1   | GND    | GND |
| 2   | VIN    | 5V supply |
| 3   | MISO   | GPIO10 |
| 4   | MOSI   | GPIO18 |
| 5   | SCLK   | GPIO19 |
| 6   | CS     | GPIO6 |
| 7   | IRQ    | GPIO7 |

## Design Notes

### Antenna
- 2-turn rectangular coil on F.Cu (inner turn) with B.Cu crossover via
- Size: 40×40mm outer, 0.5mm trace width
- Inner turn: 3.5mm from board edge; outer turn: 1.5mm from board edge
- Target inductance: ~1 µH

### RF Matching Network
```
RFO1 ──L1──┬──C3──R1──ANT1
            C1
            └── (NODE_A) ──RFI1

RFO2 ──L2──┬──C4──R2──ANT2
            C2
            └── (NODE_B) ──RFI2
```
Values from ST25R3916 Reference Design (MB1414) for a ~1 µH PCB antenna.

### SPI Mode
I2C_EN (pin 20) is pulled to GND on-board → SPI mode selected. No external jumper required.

### Power
- Input: 5V on CN1 pin 2
- On-board 3.3V LDO (U3, SOT-23-5, C7955) supplies all IC rails
- Multiple bypass capacitors on all VDD pins
