# ST25R3916B Custom PCB Design

This folder contains the source files for a custom ST25R3916B NFC Reader PCB, designed to be compatible with the **Elechouse Wilson** pinout but using the improved **ST25R3916B** chip from JLCPCB.

## Files
- `ST25R3916B_Custom.kicad_sch`: KiCad Schematic
- `ST25R3916B_Custom.kicad_pcb`: KiCad PCB Layout
- `BOM.md`: Bill of Materials with JLCPCB Part Numbers
- `generate_kicad.py`: Script used to generate these files

## How to use
1. Install [KiCad 6.0+](https://kicad.org/).
2. Open the `.kicad_pcb` file.
3. The layout includes:
   - ST25R3916B VFQFPN-32 Footprint (at 125, 125).
   - Integrated 2-turn rectangular antenna (40x40mm).
   - Standard 1.6mm FR4 stackup.
4. **Note**: You must complete the routing between the IC pins and the matching components. Use the values in `BOM.md`.
5. To export Gerbers:
   - Go to `File -> Fabrication Outputs -> Gerbers (.gbr)`.
   - Click `Plot`.
   - Go to `File -> Fabrication Outputs -> Drill Files (.drl)`.
   - Click `Generate Drill File`.

## Antenna Design
The included antenna is a 2-turn rectangular coil on the Top Copper layer. 
- Size: 40mm x 40mm
- Trace width: 0.5mm
- Target Inductance: ~1uH

## Matching Components
The components in `BOM.md` are specifically selected for the ST25R3916B to achieve optimal read range with the provided antenna design.
