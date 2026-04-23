# StikE Firmware

ESP32-S3 task manager with TFT + ePaper displays.

## Build & Upload

```bash
pio run -t upload
pio device monitor
```

## Architecture

- **Dual display**: TFT (ST7735S, active mode) + ePaper (GDEQ0213B74, sleep mode)
- **Input**: I2C keyboard on GPIO 18 (SDA), 21 (SCL)
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
- Wake button on GPIO 14 (moved to avoid ePaper RST conflict)
- TFT backlight on GPIO 42
- TFT uses VSPI (SCK=12, MOSI=11, CS=10)
- ePaper uses Software SPI (bit-banged)

## Pin Configuration (see include/pins.h)

| Component | Pins |
|-----------|------|
| TFT (ST7735S) | SCK=12, MOSI=11, CS=10, DC=9, RST=13, BL=42 |
| ePaper (GDEQ0213B74) | CS=7, DC=16, RST=15, BUSY=4 (Software SPI) |
| Keyboard (I2C) | SDA=18, SCL=21 |
| Wake Button | GPIO 14 |

## Key Files

- `src/main.cpp` - entry point, state machine, task management
- `include/pins.h` - pin definitions
- `include/display_mgr.h` - dual display driver
- `include/keyboard_mgr.h` - key input handling