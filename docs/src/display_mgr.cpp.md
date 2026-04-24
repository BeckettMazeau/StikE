# `src/display_mgr.cpp`

This source file implements the `DisplayManager` class defined in `include/display_mgr.h`. It is responsible for the abstraction layer managing the dual displays (TFT ST7735S and ePaper GxEPD2).

## Key Implementations

- **Initialization Sequence**: Implements the complex power-on and bus-sequencing logic for both the TFT and ePaper displays. Crucially, it manages the sequential initialization required to prevent SPI bus contention.
- **Active GUI Rendering (TFT)**:
  - Implements a sprite-based rendering pipeline for the TFT display using the `TFT_eSPI` library.
  - Uses a software buffer to prepare the full screen in memory before pushing it to the hardware, which eliminates screen flickering during UI updates.
- **Sleep Display Logic (ePaper)**:
  - Contains the logic for filtering and formatting active tasks and upcoming calendar events into discrete screens (views) for the sleep display.
  - Manages partial and full updates of the ePaper display.
  - Implements the logic to cycle through multiple screens of information while the device is in a low-power state.
- **Utility Functions**: Implements helper functions like `getDaysInMonth`.

## Dependencies
- `<Arduino.h>`
- `<SPI.h>`
- `<esp_sleep.h>`
- `<LittleFS.h>`
- `<FS.h>`
- `"display_mgr.h"`
- `"state_types.h"`
- `"icons.h"`
