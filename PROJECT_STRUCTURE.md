# ST25R ESPHome Component — Project Structure

## Repository Layout

```
esphome_st25r/
│
├── components/                        ESPHome component source
│   ├── st25r/                         Abstract base component
│   │   ├── __init__.py                ESPHome config schema (YAML options)
│   │   ├── binary_sensor.py           Binary sensor schema
│   │   ├── st25r.h                    C++ class definition, enums, state machine
│   │   ├── st25r.cpp                  Protocol logic, anticollision, Mifare auth
│   │   ├── crypto1.h                  Crypto1 stream cipher header
│   │   └── crypto1.cpp                Crypto1 LFSR + PRNG implementation
│   ├── st25r_spi/                     SPI transport (inherits ST25R + spi::SPIDevice)
│   │   ├── __init__.py
│   │   ├── st25r_spi.h
│   │   └── st25r_spi.cpp
│   └── st25r_i2c/                     I2C transport (inherits ST25R + i2c::I2CDevice)
│       ├── __init__.py
│       ├── st25r_i2c.h
│       └── st25r_i2c.cpp
│
├── tests/                             All YAML firmware configs
│   ├── ci-test-spi.yaml               CI compile check — SPI (Arduino, esp32dev)
│   ├── ci-test-i2c.yaml               CI compile check — I2C (Arduino, esp32dev)
│   ├── local-test.yaml                Local dev — ESP32-C6 + Elechouse SPI module
│   ├── test-vicino-i2c-c6.yaml        Local dev — ESP32-C6 I2C (reusing SPI pins)
│   └── test-*.yaml                    Other local hardware test configs
│
├── examples/                          User-facing example configs
│   ├── example-basic.yaml             Basic tag detection
│   └── example-access-control.yaml    Access control with binary sensors
│
├── memory/                            Hard-won technical notes (read before changing protocol code)
│   ├── multitag_anticol.md            Multi-tag anticollision algorithm, all bugs and fixes
│   └── datasheet_notes.md             Register map, SPI protocol, IRQ bit definitions
│
├── docs/
│   ├── fetch_datasheets.sh            Script: downloads all ST25R datasheets from st.com
│   ├── st25r3916.pdf                  Datasheet DS12484 Rev 8, 1.6 W (primary reference)
│   ├── st25r3911b.pdf                 Datasheet, 1.6 W          (fetch_datasheets.sh)
│   ├── st25r3916b.pdf                 Datasheet, 1.6 W enhanced (fetch_datasheets.sh)
│   ├── st25r3917b.pdf                 Datasheet, 1.6 W reduced  (fetch_datasheets.sh)
│   ├── st25r3919.pdf                  Datasheet, 1.6 W auto     (fetch_datasheets.sh)
│   ├── st25r3918.pdf                  Datasheet, 2.2 W          (fetch_datasheets.sh)
│   ├── st25r3918b.pdf                 Datasheet, 2.2 W enhanced (fetch_datasheets.sh)
│   └── st25r3920.pdf                  Datasheet, 2.2 W auto     (fetch_datasheets.sh)
│
├── .github/workflows/
│   └── compile.yml                    CI: compiles tests/ci-test-spi.yaml + ci-test-i2c.yaml
│
├── AGENTS.md                          Contributor & AI agent guide (read this first)
├── README.md                          User-facing documentation
├── DEVELOPMENT_STATE.md               Current implementation state, known bugs
├── CHANGELOG.md                       Version history
├── TODO.md                            Feature backlog
├── CONTRIBUTING.md                    Contribution guidelines
├── HARDWARE_SETUP.md                  Wiring and hardware guide
├── TESTING.md                         Hardware validation procedures
└── secrets.yaml                       Local secrets template (not committed with real values)
```

## Key Design Decisions

### Three-layer transport abstraction
Pure virtual transport methods (`read_register`, `write_register`, `write_command`, `write_fifo`, `read_fifo`) are defined in the `ST25R` base class and implemented by `ST25RSpi` / `ST25RI2C`. Protocol logic lives entirely in the base class.

### Non-blocking state machine
`loop()` runs the scan state machine without `delay()`. The ISR sets `irq_triggered_` flag; `loop()` reads IRQ registers and advances state. Fallback polling handles missed edges.

### Crypto1 implementation
`crypto1.cpp` implements the public Crypto1 algorithm (Courtois/Nohl 2008). Used by `mifare_authenticate_()` and `mifare_read_block_()`. Critical invariant: parity bits advance the LFSR — use `crypto1_bit(cs, 0, 0)`, not `crypto1_filter(cs->odd)`.

## Before Modifying Protocol Code

Read `memory/multitag_anticol.md` and `memory/datasheet_notes.md`. These document ST25R3916-specific quirks not visible from reading the source.
