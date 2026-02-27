# ST25R3916 Hardware Setup Guide

This guide provides detailed instructions for setting up the ST25R3916 NFC reader with ESP32.

## Table of Contents

- [Hardware Requirements](#hardware-requirements)
- [Antenna Design](#antenna-design)
- [Wiring Diagram](#wiring-diagram)
- [PCB Layout Considerations](#pcb-layout-considerations)
- [Power Supply](#power-supply)
- [Testing Setup](#testing-setup)
- [Troubleshooting](#troubleshooting)

## Hardware Requirements

### Required Components

1. **ST25R3916 or ST25R3916B IC**
   - QFN32 package (5mm x 5mm)
   - Operating voltage: 2.4V - 5.5V
   - Available from major electronics distributors

2. **ESP32 Development Board**
   - Any ESP32 variant with SPI support
   - Recommended: ESP32-DevKitC, ESP32-WROOM-32
   - Minimum 4MB flash recommended

3. **Antenna**
   - Option 1: Pre-tuned NFC antenna module (easiest)
   - Option 2: Custom antenna coil with matching circuit
   - Typical: 13.56 MHz, 1-2 µH inductance

4. **Passive Components**
   - Decoupling capacitors: 100nF, 10µF, 1µF
   - Antenna matching: Various values (see antenna section)
   - Pull-up resistors: 10kΩ (optional for some signals)

5. **Optional Components**
   - Reset button (if using hardware reset)
   - Status LEDs
   - Crystal oscillator (if not using internal)

### Tools Required

- Soldering iron and solder
- Multimeter
- Oscilloscope (for debugging)
- Logic analyzer (optional but helpful)
- NFC test cards (ISO14443A)

## Antenna Design

The antenna is the most critical part of the NFC reader system.

### Pre-Made Antenna Modules (Recommended for Beginners)

**Advantages:**
- Pre-tuned and tested
- Plug-and-play solution
- Known characteristics

**Recommended Modules:**
- ST25R3916 evaluation board antenna
- M24LR-DISCOVERY antenna
- Similar 13.56 MHz NFC antennas

### Custom Antenna Design

**Antenna Specifications:**
- Frequency: 13.56 MHz
- Inductance: 1-2 µH typical
- Quality factor (Q): 20-30
- Reading distance: 2-5 cm typical

**Coil Design:**
- Number of turns: 3-5 turns
- Wire: 0.3-0.5 mm diameter
- Size: 40mm x 60mm typical for card reading
- Rectangular or circular shape

### Antenna Matching Circuit

The matching circuit must be tuned for optimal performance.

```
ST25R3916         Matching Network              Antenna
---------         ----------------              --------
                  
RFO1 ----┬---L1---┬---C1---┬----- Antenna Coil
         |        |        |
         C2      GND      C3
         |                |
        GND              GND

RFO2 ----+--- Series Capacitor --- Antenna Coil
```

**Typical Component Values:**

For a 1.5 µH antenna:
- C1: 47pF (series tuning)
- C2: 100pF (parallel tuning)
- C3: 100pF (parallel tuning)
- L1: 220nH (optional matching)

**Tuning Process:**

1. Start with calculated values
2. Use network analyzer at 13.56 MHz
3. Adjust C1 for resonance
4. Adjust C2/C3 for impedance matching
5. Target: 50Ω impedance, resonance at 13.56 MHz

### Antenna Connection

```
ST25R3916 Pin     Connection
-------------     ----------
RFO1              Antenna terminal 1 (via matching)
RFO2              Antenna terminal 2 (via matching)
```

## Wiring Diagram

### Minimal Connection (Basic Operation)

```
ST25R3916        ESP32
---------        -----
VDD         ---> 3.3V
GND         ---> GND
MOSI        ---> GPIO23
MISO        ---> GPIO19
SCK         ---> GPIO18
CS          ---> GPIO5
IRQ         ---> GPIO21
```

### Full Connection (Recommended)

```
ST25R3916        ESP32        Notes
---------        -----        -----
VDD         ---> 3.3V         Power supply
GND         ---> GND          Ground
MOSI        ---> GPIO23       SPI Data In
MISO        ---> GPIO19       SPI Data Out
SCK         ---> GPIO18       SPI Clock
CS          ---> GPIO5        Chip Select (configurable)
IRQ         ---> GPIO21       Interrupt (configurable)
RESET       ---> GPIO22       Hardware Reset (optional)
```

### Decoupling Capacitors

Add capacitors close to the ST25R3916:

```
VDD to GND:
  - 100nF ceramic (very close to IC, <5mm)
  - 10µF ceramic or tantalum (within 10mm)
  - 1µF ceramic (optional, additional decoupling)
```

### Power Supply Filtering

```
3.3V Supply ---[Ferrite Bead]---+--- VDD (ST25R3916)
                                |
                               ===  100nF
                                |
                               GND
```

## PCB Layout Considerations

### Critical Layout Rules

1. **Ground Plane**
   - Solid ground plane under entire circuit
   - No breaks in ground under antenna area
   - Separate analog and digital grounds if possible

2. **Antenna Area**
   - Keep antenna area clear of other components
   - No copper pour under antenna
   - Guard ring around antenna (grounded)
   - Minimum 5mm clearance from other circuits

3. **SPI Traces**
   - Keep SPI traces short (<10cm)
   - Equal length for SCK, MOSI, MISO
   - Route away from antenna
   - Use ground pour around SPI traces

4. **Decoupling Capacitors**
   - Place 100nF as close as possible to VDD pin (<5mm)
   - Multiple vias to ground plane
   - Short, wide traces

5. **IRQ and RESET Lines**
   - Add series resistor (22-47Ω) close to ST25R3916
   - Route away from noisy signals
   - Add ESD protection if exposed to external connections

### Layer Stack (2-Layer PCB)

```
Top Layer:
- ST25R3916 IC
- Decoupling capacitors
- Antenna matching components
- SPI connection traces

Bottom Layer:
- Solid ground plane
- Power traces
- Antenna coil (if PCB antenna)
```

### Layer Stack (4-Layer PCB - Recommended)

```
Layer 1 (Top):      Signal traces, components
Layer 2:            Ground plane
Layer 3:            Power plane (3.3V)
Layer 4 (Bottom):   Signal traces, antenna
```

## Power Supply

### Requirements

- Voltage: 2.4V - 5.5V (3.3V recommended)
- Current: 
  - Peak: 300mA (during field on)
  - Average: 50-100mA (typical operation)
  - Idle: 5-10mA (field off)

### ESP32 3.3V Output

**Suitable if:**
- ESP32 regulator can provide 300mA peak
- Total system current < regulator limit
- Clean, well-filtered supply

**Power Connection:**

```
ESP32 3.3V Out ---+--- ST25R3916 VDD
                  |
                 ===  10µF (close to ESP32)
                  |
                 GND
```

### External Regulator (Recommended for Production)

Use dedicated 3.3V LDO regulator:

**Example Circuit:**

```
USB 5V ---[LDO 3.3V]---+---[10µF]--- ESP32 VDD
          (AMS1117)    |
                       +---[10µF]--- ST25R3916 VDD
                       |
                      GND
```

### Power Considerations

1. **Inrush Current**
   - Add 10µF capacitor at ST25R3916
   - Prevents voltage drop during field activation

2. **Ripple Rejection**
   - Use LDO with good PSRR (>60dB)
   - Add LC filter if using switching regulator

3. **Shared Power**
   - If powering multiple devices, ensure adequate current
   - Consider separate regulators for high-current loads

## Testing Setup

### Initial Hardware Test

1. **Power Up Test**
   ```
   - Connect power
   - Check VDD voltage: 3.3V ±0.1V
   - Measure current: Should be <50mA idle
   ```

2. **SPI Communication Test**
   ```yaml
   # Minimal test config
   logger:
     level: DEBUG
   
   spi:
     clk_pin: GPIO18
     mosi_pin: GPIO19
     miso_pin: GPIO23
   
   st25r3916:
     cs_pin: GPIO5
     irq_pin: GPIO21
   ```
   
   - Flash ESP32
   - Check logs for IC identity read
   - Should see: "IC Identity: 0x05" or "0x0A"

3. **Field Generation Test**
   - Place oscilloscope probe near antenna
   - Should see 13.56 MHz carrier when polling
   - Amplitude should be consistent

4. **Tag Reading Test**
   - Use known-good ISO14443A card
   - Place card near antenna (2-3 cm)
   - Check logs for UID detection

### Measurement Points

```
Test Point          Expected Value          Measurement
----------          --------------          -----------
VDD                 3.3V ± 0.1V            DC voltmeter
Field amplitude     50-100mV RMS           Oscilloscope (near antenna)
Field frequency     13.56 MHz ± 10kHz      Frequency counter
SPI clock          1 MHz                   Logic analyzer
IRQ idle state     HIGH (3.3V)            Voltmeter
```

### Using Oscilloscope

1. **Field Waveform**
   - Probe: 10:1 passive probe
   - Position: 1cm from antenna
   - Scale: 100mV/div, 100ns/div
   - Look for: Clean 13.56 MHz sine wave

2. **SPI Signals**
   - Probe: Active probe or 10:1 passive
   - Channels: SCK, MOSI, MISO, CS
   - Scale: 1V/div, 500ns/div
   - Look for: Clean square waves, proper timing

### Using Logic Analyzer

```
Channel     Signal      Expected
-------     ------      --------
CH0         SCK         1 MHz clock
CH1         MOSI        Data out to ST25R3916
CH2         MISO        Data in from ST25R3916
CH3         CS          Active low chip select
CH4         IRQ         Pulses when events occur
```

## Troubleshooting

### No Communication with ST25R3916

**Symptoms:**
- Logs show: "Chip not responding"
- IC Identity reads as 0xFF or 0x00

**Checks:**
1. Verify power supply (3.3V at VDD pin)
2. Check SPI connections (MOSI, MISO, SCK, CS)
3. Verify SPI mode in code matches hardware
4. Check for solder bridges
5. Try hardware reset

**Solution:**
```yaml
# Try with explicit SPI configuration
spi:
  id: spi_bus
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19
  interface: hardware

st25r3916:
  spi_id: spi_bus
  cs_pin: GPIO5
  reset_pin: GPIO22  # Add hardware reset
```

### Field Not Generated

**Symptoms:**
- No oscilloscope signal near antenna
- Tags never detected

**Checks:**
1. Verify antenna connection
2. Check antenna matching circuit
3. Measure field with spectrum analyzer
4. Check regulator output voltage

**Common Issues:**
- Antenna coil open/short
- Wrong matching capacitor values
- Insufficient power supply current
- Damaged ST25R3916

### Tags Not Detected

**Symptoms:**
- Field present but no tag detection
- Intermittent detection

**Checks:**
1. Verify antenna tuning (13.56 MHz)
2. Check IRQ pin connection
3. Test with different tag types
4. Adjust read distance
5. Check for RF interference

**Solution:**
```yaml
# Try longer polling interval
st25r3916:
  update_interval: 2s  # Slower polling
  cs_pin: GPIO5
  irq_pin: GPIO21
```

### IRQ Not Working

**Symptoms:**
- Logs show: "IRQ timeout"
- Slow or no detection

**Checks:**
1. Verify IRQ pin connection
2. Check IRQ pin is configured as input
3. Verify internal pull-up is enabled
4. Check for shorts to ground

**Solution:**
```yaml
st25r3916:
  irq_pin:
    number: GPIO21
    mode:
      input: true
      pullup: true
```

### False Detections

**Symptoms:**
- Random tag detections without tag present
- Unstable readings

**Causes:**
- RF interference from other devices
- Poor antenna grounding
- Insufficient decoupling
- Antenna too sensitive

**Solutions:**
1. Add ferrite beads on power supply
2. Improve ground plane
3. Add more decoupling capacitors
4. Adjust field threshold in code
5. Shield antenna area

### Poor Read Range

**Symptoms:**
- Must place tag very close (<1cm)
- Inconsistent reading

**Causes:**
- Antenna not tuned properly
- Weak field strength
- Poor antenna design
- Component value mismatch

**Solutions:**
1. Re-tune antenna matching circuit
2. Check antenna coil inductance
3. Verify field amplitude with oscilloscope
4. Use higher quality capacitors (COG/NP0)

## Safety Considerations

1. **RF Exposure**
   - ST25R3916 operates at low power
   - No significant RF exposure risk
   - Still, avoid prolonged direct contact with active antenna

2. **ESD Protection**
   - ST25R3916 is ESD sensitive
   - Use proper ESD precautions during assembly
   - Consider adding TVS diodes on exposed connections

3. **Power Supply**
   - Ensure proper current rating
   - Add fuse protection for external power
   - Monitor for overheating

## Recommended Development Boards

### Ready-Made Solutions

1. **ST25R3916-DISCO**
   - Official evaluation board from ST
   - Pre-tuned antenna
   - SPI interface ready
   - Recommended for development

2. **Custom Breakout Boards**
   - Various third-party options available
   - Check antenna tuning specifications
   - Verify 3.3V logic compatibility

### Creating Custom Board

If designing custom PCB:
1. Use ST25R3916 evaluation board as reference
2. Follow layout guidelines above
3. Leave test points for debugging
4. Make antenna matching adjustable (use pads for different cap values)
5. Include mounting holes for shielding

## Additional Resources

- [ST25R3916 Datasheet](https://www.st.com/resource/en/datasheet/st25r3916.pdf)
- [ST25R3916 Application Notes](https://www.st.com/)
- ST25R3916 Evaluation Board Schematic
- [ISO14443 Standard Information](https://www.iso.org/)

## Support

For hardware setup questions:
- GitHub Discussions
- ESPHome Discord #custom-components channel

---

**Document Version:** 1.0  
**Last Updated:** 2024-02-26
