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
| MISO | NC | Not connected (readback not used) |
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

## Developer Guide

This guide shows you how to extend StikE with new features. We'll cover the display system, task operations, keyboard handling, and provide copy-paste code templates.

---

### 1. How the Display System Works

The StikE TFT display uses a **sprite-based rendering system** for flicker-free updates:

```
┌─────────────────────────────────────────────┐
│           RENDERING PIPELINE                  │
│                                             │
│  1. Draw to sprite (in PSRAM)                │
│     guiSprite->fillRect(...)                │
│     guiSprite->print(...)                  │
│                                             │
│  2. Push to TFT screen                     │
│     guiSprite->pushSprite(0, 0)           │
│                                             │
│  Result: Smooth, no screen tearing          │
└─────────────────────────────────────────────┘
```

#### Available Colors

TFT_eSPI provides these built-in colors:

| Color Constant | Hex Value | Use For |
|--------------|----------|--------|
| `TFT_BLACK` | 0x0000 | Background |
| `TFT_WHITE` | 0xFFFF | Primary text |
| `TFT_RED` | 0xF800 | Errors, delete |
| `TFT_GREEN` | 0x07E0 | Success, completed |
| `TFT_BLUE` | 0x001F | Selection highlight |
| `TFT_CYAN` | 0x07FF | Headers, titles |
| `TFT_MAGENTA` | 0xF81F | Warnings |
| `TFT_YELLOW` | 0xFFE0 | Highlights |
| `TFT_ORANGE` | 0xFDA0 | Active items |
| `TFT_PURPLE` | 0x8010 | Categories |
| `TFT_LIGHTGREY` | 0xC618 | Inactive text |
| `TFT_DARKGREY` | 0x4208 | Disabled |

#### Drawing Primitives

```cpp
// Clear sprite with color
guiSprite->fillSprite(TFT_BLACK);

// Set text color
guiSprite->setTextColor(TFT_WHITE);           // White text on black
guiSprite->setTextColor(TFT_WHITE, TFT_BLUE); // White text, blue background

// Position cursor and print
guiSprite->setCursor(x, y);               // x, y in pixels
guiSprite->print("Hello");                 // Print string
guiSprite->println("World");               // Print with newline

// Draw shapes
guiSprite->fillRect(x, y, w, h, color);    // Filled rectangle
guiSprite->drawRect(x, y, w, h, color);    // Outline rectangle
guiSprite->fillCircle(x, y, r, color);    // Filled circle
guiSprite->drawCircle(x, y, r, color);    // Outline circle

// Draw lines
guiSprite->drawLine(x1, y1, x2, y2, color);  // Line
guiSprite->drawFastHLine(x, y, w, color);        // Horizontal line
guiSprite->drawFastVLine(x, y, h, color);        // Vertical line
```

#### Screen Dimensions

The TFT display is 160x128 pixels. Keep text and elements within these bounds:

```cpp
const uint16_t TFT_WIDTH = 160;
const uint16_t TFT_HEIGHT = 128;

// Example: draw at safe positions
guiSprite->setCursor(5, 5);     // Top-left safe zone
guiSprite->setCursor(5, 110);   // Bottom-left safe zone
// Title bar uses y = 0-15
// Task list starts at y = 20, each row is ~12 pixels tall
// Bottom status bar starts at y = 115
```

---

### 2. Working with Tasks

The task system uses a simple array in RAM with NVS persistence.

#### Task Data Structure

```cpp
// Defined in include/state_types.h
struct TaskItem {
    char title[32];           // Fixed-size buffer (no heap fragmentation!)
    bool isCompleted;         // true = done, false = pending
    uint32_t timestamp;       // millis() when created
    
    TaskItem();                              // Default: empty title
    TaskItem(const char* t, bool c, uint32_t ts);  // Custom task
};

// Task storage (defined in main.cpp)
TaskItem tasks[MAX_TASKS];    // Array in RAM (MAX_TASKS = 20)
uint32_t taskCount = 0;     // Current number of tasks
int selectedTaskIndex = -1;   // Currently selected task (-1 = none)
```

#### Adding a Task

```cpp
// Simple add (uses auto-generated name)
void addTaskSimple() {
    if (taskCount < MAX_TASKS) {
        char newTitle[32];
        snprintf(newTitle, sizeof(newTitle), "New Task %lu", taskCount + 1);
        tasks[taskCount] = TaskItem(newTitle, false, millis());
        taskCount++;
        saveTasks();  // Persist to NVS
    }
}

// Add with custom name
void addTaskCustom(const char* name) {
    if (taskCount < MAX_TASKS) {
        tasks[taskCount] = TaskItem(name, false, millis());
        taskCount++;
        saveTasks();
    }
}
```

