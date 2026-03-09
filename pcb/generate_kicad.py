#!/usr/bin/env python3
"""
ST25R3916B Custom NFC Reader PCB Generator
Generates a complete KiCad 7 PCB + JLCPCB PCBA files.
Board: 40×40mm, 2-layer 1.6mm FR4.

Run:  python3 pcb/generate_kicad.py
Then: kicad-cli pcb export gerbers ...
"""
import uuid, os, zipfile, csv

# ── Board constants ─────────────────────────────────────────────────────────
OX, OY = 100.0, 100.0
BW, BH = 40.0, 40.0
CX, CY = OX + BW / 2, OY + BH / 2   # (120, 120)
IC_X, IC_Y = CX, CY - 2.0            # (120, 118)

# VFQFPN-32 exact KiCad library values
QFN_D  = 2.4375   # pad-centre from IC centre
QFN_LR = (0.875, 0.25)
QFN_TB = (0.25, 0.875)
QFN_EP = 3.45

# ── Pin positions ────────────────────────────────────────────────────────────
def ic_pin(n):
    """Absolute (x, y) of VFQFPN-32 pad centre.  Pin 1 = top-left, CCW."""
    if   1 <= n <=  8: return IC_X - QFN_D,  IC_Y + (-1.75 + (n-1)*0.5)
    elif 9 <= n <= 16: return IC_X + (-1.75 + (n-9)*0.5),  IC_Y + QFN_D
    elif 17 <= n <= 24:return IC_X + QFN_D,  IC_Y + (1.75  - (n-17)*0.5)
    elif 25 <= n <= 32:return IC_X + (1.75  - (n-25)*0.5), IC_Y - QFN_D
    elif n == 33:       return IC_X, IC_Y
    raise ValueError(n)

# Named pins
P = {
    'VDD_IO':1,'CSO':2,'VDD_D':3,'XTO':4,'XTI':5,
    'GND_D':6,'VDD_A':7,'VDD':8,
    'VDD_RF':9,'VDD_TX':10,'VDD_AM':11,'GND_DR1':12,
    'RFO1':13,'VDD_DR':14,'RFO2':15,'GND_DR2':16,
    'EXT_LM':17,'AAT_A':18,'AAT_B':19,'I2C_EN':20,
    'VSS':21,'RFI1':22,'RFI2':23,'AGDC':24,
    'CSI':25,'GND_A':26,'IRQ':27,'MCU_CLK':28,
    'BSS':29,'SCLK':30,'MOSI':31,'MISO':32,'EP':33
}

# ── ID helpers ───────────────────────────────────────────────────────────────
_id = 0
def nid():
    global _id; _id += 1; return f"TS{_id:04d}"

# ── Primitive builders ───────────────────────────────────────────────────────
def seg(x1,y1,x2,y2, layer="F.Cu", w=0.2):
    return (f'  (segment (start {x1:.4f} {y1:.4f}) (end {x2:.4f} {y2:.4f}) '
            f'(width {w}) (layer "{layer}") (tstamp "{nid()}"))')

def via(x,y, size=0.8, drill=0.4):
    return (f'  (via (at {x:.4f} {y:.4f}) (size {size}) (drill {drill}) '
            f'(layers "F.Cu" "B.Cu") (tstamp "{nid()}"))')

def zone_gnd(layer="B.Cu"):
    """Solid GND copper fill on B.Cu over whole board."""
    xmin,ymin = OX+0.5, OY+0.5
    xmax,ymax = OX+BW-0.5, OY+BH-0.5
    return f"""  (zone (net 1) (net_name "GND") (layer "{layer}") (tstamp "{nid()}")
    (connect_pads (clearance 0.3))
    (min_thickness 0.25)
    (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5))
    (polygon (pts
      (xy {xmin} {ymin}) (xy {xmax} {ymin})
      (xy {xmax} {ymax}) (xy {xmin} {ymax})))
  )"""

# ── Footprint helpers ────────────────────────────────────────────────────────
def _fnt(sz=0.8): return f'(effects (font (size {sz} {sz}) (thickness 0.12)))'

def fp_0603(ref, val, x, y, rot=0):
    """Generic 0603 SMD component (R/L/C)."""
    return f"""  (footprint "Resistor_SMD:R_0603_1608Metric" (layer "F.Cu") (at {x:.4f} {y:.4f} {rot})
    (fp_text reference "{ref}" (at 0 -1.5 {rot}) (layer "F.SilkS") {_fnt()})
    (fp_text value    "{val}"  (at 0  1.5 {rot}) (layer "F.Fab")   {_fnt()})
    (pad "1" smd roundrect (at -0.825 0 {rot}) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (tstamp "{nid()}"))
    (pad "2" smd roundrect (at  0.825 0 {rot}) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (tstamp "{nid()}"))
  )"""

def fp_crystal_3225(ref, val, x, y, rot=0):
    """Crystal_SMD_3225-4Pin 3.2×2.5mm.
       Pad 1 (-1.1,+0.85)=GND  Pad 2 (+1.1,+0.85)=GND
       Pad 3 (+1.1,-0.85)=XTO  Pad 4 (-1.1,-0.85)=XTI"""
    return f"""  (footprint "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm" (layer "F.Cu") (at {x:.4f} {y:.4f} {rot})
    (fp_text reference "{ref}" (at 0 -2 {rot}) (layer "F.SilkS") {_fnt()})
    (fp_text value    "{val}"  (at 0  2 {rot}) (layer "F.Fab")   {_fnt()})
    (pad "1" smd rect (at -1.1  0.85 {rot}) (size 1.4 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (tstamp "{nid()}"))
    (pad "2" smd rect (at  1.1  0.85 {rot}) (size 1.4 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (tstamp "{nid()}"))
    (pad "3" smd rect (at  1.1 -0.85 {rot}) (size 1.4 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (tstamp "{nid()}"))
    (pad "4" smd rect (at -1.1 -0.85 {rot}) (size 1.4 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (tstamp "{nid()}"))
  )"""

