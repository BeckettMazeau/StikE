# `include/pins.h`

This header file defines all the hardware GPIO pin mappings for the ESP32-S3 DevKitC board used in the StikE project. It centralizes pin configuration to make hardware adjustments easier.

## Key Components

The definitions are organized within the `Pins` namespace.

**General Notes:**
- OPI PSRAM pins (35, 36, 37) are reserved.
- RGB LED is on GPIO 38, 48.
- USB D-/D+ are on GPIO 19, 20.
- Strapping pins are 0, 3, 45, 46.

**ePaper (GDEQ0213B74) Pins:**
Uses the global SPI object (FSPI, SPI2 on S3).
- `EP_SCK = 5`
- `EP_MOSI = 6`
- `EP_CS = 7`
- `EP_DC = 16`
- `EP_RST = 15`
- `EP_BUSY = 4`

**TFT (ST7735S) Pins:**
Uses Hardware VSPI (via `TFT_eSPI` user setup).
- `LCD_SCK = 12`
- `LCD_MOSI = 11`
- `LCD_MISO = 0xFF`
- `LCD_CS = 10`
- `LCD_DC = 9`
- `LCD_RST = 13`
- `LCD_BL = 42`: TFT Backlight control. Note: To completely turn off the TFT backlight and override the ESP32 PWM timer, this pin must be explicitly configured as an OUTPUT and pulled to GND using `digitalWrite(LOW)`.

**Keyboard (I2C) Pins:**
- `KEY_SDA = 18`
- `KEY_SCL = 21` (Moved to 21 to avoid conflict with ePaper DC).

**Wake Button Pin:**
- `WAKE_BTN = 14` (Moved to 14 to avoid conflict with ePaper RST).

## Dependencies
- `<cstdint>`: Standard integer types.
