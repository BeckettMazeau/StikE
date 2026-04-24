# `include/display_mgr.h`

This header file defines the `DisplayManager` class, which is responsible for managing the dual-display architecture of the StikE system. It handles both the active TFT ST7735S display and the low-power ePaper GxEPD2 display.

## Key Components

### `DisplayManager` Class
The main class that encapsulates the logic for both displays.

**Public Methods:**
- `GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT>& getEPD()`: Returns a reference to the ePaper display object.
- `DisplayManager()`: Constructor.
- `~DisplayManager()`: Destructor.
- `static int getDaysInMonth(int year, int month)`: Utility function to determine the number of days in a given month.

**Public Members:**
- `int offsetX`, `int offsetY`: Used for layout and rendering offsets.

## Dependencies
- `<cstdint>`: Standard integer types.
- `<TFT_eSPI.h>`: Driver library for the ST7735S TFT display.
- `<GxEPD2_BW.h>`: Driver library for the ePaper display.
- `"state_types.h"`: Defines the various system states and data structures used throughout the UI.
- `"pins.h"`: Defines the hardware pin mappings for the displays.
