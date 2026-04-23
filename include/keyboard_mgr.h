#pragma once

#include <Wire.h>
#include <cstdint>

class KeyboardManager {
public:
    KeyboardManager();

    void init();

    char getKeyPress();
    bool isAvailable();

    // Corrected address for M5Stack CardKB 1.1
    static constexpr uint8_t I2C_ADDR = 0x5F;

    void scanBus();

private:
    bool initialized;
    char lastKey;
};