def fp_sot23_5(ref, val, x, y, rot=0):
    """SOT-23-5 LDO.  Pad1=IN Pad2=GND Pad3=EN Pad4=OUT Pad5=FB"""
    return f"""  (footprint "Package_TO_SOT_SMD:SOT-23-5" (layer "F.Cu") (at {x:.4f} {y:.4f} {rot})
    (fp_text reference "{ref}" (at 0 -2 {rot}) (layer "F.SilkS") {_fnt()})
    (fp_text value    "{val}"  (at 0  2 {rot}) (layer "F.Fab")   {_fnt()})
    (pad "1" smd roundrect (at -1.1375 -0.95 {rot}) (size 1.325 0.6) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (tstamp "{nid()}"))
    (pad "2" smd roundrect (at -1.1375  0.0  {rot}) (size 1.325 0.6) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (tstamp "{nid()}"))
    (pad "3" smd roundrect (at -1.1375  0.95 {rot}) (size 1.325 0.6) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (tstamp "{nid()}"))
    (pad "4" smd roundrect (at  1.1375  0.95 {rot}) (size 1.325 0.6) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (tstamp "{nid()}"))
    (pad "5" smd roundrect (at  1.1375 -0.95 {rot}) (size 1.325 0.6) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (tstamp "{nid()}"))
  )"""

def fp_header_7pin(ref, x, y, rot=0):
    """7-pin 2.54mm through-hole connector (vertical, pin 1 at top)."""
    pads = ""
    for i in range(7):
        pads += (f'\n    (pad "{i+1}" thru_hole circle (at 0 {i*2.54:.2f} {rot}) '
                 f'(size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask") (tstamp "{nid()}"))')
    return f"""  (footprint "Connector_PinHeader_2.54mm:PinHeader_1x07_P2.54mm_Vertical" (layer "F.Cu") (at {x:.4f} {y:.4f} {rot})
    (fp_text reference "{ref}"      (at -2 7.62 {rot}) (layer "F.SilkS") {_fnt()})
    (fp_text value    "7P-2.54mm"   (at  2 7.62 {rot}) (layer "F.Fab")   {_fnt()}){pads}
  )"""

def fp_mount_hole(ref, x, y):
    return f"""  (footprint "MountingHole:MountingHole_3.2mm_M3" (layer "F.Cu") (at {x:.4f} {y:.4f})
    (fp_text reference "{ref}" (at 0 -2.5) (layer "F.SilkS") {_fnt()})
    (fp_text value    "M3"     (at 0  2.5) (layer "F.Fab")   {_fnt()})
    (pad "" np_thru_hole circle (at 0 0) (size 3.2 3.2) (drill 3.2) (layers "*.Cu" "*.Mask") (tstamp "{nid()}"))
  )"""

# ── U1: VFQFPN-32 footprint ──────────────────────────────────────────────────
def fp_qfn32():
    pads = ""
    # Left side 1-8
    for i, n in enumerate(range(1, 9)):
        ry = -1.75 + i*0.5
        pads += (f'\n    (pad "{n}" smd roundrect (at {-QFN_D:.4f} {ry:.4f}) '
                 f'(size {QFN_LR[0]} {QFN_LR[1]}) (layers "F.Cu" "F.Paste" "F.Mask") '
                 f'(roundrect_rratio 0.25) (tstamp "{nid()}"))')
    # Bottom side 9-16
    for i, n in enumerate(range(9, 17)):
        rx = -1.75 + i*0.5
        pads += (f'\n    (pad "{n}" smd roundrect (at {rx:.4f} {QFN_D:.4f}) '
                 f'(size {QFN_TB[0]} {QFN_TB[1]}) (layers "F.Cu" "F.Paste" "F.Mask") '
                 f'(roundrect_rratio 0.25) (tstamp "{nid()}"))')
    # Right side 17-24
    for i, n in enumerate(range(17, 25)):
        ry = 1.75 - i*0.5
        pads += (f'\n    (pad "{n}" smd roundrect (at {QFN_D:.4f} {ry:.4f}) '
                 f'(size {QFN_LR[0]} {QFN_LR[1]}) (layers "F.Cu" "F.Paste" "F.Mask") '
                 f'(roundrect_rratio 0.25) (tstamp "{nid()}"))')
    # Top side 25-32
    for i, n in enumerate(range(25, 33)):
        rx = 1.75 - i*0.5
        pads += (f'\n    (pad "{n}" smd roundrect (at {rx:.4f} {-QFN_D:.4f}) '
                 f'(size {QFN_TB[0]} {QFN_TB[1]}) (layers "F.Cu" "F.Paste" "F.Mask") '
                 f'(roundrect_rratio 0.25) (tstamp "{nid()}"))')
    # Thermal pad 33 (no paste aperture in centre to avoid solder bridging)
    pads += (f'\n    (pad "33" smd rect (at 0 0) (size {QFN_EP} {QFN_EP}) '
             f'(property pad_prop_heatsink) (layers "F.Cu" "F.Mask") '
             f'(zone_connect 2) (tstamp "{nid()}"))')
    # Silkscreen outline
    silk = (f'\n    (fp_rect (start -2.5 -2.5) (end 2.5 2.5) '
            f'(layer "F.SilkS") (stroke (width 0.12)(type solid)))')
    silk += (f'\n    (fp_line (start -2.5 -2.5) (end -2.0 -2.5) '
             f'(layer "F.SilkS") (stroke (width 0.25)(type solid)))')  # pin-1 mark
    return f"""  (footprint "Package_DFN_QFN:QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm" (layer "F.Cu") (at {IC_X:.4f} {IC_Y:.4f})
    (fp_text reference "U1" (at 0 -3.5) (layer "F.SilkS") (effects (font (size 1 1)(thickness 0.15))))
    (fp_text value "ST25R3916B"  (at 0  3.5) (layer "F.Fab")   (effects (font (size 1 1)(thickness 0.15)))){silk}{pads}
  )"""

