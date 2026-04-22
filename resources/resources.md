# StikE - Smart Task Display

## Project Overview

StikE is an ESP32-S3 based dual-display task management device featuring:
- **TFT Display** (ST7735S): Primary interactive interface for task management
- **ePaper Display** (2.13" B74): Low-power persistent display for passive task viewing
- **I2C Keyboard**: QWERTY input via I2C GPIO expander

The system alternates between ACTIVE mode (interactive TFT) and SLEEP mode (ePaper cycling) based on user activity.

---

## Pin Configuration

### ePaper Display (SPI - HSPI Bus)
| Signal | GPIO | Notes |
|--------|------|-------|
| CS/SS | 4 | SPI chip select |
| DC | 5 | Data/Command pin |
| RST | 6 | Reset pin |
| BUSY | 8 | Busy status (dedicated pin) |
| SCK | 17 | SPI clock (HSPI) |
| MOSI | 21 | SPI data out (HSPI) |
| MISO | NC | Not connected |

### TFT Display (SPI - VSPI Bus)
| Signal | GPIO | Notes |
|--------|------|-------|
| SCLK | 13 | SPI clock (VSPI) |
| MOSI | 11 | SPI data out (VSPI) |
| MISO | 12 | SPI data in (VSPI) |
| CS | 10 | SPI chip select |
| DC | 9 | Data/Command pin |
| RESET | 14 | Hardware reset |
| BACKLIGHT | 42 | Backlight enable (dedicated pin, no longer shared) |

### Keyboard (I2C)
| Signal | GPIO | Notes |
|--------|------|-------|
| SDA | 1 | I2C data |
| SCL | 2 | I2C clock |

### Wake Button
| Signal | GPIO | Notes |
|--------|------|-------|
| WAKE_BTN | 15 | Physical button to wake from sleep (RTC GPIO) |

### Dedicated Pin Configuration (Resolved GPIO Conflict)
GPIO 8 is now dedicated exclusively to ePaper BUSY monitoring.
The TFT backlight is controlled via GPIO 42, eliminating pin sharing.

---

## Project Structure

```
StikE/
├── platformio.ini              # Build configuration & dependencies
├── include/
│   ├── pins.h                  # Pin definitions (all GPIOs)
│   ├── state_types.h           # Core data types (SystemState, TaskItem)
│   ├── display_mgr.h           # DisplayManager class declaration
│   ├── keyboard_mgr.h          # KeyboardManager class declaration
│   └── README                  # Notes
├── src/
│   ├── main.cpp                # State machine, event queue, NVS persistence
│   ├── display_mgr.cpp         # Display implementation (TFT sprite + ePaper)
│   └── keyboard_mgr.cpp        # Keyboard implementation
└── resources/
    └── resources.md            # This file
```

---

## Core Data Types

### SystemState Enum
```cpp
enum class SystemState {
    STATE_ACTIVE,        // TFT interactive mode
    STATE_SLEEP,         // ePaper low-power mode
    STATE_EPAPER_UPDATE  // ePaper refresh in progress
};
```

### TaskItem Struct
```cpp
struct TaskItem {
    char title[32];          // Task description (fixed-size buffer to avoid heap fragmentation)
    bool isCompleted;       // Completion status
    uint32_t timestamp;      // Creation timestamp (millis)

    TaskItem();
    TaskItem(const char* t, bool completed = false, uint32_t ts = 0);
};
```

### DisplayManager Class
```cpp
class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();
    void initTFT();
    void initEpaper();
    void turnOnTFT();
    void turnOffTFT();
    void updateEpaperPartial(int viewIndex);
    void prepareEpaperViews(const TaskItem tasks[], uint32_t taskCount);
    void drawActiveGUI(const TaskItem tasks[], uint32_t taskCount, int selectedIndex);
    TFT_eSPI& getTFT();
    bool isTFTOn() const;

private:
    TFT_eSPI tft;                                          // Direct member
    TFT_eSprite* guiSprite;                                // PSRAM-allocated sprite for flicker-free GUI
    GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> epd; // Direct member
    bool tftOn;
    TaskItem epaperViews[EPAPER_VIEW_COUNT];
    uint32_t epaperViewCount;
    void drawEpaperView(int index);
};
```

### KeyboardManager Class
```cpp
class KeyboardManager {
public:
    KeyboardManager();
    void init();
    char getKeyPress();        // Returns key char, 0 if none
    bool isAvailable();        // I2C initialization status
    static constexpr uint8_t I2C_ADDR = 0x08;

private:
    bool initialized;
    char lastKey;              // Tracks last key to handle repeats
};
```

---

## Architecture Notes

### Dual SPI Bus Architecture
The TFT and ePaper use **separate SPI buses** to avoid bus collisions:
- **TFT**: Uses default VSPI (TFT_eSPI default) on pins 11/12/13
- **ePaper**: Uses dedicated HSPI on custom pins 17/21 with custom SPIClass

```cpp
// display_mgr.cpp
SPIClass epd_spi(HSPI);                    // Dedicated HSPI for ePaper
SPISettings epd_spi_settings(2000000, MSBFIRST, SPI_MODE0);
GxEPD2_213_B74 epd_instance(Pins::EP_CS, Pins::EP_DC, Pins::EP_RST, Pins::EP_BUSY);
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> epd(epd_instance);

// Initialization binds ePaper to HSPI
epd.init(115200, true, 10, false, epd_spi, epd_spi_settings);
```

### Sleep/Wake Architecture
The system uses `esp_light_sleep_start()` with GPIO wakeup. A volatile flag prevents infinite sleep loops:

```cpp
volatile bool wakeRequested = false;

void IRAM_ATTR wakeButtonISR() {
    wakeRequested = true;  // Set in ISR
}

// In handleSleepState() after waking:
if (wakeRequested) {
    wakeRequested = false;
    wakeToActive();  // Transition to ACTIVE mode
}
```

---

## Logic Flow

### State Machine

```
┌─────────────────────────────────────────────────────────────┐
│                         STARTUP                              │
│              initTFT(), initEpaper(), init()                │
│   Create event queue, keyboard task, load NVS tasks          │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    STATE_ACTIVE                              │
│  • TFT backlight ON                                          │
│  • Process events from queue (keyboard, system)              │
│  • Update GUI via PSRAM sprite (flicker-free)                │
│  • 10-second timer wakeup configured                         │
└─────────────────────────┬───────────────────────────────────┘
                          │
                ┌─────────────┴─────────────┐
                │ ESC key pressed           │ Timer expires (10s)
                ▼                            ▼
        ┌─────────────────────────┐   ┌─────────────────────────────┐
        │     STATE_SLEEP         │   │       STATE_SLEEP            │
        │  • TFT backlight OFF    │   │  • Same as left              │
        │  • ePaper cycles views  │   │                             │
        │  • GPIO wake enabled   │   │                             │
        └───────────┬─────────────┘   └─────────────┬───────────────┘
                    │ wakeRequested==true          │
                    ▼                                │
            wakeToActive() ◄──────────────────────┘
```

### STATE_ACTIVE Behavior

| Key | Action |
|-----|--------|
| `n` / `N` | Add new task (via system event) |
| `j` / `J` / Down Arrow (0x34) | Select next task (via system event) |
| `k` / `K` / Up Arrow (0x35) | Select previous task (via system event) |
| `x` / `X` | Toggle task completion (via system event) |
| `Esc` (0x1B) | Enter SLEEP mode (via system event) |

*All keyboard input is processed by a FreeRTOS task on core 0 and sent to the main state machine via a system event queue.*

### STATE_SLEEP Behavior

- TFT backlight turned off (ST7735_SLPIN command + GPIO LOW)
- GPIO 8 reconfigured for ePaper BUSY monitoring
- ePaper cycles through up to 5 task views (EPAPER_VIEW_COUNT)
- Each view displays: Task number, title, completion status
- Timer wakeup every 10 seconds cycles to next view
- GPIO wakeup (button on GPIO 15) returns to ACTIVE mode

---

## Implementation Status

### ✅ Completed

| Component | Status | Notes |
|------------|--------|-------|
| platformio.ini | Done | ESP32-S3, TFT_eSPI (VSPI), GxEPD2 (HSPI) configured; removed USE_HSPI_PORT flag |
| Pin definitions | Done | All GPIOs defined, WAKE_BTN=15, LCD_BL moved to GPIO 42 to resolve conflict |
| State types | Done | SystemState, TaskItem with fixed-size char buffer (no heap fragmentation) |
| DisplayManager class | Done | PSRAM-allocated sprite for flicker-free GUI, no shared pin logic |
| KeyboardManager class | Done | I2C init, repeat key handling fixed |
| State machine | Done | ACTIVE/SLEEP with wake flag and event queue |
| Task storage (NVS) | Done | Persists tasks across reboots using Preferences library |
| Build system | Done | Compiles ~378KB flash, ~26KB RAM |

### 🔲 Stubbed / Needs Implementation

| Component | Priority | Description |
|-----------|----------|-------------|
| Keyboard input | Medium | Enhanced key parsing for modifiers and special keys |
| Task creation flow | Low | Improved UI for task naming (current implementation uses auto-generated names) |
| ePaper partial optimization | Low | Further reduce ePaper update frequency |
| Settings menu | Low | Configure sleep duration, contrast, etc. via GUI |

### 📝 Known Issues (Resolved)

1. ~~**ePaper template syntax**~~ - Fixed: Direct class member with GxEPD2_213_B74 instance
2. ~~**Dual-SPI bus collision**~~ - Fixed: ePaper uses dedicated HSPI on custom pins
3. ~~**Infinite sleep loop**~~ - Fixed: volatile wakeRequested flag checked after light sleep
4. ~~**Keyboard repeat characters**~~ - Fixed: lastKey reset when no valid key
5. ~~**TFT_MISO**~~ - Fixed: Changed from -1 to 12
6. ~~**WAKE_BTN pin**~~ - Fixed: Changed from 0 to 15
7. ~~**Arduino String heap fragmentation**~~ - Fixed: Changed to std::string
8. ~~**ST7735S wake time**~~ - Fixed: Added delay(120) after SLPOUT

---

## Dependencies

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
lib_deps = 
    bodmer/TFT_eSPI@^2.5.43
    zinggjm/GxEPD2@^1.6.8
    adafruit/Adafruit GFX Library@^1.12.6
    adafruit/Adafruit BusIO@^1.16.1

build_flags =
    ; TFT_eSPI (VSPI) - Using default VSPI (FSPI on S3) - HSPI left free for ePaper
    -D ST7735_DRIVER=1
    -D TFT_MISO=12
    -D TFT_MOSI=11
    -D TFT_SCLK=13
    -D TFT_CS=10
    -D TFT_DC=9
    -D TFT_RST=14
    -D TFT_BL=42  // Reassigned from GPIO 8 to avoid conflict with EP_BUSY
    ; GxEPD2 (HSPI - custom pins in code)
    -D GxEPD2_DISPLAY_CLASS=Generic_EPD
    -D GxEPD2_DRIVER_CLASS=GxEPD2_213_B74
```

---

## Build & Flash

```bash
# Build only
pio run

# Build and upload
pio run --target upload

# Monitor serial output
pio device monitor
```

---

## Useful Links

- [TFT_eSPI Library by Bodmer](https://github.com/Bodmer/TFT_eSPI)
- [GxEPD2 ePaper Library](https://github.com/moononournation/GxEPD2)
- [ESP32-S3 GPIO Guide - Luis Llamas](https://www.luisllamas.es/en/which-pins-can-i-use-on-esp32-s3/)
- [PlatformIO ESP32 Documentation](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [ESP32 Light Sleep Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)

---

## Future Enhancements

- BLE keyboard support for wireless input
- Battery management with LiPo charger
- Physical button for forced wakeup
- NVS-backed task persistence across reboots
- WiFi sync with remote task server
- Custom PCB design integrating all components