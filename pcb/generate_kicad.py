#!/usr/bin/env python3
"""
ST25R3916B Custom NFC Reader PCB Generator
Generates a complete KiCad 7 PCB + JLCPCB PCBA files.
Board: 40x40mm, 2-layer 1.6mm FR4.

Run:  python3 pcb/generate_kicad.py
Then: kicad-cli pcb export gerbers ...
"""
import os, csv, math

# ── Board constants ──────────────────────────────────────────────────────────
OX, OY = 100.0, 100.0
BW, BH = 40.0, 40.0
CX, CY = OX + BW / 2, OY + BH / 2   # (120, 120)
IC_X, IC_Y = CX, CY                  # IC dead-center on board

# VFQFPN-32 exact KiCad library values
QFN_D  = 2.4375   # pad-centre distance from IC centre
QFN_LR = (0.875, 0.25)   # left/right pad size
QFN_TB = (0.25, 0.875)   # top/bottom pad size
QFN_EP = 3.45            # exposed thermal pad size

# ── Pin positions ─────────────────────────────────────────────────────────────
def ic_pin(n):
    """Absolute (x, y) of VFQFPN-32 pad centre. Pin 1=top-left, CCW."""
    if   1 <= n <=  8: return IC_X - QFN_D,  IC_Y + (-1.75 + (n-1)*0.5)
    elif 9 <= n <= 16: return IC_X + (-1.75 + (n-9)*0.5),  IC_Y + QFN_D
    elif 17 <= n <= 24:return IC_X + QFN_D,  IC_Y + (1.75 - (n-17)*0.5)
    elif 25 <= n <= 32:return IC_X + (1.75 - (n-25)*0.5), IC_Y - QFN_D
    elif n == 33:      return IC_X, IC_Y
    raise ValueError(n)

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

# ── ID counter ────────────────────────────────────────────────────────────────
_id = 0
def nid():
    global _id; _id += 1; return f"TS{_id:04d}"

# ── Primitives ────────────────────────────────────────────────────────────────
def seg(x1, y1, x2, y2, layer="F.Cu", w=0.2):
    return (f'  (segment (start {x1:.4f} {y1:.4f}) (end {x2:.4f} {y2:.4f}) '
            f'(width {w}) (layer "{layer}") (tstamp "{nid()}"))')

def via(x, y, size=0.8, drill=0.4):
    return (f'  (via (at {x:.4f} {y:.4f}) (size {size}) (drill {drill}) '
            f'(layers "F.Cu" "B.Cu") (tstamp "{nid()}"))')

def zone_gnd():
    xmin, ymin = OX+0.5, OY+0.5
    xmax, ymax = OX+BW-0.5, OY+BH-0.5
    return f"""  (zone (net 1) (net_name "GND") (layer "B.Cu") (tstamp "{nid()}")
    (connect_pads (clearance 0.3))
    (min_thickness 0.25)
    (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5))
    (polygon (pts
      (xy {xmin} {ymin}) (xy {xmax} {ymin})
      (xy {xmax} {ymax}) (xy {xmin} {ymax})))
  )"""

# ── Footprint helpers ─────────────────────────────────────────────────────────
def _fnt(sz=0.8):
    return f'(effects (font (size {sz} {sz}) (thickness 0.12)))'

def fp_0603(ref, val, x, y, rot=0):
    return f"""  (footprint "Resistor_SMD:R_0603_1608Metric" (layer "F.Cu") (at {x:.4f} {y:.4f} {rot})
    (fp_text reference "{ref}" (at 0 -1.5 {rot}) (layer "F.SilkS") {_fnt()})
    (fp_text value    "{val}"  (at 0  1.5 {rot}) (layer "F.Fab")   {_fnt()})
    (pad "1" smd roundrect (at -0.825 0 {rot}) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (tstamp "{nid()}"))
    (pad "2" smd roundrect (at  0.825 0 {rot}) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (tstamp "{nid()}"))
  )"""

def fp_crystal_3225(ref, val, x, y, rot=0):
    """Pads: 1(-1.1,+0.85)=GND  2(+1.1,+0.85)=GND  3(+1.1,-0.85)=XTO  4(-1.1,-0.85)=XTI"""
    return f"""  (footprint "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm" (layer "F.Cu") (at {x:.4f} {y:.4f} {rot})
    (fp_text reference "{ref}" (at 0 -2 {rot}) (layer "F.SilkS") {_fnt()})
    (fp_text value    "{val}"  (at 0  2 {rot}) (layer "F.Fab")   {_fnt()})
    (pad "1" smd rect (at -1.1  0.85 {rot}) (size 1.4 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (tstamp "{nid()}"))
    (pad "2" smd rect (at  1.1  0.85 {rot}) (size 1.4 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (tstamp "{nid()}"))
    (pad "3" smd rect (at  1.1 -0.85 {rot}) (size 1.4 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (tstamp "{nid()}"))
    (pad "4" smd rect (at -1.1 -0.85 {rot}) (size 1.4 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (tstamp "{nid()}"))
  )"""

