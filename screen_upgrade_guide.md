# Upgrading ST7735S (128x160) to ST7789v (240x320)

To maintain the UI look and take advantage of the new 2.8" 240x320 screen, follow these steps to update the firmware:

## 1. Update `platformio.ini` Driver & Resolution
Change the driver, resolution, and remove the specific ST7735 flags:

**Find and modify the TFT_eSPI Configuration:**
```ini
	; --- TFT_eSPI Configuration ---
	-D ST7789_DRIVER=1     ; Changed from ST7735_DRIVER=1
	; Remove: -D ST7735_GREENTAB2
	-D TFT_RGB_ORDER=TFT_RGB
	-D TFT_WIDTH=240       ; Changed from 128
	-D TFT_HEIGHT=320      ; Changed from 160
```

## 2. Scale UI Layout Constants in `src/display_mgr.cpp`
The UI layout relies on defined constants. Since both dimensions are essentially doubling, you should scale these by roughly 2x for consistent proportions.

**Current:**
```cpp
constexpr int HEADER_H = 14;
constexpr int FOOTER_H = 19;
constexpr int LINE_H   = 12;
```

**Proposed:**
```cpp
constexpr int HEADER_H = 28;
constexpr int FOOTER_H = 38;
constexpr int LINE_H   = 24;
```

## 3. Scale Fonts
The project currently loads size 12 fonts (`Outfit-Medium-12.vlw` or `Inter-Regular-12.vlw`) if they exist. With double the resolution, a 12pt font will look half the size.
- You should generate and upload size 24 versions of these fonts (e.g., `Outfit-Medium-24.vlw`) to LittleFS.
- Update `src/display_mgr.cpp` in `DisplayManager::initBusesAndDisplays` and `DisplayManager::drawActiveGUI` to look for and load the size 24 fonts instead.
- If using `tft.setTextSize(1)`, consider using `tft.setTextSize(2)` where the default text size is used.
- Update character width calculations. For example, `int maxVisible = (W - 14) / 6;` assumes a 6px glyph width. For a scaled-up font, this would be roughly `12px` or queried dynamically if supported.

## 4. Fix Hardcoded Coordinates in `src/display_mgr.cpp`
There are numerous hardcoded coordinates that assume a 128px width screen.

*   **Cursors targeting the right edge:** There are hardcoded `128` values, such as `guiSprite->setCursor(128, curY + 2)`. Change these to use the dynamically obtained width (`W`), e.g., `guiSprite->setCursor(W, curY + 2)` or scale them to `240`.
*   **Field boxes (`fillRect` calls) in Add/Edit menus:**
    The `drawAddViewGUI` and `drawEditViewGUI` functions contain hardcoded coordinates and dimensions for text fields:
    ```cpp
    guiSprite->fillRect(4, curY, W - 8, 12, ...);
    guiSprite->fillRect(4, curY, 40, 12, ...);
    guiSprite->fillRect(30, curY, 20, 12, ...);
    guiSprite->fillRect(56, curY, 40, 12, ...);
    guiSprite->fillRect(100, curY, 20, 12, ...);
    guiSprite->fillRect(126, curY, 20, 12, ...);
    ```
    These X coordinates, widths, and heights (`12`) need to be approximately doubled to match the new 240px width. For example:
    ```cpp
    guiSprite->fillRect(8, curY, W - 16, 24, ...);
    guiSprite->fillRect(8, curY, 80, 24, ...);
    guiSprite->fillRect(60, curY, 40, 24, ...);
    guiSprite->fillRect(112, curY, 80, 24, ...);
    guiSprite->fillRect(200, curY, 40, 24, ...);
    // ... Adjust to fit within the 240px width
    ```
*   **Calendar View coordinates:**
    Y offsets for text lines in the calendar view are hardcoded:
    ```cpp
    guiSprite->setCursor(14, 20);
    guiSprite->setCursor(14, 34);
    guiSprite->setCursor(14, 48);
    guiSprite->setCursor(14, 62);
    guiSprite->setCursor(14, 76);
    ```
    Scale these Y coordinates (and X offsets) proportionally for the 320px height, using the new `LINE_H`.

By updating these layout constants, driver flags, and hardcoded dimensions, the firmware will gracefully scale to use the new ST7789 2.8" screen while preserving the dual-display functionality without SPI collisions.
