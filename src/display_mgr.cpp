#include <Arduino.h>
#include <esp_sleep.h>
#include "display_mgr.h"
#include "state_types.h"

constexpr int MISO_DUMMY = 35;

// Dedicated HSPI bus for ePaper (separate from TFT's VSPI)
SPIClass epd_spi(HSPI);

// SPI settings for ePaper
SPISettings epd_spi_settings(2000000, MSBFIRST, SPI_MODE0);

// ePaper display instance using B74 type
GxEPD2_213_B74 epd_instance(Pins::EP_CS, Pins::EP_DC, Pins::EP_RST, Pins::EP_BUSY);

DisplayManager::DisplayManager()
    : tft()
    , guiSprite(nullptr)
    , epd(epd_instance)
    , tftOn(false)
    , epaperViewCount(0) {
}

DisplayManager::~DisplayManager() {
    if (guiSprite) {
        delete guiSprite;
        guiSprite = nullptr;
    }
}

void DisplayManager::initTFT() {
    Serial.println("[DisplayMgr] initTFT: Starting TFT_eSPI initialization");
    
    // Let TFT_eSPI handle the SPI bus initialization entirely via User_Setup.h
    tft.init();
    tft.setRotation(1);
    tftOn = true;

    // --- HARDWARE PROOF START ---
    tft.fillScreen(TFT_RED);
    delay(500);
    tft.fillScreen(TFT_GREEN);
    delay(500);
    tft.fillScreen(TFT_BLACK);
    // --- HARDWARE PROOF END ---
    
    Serial.println("[DisplayMgr] Creating sprite...");
    guiSprite = new TFT_eSprite(&tft);
    
    if (guiSprite) {
        guiSprite->setColorDepth(16); 
        if (guiSprite->createSprite(tft.width(), tft.height())) {
            guiSprite->setSwapBytes(true);
            Serial.println("[DisplayMgr] GUI sprite allocated successfully");
        } else {
            Serial.println("[DisplayMgr] ERROR: Failed to allocate GUI sprite");
            delete guiSprite;
            guiSprite = nullptr;
        }
    }
    Serial.println("[DisplayMgr] TFT initialized");
}

void DisplayManager::initEpaper() {
    // Initialize HSPI for ePaper using the safe dummy pin and -1 for CS
    epd_spi.begin(Pins::EP_SCK, MISO_DUMMY, Pins::EP_MOSI, -1);

    // Bind ePaper to HSPI bus with a 20ms reset to ensure wake from deep sleep
    epd.init(115200, true, 20, false, epd_spi, epd_spi_settings);
    epd.setRotation(1);

    // --- HARDWARE PROOF START ---
    epd.setFullWindow();
    epd.fillScreen(GxEPD_BLACK);
    epd.display();
    epd.fillScreen(GxEPD_WHITE);
    epd.display();
    // --- HARDWARE PROOF END ---

    epd.powerOff();  // Power down completely to save energy
    Serial.println("[DisplayMgr] ePaper initialized on HSPI");
}

void DisplayManager::turnOnTFT() {
    if (!tftOn) {
        // No more shared pin configuration needed - LCD_BL on GPIO 42 is dedicated
        tft.writecommand(ST7735_SLPOUT);
        delay(120);  // Physical wake-up time for ST7735S
        tftOn = true;
    }
}

void DisplayManager::turnOffTFT() {
    if (tftOn) {
        tft.writecommand(ST7735_SLPIN);
        digitalWrite(Pins::LCD_BL, LOW);
        tftOn = false;
    }
}

void DisplayManager::prepareEpaperViews(const TaskItem tasks[], uint32_t taskCount) {
    epaperViewCount = (taskCount < EPAPER_VIEW_COUNT) ? taskCount : EPAPER_VIEW_COUNT;
    for (uint32_t i = 0; i < epaperViewCount; ++i) {
        epaperViews[i] = tasks[i];
    }
    Serial.printf("[DisplayMgr] Prepared %u ePaper views\n", epaperViewCount);
}

void DisplayManager::drawEpaperView(int index) {
    if (index < 0 || index >= static_cast<int>(epaperViewCount)) {
        return;
    }

    const TaskItem& task = epaperViews[index];

    epd.setPartialWindow(0, 0, epd.width(), epd.height());
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);
        epd.setTextColor(GxEPD_BLACK);
        epd.setCursor(10, 30);
        epd.print("Task #");
        epd.print(index + 1);
        epd.setCursor(10, 60);
        epd.print(task.title);
        epd.setCursor(10, 90);
        epd.print(task.isCompleted ? "[DONE]" : "[PENDING]");
    } while (epd.nextPage());

    epd.powerOff();  // Power down completely
}

void DisplayManager::updateEpaperPartial(int viewIndex) {
    // Do not re-initialize the SPI bus here. Let the ePaper library manage bus state.
    drawEpaperView(viewIndex);
}

void DisplayManager::drawActiveGUI(const TaskItem tasks[], uint32_t taskCount, int selectedIndex) {
    if (guiSprite) {
        // Draw to sprite in PSRAM
        guiSprite->fillSprite(TFT_BLACK);
        
        guiSprite->setTextColor(TFT_CYAN);
        guiSprite->setCursor(5, 5);
        guiSprite->print("=== StikE ===");

        guiSprite->setTextColor(TFT_WHITE);
        for (uint32_t i = 0; i < taskCount && i < 10; ++i) {
            int y = 25 + static_cast<int>(i * 12);
            if (i == static_cast<uint32_t>(selectedIndex)) {
                guiSprite->fillRect(2, y - 2, 124, 12, TFT_BLUE);
                guiSprite->setTextColor(TFT_WHITE);
            } else {
                guiSprite->setTextColor(tasks[i].isCompleted ? TFT_GREEN : TFT_LIGHTGREY);
            }
            guiSprite->setCursor(5, y);
            guiSprite->print(tasks[i].title);
        }
        
        // Push sprite to screen (flicker-free update)
        guiSprite->pushSprite(0, 0);
    } else {
        // Fallback to direct drawing if sprite not available
        tft.fillScreen(TFT_BLACK);

        tft.setTextColor(TFT_CYAN);
        tft.setCursor(5, 5);
        tft.print("=== StikE ===");

        tft.setTextColor(TFT_WHITE);
        for (uint32_t i = 0; i < taskCount && i < 10; ++i) {
            int y = 25 + static_cast<int>(i * 12);
            if (i == static_cast<uint32_t>(selectedIndex)) {
                tft.fillRect(2, y - 2, 124, 12, TFT_BLUE);
                tft.setTextColor(TFT_WHITE);
            } else {
                tft.setTextColor(tasks[i].isCompleted ? TFT_GREEN : TFT_LIGHTGREY);
            }
            tft.setCursor(5, y);
            tft.print(tasks[i].title);
        }
    }
}
