#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>
#include "display_mgr.h"
#include "state_types.h"

// ePaper display instance using B74 type - pins from GxEPD2_display_selection.h
// CS=7, DC=16, RST=15, BUSY=4 (Software SPI bit-banged)
GxEPD2_213_B74 epd_instance(7, 16, 15, 4);

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
    // TFT_eSPI base ctor leaves gfxFont uninitialized; with LOAD_GFXFF defined,
    // write() dereferences it unless we force-clear via setTextFont(1).
    tft.setTextFont(1);
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
            // TFT_eSprite inherits the uninitialized gfxFont from TFT_eSPI — same fix as above.
            guiSprite->setTextFont(1);
            Serial.printf("[DisplayMgr] Sprite %dx%d allocated\n",
                          guiSprite->width(), guiSprite->height());

            // --- SPRITE PIPELINE PROOF ---
            // If these flashes don't appear, pushSprite is broken (coords/format/bus).
            // If they do appear, the UI drawing code is the culprit.
            guiSprite->fillSprite(TFT_BLUE);
            guiSprite->pushSprite(0, 0);
            delay(500);
            guiSprite->fillSprite(TFT_YELLOW);
            guiSprite->pushSprite(0, 0);
            delay(500);
            guiSprite->fillSprite(TFT_BLACK);
            guiSprite->pushSprite(0, 0);
            // --- END PROOF ---

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
    // ePaper uses hardware SPI via the global SPI object (FSPI / SPI2 on S3).
    // Explicitly begin SPI on the ePaper pins here to fix the Static Initialization
    // Order Fiasco: the GxEPD2 global was copy-constructed before SPI was alive,
    // leaving _pSPIx = null.  The 6-arg epd.init() overload calls selectSPI() which
    // resets _pSPIx to &SPI at runtime, after all globals are properly constructed.
    SPI.begin(Pins::EP_SCK, -1, Pins::EP_MOSI, -1);
    epd.init(115200, true, 20, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    epd.setRotation(1);

    // --- HARDWARE PROOF START ---
    epd.setFullWindow();
    epd.fillScreen(GxEPD_BLACK);
    epd.display();
    epd.fillScreen(GxEPD_WHITE);
    epd.display();
    // --- HARDWARE PROOF END ---

    epd.powerOff();  // Power down completely to save energy
    Serial.println("[DisplayMgr] ePaper initialized (Software SPI)");
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

    char line1[24];
    snprintf(line1, sizeof(line1), "Task %d of %u",
             index + 1, static_cast<unsigned>(epaperViewCount));
    const char* line2 = task.isCompleted ? "[DONE]" : "[PENDING]";

    // Size-2 default font = 12px wide per glyph, 16px tall
    const int CHAR_W = 12;
    const int W = epd.width();
    const int H = epd.height();

    auto centeredX = [&](const char* s) {
        int px = static_cast<int>(strlen(s)) * CHAR_W;
        int x = (W - px) / 2;
        return (x < 2) ? 2 : x;
    };

    // Vertical layout: three lines evenly spaced
    int y1 = H / 4 - 8;
    int y2 = H / 2 - 8;
    int y3 = (3 * H) / 4 - 8;

    epd.setPartialWindow(0, 0, W, H);
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);
        epd.setTextColor(GxEPD_BLACK);
        epd.setTextSize(2);

        epd.setCursor(centeredX(line1), y1);
        epd.print(line1);

        epd.setCursor(centeredX(line2), y2);
        epd.print(line2);

        epd.setCursor(centeredX(task.title), y3);
        epd.print(task.title);
    } while (epd.nextPage());

    epd.powerOff();
}

void DisplayManager::updateEpaperPartial(int viewIndex) {
    // Do not re-initialize the SPI bus here. Let the ePaper library manage bus state.
    drawEpaperView(viewIndex);
}

// Shared chrome metrics for both UI views
namespace {
constexpr int HEADER_H = 14;
constexpr int FOOTER_H = 12;
constexpr int LINE_H   = 12;
constexpr int MAX_LIST_LINES = 8;
}

void DisplayManager::drawActiveGUI(const TaskItem tasks[], uint32_t taskCount, int selectedIndex) {
    if (!guiSprite) {
        return;
    }

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(0, 0, 90);

    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->setTextSize(1);

    // --- Header ---
    guiSprite->fillRect(0, 0, W, HEADER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, 3);
    guiSprite->print("== STIKE ==");

    // --- Body: up to MAX_LIST_LINES tasks ---
    int shown = (taskCount < static_cast<uint32_t>(MAX_LIST_LINES))
                    ? static_cast<int>(taskCount)
                    : MAX_LIST_LINES;
    for (int i = 0; i < shown; ++i) {
        int y = HEADER_H + 2 + i * LINE_H;
        bool selected = (i == selectedIndex);

        char line[40];
        snprintf(line, sizeof(line), "[%c] %.28s",
                 tasks[i].isCompleted ? 'x' : ' ', tasks[i].title);

        if (selected) {
            // Inverted colors for the selected row
            guiSprite->fillRect(0, y - 1, W, LINE_H, TFT_WHITE);
            guiSprite->setTextColor(TFT_BLACK, TFT_WHITE);
        } else {
            guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
        }
        guiSprite->setCursor(4, y);
        guiSprite->print(line);
    }

    if (taskCount == 0) {
        guiSprite->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        guiSprite->setCursor(4, HEADER_H + 8);
        guiSprite->print("(No tasks. Press N)");
    }

    // --- Footer ---
    guiSprite->fillRect(0, H - FOOTER_H, W, FOOTER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, H - FOOTER_H + 2);
    guiSprite->print("N:New ENT:Tgl ESC:Sleep");

    guiSprite->pushSprite(0, 0);
}

void DisplayManager::drawAddViewGUI(const char* currentInput) {
    if (!guiSprite) {
        return;
    }

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(0, 0, 90);

    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->setTextSize(1);

    // --- Header ---
    guiSprite->fillRect(0, 0, W, HEADER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, 3);
    guiSprite->print("== NEW TASK ==");

    // --- Body: wrapped input with cursor ---
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    const int CHAR_W = 6;   // default font 6x8
    const int CHARS_PER_LINE = (W - 8) / CHAR_W;
    const int baseY = HEADER_H + 6;

    int len = currentInput ? static_cast<int>(strlen(currentInput)) : 0;
    int col = 0;
    int row = 0;
    for (int i = 0; i < len; ++i) {
        if (col >= CHARS_PER_LINE) {
            col = 0;
            row++;
        }
        guiSprite->setCursor(4 + col * CHAR_W, baseY + row * LINE_H);
        guiSprite->write(static_cast<uint8_t>(currentInput[i]));
        col++;
    }
    // Cursor marker
    if (col >= CHARS_PER_LINE) { col = 0; row++; }
    guiSprite->setCursor(4 + col * CHAR_W, baseY + row * LINE_H);
    guiSprite->print("_");

    // --- Footer ---
    guiSprite->fillRect(0, H - FOOTER_H, W, FOOTER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, H - FOOTER_H + 2);
    guiSprite->print("ENT:Save ESC:Cancel");

    guiSprite->pushSprite(0, 0);
}
