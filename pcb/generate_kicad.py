import os

def get_footprint_0603(ref, value, x, y, rot=0):
    return f"""
  (footprint "Resistor_SMD:R_0603_1608Metric" (layer "F.Cu") (at {x} {y} {rot})
    (tstamp "{ref}")
    (pad "1" smd roundrect (at -0.8 0 {rot}) (size 0.9 0.9) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
    (pad "2" smd roundrect (at 0.8 0 {rot}) (size 0.9 0.9) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
  )"""

def get_header_7pin(x, y, rot=0):
    return f"""
  (footprint "Connector_PinHeader_2.54mm:PinHeader_1x07_P2.54mm_Vertical" (layer "F.Cu") (at {x} {y} {rot})
    (tstamp "CN1")
    (pad "1" thru_hole circle (at 0 0 {rot}) (size 1.7 1.7) (drill 1) (layers "*.Cu" "*.Mask"))
    (pad "2" thru_hole circle (at 0 2.54 {rot}) (size 1.7 1.7) (drill 1) (layers "*.Cu" "*.Mask"))
    (pad "3" thru_hole circle (at 0 5.08 {rot}) (size 1.7 1.7) (drill 1) (layers "*.Cu" "*.Mask"))
    (pad "4" thru_hole circle (at 0 7.62 {rot}) (size 1.7 1.7) (drill 1) (layers "*.Cu" "*.Mask"))
    (pad "5" thru_hole circle (at 0 10.16 {rot}) (size 1.7 1.7) (drill 1) (layers "*.Cu" "*.Mask"))
    (pad "6" thru_hole circle (at 0 12.7 {rot}) (size 1.7 1.7) (drill 1) (layers "*.Cu" "*.Mask"))
    (pad "7" thru_hole circle (at 0 15.24 {rot}) (size 1.7 1.7) (drill 1) (layers "*.Cu" "*.Mask"))
  )"""

def get_mounting_hole(ref, x, y):
    return f"""
  (footprint "MountingHole:MountingHole_3.2mm_M3" (layer "F.Cu") (at {x} {y})
    (tstamp "{ref}")
    (pad "" np_thru_hole circle (at 0 0) (size 3.2 3.2) (drill 3.2) (layers "*.Cu" "*.Mask"))
  )"""

