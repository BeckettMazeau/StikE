# `src/keyboard_mgr.cpp`

This source file implements the `KeyboardManager` class defined in `include/keyboard_mgr.h`. It acts as the driver for the I2C-based M5Stack CardKB keyboard.

## Key Implementations

- **Initialization (`init`)**: Configures the I2C bus using the Arduino `Wire` library with the specific SDA and SCL pins defined in `pins.h`. It sets the clock speed and marks the manager as initialized.
- **Key Parsing (`getKeyPress`)**:
  - Reads raw I2C data from the keyboard at the specified address (`0x5F`).
  - Translates the raw bytes into characters.
  - Handles the specific mapping for the CardKB where 'Fn + key' combinations produce specific hardcoded hex values rather than standard ASCII offsets.
- **Bus Scanning (`scanBus`)**: Implements a routine to scan the I2C bus and report connected devices to the serial console, primarily used for debugging hardware connections.

## Dependencies
- `<Arduino.h>`
- `"keyboard_mgr.h"`
- `"pins.h"`
