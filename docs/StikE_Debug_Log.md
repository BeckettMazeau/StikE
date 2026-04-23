# StikE Debugging Context and Progress

This document captures the current debugging state of the StikE firmware project (ESP32-S3) with dual displays (TFT ST7735S, ePaper GxEPD2 213_B74), including issues observed, experiments performed, and the latest test results. It is intended as contextual input for a code-review/LLM-based analysis, not as a plan or set of instructions.

## Project and Environment Context

### Hardware

- **Microcontroller**: ESP32-S3 DevKitC
- **TFT Display**: ST7735S (128x160 resolution), driven over HSPI (SPI3). Backlight connected to GPIO 42 (TFT_BL).
- **E-Paper Display**: GxEPD2_213_B74 (B74 controller) driven over FSPI (SPI2).
- **Input**: I2C keyboard on GPIO 18 (SDA), 21 (SCL)
- **Wake Button**: GPIO 14

### Software Stack

- **TFT Control**: TFT_eSPI library + TFT_eSprite (software sprite buffer for UI)
- **E-Paper Control**: GxEPD2 library
- **Display Manager Module**: `src/display_mgr.cpp` / `include/display_mgr.h`
- **UI Loop**: `src/main.cpp` (drawActiveGUI, drawAddViewGUI)
- **Test Harness**: Guarded by STike_SYSTEM_TEST compile flag

### Build Configuration

- PlatformIO configuration mirrors known-good DualDisplayTest setup:
  - ST7735_GREENTAB for TFT driver
  - HSPI for TFT, FSPI for ePaper (separate SPI buses)
  - TFT_WIDTH=128, TFT_HEIGHT=160
  - USE_HSPI_PORT
  - SPI_FREQUENCY=27000000
- USER_SETUP_LOADED is used to override User_Setup.h defaults via build flags
- DIAG_UI_ONLY flag available (commented out) to disable ePaper updates during TFT diagnostics

### Known Hardware Issues

- **ESP32-S3 SPI Routing**: HSPI has non-default pin mappings on ESP32-S3; logs consistently show:
  ```
  [ESP32] spiAttachMISO(): HSPI Does not have default pins on ESP32S3!
  ```
- This is a known ESP32-S3 specific issue where the GPIO matrix doesn't automatically assign default pins for HSPI.

## What We Are Trying to Solve

1. **Primary Issue**: After TFT initialization (color flash sequence), the display shows white and the UI does not render consistently.
2. **Secondary Issue**: E-paper updates are unreliable; suspected dual-SPI bus contention.
3. **Goal**: Determine whether the issue is in:
   - (a) The hardware SPI bus path
   - (b) The sprite buffer memory/path
   - (c) The UI drawing code (drawActiveGUI)

## Known Issues and Contextual Observations

### Observed Behavior

- During init, TFT successfully executes a red → green → black flash sequence (hardware proof of SPI wiring and init)
- After init completes, screen transitions to solid white
- Sprite-based test path (blue/yellow rectangles) CAN render content, but full UI does not appear
- Backlight (TFT_BL on GPIO 42) behavior verified: HIGH = on, LOW/GND = off, floating keeps backlight on
- The backlight was previously disconnected and re-connected; no change in behavior

### Sprite Path Observations

- The sprite pipeline proof (blue/yellow during init) works and reaches the panel:
  ```
  [DisplayMgr] Creating sprite...
  [DisplayMgr] Sprite 160x128 allocated
  [DisplayMgr] GUI sprite allocated successfully
  [DisplayMgr] TFT initialized
  ```
- However, UI content from drawActiveGUI does not become visible after boot

### E-Paper Observations

- E-paper shows garbled text when tests run
- No proper updates visible on ePaper physical display
- The _Update_Full sequences appear in logs during init, but no clean image renders

### Build Configuration Notes

