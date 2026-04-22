#pragma once

#include <Wire.h>
#include <cstdint>

class KeyboardManager {
public:
    KeyboardManager();

    void init();

    char getKeyPress();
    bool isAvailable();

    static constexpr uint8_t I2C_ADDR = 0x08;

private:
    bool initialized;
    char lastKey;
};