def generate_kicad_pcb(path):
    ox, oy = 100, 100
    w, h = 40, 40
    cx, cy = ox + w/2, oy + h/2
    content = f"""(kicad_pcb (version 20221018) (generator pcbnew)
  (layers (0 "F.Cu" signal) (31 "B.Cu" signal) (36 "B.SilkS" user) (37 "F.SilkS" user) (38 "B.Mask" user) (39 "F.Mask" user) (44 "Edge.Cuts" user))
  (setup (stackup (layer "F.Cu" (type "copper") (thickness 0.035)) (layer "dielectric 1" (type "core") (thickness 1.51) (material "FR4") (epsilon_r 4.5)) (layer "B.Cu" (type "copper") (thickness 0.035))))
  (gr_line (start {ox} {oy}) (end {ox+w} {oy}) (layer "Edge.Cuts") (width 0.1) (tstamp "E1"))
  (gr_line (start {ox+w} {oy}) (end {ox+w} {oy+h}) (layer "Edge.Cuts") (width 0.1) (tstamp "E2"))
  (gr_line (start {ox+w} {oy+h}) (end {ox} {oy+h}) (layer "Edge.Cuts") (width 0.1) (tstamp "E3"))
  (gr_line (start {ox} {oy+h}) (end {ox} {oy}) (layer "Edge.Cuts") (width 0.1) (tstamp "E4"))
  {get_mounting_hole("H1", ox+10, oy+10)}
  {get_mounting_hole("H2", ox+w-10, oy+10)}
  {get_mounting_hole("H3", ox+w-10, oy+h-10)}
  {get_mounting_hole("H4", ox+10, oy+h-10)}
  (footprint "Custom:ST25R3916B" (layer "F.Cu") (at {cx} {cy})
    (tstamp "U1")
    {" ".join([f'(pad "{i+1}" smd rect (at {-2.425 if i < 8 else 2.425 if 16 <= i < 24 else (i-12)*0.5 if 8 <= i < 16 else (28-i)*0.5} {-1.75 + (i%8)*0.5 if i < 16 else 1.75 - (i%8)*0.5}) (size 0.65 0.25) (layers "F.Cu" "F.Paste" "F.Mask"))' for i in range(32)])}
  )
  {get_header_7pin(cx-10, cy-12, 90)}
  {get_footprint_0603("L1", "270nH", cx-3, cy+6, 90)}
  {get_footprint_0603("C3", "120pF", cx-3, cy+10, 90)}
  {get_footprint_0603("R1", "2.2R", cx-3, cy+14, 90)}
  {get_footprint_0603("L2", "270nH", cx+3, cy+6, 90)}
  {get_footprint_0603("C4", "120pF", cx+3, cy+10, 90)}
  {get_footprint_0603("R2", "2.2R", cx+3, cy+14, 90)}
  (segment (start {cx+0.25} {cy+2.425}) (end {cx-3} {cy+6-0.8}) (width 0.3) (layer "F.Cu") (tstamp "S1"))
  (segment (start {cx+1.25} {cy+2.425}) (end {cx+3} {cy+6-0.8}) (width 0.3) (layer "F.Cu") (tstamp "S2"))
  (segment (start {cx-3} {cy+6+0.8}) (end {cx-3} {cy+10-0.8}) (width 0.3) (layer "F.Cu") (tstamp "S3"))
  (segment (start {cx-3} {cy+10+0.8}) (end {cx-3} {cy+14-0.8}) (width 0.3) (layer "F.Cu") (tstamp "S4"))
  (segment (start {cx+3} {cy+6+0.8}) (end {cx+3} {cy+10-0.8}) (width 0.3) (layer "F.Cu") (tstamp "S5"))
  (segment (start {cx+3} {cy+10+0.8}) (end {cx+3} {cy+14-0.8}) (width 0.3) (layer "F.Cu") (tstamp "S6"))
  (segment (start {ox+1.5} {oy+1.5}) (end {ox+w-1.5} {oy+1.5}) (width 0.6) (layer "F.Cu") (tstamp "A1"))
  (segment (start {ox+w-1.5} {oy+1.5}) (end {ox+w-1.5} {oy+h-1.5}) (width 0.6) (layer "F.Cu") (tstamp "A2"))
  (segment (start {ox+w-1.5} {oy+h-1.5}) (end {ox+1.5} {oy+h-1.5}) (width 0.6) (layer "F.Cu") (tstamp "A3"))
  (segment (start {ox+1.5} {oy+h-1.5}) (end {ox+1.5} {oy+2.5}) (width 0.6) (layer "F.Cu") (tstamp "A4"))
  (segment (start {ox+1.5} {oy+2.5}) (end {ox+w-3} {oy+2.5}) (width 0.6) (layer "F.Cu") (tstamp "A5"))
  (segment (start {ox+w-3} {oy+2.5}) (end {ox+w-3} {oy+h-3}) (width 0.6) (layer "F.Cu") (tstamp "A6"))
  (segment (start {ox+w-3} {oy+h-3}) (end {ox+3} {oy+h-3}) (width 0.6) (layer "F.Cu") (tstamp "A7"))
  (segment (start {ox+3} {oy+h-3}) (end {ox+3} {oy+4}) (width 0.6) (layer "F.Cu") (tstamp "A8"))
  (via (at {ox+1.5} {oy+1.5}) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (tstamp "V1"))
  (segment (start {ox+1.5} {oy+1.5}) (end {cx-3} {cy+14+0.8+2}) (width 0.5) (layer "B.Cu") (tstamp "B1"))
  (via (at {cx-3} {cy+14+0.8+2}) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (tstamp "V2"))
  (segment (start {cx-3} {cy+14+0.8+2}) (end {cx-3} {cy+14+0.8}) (width 0.5) (layer "F.Cu") (tstamp "C1"))
  (segment (start {ox+3} {oy+4}) (end {cx+3} {cy+14+0.8}) (width 0.5) (layer "F.Cu") (tstamp "C2"))
)
"""
    with open(path, 'w') as f:
        f.write(content)

generate_kicad_pcb("pcb/ST25R3916B_Custom.kicad_pcb")
