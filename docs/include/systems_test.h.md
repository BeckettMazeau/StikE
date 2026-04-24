# `include/systems_test.h`

This header file defines the `SystemsTest` class, which provides a diagnostic suite for hardware validation and debugging of the StikE system. This code is conditionally compiled using the `STike_SYSTEM_TEST` preprocessor macro.

## Key Components

### `SystemsTest` Class
Manages hardware tests and interactive diagnostics.

**Public Methods:**
- `SystemsTest()`: Constructor.
- `void init()`: Initializes the test suite.
- `void update()`: Called periodically to manage ongoing tests.
- `void handleSerialInput()`: Processes commands received via the serial port.
- `void handleKeyPress(char key)`: Processes input from the physical keyboard during testing.
- `static void setTestMode(bool enabled)`: Enables or disables the test mode.
- `static bool isTestMode()`: Returns whether the system is currently in test mode.

**Private Members and Methods:**
- Includes internal state for tracking key history, active tests (`m_tftTestActive`, `m_epaperTestActive`), and test cycles.
- Defines the `TestSubMode` enum (`TEST_NORMAL`, `TEST_TFT_COLORS`, `TEST_EPAPER_PATTERNS`, `TEST_SLEEP`, `TEST_KEYBOARD`).
- Contains methods for drawing test patterns on the TFT and ePaper displays, and logging output.

## Dependencies
- `<cstdint>`: Standard integer types.
