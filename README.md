# StikE

## Software Build Instructions

Follow these steps to compile the firmware to your ESP32-S3 DevKitC-1 device using PlatformIO.

1. Install Python 3.
2. Install the PlatformIO Core CLI:
   ```bash
   python3 -m pip install -U platformio
   ```
3. Copy the custom board configuration file to the PlatformIO boards directory:
   ```bash
   mkdir -p ~/.platformio/boards && cp esp32-s3-devkitc-1-n16r8v.json ~/.platformio/boards/
   ```
4. Compile the project:
   ```bash
   python3 -m platformio run -e esp32-s3-devkitc-1-n16r8v
   ```

## Software Operation Instructions

The StikE application logic follows a strictly ordered dual-display initialization sequence: ePaper SPI initialized and hibernated first, then TFT high-speed SPI initialized. This sequence prevents severe hardware bus contention between the ePaper and TFT displays.

The system operates via a dual-core FreeRTOS architecture using an Event-Driven State Machine:
*   **Core 0:** Dedicated to asynchronous input. It continuously polls the I2C bus (address 0x5F) for user input from the M5Stack CardKB. Upon detecting a keystroke, it translates the raw byte into a system event and pushes it onto a FreeRTOS event queue.
*   **Core 1:** Runs the main application loop. It blocks on the event queue, processing events, updating the centralized system state, and triggering the UI rendering pipeline.

The active UI renders to the ST7735S TFT display utilizing software buffering capabilities to prevent flickering. During sleep cycles, the system transitions to an ultra-low-power ePaper display for persistent information viewing.

Task and calendar data are persisted across power cycles utilizing fixed-size arrays (`MAX_TASKS = 20`, `MAX_CALENDAR_EVENTS = 50`) stored via the ESP32 Preferences library (NVS) as binary blobs, strictly avoiding dynamic memory allocation.

## Hackster.io Link

[Insert Hackster.io Link Here]
