# AGENTS.MD - ST25R ESPHome Component

This document provides a technical overview and guidance for AI agents (like Gemini, Claude, etc.) working on the `esphome_st25r` project.

## Project Overview

The `esphome_st25r` project is an ESPHome external component for the STMicroelectronics ST25R family of NFC reader ICs (specifically ST25R3916/B and variants). It implements a high-performance, non-blocking NFC-A (ISO14443A) stack.

### Supported Hardware
- **Chips:** ST25R3916, ST25R3916B, ST25R3917, ST25R3917B, ST25R3919, ST25R3920.
- **Transports:** SPI and I2C.
- **Primary Test Platform:** ESP32-C6 (ESP-IDF framework).

## Project Structure

The component follows a modular three-layer architecture:

- `components/st25r/`: Abstract base class (`ST25R`), shared logic, triggers, and sensors.
- `components/st25r_spi/`: Concrete SPI transport implementation.
- `components/st25r_i2c/`: Concrete I2C transport implementation.

### Key Files
- `components/st25r/st25r.h/cpp`: The core state machine and NFC-A logic.
- `components/st25r/st25r_registers.h`: Register definitions and command constants from the ST25R datasheet.
- `components/st25r_spi/st25r_spi.h/cpp`: SPI-specific register access and FIFO operations.
- `components/st25r_i2c/st25r_i2c.h/cpp`: I2C-specific register access and FIFO operations.
- `CLAUDE.md`: Contains build commands, architectural details, and known hardware specifics.
- `DEVELOPMENT_STATE.md`: Tracks current progress, known bugs, and next steps.

## Development Workflow

### Build and Validation
Always run compilation tests before suggesting or committing changes:
```bash
# SPI Test (Arduino framework)
esphome compile ci-test-spi.yaml

# I2C Test (Arduino framework)
esphome compile ci-test-i2c.yaml

# Local Hardware Test (ESP32-C6, ESP-IDF framework)
esphome compile local-test.yaml
```

### Linting
```bash
black --check components/
pylint components/
```

## Technical Architecture

### Non-Blocking State Machine
The component uses a `loop()` based state machine to avoid blocking the main ESPHome execution thread.
1. `update()` initiates a scan (Field ON -> Measure Amplitude -> WUPA).
2. `loop()` handles transitions based on IRQ flags and timers.
3. States include `STATE_IDLE`, `STATE_WUPA`, `STATE_ANTICOL`, and `STATE_REINITIALIZING`.

### Transport Layer
Transport is abstracted via pure virtual methods in the base `ST25R` class:
- `read_register()`, `write_register()`
- `write_command()`
- `write_fifo()`, `read_fifo()`

### Critical Rules
- **Datasheet §4.4.4**: Before `Transmit with/without CRC`, the FIFO MUST be cleared via `0xDB`, `NUM_TX_BYTES` set, and data loaded.
- **SPI Mode**: Must use `CLOCK_PHASE_TRAILING` (CPHA=1, Mode 1).
- **IRQ Handling**: Uses a static ISR to set a volatile flag, which is then processed in the main `loop()`.

## Hardware Specifications (ESP32-C6 + ST25R3916)

The primary development board uses the following pinout:
- **SPI**: CLK=19, MISO=10, MOSI=18, CS=6
- **IRQ**: 7
- **Status LED**: 2
- **I2C Address**: 0x50

**Note**: Avoid using GPIO9 for CS on ESP32-C6 as it is a strapping pin.

## Coding Standards

- **Style**: Follow ESPHome conventions (PascalCase for types, camelCase for variables/methods).
- **Non-Blocking**: Never use `delay()` in the C++ code; use `millis()` or state machine transitions.
- **Formatting**: Use `.clang-format` (2-space indentation) and `black` for Python.
- **Security**: **NEVER** store real UIDs in any logic, configuration examples, or documentation. Always use placeholders like `00-00-00-00` or `00000000` to prevent exploitation.

## References
- `docs/st25r3916.pdf`: Official datasheet (DS12484).
- `API_DOCUMENTATION.md`: Component-specific API details.
- `HARDWARE_SETUP.md`: Wiring and hardware configuration.
