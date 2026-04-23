#pragma once

#include <cstdint>
#include <TFT_eSPI.h>
#include <GxEPD2_BW.h>
#include "state_types.h"
#include "pins.h"

extern SPIClass epd_spi;

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