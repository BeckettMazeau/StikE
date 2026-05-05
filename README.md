# StikE

---

## Project Overview

StikE is a dual-display task and calendar management system built on the ESP32-S3 architecture. It provides an interactive user interface on a TFT display during active use and transitions to a low-power ePaper display for persistent information viewing during sleep cycles. The system features a full physical keyboard for input and utilizes non-volatile storage to ensure data persistence across power cycles.

---

## System Architecture & Logic

The application logic follows a strictly ordered initialization sequence to prevent hardware bus contention. Upon startup, the system first initializes the ePaper display and its associated SPI bus. Once the ePaper is in a stable state and hibernated, the system initializes the high-speed SPI bus for the TFT display. This sequential approach is critical due to the shared resource nature of the ESP32-S3 internal peripheral routing.

The system operates using a dual-core FreeRTOS architecture. Core 0 is dedicated to the keyboard management task, which continuously polls the I2C bus for user input. When a key press is detected, it is translated into a system event and sent to a prioritized event queue. Core 1 runs the main application loop, which processes these events, updates the system state machine, and renders the appropriate UI to the displays.

---

## Getting Started

Follow these steps to compile and upload the firmware to your ESP32-S3 DevKitC-1 device using PlatformIO.

### Installation

1. Install Python 3.
2. Install the PlatformIO Core CLI:
   ```bash
   python3 -m pip install -U platformio
   ```
3. Copy the custom board configuration file to the PlatformIO boards directory:
   ```bash
   mkdir -p ~/.platformio/boards && cp esp32-s3-devkitc-1-n16r8v.json ~/.platformio/boards/
   ```
4. Compile and upload the project (ensure your board is connected):
   ```bash
   python3 -m platformio run -t upload -e esp32-s3-devkitc-1-n16r8v
   ```

### Usage

Upon first boot, the system will initialize the displays and load default data if no prior configuration is found. You can navigate the UI using the connected I2C M5Stack CardKB keyboard.

- **Active Mode:** Interact with the task list, calendar, and settings menus on the TFT display.
- **Sleep Mode:** The TFT display turns off, and the ePaper display shows upcoming tasks and events.
- **Pin Mapping:** 
  | Component | Signal | GPIO | Bus |
  | :--- | :--- | :--- | :--- |
  | **ePaper** | SCK / MOSI | 5 / 6 | FSPI (SPI2) |
  | | CS / DC / RST | 7 / 16 / 15 | |
  | | BUSY | 4 | |
  | **TFT** | SCK / MOSI | 12 / 11 | HSPI (SPI3) |
  | | MISO / CS / DC | 8 / 10 / 9 | |
  | | RST / BL | 13 / 42 | |
  | **I2C Key** | SDA / SCL | 18 / 21 | I2C |
  | **Wake** | Button | 14 | GPIO |

---

## Key Features

- **Dual-Display Output:** Utilizes an ST7735S TFT display for active interaction and a GxEPD2 ePaper display for low-power persistent viewing.
- **Hardware Keyboard Support:** Integrates with an M5Stack CardKB via I2C for input and navigation.
- **Non-Volatile Storage:** Persists tasks, calendar events, and system settings across reboots using the ESP32 Preferences library.
- **Low Power Mode:** Offers a dynamically toggled low power mode that reduces CPU frequency and disables unused radios to extend battery life.
- **Hardware Diagnostics:** Includes a comprehensive systems test suite (`systems_test.cpp`) for validating display integrity and SPI bus functionality.
- **Pomodoro Timer:** Built-in timer for structured work sessions directly on the device.