# ── Pad absolute position (accounting for rotation) ─────────────────────────
def pad_abs(cx, cy, lx, ly, rot):
    """Rotate local (lx,ly) by 'rot' degrees CCW, then add (cx,cy)."""
    import math
    r = math.radians(rot)
    ax = cx + lx*math.cos(r) - ly*math.sin(r)
    ay = cy + lx*math.sin(r) + ly*math.cos(r)
    return ax, ay

# ── Main PCB generator ───────────────────────────────────────────────────────
def generate():
    lines = []

    # ── Header ────────────────────────────────────────────────────────────────
    lines.append('(kicad_pcb (version 20221018) (generator pcbnew)')
    lines.append('  (general (thickness 1.6))')
    lines.append(
        '  (layers\n'
        '    (0 "F.Cu" signal) (31 "B.Cu" signal)\n'
        '    (36 "B.SilkS" user) (37 "F.SilkS" user)\n'
        '    (38 "B.Mask" user) (39 "F.Mask" user)\n'
        '    (44 "Edge.Cuts" user))'
    )
    lines.append(
        '  (setup\n'
        '    (stackup\n'
        '      (layer "F.Cu"       (type "copper")     (thickness 0.035))\n'
        '      (layer "dielectric 1" (type "core")     (thickness 1.51)(material "FR4")(epsilon_r 4.5))\n'
        '      (layer "B.Cu"       (type "copper")     (thickness 0.035)))\n'
        '    (pad_to_mask_clearance 0.05)\n'
        '    (solder_mask_min_width 0.0)\n'
        '    (pcbplotparams\n'
        '      (layerselection 0x00010fc_ffffffff)\n'
        '      (usegerberextensions true)\n'
        '      (usegerberattributes true)\n'
        '      (usegerberadvancedattributes true)\n'
        '      (creategerberjobfile false)\n'
        '      (gerberprecision 6)\n'
        '      (useauxorigin false)\n'
        '      (hpglpennumber 1)\n'
        '      (hpglpenspeed 20)\n'
        '      (hpglpendiameter 15.000000)\n'
        '      (dxfpolygonmode true)\n'
        '      (dxfimperialunits true)\n'
        '      (dxfusepcbnewfont true)\n'
        '      (psnegative false)\n'
        '      (psa4output false)\n'
        '      (plotreference true)\n'
        '      (plotvalue false)\n'
        '      (plotinvisibletext false)\n'
        '      (sketchpadsonfab false)\n'
        '      (subtractmaskfromsilk true)\n'
        '      (outputformat 1)\n'
        '      (mirror false)\n'
        '      (drillshape 0)\n'
        '      (scaleselection 1)\n'
        '      (outputdirectory "gerbers/")))'
    )

    # ── Net declarations ───────────────────────────────────────────────────────
    lines.append('  (net 0 "")')
    lines.append('  (net 1 "GND")')

    # ── Board outline ──────────────────────────────────────────────────────────
    for s,e in [((OX,OY),(OX+BW,OY)),((OX+BW,OY),(OX+BW,OY+BH)),
                ((OX+BW,OY+BH),(OX,OY+BH)),((OX,OY+BH),(OX,OY))]:
        lines.append(f'  (gr_line (start {s[0]} {s[1]}) (end {e[0]} {e[1]}) '
                     f'(layer "Edge.Cuts") (width 0.1) (tstamp "{nid()}"))')

    # ── Mounting holes (at board corners, inside antenna area) ────────────────
    lines.append(fp_mount_hole("H1", OX+3.5,  OY+3.5))
    lines.append(fp_mount_hole("H2", OX+BW-3.5, OY+3.5))
    lines.append(fp_mount_hole("H3", OX+BW-3.5, OY+BH-3.5))
    lines.append(fp_mount_hole("H4", OX+3.5,  OY+BH-3.5))

    # ── U1: ST25R3916B ─────────────────────────────────────────────────────────
    lines.append(fp_qfn32())

    # ── X1: Crystal (left of IC, pads 3=XTO, 4=XTI are at top, 1&2=GND at bot) ─
    X1_X, X1_Y = 111.5, IC_Y   # crystal centre
    lines.append(fp_crystal_3225("X1", "27.12MHz", X1_X, X1_Y))

    # ── Crystal load caps (C31 XTO-GND, C32 XTI-GND) ─────────────────────────
    # XTO (pin 4) abs ≈ (117.5625, 117.75); XTI (pin 5) ≈ (117.5625, 118.25)
    C31_X, C31_Y = 115.0, 117.0   # horizontal, left pad → XTO net, right → GND
    C32_X, C32_Y = 115.0, 119.0   # horizontal, left pad → XTI net, right → GND
    lines.append(fp_0603("C31", "12pF", C31_X, C31_Y, 0))
    lines.append(fp_0603("C32", "12pF", C32_X, C32_Y, 0))

    # ── RF matching network (below IC) ─────────────────────────────────────────
    # Vertical (rot=90): pad1=top (y-0.825), pad2=bottom (y+0.825)
    L1_X,  L1_Y  = 120.25, 122.5
    L2_X,  L2_Y  = 121.25, 122.5
    C1_X,  C1_Y  = 117.5,  122.5   # rot=0, horizontal shunt cap (pad2→NODE_A)
    C2_X,  C2_Y  = 124.0,  122.5   # rot=0, horizontal shunt cap (pad1→NODE_B)
    C3_X,  C3_Y  = 120.25, 125.0
    C4_X,  C4_Y  = 121.25, 125.0
    R1_X,  R1_Y  = 120.25, 127.5
    R2_X,  R2_Y  = 121.25, 127.5

    lines.append(fp_0603("L1", "270nH",  L1_X, L1_Y, 90))
    lines.append(fp_0603("L2", "270nH",  L2_X, L2_Y, 90))
    lines.append(fp_0603("C1", "220pF",  C1_X, C1_Y, 0))
    lines.append(fp_0603("C2", "220pF",  C2_X, C2_Y, 0))
    lines.append(fp_0603("C3", "120pF",  C3_X, C3_Y, 90))
    lines.append(fp_0603("C4", "120pF",  C4_X, C4_Y, 90))
    lines.append(fp_0603("R1", "2.2R",   R1_X, R1_Y, 90))
    lines.append(fp_0603("R2", "2.2R",   R2_X, R2_Y, 90))

    # ── RX coupling/shunt caps (right of IC, near RFI1/RFI2) ──────────────────
    # C5 pad1 → RFI1 net; C5 pad2 → GND
    # C6 pad1 → RFI2 net; C6 pad2 → GND
    C5_X, C5_Y = 124.5, 117.25
    C6_X, C6_Y = 124.5, 116.75
    lines.append(fp_0603("C5", "180pF",  C5_X, C5_Y, 0))
    lines.append(fp_0603("C6", "180pF",  C6_X, C6_Y, 0))

    # ── Decoupling caps ───────────────────────────────────────────────────────
    C15_X, C15_Y = 115.0, 119.75   # VDD bypass (pin 8)
    C16_X, C16_Y = 115.0, 116.25   # VDD_IO bypass (pin 1)
    C17_X, C17_Y = 113.5, 119.75   # VDD bulk 2.2µF
    C18_X, C18_Y = 113.5, 116.25   # VDD_IO bulk
    C19_X, C19_Y = 124.5, 115.5    # AGDC (pin 24) bypass
    C20_X, C20_Y = 120.0, 121.5    # VDD_TX/VDD_DR bypass (bottom pins)
    lines.append(fp_0603("C15", "100nF",  C15_X, C15_Y, 0))
    lines.append(fp_0603("C16", "100nF",  C16_X, C16_Y, 0))
    lines.append(fp_0603("C17", "2.2uF",  C17_X, C17_Y, 0))
    lines.append(fp_0603("C18", "2.2uF",  C18_X, C18_Y, 0))
    lines.append(fp_0603("C19", "10nF",   C19_X, C19_Y, 0))
    lines.append(fp_0603("C20", "10nF",   C20_X, C20_Y, 0))

    # ── U3: 3.3V LDO SOT-23-5 (top-right, above IC) ──────────────────────────
    U3_X, U3_Y = 126.5, 107.0
    lines.append(fp_sot23_5("U3", "3V3_LDO", U3_X, U3_Y, 0))

    # ── CN1: 7-pin 2.54mm through-hole connector (left side, vertical) ────────
    # Pin 1=GND, 2=VIN(5V), 3=MISO, 4=MOSI, 5=SCLK, 6=CS(/SS), 7=IRQ
    CN1_X, CN1_Y = 106.5, 107.5
    lines.append(fp_header_7pin("CN1", CN1_X, CN1_Y, 0))

    # ── Traces ────────────────────────────────────────────────────────────────
    # --- Crystal circuit ---
    # XTO (pin4) → C31 pad1 → X1 pad3
    xto = ic_pin(P['XTO'])   # (117.5625, 117.75)
    xti = ic_pin(P['XTI'])   # (117.5625, 118.25)
    # C31 at (115, 117): pad1=(114.175,117) pad2=(115.825,117)
    # C32 at (115, 119): pad1=(114.175,119) pad2=(115.825,119)
    # X1 pad3=(112.6, 117.15)  X1 pad4=(109.9, 117.15)
    # X1 pad1=(109.9, 118.85)  X1 pad2=(112.6, 118.85) [GND]
    lines += [
        # XTO → C31 pad2 (right pad)
        seg(xto[0], xto[1], 115.825, C31_Y),
        # C31 pad1 (left) → X1 pad3 (XTO terminal)
        seg(114.175, C31_Y, 112.6, 117.15),
        # XTI → C32 pad2 (right pad) -- need short detour to avoid C31
        seg(xti[0], xti[1], xti[0], C32_Y),
        seg(xti[0], C32_Y, 115.825, C32_Y),
        # C32 pad1 (left) → X1 pad4 (XTI terminal)
        seg(114.175, C32_Y, 112.6, 119.0),
        seg(112.6, 119.0, 109.9, 119.0),
        seg(109.9, 119.0, 109.9, 118.85),
        # X1 GND pads → GND vias
        # X1 pad1 (-1.1, +0.85) abs = (110.4, 118.85)
        # X1 pad2 (+1.1, +0.85) abs = (112.6, 118.85)
    ]

    # --- Matching network: RFO1 → L1 → NODE_A (C1+C3) → R1 → ANT1 ---
    # RFO1 pad abs: ic_pin(13)
    rfo1 = ic_pin(P['RFO1'])  # (120.25, 120.4375)
    rfo2 = ic_pin(P['RFO2'])  # (121.25, 120.4375)
    # L1 rot=90: pad1 top=(L1_X, L1_Y-0.825)=(120.25,121.675), pad2 bot=(120.25,123.325)
    # NODE_A = L1pad2 = C3pad1_top = C1pad2_right junction at x=120.25, y=123.325
    # C1 rot=0: pad1=(116.675,122.5), pad2=(118.325,122.5) → pad2 connects to NODE_A via trace
    # C3 rot=90: pad1 top=(120.25,124.175), pad2 bot=(120.25,125.825)
    # R1 rot=90: pad1=(120.25,126.675), pad2=(120.25,128.325)=ANT1

    lines += [
        seg(rfo1[0], rfo1[1], L1_X, L1_Y-0.825),          # RFO1→L1pad1
        seg(L1_X, L1_Y+0.825, L1_X, C3_Y-0.825),           # L1pad2→C3pad1 (NODE_A vertical)
        seg(L1_X, L1_Y+0.825, 118.325, C1_Y),               # NODE_A→C1pad2 (L-shape)
        seg(C3_Y-0.825-0.5, C1_Y, 118.325, C1_Y),           # not needed; join inline
        seg(C3_X, C3_Y+0.825, R1_X, R1_Y-0.825),            # C3pad2→R1pad1
    ]
    # Same for RFO2 branch
    lines += [
        seg(rfo2[0], rfo2[1], L2_X, L2_Y-0.825),            # RFO2→L2pad1
        seg(L2_X, L2_Y+0.825, L2_X, C4_Y-0.825),            # L2pad2→C4pad1 (NODE_B)
        seg(L2_X, L2_Y+0.825, 123.175, C2_Y),               # NODE_B→C2pad1
        seg(C4_X, C4_Y+0.825, R2_X, R2_Y-0.825),            # C4pad2→R2pad1
    ]

    # --- ANT1 (R1 pad2) → antenna inner turn ---
    # ANT1 = (120.25, 128.325)
    # Route: down to y=132, left to x=104.5 (inner turn left side)
    ANT1_X, ANT1_Y = R1_X, R1_Y + 0.825   # (120.25, 128.325)
    ANT2_X, ANT2_Y = R2_X, R2_Y + 0.825   # (121.25, 128.325)
    INNER = 3.5   # inner turn offset from board edge
    OUTER = 1.5   # outer turn offset from board edge
    ix_min = OX + INNER   # 103.5
    ix_max = OX+BW - INNER  # 136.5
    iy_min = OY + INNER   # 103.5
    iy_max = OY+BH - INNER  # 136.5

    # Inner turn: ANT1 → left → up → right → down → left → up to crossover via
    lines += [
        seg(ANT1_X, ANT1_Y, ANT1_X, 132.0, w=0.5),          # ANT1 down to y=132
        seg(ANT1_X, 132.0, ix_min, 132.0, w=0.5),            # left to inner-left
        seg(ix_min, 132.0, ix_min, iy_min, w=0.5),           # up inner-left
        seg(ix_min, iy_min, ix_max, iy_min, w=0.5),          # inner-top
        seg(ix_max, iy_min, ix_max, iy_max, w=0.5),          # inner-right
        seg(ix_max, iy_max, ix_min, iy_max, w=0.5),          # inner-bottom
        seg(ix_min, iy_max, ix_min, 134.0, w=0.5),           # inner-left partial (up to via)
    ]
    # Via from inner turn to B.Cu for crossover to outer turn
    lines.append(via(ix_min, 134.0, size=1.0, drill=0.5))     # V_cross_inner
    # B.Cu crossover: go left to outer-left, then up slightly to outer turn start
    ox_min = OX + OUTER   # 101.5
    lines += [
        seg(ix_min, 134.0, ox_min+0.5, 134.0, layer="B.Cu", w=0.5),  # B.Cu crossover
    ]
    lines.append(via(ox_min+0.5, 134.0, size=1.0, drill=0.5))  # V_cross_outer
    # Outer turn: starts at (102, 134) → up outer-left → right outer-top → down outer-right
    # → left outer-bottom → short up to ANT2 feed via B.Cu
    lines += [
        seg(ox_min+0.5, 134.0, ox_min, 134.0, w=0.5),
        seg(ox_min, 134.0, ox_min, iy_min-1, w=0.5),         # outer-left up
        seg(ox_min, iy_min-1, ox_min, OY+OUTER, w=0.5),      # continue to outer-top-left
        seg(OX+OUTER, OY+OUTER, OX+BW-OUTER, OY+OUTER, w=0.5),  # outer-top
        seg(OX+BW-OUTER, OY+OUTER, OX+BW-OUTER, OY+BH-OUTER, w=0.5),  # outer-right
        seg(OX+BW-OUTER, OY+BH-OUTER, OX+OUTER, OY+BH-OUTER, w=0.5),  # outer-bottom
        seg(OX+OUTER, OY+BH-OUTER, OX+OUTER, 136.0, w=0.5),  # outer-left partial
    ]
    # ANT2 connection: outer turn end at (101.5, 136) → via → B.Cu → via → R2pad2
    lines.append(via(OX+OUTER, 136.0, size=1.0, drill=0.5))   # V_ant2a
    lines += [
        seg(OX+OUTER, 136.0, ANT2_X, 136.0, layer="B.Cu", w=0.5),
        seg(ANT2_X, 136.0, ANT2_X, ANT2_Y, layer="B.Cu", w=0.5),
    ]
    lines.append(via(ANT2_X, ANT2_Y, size=1.0, drill=0.5))    # V_ant2b

    # --- RFI1 / RFI2: connect NODE_A/NODE_B to RFI pins via B.Cu around IC ---
    # NODE_A = L1pad2 = (120.25, 123.325); RFI1 = ic_pin(22)
    rfi1 = ic_pin(P['RFI1'])   # (122.4375, 117.25)
    rfi2 = ic_pin(P['RFI2'])   # (122.4375, 116.75)
    # Via near NODE_A → B.Cu → outside IC → via → short F.Cu → RFI1
    lines.append(via(119.0, 124.0))   # V_rfi1a: on NODE_A rail
    lines += [
        seg(L1_X, L1_Y+0.825, 119.0, L1_Y+0.825),           # NODE_A branch to via
        seg(119.0, L1_Y+0.825, 119.0, 124.0),
        # B.Cu path around IC left → top → right to RFI1
        seg(119.0, 124.0, 115.0, 124.0, layer="B.Cu"),
        seg(115.0, 124.0, 115.0, 114.5, layer="B.Cu"),
        seg(115.0, 114.5, 123.0, 114.5, layer="B.Cu"),
        seg(123.0, 114.5, 123.0, rfi1[1], layer="B.Cu"),
    ]
    lines.append(via(123.0, rfi1[1]))   # V_rfi1b
    lines += [
        seg(123.0, rfi1[1], rfi1[0], rfi1[1]),               # → RFI1 pad
        seg(123.0, rfi1[1], C5_X-0.825, C5_Y),               # → C5 pad1
    ]
    # RFI2 via NODE_B
    lines.append(via(119.5, 124.5))   # V_rfi2a
    lines += [
        seg(L2_X, L2_Y+0.825, 119.5, L2_Y+0.825),
        seg(119.5, L2_Y+0.825, 119.5, 124.5),
        seg(119.5, 124.5, 114.5, 124.5, layer="B.Cu"),
        seg(114.5, 124.5, 114.5, 114.0, layer="B.Cu"),
        seg(114.5, 114.0, 123.5, 114.0, layer="B.Cu"),
        seg(123.5, 114.0, 123.5, rfi2[1], layer="B.Cu"),
    ]
    lines.append(via(123.5, rfi2[1]))
    lines += [
        seg(123.5, rfi2[1], rfi2[0], rfi2[1]),
        seg(123.5, rfi2[1], C6_X-0.825, C6_Y),
    ]

    # --- SPI interface: IC top pins → CN1 ---
    # CN1 at (106.5, 107.5), rot=0, pins at y=107.5, 110.04, 112.58, 115.12, 117.66, 120.20, 122.74
    # Pin3=MISO(y=110.04), 4=MOSI(y=112.58), 5=SCLK(y=115.12), 6=CS(y=117.66), 7=IRQ(y=120.2)
    cn1_x = CN1_X
    cn1_pins_y = [CN1_Y + i*2.54 for i in range(7)]  # [107.5, 110.04, 112.58, 115.12, 117.66, 120.20, 122.74]
    miso = ic_pin(P['MISO'])   # (118.25, 115.5625)
    mosi = ic_pin(P['MOSI'])   # (118.75, 115.5625)
    sclk = ic_pin(P['SCLK'])   # (119.25, 115.5625)
    bss  = ic_pin(P['BSS'])    # (119.75, 115.5625)
    irq  = ic_pin(P['IRQ'])    # (120.75, 115.5625)
    # Route each SPI signal: go up to y=113, then left to x=cn1_x, then down to connector pin
    SPI_BUS_Y = 113.0
    for sig, pin, py in [
        (miso, 3, cn1_pins_y[2]),   # MISO → CN1 pin3
        (mosi, 4, cn1_pins_y[3]),   # MOSI → CN1 pin4
        (sclk, 5, cn1_pins_y[4]),   # SCLK → CN1 pin5
        (bss,  6, cn1_pins_y[5]),   # CS   → CN1 pin6
        (irq,  7, cn1_pins_y[6]),   # IRQ  → CN1 pin7
    ]:
        lines += [
            seg(sig[0], sig[1], sig[0], SPI_BUS_Y),
            seg(sig[0], SPI_BUS_Y, cn1_x, SPI_BUS_Y),
            seg(cn1_x, SPI_BUS_Y, cn1_x, py),
        ]

    # I2C_EN (pin 20) pull to GND via short trace + via to B.Cu GND pour
    i2c_en = ic_pin(P['I2C_EN'])
    lines += [seg(i2c_en[0], i2c_en[1], i2c_en[0]+1.5, i2c_en[1])]
    lines.append(via(i2c_en[0]+1.5, i2c_en[1]))  # to GND pour

    # --- Power rail: 3.3V bus on F.Cu ---
    # LDO: U3 at (126.5, 107)  pad5=(127.6375, 106.05)=OUT
    # VDD power bus: vertical trace at x=116 from y=106 to y=120.5
    VDD_BUS_X = 116.0
    u3_out = (U3_X + 1.1375, U3_Y - 0.95)   # pad 5
    u3_in  = (U3_X - 1.1375, U3_Y - 0.95)   # pad 1 (IN)
    u3_en  = (U3_X - 1.1375, U3_Y + 0.95)   # pad 3 (EN, tie to IN)
    u3_fb  = (U3_X + 1.1375, U3_Y + 0.95)   # pad 4 (FB = OUT for fixed)
    u3_gnd = (U3_X - 1.1375, U3_Y)          # pad 2 (GND)

    # LDO output → VDD bus
    lines += [
        seg(u3_out[0], u3_out[1], u3_out[0], 106.0),
        seg(u3_out[0], 106.0, VDD_BUS_X, 106.0),
        # VDD bus down left side of IC
        seg(VDD_BUS_X, 106.0, VDD_BUS_X, 120.5, w=0.4),
        # EN tied to IN (always on)
        seg(u3_en[0], u3_en[1], u3_in[0], u3_in[1]),
        # FB tied to OUT
        seg(u3_fb[0], u3_fb[1], u3_out[0], u3_out[1]),
    ]
    # VDD bus → IC supply pins (short horizontal traces)
    vdd_pins = [
        (P['VDD_IO'], ic_pin(P['VDD_IO'])),
        (P['VDD_D'],  ic_pin(P['VDD_D'])),
        (P['VDD_A'],  ic_pin(P['VDD_A'])),
        (P['VDD'],    ic_pin(P['VDD'])),
    ]
    for _, pp in vdd_pins:
        lines.append(seg(VDD_BUS_X, pp[1], pp[0], pp[1], w=0.3))

    # VDD_TX (p10) and VDD_DR (p14) on bottom: horizontal bus at y=121.5
    lines += [
        seg(VDD_BUS_X, 121.5, 121.0, 121.5, w=0.3),          # H-bus
        seg(VDD_BUS_X, 120.5, VDD_BUS_X, 121.5, w=0.3),      # join V to H bus
    ]
    for pn in [P['VDD_TX'], P['VDD_DR']]:
        pp = ic_pin(pn)
        lines.append(seg(pp[0], 121.5, pp[0], pp[1], w=0.3))

    # VDD bus → bypass caps
    # C15(100nF) at (115,119.75) rot=0: pad1=(114.175,119.75) pad2=(115.825,119.75)
    # pad2 → VDD bus
    lines.append(seg(115.825, C15_Y, VDD_BUS_X, C15_Y, w=0.3))
    lines.append(seg(115.825, C16_Y, VDD_BUS_X, C16_Y, w=0.3))
    lines.append(seg(115.825, C17_Y, VDD_BUS_X, C17_Y, w=0.3))
    lines.append(seg(115.825, C18_Y, VDD_BUS_X, C18_Y, w=0.3))

    # C20 (VDD_TX bypass) at (120.0, 121.5) rot=0: pad2=(120.825, 121.5) → H-bus
    lines.append(seg(120.825, C20_Y, 121.0, C20_Y, w=0.3))

    # LDO VIN (pad1) ← CN1 pin2 (VIN/5V)
    # CN1 pin2 at (cn1_x, cn1_pins_y[1]) = (106.5, 110.04)
    lines += [
        seg(cn1_x, cn1_pins_y[1], cn1_x, 106.5),
        seg(cn1_x, 106.5, u3_in[0], 106.5),
        seg(u3_in[0], 106.5, u3_in[0], u3_in[1]),
    ]

    # --- GND connections: vias to B.Cu pour ---
    # IC GND pins: 6 (GND_D), 12 (GND_DR1), 16 (GND_DR2), 21 (VSS), 26 (GND_A)
    for pn in [P['GND_D'], P['GND_DR1'], P['GND_DR2'], P['VSS'], P['GND_A']]:
        pp = ic_pin(pn)
        lines.append(via(pp[0], pp[1]))

    # Thermal pad vias (3×3 array)
    for dx in [-1.0, 0.0, 1.0]:
        for dy in [-1.0, 0.0, 1.0]:
            lines.append(via(IC_X+dx, IC_Y+dy, size=0.6, drill=0.3))

    # Bypass cap GND pads → via
    for cx, cy, side in [
        (C31_X+0.825, C31_Y, None),   # C31 pad2 → GND
        (C32_X+0.825, C32_Y, None),   # C32 pad2
        (C1_X-0.825,  C1_Y,  None),   # C1 pad1
        (C2_X+0.825,  C2_Y,  None),   # C2 pad2
        (C5_X+0.825,  C5_Y,  None),   # C5 pad2
        (C6_X+0.825,  C6_Y,  None),   # C6 pad2
        (C15_X-0.825, C15_Y, None),   # C15 pad1
        (C16_X-0.825, C16_Y, None),
        (C17_X-0.825, C17_Y, None),
        (C18_X-0.825, C18_Y, None),
        (C19_X-0.825, C19_Y, None),
        (C20_X-0.825, C20_Y, None),
        (u3_gnd[0],   u3_gnd[1], None),  # LDO GND
    ]:
        lines.append(via(cx, cy))

    # Crystal GND pads
    lines.append(via(X1_X-1.1, X1_Y+0.85))
    lines.append(via(X1_X+1.1, X1_Y+0.85))

    # CN1 pin1 (GND) → via
    lines.append(via(cn1_x, cn1_pins_y[0]))

    # AGDC bypass cap GND
    lines.append(seg(C19_X+0.825, C19_Y, C19_X+1.2, C19_Y))
    lines.append(via(C19_X+1.2, C19_Y))
    # AGDC (pin24) → C19 pad1
    agdc = ic_pin(P['AGDC'])
    lines.append(seg(agdc[0], agdc[1], agdc[0]+0.5, agdc[1]))
    lines.append(seg(agdc[0]+0.5, agdc[1], agdc[0]+0.5, C19_Y))
    lines.append(seg(agdc[0]+0.5, C19_Y, C19_X-0.825, C19_Y))

    # ── Silkscreen labels ──────────────────────────────────────────────────────
    lines.append(
        f'  (gr_text "ST25R3916B NFC Reader" (at {CX} {OY+1.5} 0) (layer "F.SilkS")'
        f'  (effects (font (size 1.0 1.0)(thickness 0.15))))'
    )
    lines.append(
        f'  (gr_text "GND VIN MISO MOSI SCLK CS IRQ" (at {CX} {OY+BH-1.5} 0) (layer "F.SilkS")'
        f'  (effects (font (size 0.6 0.6)(thickness 0.10))))'
    )

    # ── B.Cu GND pour ──────────────────────────────────────────────────────────
    lines.append(zone_gnd("B.Cu"))

    lines.append(')')   # close kicad_pcb

    return '\n'.join(lines)