- PlatformIO configuration mirrors DualDisplayTest (known working on same hardware)
- ST7735_GREENTAB is used (correct for modern ST7735S panels)
- USE_HSPI_PORT places TFT on SPI3, leaving SPI2 for ePaper (theoretically avoids contention)
- Previous attempts to modify User_Setup.h were overridden by platformio.ini build flags via USER_SETUP_LOADED

## Experiments Performed

### Direct Color Path (Hardware Bypass)

- Added `drawDirectColorFrame(uint16_t color)` to write directly to TFT via `tft.fillScreen()` bypassing sprite buffer
- This test PASSES - direct color writes do render on the TFT
- Example log:
  ```
  [SYS_TEST] Direct color frame drawn (sprite present) 0xF81F
  ```
- Shows Magenta color on screen when invoked

### Tiny Sprite Test

- Added `drawActiveGUISimpleTest()` to create a small 64x32 sprite for isolated testing
- Test creates sprite, draws blue rectangle at (10,10), then yellow at (20,20)
- Test PASSES - small rectangles DO appear on screen
- Example log:
  ```
  [SYS_TEST] Simple sprite test: creating 64x32 sprite
  [SYS_TEST] Simple test: 64x32 sprite allocated, drawing blue
  [SYS_TEST] Simple test: pushed blue rect at 10,10
  [SYS_TEST] Simple test: pushed yellow rect at 20,20
  [SYS_TEST] Simple sprite test complete
  ```
- Observed: blue rectangle in upper-left, yellow rectangle overlapping toward bottom-right

### Memory Telemetry

- Added ESP.getFreeHeap() logging before and after sprite operations in drawActiveGUI
- Heap values logged but no allocation failures observed in serial output
- Indicates memory pressure is not the primary cause

### E-Paper Guard

- Added DIAG_UI_ONLY compile flag to disable ePaper updates during TFT diagnostics
- When enabled in platformio.ini, ePaper updates are skipped in:
  - handleSleepState() in main.cpp
  - drawEpaperTestPattern() in systems_test.cpp
- Purpose: eliminate SPI bus contention as variable during TFT debugging

## The Most Recent Test Run

### Full Serial Output

```
=== StikE Firmware Starting ===
[Setup] Calling displayMgr.initTFT()...
[DisplayMgr] initTFT: Starting TFT_eSPI initialization
[   646][E][esp32-hal-spi.c:215] spiAttachMISO(): HSPI Does not have default pins on ESP32S3!
[DisplayMgr] Creating sprite...
[DisplayMgr] Sprite 160x128 allocated
[DisplayMgr] GUI sprite allocated successfully
[DisplayMgr] TFT initialized
[Setup] displayMgr.initTFT() returned
[Setup] Calling displayMgr.initEpaper()...
_Update_Full : 7
_Update_Full : 1
_Update_Full : 1
[DisplayMgr] ePaper initialized (Software SPI)
[Setup] displayMgr.initEpaper() returned
[Setup] Calling keyboardMgr.init()...
[  4851][I][esp32-hal-i2c.c:75] i2cInit(): Initialising I2C Master: sda=18 scl=21 freq=100000
[KeyboardMgr] I2C initialized on SDA=18, SCL=21
[Setup] keyboardMgr.init() returned
[SYS_TEST] Triggering UI smoke test (MAGENTA)
[SYS_TEST] Smoke test drawn MAGENTA
[SYS_TEST] Triggering full-red screen diagnostic
[SYS_TEST] Full-screen RED test drawn
[SYS_TEST] Triggering direct color frame (MAGENTA) bypass sprite
[SYS_TEST] Direct color frame drawn (sprite present) 0xF81F
[SYS_TEST] Triggering simple sprite test (64x32)
[SYS_TEST] Simple sprite test: creating 64x32 sprite
[SYS_TEST] Simple test: 64x32 sprite allocated, drawing blue
[SYS_TEST] Simple test: pushed blue rect at 10,10
[SYS_TEST] Simple test: pushed yellow rect at 20,20
[SYS_TEST] Simple sprite test complete
[NVS] Loaded 4 tasks
[Setup] Loaded 4 tasks
[Setup] Entering UI_LIST state

=== STIK E SYSTEMS TEST MODE ===
Type 'help' for commands
==============================

[I] Key Mapping:
  0x1B = ESC  | Enter sleep mode
  'n'       | Add new task
  'j'/'0x34' | Navigate down
  'k'/'0x35' | Navigate up
  'x'       | Toggle task
  't'       | Run TFT test
  'e'       | Run ePaper test
  's'       | Run sleep test
  '?'       | Show this help
[TEST] Key Pressed: t (0x74)
[TEST] Key Pressed: t (0x74)
[KEY] 0x74 't' | TFT TEST
_Update_Part : 6
[TEST] Key display updated: 0x74 'TFT TEST'
```

