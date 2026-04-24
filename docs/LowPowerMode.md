# StikE Low Power Mode

## Overview
To improve battery life, StikE provides a low power mode that can be dynamically toggled. When activated, the system trades responsiveness and compute capability for power efficiency.

## Activation
The low power mode can be integrated into the settings page (currently under development) or activated programmatically using the functions exposed in `include/power_mgr.h`.

### Example Integration

```cpp
#include "power_mgr.h"

// To enable Low Power Mode:
setLowPowerMode(true);

// To restore Standard Performance Mode:
setLowPowerMode(false);
```

## What it does
1. **CPU Frequency Scaling:**
   Reduces the ESP32-S3 CPU frequency from the standard 240MHz down to 80MHz. 80MHz is the lowest stable frequency on the ESP32 that maintains functional APB bus speeds without severely disrupting peripherals.

2. **Reduced Keyboard Polling:**
   The `keyboardTask` (running on Core 0), which continuously polls the I2C bus for user input, reduces its polling frequency from every 10ms to every 50ms. This decreases I2C activity and Core 0 awakenings.

3. **Reduced UI Loop Execution:**
   The main UI `loop()` (running on Core 1) decreases its event processing frequency by increasing the main loop delay from 50ms to 100ms.

4. **Radio Disabling:**
   Unused radios (WiFi and Bluetooth) are explicitly disabled in the firmware initialization routine to drastically reduce the baseline power consumption of the ESP32.