def fp_sot23_5(ref, val, x, y, rot=0):
    """SOT-23-5: Pad1=IN Pad2=GND Pad3=EN Pad4=OUT Pad5=FB"""
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
    """7-pin 2.54mm through-hole, pin1 at top."""
    pads = ""
    for i in range(7):
        pads += (f'\n    (pad "{i+1}" thru_hole circle (at 0 {i*2.54:.2f} {rot}) '
                 f'(size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask") (tstamp "{nid()}"))')
    return f"""  (footprint "Connector_PinHeader_2.54mm:PinHeader_1x07_P2.54mm_Vertical" (layer "F.Cu") (at {x:.4f} {y:.4f} {rot})
    (fp_text reference "{ref}" (at -2 7.62 {rot}) (layer "F.SilkS") {_fnt()})
    (fp_text value    "7P-2.54mm" (at 2 7.62 {rot}) (layer "F.Fab")   {_fnt()}){pads}
  )"""

def fp_qfn32():
    pads = ""
    for i, n in enumerate(range(1, 9)):    # Left 1-8
        ry = -1.75 + i*0.5
        pads += (f'\n    (pad "{n}" smd roundrect (at {-QFN_D:.4f} {ry:.4f}) '
                 f'(size {QFN_LR[0]} {QFN_LR[1]}) (layers "F.Cu" "F.Paste" "F.Mask") '
                 f'(roundrect_rratio 0.25) (tstamp "{nid()}"))')
    for i, n in enumerate(range(9, 17)):   # Bottom 9-16
        rx = -1.75 + i*0.5
        pads += (f'\n    (pad "{n}" smd roundrect (at {rx:.4f} {QFN_D:.4f}) '
                 f'(size {QFN_TB[0]} {QFN_TB[1]}) (layers "F.Cu" "F.Paste" "F.Mask") '
                 f'(roundrect_rratio 0.25) (tstamp "{nid()}"))')
    for i, n in enumerate(range(17, 25)):  # Right 17-24
        ry = 1.75 - i*0.5
        pads += (f'\n    (pad "{n}" smd roundrect (at {QFN_D:.4f} {ry:.4f}) '
                 f'(size {QFN_LR[0]} {QFN_LR[1]}) (layers "F.Cu" "F.Paste" "F.Mask") '
                 f'(roundrect_rratio 0.25) (tstamp "{nid()}"))')
    for i, n in enumerate(range(25, 33)):  # Top 25-32
        rx = 1.75 - i*0.5
        pads += (f'\n    (pad "{n}" smd roundrect (at {rx:.4f} {-QFN_D:.4f}) '
                 f'(size {QFN_TB[0]} {QFN_TB[1]}) (layers "F.Cu" "F.Paste" "F.Mask") '
                 f'(roundrect_rratio 0.25) (tstamp "{nid()}"))')
    pads += (f'\n    (pad "33" smd rect (at 0 0) (size {QFN_EP} {QFN_EP}) '
             f'(property pad_prop_heatsink) (layers "F.Cu" "F.Mask") '
             f'(zone_connect 2) (tstamp "{nid()}"))')
    silk  = (f'\n    (fp_rect (start -2.5 -2.5) (end 2.5 2.5) '
             f'(layer "F.SilkS") (stroke (width 0.12)(type solid)))')
    silk += (f'\n    (fp_line (start -2.5 -2.5) (end -2.0 -2.5) '
             f'(layer "F.SilkS") (stroke (width 0.25)(type solid)))')
    return f"""  (footprint "Package_DFN_QFN:QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm" (layer "F.Cu") (at {IC_X:.4f} {IC_Y:.4f})
    (fp_text reference "U1" (at 0 -3.5) (layer "F.SilkS") (effects (font (size 1 1)(thickness 0.15))))
    (fp_text value "ST25R3916B" (at 0 3.5) (layer "F.Fab") (effects (font (size 1 1)(thickness 0.15)))){silk}{pads}
  )"""

