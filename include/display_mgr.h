#pragma once

#include <cstdint>
#include <TFT_eSPI.h>
#include <GxEPD2_BW.h>
#include "state_types.h"
#include "pins.h"

class DisplayManager {
public:
    GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT>& getEPD() { return epd; }
public:
    DisplayManager();
    ~DisplayManager();

    static int getDaysInMonth(int year, int month);

    int offsetX = 0;
    int offsetY = 0;

    void initBusesAndDisplays();

    void turnOnTFT();
    void turnOffTFT();

    void updateEpaperPartial(int viewIndex);
    void prepareEpaperViews(const TaskItem tasks[], uint32_t taskCount,
                            const CalendarEvent events[], uint32_t eventCount,
                            uint16_t curYear, uint8_t curMonth, uint8_t curDay, uint8_t curHour);

    void drawActiveGUI(const TaskItem tasks[], const int filteredIndices[], uint32_t filteredCount, int selectedIndex, int topIndex = 0, int viewMode = 0);
    void drawAddViewGUI(const char* currentInput, int activeField, bool hasDue, int y, int m, int d, int h, int min);
    void drawEditViewGUI(const char* currentInput, int activeField, bool hasDue, int y, int m, int d, int h, int min);
    void drawQuickAddGUI(const char* currentInput);
    void drawAlignGUI();
    void drawCalendarGUI(CalendarView view, int year, int month, int day, const CalendarEvent events[], uint32_t eventCount, int selectedEventIdx);
    void drawEventDetailGUI(const CalendarEvent& event, int scrollOffset = 0);
    void drawHelpGUI(SystemState fromState);
    void drawAddEventGUI(const char* title, int hour, int duration, int activeField);
    void clearFullHardwareScreen();
    // Smoke test drawing path for diagnostics
    void drawSmokeTest();
    // Full-screen solid color diagnostic test to verify end-to-end render path
    void drawTestFullRed();
    // Additional diagnostic overlay to test end-to-end render path
    void drawTestOverlay();
    // Direct color frame test (bypass sprite) to verify TFT path
    void drawDirectColorFrame(uint16_t color);
    // Simple sprite test - tiny sprite to verify sprite path works
    void drawActiveGUISimpleTest();
    uint32_t getEpaperViewCount() const { return epaperViewCount; }

    TFT_eSPI& getTFT() { return tft; }
    bool isTFTOn() const { return tftOn; }

    void updateAnimations();
    bool isAnimating() const;

    void pushDirtySprite(int x, int y);
    void forceFullRedraw();

private:
    TFT_eSPI tft;
    TFT_eSprite* guiSprite;
    GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> epd;

    bool tftOn;
    EpaperViewItem epaperViews[EPAPER_VIEW_COUNT];
    uint32_t epaperViewCount;

    float currentSelectionY;
    float targetSelectionY;

    uint32_t rowHashes[160]; // Max height in any rotation
    bool fullRedrawPending;

    void drawEpaperView(int index);
};
