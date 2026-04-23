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

    void initTFT();
    void initEpaper();

    void turnOnTFT();
    void turnOffTFT();

    void updateEpaperPartial(int viewIndex);
    void prepareEpaperViews(const TaskItem tasks[], uint32_t taskCount);

    void drawActiveGUI(const TaskItem tasks[], uint32_t taskCount, int selectedIndex);
    void drawAddViewGUI(const char* currentInput);
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

    TFT_eSPI& getTFT() { return tft; }
    bool isTFTOn() const { return tftOn; }

private:
    TFT_eSPI tft;
    TFT_eSprite* guiSprite;
    GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> epd;

    bool tftOn;
    TaskItem epaperViews[EPAPER_VIEW_COUNT];
    uint32_t epaperViewCount;

    void drawEpaperView(int index);
};
