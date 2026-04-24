# `include/keyboard_mgr.h`

This header file defines the `KeyboardManager` class, which is the driver for the M5Stack CardKB I2C keyboard used in the StikE project.

## Key Components

### `KeyboardManager` Class
Manages the initialization and reading of key presses from the I2C keyboard.

**Constants:**
- `static constexpr uint8_t I2C_ADDR = 0x5F`: The I2C address for the M5Stack CardKB 1.1.

**Public Methods:**
- `KeyboardManager()`: Constructor.
- `void init()`: Initializes the I2C bus and the keyboard connection.
- `char getKeyPress()`: Reads a single character from the keyboard. Note: The M5Stack CardKB maps 'Fn + key' combinations to hardcoded hex values based on physical layout, not standard ASCII offsets.
- `bool isAvailable()`: Checks if the keyboard has been successfully initialized.
- `void scanBus()`: Utility function to scan the I2C bus for devices (useful for debugging).

**Private Members:**
- `bool initialized`: Tracks whether the keyboard has been successfully initialized.
- `char lastKey`: Stores the most recently read key.

## Dependencies
- `<Wire.h>`: Arduino's I2C library.
- `<cstdint>`: Standard integer types.
