# StikE Architecture Document

## 1. High-Level Overview

StikE is an embedded dual-display task and calendar management system built on the ESP32-S3 architecture. It provides an interactive user interface on a color TFT display during active use and transitions to an ultra-low-power ePaper display for persistent information viewing during sleep cycles. The system features a full physical I2C keyboard for user input and utilizes non-volatile storage (NVS) to ensure data persistence across power cycles.

The core value proposition of StikE is providing a dedicated, distraction-free productivity hardware device that balances an active, responsive interface with long-term, low-power information persistence.

## 2. Tech Stack & Dependencies

### Hardware Stack
*   **Microcontroller**: ESP32-S3 DevKitC (Dual-core Xtensa LX7)
*   **Active Display**: ST7735S TFT (128x160 resolution), driven over HSPI (SPI3).
*   **Persistent Display**: GDEQ0213B74 ePaper (B74 controller), driven over FSPI (SPI2).
*   **Input**: M5Stack CardKB (I2C interface).

### Software Stack & Libraries
*   **Framework**: Arduino Framework (via PlatformIO).
*   **RTOS**: FreeRTOS (Native to ESP-IDF/Arduino core for ESP32).
*   **`TFT_eSPI`**: High-performance driver for the TFT display, utilized heavily for its `TFT_eSprite` software buffering capabilities to prevent UI flickering.
*   **`GxEPD2`**: Robust driver managing the complex waveform timings for the E-Ink display.
*   **`Adafruit GFX Library`**: Core graphics primitives and text rendering.
*   **`Preferences`**: ESP32 NVS wrapper for persistent storage of binary blobs (Tasks and Events).

## 3. System Architecture

The architecture relies on an **Event-Driven State Machine** running across a **Dual-Core FreeRTOS** setup, with a strict emphasis on **Hardware Isolation** to prevent peripheral contention.

### Core Isolation
*   **Core 0 (Asynchronous Input)**: Dedicated to the `KeyboardTask`. It continuously polls the I2C bus for user input. Upon detecting a keystroke, it translates the raw byte into a `SystemEvent` and pushes it onto a FreeRTOS event queue.
*   **Core 1 (Main Event Loop & UI)**: Runs the primary application `loop()`. It blocks on the event queue, processing events, updating the centralized `SystemState`, and triggering the rendering pipeline in `DisplayManager`.

### Display Sequencing (The SPI Contention Problem)
Because the ESP32-S3 GPIO matrix handles internal peripheral routing, sharing SPI buses or initializing them concurrently causes severe hardware-level contention between the ePaper and TFT displays. StikE solves this with strict sequential initialization:
1.  Initialize ePaper SPI (FSPI).
2.  Initialize the ePaper panel and push it to a `hibernate()` state.
3.  Initialize the high-speed TFT SPI (HSPI).
4.  Initialize the TFT panel.

### Data Management
To prevent heap fragmentation in a long-running embedded environment, dynamic memory allocation (`malloc`/`new`) is strictly avoided for core data models. Instead, fixed-size arrays (`MAX_TASKS = 20`, `MAX_CALENDAR_EVENTS = 50`) are used. Arrays are manipulated using block memory operations (e.g., `memmove` for insertions/deletions) and serialized directly to NVS as binary blobs.

## 4. Module/Directory Breakdown

*   `src/`: Contains the primary C++ implementation files.
    *   `main.cpp`: Entry point, RTOS task definitions, and the main state machine.
    *   `display_mgr.cpp`: UI rendering pipeline and hardware display abstraction.
    *   `keyboard_mgr.cpp`: I2C driver for the CardKB.
    *   `systems_test.cpp`: Diagnostic harness for hardware validation.
*   `include/`: Header files defining classes, structs, and hardware mappings.
    *   `state_types.h`: Core data models and enum definitions.
    *   `pins.h`: Centralized GPIO mappings.
    *   `power_mgr.h`: Power optimization flags.
*   `test/`: PlatformIO unit tests using the Unity framework.
*   `docs/`: Developer documentation and debug logs.

## 5. Detailed Technical Reference

### 5.1 `src/main.cpp`
**Purpose**: Orchestrates initialization, task creation, and the main event loop.