### Observed Screen Behavior (During This Run)

1. Brief white flash
2. Extremely brief green fill → immediate transition to red
3. Green fill
4. Blue fill
5. Yellow fill
6. Extremely brief black flash then blank/white
7. ~1.5 second delay
8. Purple fill (from sprite simple test) and stays on purple

### Observed Physical Results

- **TFT**: Small blue rectangle visible in upper-left, yellow rectangle overlapping it toward bottom-right (from tiny sprite test)
- **E-Paper**: Garbled text, no clean updates visible

## Current Synthesis

### What Works

- Direct TFT write path (tft.fillScreen) - reliably renders solid colors
- Tiny sprite rendering (64x32) - successfully draws small primitives
- Init flash sequence - proves SPI wiring and basic TFT initialization

### What Does Not Work

- Full UI rendering via drawActiveGUI with 160x128 sprite - content not visible
- E-paper updates - unreliable/gargled output

### Hypotheses (From Evidence)

1. **Sprite Dimension/Orientation Mismatch**: The 160x128 sprite dimensions may not align correctly with the TFT's rotation=1 setting, causing content to render off-screen or be clipped
2. **Memory Fragmentation**: Large sprite creation (160x128 at 16-bit depth = ~40KB) may be hitting heap limits under dual-display initialization
3. **Bus Contention**: Even with separate SPI buses, ePaper initialization may leave SPI bus in incorrect state for TFT rendering
4. **UI Drawing Code Issue**: drawActiveGUI may have color/text issues making content invisible (dark-on-dark)

## Files Modified During Debugging

- `include/display_mgr.h` - Added diagnostic function declarations
- `src/display_mgr.cpp` - Implemented diagnostic tests, memory telemetry, enhanced logging
- `src/main.cpp` - Added test invocations, DIAG_UI_ONLY guards
- `src/systems_test.cpp` - Added DIAG_UI_ONLY guards for ePaper
- `platformio.ini` - Added DIAG_UI_ONLY flag option

## Appendices

### Key Code References

- Sprite creation: `display_mgr.cpp:initTFT()` - creates 160x128 sprite
- UI rendering: `display_mgr.cpp:drawActiveGUI()` - main UI drawing function
- Diagnostic path: `display_mgr.cpp:drawDirectColorFrame()` - direct hardware write
- Simple test: `display_mgr.cpp:drawActiveGUISimpleTest()` - tiny sprite test

### PlatformIO Build Flags (Relevant)

```
-D USER_SETUP_LOADED=1
-D ST7735_DRIVER=1
-D ST7735_GREENTAB
-D TFT_RGB_ORDER=TFT_RGB
-D TFT_WIDTH=128
-D TFT_HEIGHT=160
-D TFT_MISO=-1
-D TFT_MOSI=11
-D TFT_SCLK=12
-D TFT_CS=10
-D TFT_DC=9
-D TFT_RST=13
-D TFT_BL=42
-D TFT_BACKLIGHT_ON=HIGH
-D SPI_FREQUENCY=27000000
-D USE_HSPI_PORT
-D STike_SYSTEM_TEST
; -D DIAG_UI_ONLY  (commented out)
```

---

*This document is intended for independent review. It does not prescribe next steps - reviewers should assess the evidence and propose diagnostics based on their own analysis.*