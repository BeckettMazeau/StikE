#include <Arduino.h>
#include "keyboard_mgr.h"
#include "pins.h"

KeyboardManager::KeyboardManager()
    : initialized(false), lastKey(0) {
}

void KeyboardManager::init() {
    Wire.begin(Pins::KEY_SDA, Pins::KEY_SCL);
    Wire.setClock(100000);
    initialized = true;
    Serial.println("[KeyboardMgr] I2C initialized on SDA=1, SCL=2");
}

bool KeyboardManager::isAvailable() {
    return initialized;
}

char KeyboardManager::getKeyPress() {
    if (!initialized) {
        return 0;
    }

    Wire.requestFrom(I2C_ADDR, static_cast<uint8_t>(1));
    if (Wire.available()) {
        char c = Wire.read();
        if (c != 0 && c != lastKey) {
            lastKey = c;
            return c;
        }
    }

    // No valid key pressed - reset lastKey to allow repeating characters
    lastKey = 0;
    delay(10);
    return 0;
}