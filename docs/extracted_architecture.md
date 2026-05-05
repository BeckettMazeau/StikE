# Software Architecture Outline

## Overview
StikE is an ESP32-S3 based, dual-display (TFT + ePaper) productivity device built with the Arduino framework (PlatformIO). The architecture is driven by a FreeRTOS event loop responding to an I2C keyboard, managing state transitions between various UI modes (List, Calendar, Tasks, Settings, etc.).

## 1. Modules and Classes
- **DisplayManager**: Handles all rendering to both the TFT (ST7735S via TFT_eSPI) and ePaper (GxEPD2) displays. Manages UI sprites, layout, and ePaper partial/full refreshes.
- **KeyboardManager**: Interfaces with the M5Stack CardKB over I2C, scanning for key presses and buffering input.
- **Power Management (`power_mgr`)**: Controls `isLowPowerMode`, adjusting CPU frequency and sleeping displays/radios to conserve battery.
- **System State (`state_types.h`)**: Defines the core data structures (`TaskItem`, `CalendarEvent`, `SystemEvent`, `EpaperItem`) and the global state enum (`SystemState`).
- **Main Controller (`main.cpp`)**: The central FreeRTOS event loop. It polls the keyboard (Core 0), dispatches events to the main loop (Core 1), updates state, and triggers display rendering.
- **Time/NVS Utilities**: Non-hardware specific logic for parsing time and managing persistent storage (Preferences/NVS) of tasks and settings.

## 2. User Inputs
- **I2C Keyboard (M5Stack CardKB)**: Mapped to specific hex values. Captures alphanumeric characters, navigation keys (Up, Down, Left, Right), Select (Enter), Backspace, and Cancel (Esc).
- **Wake Button**: A hardware button (GPIO 14) to wake the device from deep sleep.

## 3. User Outputs
- **TFT Display (ST7735S)**: Active, full-color display for immediate interaction (menus, task editing, calendar view). Rendered using a sprite-based pipeline to avoid flickering.
- **ePaper Display (GDEQ0213B74)**: Low-power display for persistent information (tasks, events, pomodoro timer). Refreshed partially or fully based on state.

## 4. Software Handling Mechanisms & Data Flow
1. **Input Polling**: A FreeRTOS task on Core 0 (`keyboardTask`) continuously polls `KeyboardManager` for new keys.
2. **Event Dispatch**: Key presses are translated into `SystemEvent` structs and pushed to an event queue.
3. **State Update**: The main loop on Core 1 (`loop()`) processes events from the queue, updating `currentState` and modifying underlying data arrays (`tasks`, `calendarEvents`).
4. **Rendering**: Based on `currentState`, `DisplayManager` is called to draw the appropriate UI onto the TFT sprite buffer, which is then pushed to the hardware. ePaper updates are scheduled asynchronously.
5. **Persistence**: Changes to tasks or settings trigger NVS saves via the ESP32 Preferences library.

## 5. Build Instructions
1.  **Prerequisites**: Install Python 3 and PlatformIO (`python3 -m pip install -U platformio`).
2.  **Board Configuration**: Copy the custom board definition: `mkdir -p ~/.platformio/boards && cp esp32-s3-devkitc-1-n16r8v.json ~/.platformio/boards/`.
3.  **Compile**: Run `python3 -m platformio run -e esp32-s3-devkitc-1-n16r8v`.
4.  **Upload (if hardware attached)**: Run `python3 -m platformio run -t upload -e esp32-s3-devkitc-1-n16r8v`.
*(Note: Pure C++ logic testing can be done with `g++` and mock files without hardware dependencies).*

## 6. Operation Instructions
1.  **Power On**: The device initializes buses (ePaper first, then TFT) and loads saved data from NVS.
2.  **Navigation**: Use the keyboard arrow keys to move through lists/calendars. Press Enter to select.
3.  **Modes**: The UI starts in List view (`STATE_UI_LIST`). Use specific keys (e.g., 'c' for calendar, 'a' for add task, 'p' for pomodoro) to switch contexts.
4.  **Input**: When in text input modes (Add Task, Quick Add, Settings), type directly. The input buffer handles up to 128 characters safely.
5.  **Sleep**: The device automatically enters sleep mode (`STATE_SLEEP`) after a period of inactivity, turning off the TFT. Press the Wake Button or a key to resume.
