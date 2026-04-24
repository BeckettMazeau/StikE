# `include/power_mgr.h`

This header file exposes the power management configuration for the StikE system.

## Key Components

- **`extern bool isLowPowerMode`**: A global flag indicating whether the system is currently in low-power mode. Power optimization involves dynamically adjusting the ESP32 CPU frequency, disabling unused radios (WiFi/BT), and increasing FreeRTOS task polling delays.
- **`void setLowPowerMode(bool enable)`**: A function prototype to toggle the low power mode state.