# ── Main generator ────────────────────────────────────────────────────────────
def generate():
    lines = []

    # Header
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
        '      (layer "F.Cu"         (type "copper") (thickness 0.035))\n'
        '      (layer "dielectric 1" (type "core")   (thickness 1.51)(material "FR4")(epsilon_r 4.5))\n'
        '      (layer "B.Cu"         (type "copper") (thickness 0.035)))\n'
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
        '      (outputformat 1)\n'
        '      (mirror false)\n'
        '      (drillshape 0)\n'
        '      (scaleselection 1)\n'
        '      (outputdirectory "gerbers/")))'
    )

    # Net declarations
    lines.append('  (net 0 "")')
    lines.append('  (net 1 "GND")')

    # Board outline (40x40mm)
    for s, e in [((OX,OY),(OX+BW,OY)), ((OX+BW,OY),(OX+BW,OY+BH)),
                 ((OX+BW,OY+BH),(OX,OY+BH)), ((OX,OY+BH),(OX,OY))]:
        lines.append(f'  (gr_line (start {s[0]} {s[1]}) (end {e[0]} {e[1]}) '
                     f'(layer "Edge.Cuts") (width 0.1) (tstamp "{nid()}"))')

    # ── U1: ST25R3916B ─────────────────────────────────────────────────────────
    lines.append(fp_qfn32())

    # ── Component positions ───────────────────────────────────────────────────
    # Crystal: 9.5mm left of IC centre
    X1_X,  X1_Y  = 110.5, 120.0
    # Crystal load caps: between X1 and IC
    C31_X, C31_Y = 114.5, 119.0   # XTO cap, horizontal
    C32_X, C32_Y = 114.5, 121.0   # XTI cap, horizontal

    # VDD decoupling: two columns, above and below crystal y-zone
    C15_X, C15_Y = 111.5, 115.0   # VDD_IO 100nF  (above crystal)
    C16_X, C16_Y = 111.5, 124.0   # VDD_D  100nF  (below crystal)
    C17_X, C17_Y = 108.5, 115.0   # VDD_IO 2.2uF
    C18_X, C18_Y = 108.5, 124.0   # VDD_D  2.2uF

    # RF matching: below IC, generously spaced
    # L1/L2 vertical (rot=90): pad1=top (y-0.825), pad2=bottom (y+0.825)
    L1_X,  L1_Y  = 119.5, 126.0
    L2_X,  L2_Y  = 121.5, 126.0
    C1_X,  C1_Y  = 116.5, 126.0   # EMC shunt NODE_A
    C2_X,  C2_Y  = 124.5, 126.0   # EMC shunt NODE_B
    C3_X,  C3_Y  = 119.5, 129.0   # series matching
    C4_X,  C4_Y  = 121.5, 129.0
    R1_X,  R1_Y  = 119.5, 132.0   # damping; pad2=ANT1 feed
    R2_X,  R2_Y  = 121.5, 132.0   # pad2=ANT2 feed

    # RFI shunt and AGDC bypass (right of IC)
    C5_X,  C5_Y  = 125.5, 119.5   # RFI1 shunt
    C6_X,  C6_Y  = 125.5, 118.0   # RFI2 shunt
    C19_X, C19_Y = 125.5, 116.5   # AGDC bypass

    # VDD_TX/DR bypass (below IC, left of matching network)
    C20_X, C20_Y = 117.5, 124.0

    # LDO (upper right) and connector (upper left)
    U3_X,  U3_Y  = 127.5, 109.0
    CN1_X, CN1_Y = 107.5, 106.0

    # VDD power bus x position (between crystal area and IC left pads)
    VDD_BUS_X = 116.5
    # SPI horizontal bus y (above decoupling caps, well clear of VDD bus)
    SPI_BUS_Y = 112.0

    # Place footprints
    lines.append(fp_crystal_3225("X1",  "27.12MHz", X1_X,  X1_Y))
    lines.append(fp_0603("C31", "10pF",  C31_X, C31_Y, 0))
    lines.append(fp_0603("C32", "10pF",  C32_X, C32_Y, 0))
    lines.append(fp_0603("C15", "100nF", C15_X, C15_Y, 0))
    lines.append(fp_0603("C16", "100nF", C16_X, C16_Y, 0))
    lines.append(fp_0603("C17", "2.2uF", C17_X, C17_Y, 0))
    lines.append(fp_0603("C18", "2.2uF", C18_X, C18_Y, 0))
    lines.append(fp_0603("L1",  "27nH",  L1_X,  L1_Y,  90))
    lines.append(fp_0603("L2",  "27nH",  L2_X,  L2_Y,  90))
    lines.append(fp_0603("C1",  "220pF", C1_X,  C1_Y,  0))
    lines.append(fp_0603("C2",  "220pF", C2_X,  C2_Y,  0))
    lines.append(fp_0603("C3",  "120pF", C3_X,  C3_Y,  90))
    lines.append(fp_0603("C4",  "120pF", C4_X,  C4_Y,  90))
    lines.append(fp_0603("R1",  "2.2R",  R1_X,  R1_Y,  90))
    lines.append(fp_0603("R2",  "2.2R",  R2_X,  R2_Y,  90))
    lines.append(fp_0603("C5",  "180pF", C5_X,  C5_Y,  0))
    lines.append(fp_0603("C6",  "180pF", C6_X,  C6_Y,  0))
    lines.append(fp_0603("C19", "10nF",  C19_X, C19_Y, 0))
    lines.append(fp_0603("C20", "10nF",  C20_X, C20_Y, 0))
    lines.append(fp_sot23_5("U3",  "3V3_LDO", U3_X,  U3_Y))
    lines.append(fp_header_7pin("CN1", CN1_X, CN1_Y))

    # ── Traces ────────────────────────────────────────────────────────────────

    # --- Crystal circuit ---
    # XTO (pin4) at (117.5625, 119.75); XTI (pin5) at (117.5625, 120.25)
    xto = ic_pin(P['XTO'])   # (117.5625, 119.75)
    xti = ic_pin(P['XTI'])   # (117.5625, 120.25)
    # C31 at (114.5, 119.0): pad1=(113.675,119.0)  pad2=(115.325,119.0)
    # C32 at (114.5, 121.0): pad1=(113.675,121.0)  pad2=(115.325,121.0)
    # X1 at (110.5, 120.0):  pad3=(111.6,119.15)   pad4=(109.4,119.15)
    lines += [
        # XTO -> C31 pad2 -> X1 pad3
        seg(xto[0], xto[1], 115.325, 119.0),
        seg(113.675, 119.0, 113.675, 119.15),
        seg(113.675, 119.15, 111.6,  119.15),
        # XTI -> C32 pad2 -> X1 pad4
        seg(xti[0], xti[1], 115.325, 120.25),
        seg(115.325, 120.25, 115.325, 121.0),
        seg(113.675, 121.0, 109.4, 121.0),
        seg(109.4,   121.0, 109.4, 119.15),
    ]

    # --- RF matching: RFO1/RFO2 -> L1/L2 -> NODE_A/NODE_B -> C3/C4 -> R1/R2 -> ANT ---
    rfo1 = ic_pin(P['RFO1'])   # (120.25, 122.4375)
    rfo2 = ic_pin(P['RFO2'])   # (121.25, 122.4375)
    # L1 rot=90: pad1=(119.5,125.175)  pad2=(119.5,126.825)  NODE_A=pad2
    # L2 rot=90: pad1=(121.5,125.175)  pad2=(121.5,126.825)  NODE_B=pad2
    # Route RFO1 -> L1pad1 (go down then left)
    lines += [
        seg(rfo1[0], rfo1[1], rfo1[0], 123.5),
        seg(rfo1[0], 123.5, L1_X, 123.5),
        seg(L1_X, 123.5, L1_X, L1_Y-0.825),        # L1 pad1
        # L1pad2(NODE_A) -> C1pad2 (shunt) and -> C3pad1 (series)
        seg(L1_X, L1_Y+0.825, L1_X, C3_Y-0.825),   # straight to C3pad1
        seg(L1_X, L1_Y+0.825, C1_X+0.825, C1_Y),   # branch to C1pad2
    ]
    lines += [
        seg(rfo2[0], rfo2[1], rfo2[0], 124.0),
        seg(rfo2[0], 124.0, L2_X, 124.0),
        seg(L2_X, 124.0, L2_X, L2_Y-0.825),
        seg(L2_X, L2_Y+0.825, L2_X, C4_Y-0.825),
        seg(L2_X, L2_Y+0.825, C2_X-0.825, C2_Y),   # branch to C2pad1
    ]
    # C3pad2 -> R1pad1  (both rot=90, same x)
    lines += [
        seg(C3_X, C3_Y+0.825, R1_X, R1_Y-0.825),
        seg(C4_X, C4_Y+0.825, R2_X, R2_Y-0.825),
    ]
    # ANT1 = R1pad2 = (119.5, 132.825); ANT2 = R2pad2 = (121.5, 132.825)

    # --- 2-turn antenna routing ---
    ANT1_X, ANT1_Y = R1_X, R1_Y + 0.825   # (119.5, 132.825)
    ANT2_X, ANT2_Y = R2_X, R2_Y + 0.825   # (121.5, 132.825)
    INNER = 5.0   # inner turn: 5mm from board edge (clears all components)
    OUTER = 2.0   # outer turn: 2mm from board edge
    ix_min = OX + INNER    # 105.0
    ix_max = OX+BW - INNER # 135.0
    iy_min = OY + INNER    # 105.0
    iy_max = OY+BH - INNER # 135.0
    ox_min = OX + OUTER    # 102.0
    ox_max = OX+BW - OUTER # 138.0
    oy_min = OY + OUTER    # 102.0
    oy_max = OY+BH - OUTER # 138.0

    # Inner turn: ANT1 -> down -> left -> up-CCW around board -> back to crossover via
    lines += [
        seg(ANT1_X, ANT1_Y, ANT1_X, iy_max, w=0.5),  # ANT1 down to inner-bottom
        seg(ANT1_X, iy_max, ix_min, iy_max, w=0.5),   # inner-bottom left
        seg(ix_min, iy_max, ix_min, iy_min, w=0.5),   # inner-left up
        seg(ix_min, iy_min, ix_max, iy_min, w=0.5),   # inner-top
        seg(ix_max, iy_min, ix_max, iy_max, w=0.5),   # inner-right
        seg(ix_max, iy_max, ix_min+1.5, iy_max, w=0.5), # inner-bottom partial (to near crossover)
    ]
    # Via: inner turn -> B.Cu crossover
    V_CROSS_X = ix_min + 0.5   # 105.5
    V_CROSS_Y = iy_max          # 135.0
    lines.append(via(V_CROSS_X, V_CROSS_Y, size=1.0, drill=0.5))
    # B.Cu: jump from inner left to outer left
    lines.append(seg(V_CROSS_X, V_CROSS_Y, ox_min+0.5, V_CROSS_Y, layer="B.Cu", w=0.5))
    lines.append(via(ox_min+0.5, V_CROSS_Y, size=1.0, drill=0.5))

    # Outer turn: (102.5, 135) -> up left -> top -> right -> bottom -> partial left -> ANT2 via
    lines += [
        seg(ox_min+0.5, V_CROSS_Y, ox_min, V_CROSS_Y, w=0.5),
        seg(ox_min, V_CROSS_Y, ox_min, oy_min, w=0.5),   # outer-left up
        seg(ox_min, oy_min, ox_max, oy_min, w=0.5),       # outer-top
        seg(ox_max, oy_min, ox_max, oy_max, w=0.5),       # outer-right
        seg(ox_max, oy_max, ox_min, oy_max, w=0.5),       # outer-bottom
        seg(ox_min, oy_max, ox_min, ANT2_Y+1.0, w=0.5),  # outer-left partial (down to ANT2 via level)
    ]
    # Via: outer turn end -> B.Cu -> ANT2
    lines.append(via(ox_min, ANT2_Y+1.0, size=1.0, drill=0.5))
    lines.append(seg(ox_min, ANT2_Y+1.0, ANT2_X, ANT2_Y+1.0, layer="B.Cu", w=0.5))
    lines.append(seg(ANT2_X, ANT2_Y+1.0, ANT2_X, ANT2_Y, layer="B.Cu", w=0.5))
    lines.append(via(ANT2_X, ANT2_Y, size=1.0, drill=0.5))

    # --- RFI1/RFI2 routing: NODE_A/B -> via -> B.Cu around IC -> via -> RFI pins ---
    rfi1 = ic_pin(P['RFI1'])   # (122.4375, 119.25)
    rfi2 = ic_pin(P['RFI2'])   # (122.4375, 118.75)
    # RFI1: via at NODE_A, B.Cu route left->up->right, via near RFI1
    lines.append(via(L1_X, L1_Y+1.5))           # via near NODE_A at (119.5, 127.5)
    lines += [
        seg(L1_X, L1_Y+0.825, L1_X, L1_Y+1.5), # NODE_A -> via
        seg(L1_X, L1_Y+1.5, 115.0, L1_Y+1.5, layer="B.Cu"),
        seg(115.0, L1_Y+1.5, 115.0, 114.0,     layer="B.Cu"),
        seg(115.0, 114.0, 123.5, 114.0,         layer="B.Cu"),
        seg(123.5, 114.0, 123.5, rfi1[1],       layer="B.Cu"),
    ]
    lines.append(via(123.5, rfi1[1]))
    lines.append(seg(123.5, rfi1[1], rfi1[0], rfi1[1]))
    # C5 connects from RFI1 side: C5pad1 at (124.675, 119.5), connect via RFI1 junction
    lines.append(seg(124.675, C5_Y, 123.5, rfi1[1]))

    # RFI2: via at NODE_B, similar B.Cu route offset by 0.5mm
    lines.append(via(L2_X, L2_Y+1.5))
    lines += [
        seg(L2_X, L2_Y+0.825, L2_X, L2_Y+1.5),
        seg(L2_X, L2_Y+1.5, 114.5, L2_Y+1.5, layer="B.Cu"),
        seg(114.5, L2_Y+1.5, 114.5, 113.5,    layer="B.Cu"),
        seg(114.5, 113.5, 124.0, 113.5,        layer="B.Cu"),
        seg(124.0, 113.5, 124.0, rfi2[1],      layer="B.Cu"),
    ]
    lines.append(via(124.0, rfi2[1]))
    lines.append(seg(124.0, rfi2[1], rfi2[0], rfi2[1]))
    lines.append(seg(124.675, C6_Y, 124.0, rfi2[1]))

    # --- AGDC (pin24) -> C19pad1 ---
    agdc = ic_pin(P['AGDC'])   # (122.4375, 118.25)
    lines += [
        seg(agdc[0], agdc[1], C19_X-0.825, agdc[1]),
        seg(C19_X-0.825, agdc[1], C19_X-0.825, C19_Y),
    ]

    # --- I2C_EN (pin20) pulled to GND via via ---
    i2c_en = ic_pin(P['I2C_EN'])   # (122.4375, 120.25)
    lines.append(seg(i2c_en[0], i2c_en[1], i2c_en[0]+1.5, i2c_en[1]))
    lines.append(via(i2c_en[0]+1.5, i2c_en[1]))

    # --- SPI connector routing ---
    # CN1 at (107.5, 106.0), pins at y=106, 108.54, 111.08, 113.62, 116.16, 118.70, 121.24
    # pin1=GND, pin2=VIN, pin3=MISO, pin4=MOSI, pin5=SCLK, pin6=CS, pin7=IRQ
    cn1_pins_y = [CN1_Y + i*2.54 for i in range(7)]

    miso = ic_pin(P['MISO'])   # (118.25, 117.5625)
    mosi = ic_pin(P['MOSI'])   # (118.75, 117.5625)
    sclk = ic_pin(P['SCLK'])   # (119.25, 117.5625)
    bss  = ic_pin(P['BSS'])    # (119.75, 117.5625)  = CS
    irq  = ic_pin(P['IRQ'])    # (120.75, 117.5625)

    # Each SPI signal: go UP to individual y-level, then left to CN1_X, then to pin
    spi_signals = [
        (miso, cn1_pins_y[2], SPI_BUS_Y - 0.0),  # MISO -> pin3
        (mosi, cn1_pins_y[3], SPI_BUS_Y - 0.4),  # MOSI -> pin4
        (sclk, cn1_pins_y[4], SPI_BUS_Y - 0.8),  # SCLK -> pin5
        (bss,  cn1_pins_y[5], SPI_BUS_Y - 1.2),  # CS   -> pin6
        (irq,  cn1_pins_y[6], SPI_BUS_Y - 1.6),  # IRQ  -> pin7
    ]
    for sig, py, bus_y in spi_signals:
        lines += [
            seg(sig[0], sig[1], sig[0], bus_y),       # IC pin up to bus_y
            seg(sig[0], bus_y, CN1_X, bus_y),          # left to CN1_X
            seg(CN1_X, bus_y, CN1_X, py),              # down/up to connector pin
        ]

    # --- VDD power rail ---
    # LDO U3 at (127.5, 109.0):
    #   pad1 (IN) = (126.3625, 108.05)  pad2 (GND) = (126.3625, 109.0)
    #   pad3 (EN) = (126.3625, 109.95)  pad4 (FB)  = (128.6375, 109.95)
    #   pad5 (OUT)= (128.6375, 108.05)
    u3_in  = (U3_X - 1.1375, U3_Y - 0.95)   # pad1
    u3_gnd = (U3_X - 1.1375, U3_Y)           # pad2
    u3_en  = (U3_X - 1.1375, U3_Y + 0.95)   # pad3
    u3_fb  = (U3_X + 1.1375, U3_Y + 0.95)   # pad4
    u3_out = (U3_X + 1.1375, U3_Y - 0.95)   # pad5

    # EN tied to IN (always on), FB tied to OUT (fixed voltage)
    lines += [
        seg(u3_en[0], u3_en[1], u3_in[0], u3_in[1]),
        seg(u3_fb[0], u3_fb[1], u3_out[0], u3_out[1]),
    ]

    # VDD bus: LDO out -> up -> left -> down VDD_BUS_X to IC pins
    # Route at y=107.5 (above SPI_BUS_Y=112, below LDO)
    VDD_ENTRY_Y = 107.5
    lines += [
        seg(u3_out[0], u3_out[1], u3_out[0], VDD_ENTRY_Y),  # LDO out up
        seg(u3_out[0], VDD_ENTRY_Y, VDD_BUS_X, VDD_ENTRY_Y), # left to VDD bus
        seg(VDD_BUS_X, VDD_ENTRY_Y, VDD_BUS_X, 122.0, w=0.3), # VDD bus down
    ]
    # VDD bus -> IC supply pins (short horizontal stubs)
    for pn in [P['VDD_IO'], P['VDD_D'], P['VDD_A'], P['VDD']]:
        pp = ic_pin(pn)
        lines.append(seg(VDD_BUS_X, pp[1], pp[0], pp[1], w=0.3))

    # VDD bus -> bypass caps pad2
    for cx, cy in [(C15_X, C15_Y), (C16_X, C16_Y), (C17_X, C17_Y), (C18_X, C18_Y)]:
        # pad2 is at cx+0.825 (rot=0)
        lines.append(seg(cx+0.825, cy, VDD_BUS_X, cy, w=0.2))

    # VDD_TX (pin10), VDD_DR (pin14) bypass via C20
    vdd_tx = ic_pin(P['VDD_TX'])   # (118.75, 122.4375)
    lines += [
        seg(vdd_tx[0], vdd_tx[1], C20_X+0.825, vdd_tx[1]),
        seg(C20_X+0.825, vdd_tx[1], C20_X+0.825, C20_Y),
    ]

    # LDO VIN (pad1) <- CN1 pin2
    lines += [
        seg(CN1_X, cn1_pins_y[1], CN1_X, VDD_ENTRY_Y-0.3),
        seg(CN1_X, VDD_ENTRY_Y-0.3, u3_in[0], VDD_ENTRY_Y-0.3),
        seg(u3_in[0], VDD_ENTRY_Y-0.3, u3_in[0], u3_in[1]),
    ]

    # --- GND connections (vias to B.Cu pour) ---
    gnd_pins = [P['GND_D'], P['GND_DR1'], P['GND_DR2'], P['VSS'], P['GND_A']]
    for pn in gnd_pins:
        pp = ic_pin(pn)
        lines.append(via(pp[0], pp[1]))

    # Thermal pad vias (3x3 grid under IC)
    for dx in [-1.0, 0.0, 1.0]:
        for dy in [-1.0, 0.0, 1.0]:
            lines.append(via(IC_X+dx, IC_Y+dy, size=0.6, drill=0.3))

    # GND vias for bypass cap pad1s
    gnd_pads = [
        (C31_X-0.825, C31_Y), (C32_X-0.825, C32_Y),
        (C1_X-0.825,  C1_Y),  (C2_X+0.825,  C2_Y),
        (C5_X+0.825,  C5_Y),  (C6_X+0.825,  C6_Y),
        (C19_X+0.825, C19_Y), (C20_X-0.825, C20_Y),
        (C15_X-0.825, C15_Y), (C16_X-0.825, C16_Y),
        (C17_X-0.825, C17_Y), (C18_X-0.825, C18_Y),
    ]
    for gx, gy in gnd_pads:
        lines.append(via(gx, gy))

    # X1 GND pads (pad1, pad2) -> GND via
    lines.append(via(X1_X-1.1, X1_Y+0.85))
    lines.append(via(X1_X+1.1, X1_Y+0.85))

    # LDO GND (pad2) -> via
    lines.append(via(u3_gnd[0], u3_gnd[1]))

    # CN1 pin1 (GND) -> via
    lines.append(via(CN1_X, cn1_pins_y[0]))

    # --- Silkscreen ---
    lines.append(
        f'  (gr_text "ST25R3916B NFC Reader" (at {CX} {OY+1.5} 0) (layer "F.SilkS") '
        f'(effects (font (size 1.0 1.0)(thickness 0.15))))'
    )
    lines.append(
        f'  (gr_text "GND VIN MISO MOSI SCLK CS IRQ" (at {CX} {OY+BH-1.5} 0) (layer "F.SilkS") '
        f'(effects (font (size 0.6 0.6)(thickness 0.10))))'
    )

    # B.Cu GND pour
    lines.append(zone_gnd())

    lines.append(')')
    return '\n'.join(lines)