#### Toggling Completion

```cpp
void toggleTask(int index) {
    if (index >= 0 && index < taskCount) {
        tasks[index].isCompleted = !tasks[index].isCompleted;
        saveTasks();
    }
}
```

#### Deleting a Task

```cpp
void deleteTask(int index) {
    if (index < 0 || index >= taskCount) return;
    
    // Shift remaining tasks left
    for (uint32_t i = index; i < taskCount - 1; i++) {
        tasks[i] = tasks[i + 1];
    }
    
    taskCount--;
    
    // Adjust selection if needed
    if (selectedTaskIndex >= taskCount) {
        selectedTaskIndex = taskCount - 1;
    }
    
    saveTasks();
}
```

#### Navigation

```cpp
void navigateDown() {
    if (taskCount > 0) {
        selectedTaskIndex = (selectedTaskIndex + 1) % taskCount;
    }
}

void navigateUp() {
    if (taskCount > 0) {
        selectedTaskIndex = (selectedTaskIndex - 1 + taskCount) % taskCount;
    }
}

// Navigate with bounds checking (no wrap)
void navigateDownBounded() {
    if (selectedTaskIndex < taskCount - 1) {
        selectedTaskIndex++;
    }
}
```

#### NVS Persistence

Tasks are saved to ESP32's Non-Volatile Storage (NVS) using the Preferences library:

```cpp
// Save all tasks to NVS
void saveTasks() {
    prefs.begin("stike", false);           // "stike" namespace, write mode
    prefs.putUInt("taskCount", taskCount); // Save count first
    
    for (uint32_t i = 0; i < taskCount; i++) {
        char key[16];
        snprintf(key, sizeof(key), "task_%lu_title", i);
        prefs.putString(key, tasks[i].title);
        
        snprintf(key, sizeof(key), "task_%lu_completed", i);
        prefs.putBool(key, tasks[i].isCompleted);
        
        snprintf(key, sizeof(key), "task_%lu_timestamp", i);
        prefs.putUInt(key, tasks[i].timestamp);
    }
    
    prefs.end();
}

// Load all tasks from NVS
void loadTasks() {
    prefs.begin("stike", true);  // Read-only mode
    taskCount = prefs.getUInt("taskCount", 0);
    
    for (uint32_t i = 0; i < taskCount; i++) {
        char key[16];
        snprintf(key, sizeof(key), "task_%lu_title", i);
        String titleStr = prefs.getString(key, "");
        strncpy(tasks[i].title, titleStr.c_str(), 31);
        tasks[i].title[31] = '\0';
        
        snprintf(key, sizeof(key), "task_%lu_completed", i);
        tasks[i].isCompleted = prefs.getBool(key, false);
        
        snprintf(key, sizeof(key), "task_%lu_timestamp", i);
        tasks[i].timestamp = prefs.getUInt(key, 0);
    }
    
    prefs.end();
}
```

---

### 3. Keyboard Input System

Keyboard input uses a **FreeRTOS task + event queue** architecture:

```
┌───────────────────────────────────────────────────────────���─┐
│              KEYBOARD INPUT PIPELINE                        │
│                                                        │
│  ┌──────────────┐     ┌─────────────────┐              │
│  │ I2C Keyboard │────►│ keyboardTask()   │              │
│  │ (I2C GPIO   │     │ (FreeRTOS task  │              │
│  │  expander)  │     │  on Core 0)     │              │
│  └──────────────┘     └────────┬────────┘              │
│                                 │                       │
│                                 ▼                       │
│                        ┌─────────────────┐              │
│                        │ xQueueSend()     │              │
│                        │ (system events) │              │
│                        └────────┬────────┘              │
│                                 │                       │
│                                 ▼                       │
│                        ┌─────────────────┐              │
│                        │ handleActive    │              │
│                        │ State()        │              │
│                        └─────────────────┘              │
└─────────────────────────────────────────────────────────────┘
```

#### Currently Mapped Keys

| Key Code | Character | Action |
|----------|-----------|--------|
| `0x1B` | ESC | Enter SLEEP mode |
| `n` / `N` | n, N | Add new task |
| `j` / `J` | j, J | Select next task |
| `k` / `K` | k, K | Select previous task |
| `x` / `X` | x, X | Toggle task completion |
| `0x34` | ↓ arrow | Select next task |
| `0x35` | ↑ arrow | Select previous task |

#### Adding a New Key Mapping

To add a new key action (e.g., 'D' to delete the selected task):