# ── BOM & CPL generators ─────────────────────────────────────────────────────
def write_bom(path):
    rows = [
        ["Comment",      "Designator",             "Footprint",                        "LCSC Part #"],
        ["ST25R3916B",   "U1",                     "QFN-32-1EP_5x5mm_P0.5mm_EP3.45",  "C17315217"],
        ["270nH",        "L1,L2",                  "0603",                             "C1034"],
        ["220pF",        "C1,C2",                  "0603",                             "C1600"],
        ["120pF",        "C3,C4",                  "0603",                             "C1591"],
        ["180pF",        "C5,C6",                  "0603",                             "C1598"],
        ["2.2R",         "R1,R2",                  "0603",                             "C22935"],
        ["27.12MHz",     "X1",                     "Crystal_SMD_3225-4Pin_3.2x2.5mm", "C112441"],
        ["12pF",         "C31,C32",                "0603",                             "C1547"],
        ["3V3 LDO",      "U3",                     "SOT-23-5",                         "C7955"],
        ["100nF",        "C15,C16",                "0603",                             "C14663"],
        ["2.2uF",        "C17,C18",                "0603",                             "C1602"],
        ["10nF",         "C19,C20",                "0603",                             "C1514"],
    ]
    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerows(rows)
    print(f"  Wrote {path}")