**Key Logic & Functions**:
*   `setup()`: Initializes NVS, calls `displayMgr.initBusesAndDisplays()`, `keyboardMgr.init()`, and spawns the `KeyboardTask` on Core 0.
*   `loop()`: The Core 1 event consumer. Switches on `SystemState` (e.g., `STATE_UI_LIST`, `STATE_UI_CALENDAR`) and calls the corresponding UI handler.
*   **Data Persistence Functions**: Code block responsible for saving/loading `CalendarEvent` and `TaskItem` arrays via the `Preferences` library.

**Developer Notes**:
*   *Gotcha*: When safely copying strings from NVS-backed structs, always use `snprintf(dest, sizeof(dest), "%.*s", (int)sizeof(src), src)` instead of `strncpy` to guarantee null-termination and prevent buffer over-reads.

### 5.2 `src/display_mgr.cpp` / `include/display_mgr.h`
**Purpose**: The abstraction layer for both displays. Handles all graphics drawing.

**Key Logic & Functions**:
*   `DisplayManager::initBusesAndDisplays()`: Enforces the strict ePaper-first, TFT-second initialization sequence.
*   `DisplayManager::drawActiveGUI(...)`: The primary rendering loop for the TFT. It allocates a `TFT_eSprite` buffer, draws the entire UI frame into memory, and then pushes it to the TFT hardware to ensure zero flicker.
*   `DisplayManager::prepareEpaperViews(...)`: Calculates which tasks/events need to be displayed during sleep mode and chunks them into `EpaperViewItem` structs to fit the limited vertical resolution of the ePaper.
*   `DisplayManager::updateEpaperPartial(...)`: Wakes the ePaper from hibernation, executes a partial screen update to display the next chunk of data, and hibernates it again.

**Developer Notes**:
*   *Gotcha*: Do not attempt to optimize array loops (like display buffers) by casting `uint16_t*` pointers to `uint32_t*` to process larger chunks. This causes Undefined Behavior due to strict aliasing and unaligned hardware access faults on the ESP32.
*   *Gotcha*: To completely turn off the TFT backlight (`Pins::LCD_BL`), the pin must be explicitly configured as an `OUTPUT` and pulled low using `digitalWrite(LOW)`. `analogWrite(0)` is insufficient due to PWM timer overrides.

### 5.3 `src/keyboard_mgr.cpp` / `include/keyboard_mgr.h`
**Purpose**: Interacts with the M5Stack CardKB over I2C.

**Key Logic & Functions**:
*   `KeyboardManager::getKeyPress()`: Polls I2C address `0x5F`. Returns a `char` or `0` if no key is pressed.

**Developer Notes**:
*   *Gotcha*: The CardKB does not use standard ASCII offset logic for its 'Fn' key modifier. It returns specific, hardcoded hex values (e.g., Fn + A = 0x9A).
*   *Gotcha*: I2C read failures trigger a backoff timer to prevent serial console spam from the ESP-IDF I2C driver.

### 5.4 Data Models (`include/state_types.h`)
**Purpose**: Defines the shape of data in the system.

**Key Structs**:
*   `SystemEvent`: Contains `SystemEventType type` and `int param` (usually the char pressed).
*   `TaskItem`: Trivially copyable struct containing title strings, completion booleans, and timestamp integers.
*   `CalendarEvent`: Trivially copyable struct for scheduled items.

**Developer Notes**:
*   *Gotcha*: Because `TaskItem` and `CalendarEvent` are trivially copyable, it is safe (and preferred) to use `memmove` for block shifting when deleting or inserting items into the `tasks` or `calendarEvents` arrays.

### 5.5 Input Management
**Developer Notes**:
*   *Gotcha*: The global `inputBuffer` is size `INPUT_BUFFER_SIZE` (128 bytes). It must be explicitly null-terminated at the current index after every character update in the UI event handlers to prevent buffer over-reads during rendering.

### 5.6 Power Management (`include/power_mgr.h`)
**Purpose**: Dictates power states.
**Developer Notes**:
*   The system uses `esp_light_sleep_start()` to yield execution. Ensure the ePaper is hibernated and the TFT backlight is explicitly pulled to GND before entering sleep to hit micro-amp current targets.