**Step 1:** Add event type in `include/state_types.h`

```cpp
enum class SystemEventType {
    KEY_PRESS,
    SLEEP_REQ,
    WAKE_REQ,
    TASK_ADDED,
    TASK_TOGGLED,
    TASK_DELETE    // 👈 ADD THIS
};
```

**Step 2:** Add key handling in `src/main.cpp` - `keyboardTask()` function

```cpp
void keyboardTask(void* parameter) {
    for (;;) {
        if (keyboardMgr.isAvailable()) {
            char key = keyboardMgr.getKeyPress();
            if (key != 0) {
                switch (key) {
                    // ... existing cases ...
                    
                    // 👇 ADD YOUR NEW KEY 👇
                    case 'd':  // Press 'd' to delete
                    case 'D':
                        sendSystemEvent(SystemEventType::TASK_DELETE, selectedTaskIndex);
                        break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

**Step 3:** Handle the event in `src/main.cpp` - `handleActiveState()` function

```cpp
void handleActiveState() {
    SystemEvent event;
    while (xQueueReceive(systemEventQueue, &event, 0) == pdTRUE) {
        switch (event.type) {
            // ... existing cases ...
            
            // 👇 ADD YOUR NEW HANDLER 👇
            case SystemEventType::TASK_DELETE:
                if (event.param >= 0 && event.param < taskCount) {
                    deleteTask(event.param);
                    LOG_PRINTF("[Input] Deleted task %d\n", event.param);
                }
                break;
        }
    }
    
    displayMgr.drawActiveGUI(tasks, taskCount, selectedTaskIndex);
}
```

That's it! Three files modified, one new key action.

---

### 4. System Architecture

#### State Machine

```
┌─────────────────────────────────────────────────────────────┐
│                    STATE_MACHINE                         │
│                                                        │
│    ┌──────────────┐         ┌──────────────┐          │
│    │  ACTIVE MODE │◄───────►│  SLEEP MODE  │          │
│    │             │  ESC on │             │           │
│    │ • TFT ON    │  Timer  │ • TFT OFF   │           │
│    │ • Events   │  Button │ • ePaper   │           │
│    │ • GUI     │         │ • Light    │           │
│    └──────────────┘         │  Sleep    │           │
│           ▲                 └──────────────┘          │
│           │                        │                   │
│           └────────────────────────┘                   │
│              wakeToActive()                           │
└─────────────────────────────────────────────────────┘
```

#### Memory Layout

```
┌─────────────────────────────────────────────┐
│              ESP32-S3 MEMORY               │
│                                           │
│ PSRAM (8MB OPI)                           │
│ ├─ GUI Sprite (128x128x16bpp = 32KB)     │
│ │  └─ Used for flicker-free TFT rendering  │
│ └─ Available for custom buffers          │
│                                           │
│ RAM (512KB)                             │
│ ├─ tasks[] array (20 × ~40 bytes)        │
│ │  └─ ~800 bytes for 20 tasks          │
│ ├─ Stack (main loop, keyboard task)     │
│ ├─ Heap (dynamic allocations)          │
│ └─ Static variables                 │
└─────────────────────────────────────────────┘
```

#### Power States

| State | TFT | ePaper | CPU | Power Usage |
|-------|-----|-------|-----|------------|
| ACTIVE | On | Off | Active | ~80mA |
| SLEEP | Off | Cycling | Light Sleep | ~10mA |
| DEEP_SLEEP | Off | Off | Deep Sleep | ~10μA |

#### Event Queue

The system event queue passes information between the keyboard task and main loop:

```cpp
// Created in setup()
systemEventQueue = xQueueCreate(10, sizeof(SystemEvent));

// Event structure (defined in state_types.h)
struct SystemEvent {
    SystemEventType type;  // Event type enum
    int param;           // Optional parameter (e.g., task index)
};

// Send an event
void sendSystemEvent(SystemEventType type, int param = 0) {
    if (systemEventQueue) {
        SystemEvent event = {type, param};
        xQueueSendFromISR(systemEventQueue, &event, nullptr);
    }
}

