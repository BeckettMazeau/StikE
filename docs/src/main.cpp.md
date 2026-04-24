# `src/main.cpp`

This is the primary entry point and core logic controller for the StikE firmware. It orchestrates the setup, the dual-core FreeRTOS architecture, and the main event-driven state machine.

## Key Implementations

- **System Setup (`setup`)**:
  - Initializes serial communication.
  - Initializes the NVS (Preferences) to restore saved task and calendar data.
  - Coordinates the initialization of the display manager and keyboard manager.
  - Creates the FreeRTOS tasks and event queues.
- **Main Loop (`loop`)**:
  - Runs on Core 1.
  - Acts as the main event loop, continuously monitoring the FreeRTOS event queue for system events (like key presses or sleep requests).
  - Dispatches events to the appropriate state handler based on the current `SystemState`.
  - Triggers UI rendering updates on the TFT display.
- **Keyboard Task**:
  - A dedicated FreeRTOS task running on Core 0.
  - Continuously polls the I2C bus via the `KeyboardManager` for user input.
  - Translates raw input into `SystemEvent` structs and pushes them to the event queue.
- **State Handlers**:
  - Contains individual functions (or calls to them) that manage the specific logic for different UI screens (Task List, Calendar views, Add/Edit menus).
- **Persistence Logic**:
  - Interacts with the ESP32 `Preferences` library to serialize and deserialize the fixed-size arrays of tasks and calendar events, ensuring data persistence across reboots and power cycles.

## Conditional Compilation
Includes diagnostic and system test code wrapped in `#ifdef STike_SYSTEM_TEST` directives.

## Dependencies
- `<Arduino.h>`
- `<esp_sleep.h>`
- `<WiFi.h>`
- `<esp_log.h>`
- `<Preferences.h>`
- `<freertos/FreeRTOS.h>`, `<freertos/task.h>`, `<freertos/queue.h>`
- `"pins.h"`, `"state_types.h"`, `"display_mgr.h"`, `"keyboard_mgr.h"`, `"power_mgr.h"`
- `"systems_test.h"` (conditionally)
