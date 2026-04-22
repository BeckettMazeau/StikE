# REFERENCE DOCUMENTATION & API CHEAT SHEET
Use the following library documentation and API patterns to implement the scaffold accurately. 

## 1. ESP32-S3 Power Management & Sleep APIs
To handle the 10-second sleep cycles, use the native ESP-IDF sleep functions included in `<esp_sleep.h>`.
* **Deep Sleep vs Light Sleep**: Light sleep retains RAM and peripheral state. Deep sleep wipes RAM unless variables are marked with `RTC_DATA_ATTR`. Since you are building a scaffold, assume Light Sleep is preferred for a quick 10s cycle unless memory constraints dictate otherwise.
* **Timer Wakeup**: 
  ```cpp
  #include <esp_sleep.h>
  uint64_t WAKEUP_TIME_US = 10 * 1000000; // 10 seconds in microseconds
  esp_sleep_enable_timer_wakeup(WAKEUP_TIME_US);
  esp_light_sleep_start(); // or esp_deep_sleep_start();
``

## 2. Shared Pin Logic (CRITICAL)

Pin 8 is shared between the TFT Backlight and the ePaper BUSY signal. You must document or stub a hardware multiplexing approach in the code.

-   **When TFT is Active**: Pin 8 must be configured as an `OUTPUT` and written `HIGH`/`LOW` to control the backlight.
    
-   **When ePaper is Updating**: Pin 8 must be temporarily reconfigured as an `INPUT` so the GxEPD2 library can read the BUSY state.
    
-   **Pattern**:
    
    C++
    
    ```
    // Before ePaper update
    pinMode(8, INPUT); 
    // ... do ePaper update ...
    // After ePaper update (returning to active state)
    pinMode(8, OUTPUT);
    digitalWrite(8, HIGH); // Turn TFT backlight back on
    
    ```
    

## 3. GxEPD2 Library (ePaper)

The GxEPD2 library uses Adafruit_GFX for drawing but relies on a specific "paged drawing" loop to manage memory and send data to the display via SPI.

-   **Initialization**:
    
    C++
    
    ```
    #include <GxEPD2_BW.h>
    #include <Fonts/FreeSans9pt7b.h>
    // Template: GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT> display(GxEPD2_DRIVER_CLASS(CS, DC, RST, BUSY));
    GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> epd(GxEPD2_213_B74(4, 5, 6, 8));
    
    ```
    
-   **Paged Drawing Loop (Partial Update)**:
    
    C++
    
    ```
    epd.setPartialWindow(x, y, width, height); // Define update area
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);
        epd.setTextColor(GxEPD_BLACK);
        // ... Adafruit_GFX drawing commands here (e.g., epd.setCursor(), epd.print()) ...
    } while (epd.nextPage());
    
    ```
    
-   **Power Management**: Call `epd.hibernate();` immediately after the `do...while` loop finishes to cut power to the ePaper controller before entering ESP32 sleep.
    

## 4. TFT_eSPI Library (TFT Display)

Used for the active GUI. It is highly optimized for the ESP32.

-   **Initialization**:
    
    C++
    
    ```
    #include <TFT_eSPI.h>
    TFT_eSPI tft = TFT_eSPI(); 
    // Note: Pins are normally defined in User_Setup.h in PlatformIO, but the code should at least call:
    tft.init();
    tft.setRotation(1); // Landscape
    tft.fillScreen(TFT_BLACK);
    
    ```
    
-   **Power Management**: `tft.writecommand(ST7735_SLPIN);` puts the driver IC to sleep. Use `tft.writecommand(ST7735_SLPOUT);` to wake it. Do not forget to also pull the Backlight pin (8) LOW when putting the TFT to sleep.
    

## 5. Wire Library (M5Stack I2C Keyboard)

The M5Stack Cardputer/Faces keyboard operates as an I2C peripheral (default address `0x08`).

-   **Initialization**:
    
    C++
    
    ```
    #include <Wire.h>
    Wire.begin(1, 2); // SDA = 1, SCL = 2
    
    ```
    
-   **Reading a Key**:
    
    C++
    
    ```
    #define KEYBOARD_I2C_ADDR 0x08
    char getKeyPress() {
        Wire.requestFrom(KEYBOARD_I2C_ADDR, 1);
        if (Wire.available()) {
            char c = Wire.read();
            if (c != 0) return c; // Return valid character
        }
        return 0; // No key pressed
    }
    
    ```
    

## 6. Adafruit_GFX Drawing Basics (For Stubs)

Both `TFT_eSPI` and `GxEPD2` inherit from Adafruit_GFX (or replicate its API).

-   Coordinates: `(0,0)` is top-left.
    
-   Text: `setCursor(x,y)`, `setTextSize(n)`, `setTextColor(color)`, `print("text")`.
    
-   Shapes: `fillRect(x, y, w, h, color)`, `drawFastHLine(x, y, w, color)`.
