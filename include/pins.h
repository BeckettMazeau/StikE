#pragma once

#include <cstdint>

namespace Pins {

// OPI PSRAM pins (35, 36, 37) are reserved for 8MB Octal PSRAM - NOT available for external use
// RGB LED on GPIO 38, 48 | USB D-/D+ on GPIO 19, 20 | Strapping pins 0, 3, 45, 46

constexpr uint8_t EP_CS   = 4;
constexpr uint8_t EP_DC   = 5;
constexpr uint8_t EP_RST  = 6;
constexpr uint8_t EP_BUSY = 8;
constexpr uint8_t EP_SCK  = 17;
constexpr uint8_t EP_MOSI = 21;

constexpr uint8_t LCD_SCK   = 13;
constexpr uint8_t LCD_MOSI  = 11;
constexpr uint8_t LCD_MISO  = 12;
constexpr uint8_t LCD_CS    = 10;
constexpr uint8_t LCD_DC    = 9;
constexpr uint8_t LCD_RST   = 14;
constexpr uint8_t LCD_BL    = 42;  // Reassigned from GPIO 8 to avoid conflict with EP_BUSY

constexpr uint8_t KEY_SDA = 1;
constexpr uint8_t KEY_SCL = 2;

constexpr uint8_t WAKE_BTN = 15;

}