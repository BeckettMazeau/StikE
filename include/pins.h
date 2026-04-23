#pragma once

#include <cstdint>

namespace Pins {

// OPI PSRAM pins (35, 36, 37) are reserved for 8MB Octal PSRAM - NOT available for external use
// RGB LED on GPIO 38, 48 | USB D-/D+ on GPIO 19, 20 | Strapping pins 0, 3, 45, 46

// ePaper (GDEQ0213B74) - Hardware SPI via global SPI object (FSPI, SPI2 on S3)
// SCK=5, MOSI=6 match DualDisplayTest reference wiring — confirm against your hardware
constexpr uint8_t EP_SCK  = 5;
constexpr uint8_t EP_MOSI = 6;
constexpr uint8_t EP_CS   = 7;
constexpr uint8_t EP_DC   = 16;
constexpr uint8_t EP_RST  = 15;
constexpr uint8_t EP_BUSY = 4;

// TFT (ST7735S) - Hardware VSPI (via TFT_eSPI/User_Setup.h)
// SCK=12, MOSI=11, CS=10, DC=9, RST=13
constexpr uint8_t LCD_SCK   = 12;
constexpr uint8_t LCD_MOSI  = 11;
constexpr uint8_t LCD_MISO  = 0xFF;
constexpr uint8_t LCD_CS    = 10;
constexpr uint8_t LCD_DC    = 9;
constexpr uint8_t LCD_RST   = 13;
constexpr uint8_t LCD_BL    = 42;

// Keyboard (I2C) - moved SCL to GPIO 21 to avoid ePaper DC conflict
constexpr uint8_t KEY_SDA = 18;
constexpr uint8_t KEY_SCL = 21;

// Wake Button - moved to GPIO 14 to avoid ePaper RST=15 conflict
constexpr uint8_t WAKE_BTN = 14;

}