// Receive events (non-blocking)
SystemEvent event;
while (xQueueReceive(systemEventQueue, &event, 0) == pdTRUE) {
    // Process event
}
```

---

### 5. Quick Reference

#### File Purposes

| File | Purpose | When to Modify |
|------|---------|--------------|
| `main.cpp` | Main loop, state machine, events | Add key handlers, state logic |
| `display_mgr.cpp` | TFT/ePaper rendering | Change GUI appearance |
| `display_mgr.h` | Display class declaration | Add display methods |
| `state_types.h` | TaskItem, event types | Add task fields, new event types |
| `pins.h` | GPIO pin definitions | Change pin assignments |
| `keyboard_mgr.cpp` | I2C keyboard input | Change keyboard behavior |

#### Common Operations

| Operation | Function to Modify |
|-----------|------------------|
| Add key handler | `main.cpp` - `keyboardTask()` |
| Handle key action | `main.cpp` - `handleActiveState()` |
| Change task appearance | `display_mgr.cpp` - `drawActiveGUI()` |
| Add new task field | `state_types.h` - `TaskItem` struct |
| Change pins | `pins.h` |

#### Debugging Tips

```cpp
// Serial output (connected to USB CDC)
Serial.print("Value: ");
Serial.println(myValue);

// Check if code is reached
Serial.println("[DEBUG] Reached point A");

// Check heap available
Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

// Check NVS contents
prefs.begin("stike", true);
Serial.printf("Saved tasks: %u\n", prefs.getUInt("taskCount", 0));
prefs.end();
```

---

### 6. Code Templates

Copy these complete templates into your project.

#### Template: Add Task with Custom Name

This template adds a task with a custom name entered via serial (for testing):

```cpp
// Add to main.cpp - call from handleActiveState()
// or create a new function to call

void addTaskWithInput(const char* inputName) {
    if (taskCount < MAX_TASKS) {
        tasks[taskCount] = TaskItem(inputName, false, millis());
        taskCount++;
        saveTasks();
        LOG_PRINTF("[Task] Added: %s\n", inputName);
    }
}

// Example usage:
addTaskWithInput("My Custom Task Name");
```

#### Template: Add New Key Handler

This template shows the complete pattern for adding a new key:

```cpp
// ===== STEP 1: Add to state_types.h =====
// In enum SystemEventType, add:
// TASK_RENAME,

// ===== STEP 2: Add to main.cpp keyboardTask() =====
// In the switch statement under keyboardTask():
case 'r':  // Press 'r' to rename
case 'R':
    sendSystemEvent(SystemEventType::TASK_RENAME, selectedTaskIndex);
    break;

// ===== STEP 3: Add handler in main.cpp handleActiveState() =====
// In the switch statement under handleActiveState():
case SystemEventType::TASK_RENAME:
    if (event.param >= 0 && event.param < taskCount) {
        // Example: rename selected task
        char newName[32] = "Renamed Task";
        strncpy(tasks[event.param].title, newName, 31);
        tasks[event.param].title[31] = '\0';
        saveTasks();
        LOG_PRINTF("[Task] Renamed task %d\n", event.param);
    }
    break;
```

#### Template: Draw Custom Element (Button)

This template draws a clickable button on the TFT:

```cpp
// Add to display_mgr.cpp - call from drawActiveGUI()

void drawButton(TFT_eSprite* s, int x, int y, int w, int h, 
              const char* label, bool selected) {
    if (selected) {
        // Selected state: filled blue
        s->fillRoundRect(x, y, w, h, 4, TFT_BLUE);
        s->setTextColor(TFT_WHITE);
    } else {
        // Normal state: outline gray
        s->fillRoundRect(x, y, w, h, 4, TFT_DARKGREY);
        s->setTextColor(TFT_WHITE);
    }
    
    // Draw label centered
    int16_t textW = s->textWidth(label);
    int16_t textH = 16;  // Approximate height
    int textX = x + (w - textW) / 2;
    int textY = y + (h - textH) / 2;
    s->setCursor(textX, textY);
    s->print(label);
}

// Usage example (inside drawActiveGUI):
drawButton(guiSprite, 40, 50, 80, 20, "DELETE", selectedIndex == -1);
```

#### Template: Draw Complete Screen

This template shows how to draw a complete screen with multiple elements:

```cpp
// Complete screen template - add to display_mgr.cpp

void drawScreen(TFT_eSprite* s, const char* title, 
              const char* lines[], uint8_t lineCount,
              int selectedIndex) {
    // 1. Clear screen
    s->fillScreen(TFT_BLACK);
    
    // 2. Draw header
    s->fillRect(0, 0, s->width(), 20, TFT_BLUE);
    s->setTextColor(TFT_WHITE);
    s->setCursor(5, 5);
    s->print(title);
    
    // 3. Draw lines/items
    s->setTextColor(TFT_WHITE);
    for (uint8_t i = 0; i < lineCount; i++) {
        int y = 25 + i * 15;
        
        if (i == selectedIndex) {
            s->fillRect(0, y - 2, s->width(), 14, TFT_BLUE);
            s->setTextColor(TFT_WHITE);
        } else {
            s->setTextColor(TFT_LIGHTGREY);
        }
        
        s->setCursor(5, y);
        s->print(lines[i]);
    }
    
    // 4. Draw status bar
    s->fillRect(0, s->height() - 12, s->width(), 12, TFT_DARKGREY);
    s->setTextColor(TFT_WHITE);
    s->setCursor(5, s->height() - 10);
    s->print("Ready");
    
    // 5. Push to screen
    s->pushSprite(0, 0);
}

