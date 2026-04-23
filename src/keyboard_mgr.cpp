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
    Serial.printf("[KeyboardMgr] I2C initialized on SDA=%d, SCL=%d\n", Pins::KEY_SDA, Pins::KEY_SCL);
}

bool KeyboardManager::isAvailable() {
    return initialized;
}

char KeyboardManager::getKeyPress() {
    if (!initialized) {
        return 0;
    }

    // --- LOG SPAM BACKOFF START ---
    static unsigned long lastFailTime = 0;
    if (millis() - lastFailTime < 1000) {
        return 0; 
    }
    // --- LOG SPAM BACKOFF END ---

    // Capture return value: 0 indicates hardware failure/timeout
    uint8_t bytesRead = Wire.requestFrom((uint16_t)I2C_ADDR, (uint8_t)1);
    
    if (bytesRead > 0 && Wire.available()) {
        char c = Wire.read();
        if (c != 0 && c != lastKey) {
            lastKey = c;
            return c;
        }
    } else {
        // Read failed: set backoff timer to silence ESP-IDF error spam
        lastFailTime = millis();
    }

    lastKey = 0;
    delay(10);
    return 0;
}

void KeyboardManager::scanBus() {
    Serial.println("[KeyboardMgr] Starting I2C bus scan...");
    int nDevices = 0;
    for (byte address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();

        if (error == 0) {
            Serial.printf("[KeyboardMgr] Device found at address 0x%02X\n", address);
            nDevices++;
        }
    }
    if (nDevices == 0) Serial.println("[KeyboardMgr] No I2C devices found.");
    else Serial.printf("[KeyboardMgr] Scan complete. Found %d device(s).\n", nDevices);
}