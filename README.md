# STIKE PROJECT DOCUMENTATION

## PROJECT OVERVIEW

StikE is a dual-display task and calendar management system built on the ESP32-S3 architecture. It provides an interactive user interface on a TFT display during active use and transitions to a low-power ePaper display for persistent information viewing during sleep cycles. The system features a full physical keyboard for input and utilizes non-volatile storage to ensure data persistence across power cycles.

## CORE LOGIC FLOW

The application logic follows a strictly ordered initialization sequence to prevent hardware bus contention. Upon startup, the system first initializes the ePaper display and its associated SPI bus. Once the ePaper is in a stable state and hibernated, the system initializes the high-speed SPI bus for the TFT display. This sequential approach is critical due to the shared resource nature of the ESP32-S3 internal peripheral routing.

The system operates using a dual-core FreeRTOS architecture. Core 0 is dedicated to the keyboard management task, which continuously polls the I2C bus for user input. When a key press is detected, it is translated into a system event and sent to a prioritized event queue. Core 1 runs the main application loop, which processes these events, updates the system state machine, and renders the appropriate UI to the displays.

## FILE STRUCTURE AND FUNCTIONS

### main.cpp
This is the entry point of the firmware. It handles the primary system setup, the FreeRTOS task creation, and the main state machine logic.
- Setup: Initializes serial communication, display hardware, keyboard drivers, and restores saved data from non-volatile storage.
- Loop: Monitors the system event queue and dispatches events to the active state handler.
- Keyboard Task: A background task that reads I2C keyboard data and generates system events.
- State Handlers: Individual functions that manage logic for different screens such as the task list, calendar views, and add/edit menus.
- Persistence Logic: Functions to save and load task and calendar data using the Preferences library.

### display_mgr.cpp
This module manages the abstraction layer for the dual displays. It handles low-level SPI transactions and high-level UI rendering.
- Initialization: Manages the complex power-on and bus-sequencing logic for both TFT and ePaper displays.
- Active GUI Rendering: Implements a sprite-based rendering pipeline for the TFT display. It uses a software buffer to prepare the full screen before pushing it to hardware, which eliminates flickering.
- ePaper View Preparation: Logic that filters and formats active tasks and upcoming calendar events into discrete screens for the sleep display.
- Sleep Display Logic: Manages the partial and full updates of the ePaper display, including the logic to cycle through multiple screens of information while the device is in a low-power state.

### keyboard_mgr.cpp
This file implements the driver for the I2C-based keyboard.
- Initialization: Configures the I2C bus and verifies hardware presence.
- Key Parsing: Translates raw I2C data into ASCII characters and handles special function key combinations like Fn-shortcuts for navigation.

### systems_test.cpp
A diagnostic suite used for hardware validation and debugging.
- Hardware Tests: Functions to verify the integrity of the SPI buses, TFT color rendering, and ePaper update cycles.
- Interactive Diagnostics: A serial-command-based interface to trigger specific hardware states and display patterns.

## LIBRARIES AND THEIR ROLES

- TFT_eSPI: Provides the high-performance driver for the ST7735S TFT display. It is used primarily for its sprite support, which allows for smooth UI transitions.
- GxEPD2: A robust driver for the ePaper display. It manages the complex waveform timings and paged drawing required for E-Ink technology.
- Adafruit GFX: A core graphics library that provides the geometric primitives and text rendering capabilities used by both display drivers.
- Preferences: An ESP32 library used for accessing the Non-Volatile Storage (NVS). This is used to persist tasks and calendar events so they remain available after the device is turned off.

## STATE MANAGEMENT

The system uses a centralized state machine to manage the user interface. The active state determines how the system responds to keyboard events and what is rendered on the TFT.
- Active States: Includes the Task List, Calendar (Month/Week/Day views), Add/Edit Task menus, and Help screens.
- Sleep State: Triggered after a period of inactivity or via a manual shortcut. In this state, the TFT and its backlight are powered down, and control is handed over to the ePaper update logic.

## DISPLAY HANDLING

### Active Mode
In active mode, the TFT display is the primary output. To ensure a premium feel, the system uses a double-buffering technique via the sprite library. The UI is drawn entirely in internal memory and then copied to the display in a single operation.

### Sleep Mode
When the system enters sleep mode, it calculates the most relevant information to display on the ePaper. This includes incomplete tasks and calendar events scheduled within the next twenty-four hours. Because the ePaper display has a limited vertical resolution, the system automatically partitions this information into multiple "views." While the device is "asleep," it periodically wakes to cycle the ePaper to the next view, ensuring that all information is eventually displayed without user intervention.

## DATA PERSISTENCE

Data management is handled through a fixed-size array structure to prevent memory fragmentation in long-running sessions. When a task is added, edited, or completed, the system immediately serializes the entire data structure to the NVS. This ensures that even in the event of an unexpected power loss, the user's data remains intact. Calendar events can be optionally linked to tasks, allowing for automatic synchronization between the two views.