# ── BOM ───────────────────────────────────────────────────────────────────────
def write_bom(path):
    rows = [
        ["Comment", "Designator", "Footprint", "LCSC Part #"],
        ["ST25R3916B",  "U1",          "QFN-32-1EP_5x5mm_P0.5mm_EP3.45", "C17315217"],
        ["27nH",        "L1,L2",       "0603",                            "C148128"],
        ["220pF",       "C1,C2",       "0603",                            "C1600"],
        ["120pF",       "C3,C4",       "0603",                            "C1591"],
        ["180pF",       "C5,C6",       "0603",                            "C1598"],
        ["2.2R",        "R1,R2",       "0603",                            "C22939"],
        ["27.12MHz",    "X1",          "Crystal_SMD_3225-4Pin_3.2x2.5mm","C112441"],
        ["10pF",        "C31,C32",     "0603",                            "C1634"],
        ["3V3 LDO",     "U3",          "SOT-23-5",                        "C7955"],
        ["100nF",       "C15,C16",     "0603",                            "C14663"],
        ["2.2uF",       "C17,C18",     "0603",                            "C1602"],
        ["10nF",        "C19,C20",     "0603",                            "C1514"],
    ]
    with open(path, 'w', newline='') as f:
        csv.writer(f).writerows(rows)
    print(f"  Wrote {path}")


