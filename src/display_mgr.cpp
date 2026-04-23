#include <Arduino.h>
#include <esp_sleep.h>
#include "display_mgr.h"
#include "state_types.h"

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
    Serial.println("[DisplayMgr] initTFT: about to call tft.init()");
    
    // Removed manual SPI.begin(); TFT_eSPI will handle bus init
    Serial.println("[DisplayMgr] Skipping manual SPI.begin()");
    
    tft.init();
    Serial.println("[DisplayMgr] tft.init() returned");
    tft.setRotation(1);
    tftOn = true;
    
    // Initialize GUI sprite in PSRAM (128x160x16bpp = 40KB)
    Serial.println("[DisplayMgr] Creating sprite...");
    guiSprite = new TFT_eSprite(&tft);
    if (guiSprite && guiSprite->createSprite(tft.width(), tft.height())) {
        guiSprite->setColorDepth(16);
        guiSprite->setSwapBytes(true);
        Serial.println("[DisplayMgr] GUI sprite allocated in PSRAM");
    } else {
        Serial.println("[DisplayMgr] ERROR: Failed to allocate GUI sprite in PSRAM");
        // Fallback to direct drawing if sprite creation fails
        delete guiSprite;
        guiSprite = nullptr;
    }
    
    tft.fillScreen(TFT_BLACK);
    Serial.println("[DisplayMgr] TFT initialized");
}

void DisplayManager::initEpaper() {
    // Initialize HSPI for ePaper on custom pins
    epd_spi.begin(Pins::EP_SCK, -1, Pins::EP_MOSI, -1);

    // Bind ePaper to HSPI bus with full SPI settings
    epd.init(115200, true, 10, false, epd_spi, epd_spi_settings);
    epd.setRotation(1);
    epd.fillScreen(GxEPD_WHITE);
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
    // Re-initialize HSPI and bind ePaper
    epd_spi.begin(Pins::EP_SCK, -1, Pins::EP_MOSI, -1);
    epd.init(115200, true, 10, false, epd_spi, epd_spi_settings);
    delay(20);

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