// Usage example:
const char* myLines[] = {"Task 1", "Task 2", "Task 3"};
drawScreen(guiSprite, "=== StikE ===", myLines, 3, selectedTaskIndex);
```

---

### 7. Troubleshooting

Quick fixes for common issues.

#### Issue: TFT Screen Shows Nothing

| Check | Fix |
|-------|-----|
| Is TFT powered on? | Call `displayMgr.turnOnTFT()` before drawing |
| Is backlight enabled? | Ensure `digitalWrite(Pins::LCD_BL, HIGH)` is called |
| Is sprite created? | Check `guiSprite != nullptr` after init |
| Are colors visible? | Try `TFT_WHITE` on `TFT_BLACK` background |
| Is MISO pin set? | Set `-D TFT_MISO=-1` in platformio.ini if display has no MISO |

**Quick test**: Add this to `setup()`:
```cpp
tft.fillScreen(TFT_RED);
delay(1000);
tft.fillScreen(TFT_GREEN);
delay(1000);
tft.fillScreen(TFT_BLUE);
```

#### Issue: Display Has No MISO Pin

Some TFT displays (like ST7735S) don't have an MISO pin. This causes display issues.

| Check | Fix |
|-------|-----|
| Is MISO defined in platformio.ini? | Set `-D TFT_MISO=-1` |
| Is LCD_MISO set in pins.h? | Set to `0xFF` or remove |

**Fix in platformio.ini**:
```ini
build_flags =
    ; ... other flags ...
    -D TFT_MISO=-1    ; Set to -1 if display has no MISO pin
```

**Fix in include/pins.h**:
```cpp
constexpr uint8_t LCD_MISO = 0xFF;  // Not Connected
```

#### Issue: Tasks Not Saving to NVS

| Check | Fix |
|-------|-----|
| Is `saveTasks()` called? | Call after every task modification |
| Is NVS initialized? | Call `prefs.begin("stike", false)` before write |
| Is taskCount saved first? | Save count before individual tasks |

**Debug**: Check saved data:
```cpp
prefs.begin("stike", true);
Serial.printf("Saved: %u\n", prefs.getUInt("taskCount", 0));
prefs.end();
```

#### Issue: Keyboard Not Responding

| Check | Fix |
|-------|-----|
| Is I2C initialized? | Call `keyboardMgr.init()` in setup() |
| Is queue created? | Check `systemEventQueue != nullptr` |
| Is task running? | Verify no error in keyboardTask creation |

**Quick test**: Add to loop():
```cpp
char key = keyboardMgr.getKeyPress();
if (key) Serial.printf("Key: %c\n", key);
```

#### Issue: ePaper Not Updating

| Check | Fix |
|-------|-----|
| Is HSPI initialized? | Call `epd.init()` before each update |
| Is powerOff() called? | Use `epd.powerOff()` not `hibernate()` |
| Is BUSY pin correct? | Verify GPIO 8 in `pins.h` |

**Debug**: Check ePaper connection:
```cpp
Serial.printf("BUSY: %d\n", digitalRead(Pins::EP_BUSY));
```

#### Issue: Compiler Errors

| Error | Fix |
|-------|-----|
| `queue.h not found` | Add `#include <freertos/queue.h>` to main.cpp |
| `xQueueSendFromISR not declared` | Include FreeRTOS headers |
| `Preferences not found` | Add `#include <Preferences.h>` |
| `TFT_eSprite undeclared` | Add `#include <TFT_eSPI.h>` |

#### Issue: Keys Not Working

| Check | Fix |
|-------|-----|
| Is key mapped? | Add case in `keyboardTask()` switch |
| Is event handled? | Add case in `handleActiveState()` switch |
| Is queue working? | Add debug print when event received |

**Debug**: Add to handleActiveState():
```cpp
LOG_PRINTF("[DEBUG] Event type: %d, param: %d\n", event.type, event.param);
```

---

## End of Developer Guide

- BLE keyboard support for wireless input
- Battery management with LiPo charger
- Physical button for forced wakeup
- NVS-backed task persistence across reboots
- WiFi sync with remote task server
- Custom PCB design integrating all components