def write_cpl(path):
    # JLCPCB CPL: Designator, Mid X (mm from left), Mid Y (mm from bottom), Layer, Rotation
    # KiCad Y increases DOWN, JLCPCB Y increases UP → flip: jlc_y = BH - (kicad_y - OY)
    def jy(kicad_y): return round(BH - (kicad_y - OY), 4)
    def jx(kicad_x): return round(kicad_x - OX, 4)

    rows = [["Designator","Mid X","Mid Y","Layer","Rotation"]]
    comps = [
        ("U1",  IC_X,   IC_Y,   "Top", 0),
        ("X1",  111.5,  IC_Y,   "Top", 0),
        ("C31", 115.0,  117.0,  "Top", 0),
        ("C32", 115.0,  119.0,  "Top", 0),
        ("L1",  120.25, 122.5,  "Top", 90),
        ("L2",  121.25, 122.5,  "Top", 90),
        ("C1",  117.5,  122.5,  "Top", 0),
        ("C2",  124.0,  122.5,  "Top", 0),
        ("C3",  120.25, 125.0,  "Top", 90),
        ("C4",  121.25, 125.0,  "Top", 90),
        ("R1",  120.25, 127.5,  "Top", 90),
        ("R2",  121.25, 127.5,  "Top", 90),
        ("C5",  124.5,  117.25, "Top", 0),
        ("C6",  124.5,  116.75, "Top", 0),
        ("C15", 115.0,  119.75, "Top", 0),
        ("C16", 115.0,  116.25, "Top", 0),
        ("C17", 113.5,  119.75, "Top", 0),
        ("C18", 113.5,  116.25, "Top", 0),
        ("C19", 124.5,  115.5,  "Top", 0),
        ("C20", 120.0,  121.5,  "Top", 0),
        ("U3",  126.5,  107.0,  "Top", 0),
        # CN1 is through-hole - typically not assembled by JLCPCB basic PCBA
        # Include it so user can optionally add to assembly order
        ("CN1", 106.5,  114.27, "Top", 0),   # centre of 7-pin header
    ]
    for ref, kx, ky, layer, rot in comps:
        rows.append([ref, jx(kx), jy(ky), layer, rot])
    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerows(rows)
    print(f"  Wrote {path}")


# ── Entry point ──────────────────────────────────────────────────────────────
if __name__ == "__main__":
    base = os.path.dirname(os.path.abspath(__file__))
    pcb_path = os.path.join(base, "ST25R3916B_Custom.kicad_pcb")
    bom_path = os.path.join(base, "jlcpcb_bom.csv")
    cpl_path = os.path.join(base, "jlcpcb_cpl.csv")

    print("Generating PCB …")
    with open(pcb_path, 'w') as f:
        f.write(generate())
    print(f"  Wrote {pcb_path}")

    write_bom(bom_path)
    write_cpl(cpl_path)
    print("Done.")
