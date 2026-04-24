# `src/systems_test.cpp`

This source file implements the `SystemsTest` class defined in `include/systems_test.h`. It contains a suite of diagnostic routines used for hardware validation and low-level debugging of the StikE system. This code is only active when the `STike_SYSTEM_TEST` macro is defined.

## Key Implementations

- **Hardware Tests**:
  - Implements specific routines to verify the integrity of the SPI buses connected to the displays.
  - Contains logic to draw test patterns (e.g., color bars, geometry) on the TFT display to verify color rendering and driver configuration.
  - Contains logic to trigger specific update cycles and test patterns on the ePaper display.
- **Interactive Diagnostics**:
  - Implements a serial command interface (`handleSerialInput`) allowing a developer to trigger specific tests or read system states via the serial monitor.
  - Implements keyboard input handling (`handleKeyPress`) specifically for the test mode, allowing for physical testing of the key matrix and character mapping.
  - Logs detailed test output and system status to the serial console.

## Dependencies
- `<Arduino.h>`
- `<esp_sleep.h>`
- `"systems_test.h"`
- `"display_mgr.h"`
- `"keyboard_mgr.h"`
- `"state_types.h"`
