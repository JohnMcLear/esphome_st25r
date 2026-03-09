
def generate_pcb_svg(path):
    # Simplified SVG representation of the PCB
    svg = """<svg width="500" height="500" xmlns="http://www.w3.org/2000/svg">
  <rect x="50" y="50" width="400" height="400" fill="#006400" stroke="black" stroke-width="2" />
  <!-- Silkscreen -->
  <text x="70" y="80" font-family="Arial" font-size="20" fill="white">ST25R3916B Custom Reader</text>
  <text x="70" y="110" font-family="Arial" font-size="14" fill="white">Rev V1.0 - 2026-03-09</text>
  
  <!-- IC -->
  <rect x="225" y="225" width="50" height="50" fill="#333" />
  <text x="230" y="255" font-family="Arial" font-size="10" fill="white">U1</text>
  
  <!-- Antenna -->
  <rect x="100" y="100" width="300" height="300" fill="none" stroke="#FFD700" stroke-width="5" stroke-opacity="0.5" />
  <rect x="110" y="110" width="280" height="280" fill="none" stroke="#FFD700" stroke-width="5" stroke-opacity="0.5" />
  
  <!-- Matching components (simplified) -->
  <rect x="280" y="235" width="10" height="5" fill="#CCC" />
  <rect x="280" y="255" width="10" height="5" fill="#CCC" />
  <text x="300" y="240" font-family="Arial" font-size="8" fill="white">Matching</text>
  
  <!-- Connector -->
  <rect x="60" y="200" width="10" height="100" fill="#555" />
  <text x="75" y="255" font-family="Arial" font-size="10" fill="white" transform="rotate(-90 75,255)">Connector</text>
</svg>"""
    with open(path, 'w') as f:
        f.write(svg)

generate_pcb_svg("pcb/layout_preview.svg")
