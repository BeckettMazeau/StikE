# StikE Firmware

ESP32-S3 task manager with TFT + ePaper displays.

## Build & Upload

```bash
pio run -t upload
pio device monitor
```

## Architecture

- **Dual display**: TFT (ST7735S, active mode) + ePaper (GDEQ0213B74, sleep mode)
- **Input**: I2C keyboard on GPIO 18 (SDA), 16 (SCL)
- **Storage**: NVS via Preferences library
- **RTOS**: FreeRTOS with keyboard task pinned to core 0

## State Machine

```
STATE_ACTIVE <-> STATE_SLEEP
```
- ACTIVE: TFT on, serial logging enabled
- SLEEP: TFT off, ePaper cycling, light sleep with timer/GPIO wake

## Hardware Notes

- OPI PSRAM uses GPIO 35-37 (not available)
- Wake button on GPIO 15
- TFT backlight reassigned to GPIO 42 to avoid conflict with ePaper busy

## Key Files

- `src/main.cpp` - entry point, state machine, task management
- `include/pins.h` - pin definitions
- `include/display_mgr.h` - dual display driver
- `include/keyboard_mgr.h` - key input handling