# ── CPL ───────────────────────────────────────────────────────────────────────
def write_cpl(path):
    def jy(ky): return round(BH - (ky - OY), 4)
    def jx(kx): return round(kx - OX, 4)

    comps = [
        ("U1",  120.0,  120.0,  "Top", 0),
        ("X1",  110.5,  120.0,  "Top", 0),
        ("C31", 114.5,  119.0,  "Top", 0),
        ("C32", 114.5,  121.0,  "Top", 0),
        ("C15", 111.5,  115.0,  "Top", 0),
        ("C16", 111.5,  124.0,  "Top", 0),
        ("C17", 108.5,  115.0,  "Top", 0),
        ("C18", 108.5,  124.0,  "Top", 0),
        ("L1",  119.5,  126.0,  "Top", 90),
        ("L2",  121.5,  126.0,  "Top", 90),
        ("C1",  116.5,  126.0,  "Top", 0),
        ("C2",  124.5,  126.0,  "Top", 0),
        ("C3",  119.5,  129.0,  "Top", 90),
        ("C4",  121.5,  129.0,  "Top", 90),
        ("R1",  119.5,  132.0,  "Top", 90),
        ("R2",  121.5,  132.0,  "Top", 90),
        ("C5",  125.5,  119.5,  "Top", 0),
        ("C6",  125.5,  118.0,  "Top", 0),
        ("C19", 125.5,  116.5,  "Top", 0),
        ("C20", 117.5,  124.0,  "Top", 0),
        ("U3",  127.5,  109.0,  "Top", 0),
        # CN1 through-hole: included for reference, JLCPCB won't assemble
        ("CN1", 107.5,  106.0,  "Top", 0),
    ]
    rows = [["Designator", "Mid X", "Mid Y", "Layer", "Rotation"]]
    for ref, kx, ky, layer, rot in comps:
        rows.append([ref, jx(kx), jy(ky), layer, rot])
    with open(path, 'w', newline='') as f:
        csv.writer(f).writerows(rows)
    print(f"  Wrote {path}")


# ── Entry point ───────────────────────────────────────────────────────────────
if __name__ == "__main__":
    base = os.path.dirname(os.path.abspath(__file__))
    pcb_path = os.path.join(base, "ST25R3916B_Custom.kicad_pcb")
    bom_path = os.path.join(base, "jlcpcb_bom.csv")
    cpl_path = os.path.join(base, "jlcpcb_cpl.csv")

    print("Generating PCB...")
    with open(pcb_path, 'w') as f:
        f.write(generate())
    print(f"  Wrote {pcb_path}")
    write_bom(bom_path)
    write_cpl(cpl_path)
    print("Done.")
