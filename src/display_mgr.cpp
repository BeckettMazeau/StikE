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

void DisplayManager::initBusesAndDisplays() {
    // === ePaper: Complete ALL ePaper work before touching TFT ===
    // The TFT's HSPI init can disrupt the FSPI bus state on ESP32-S3,
    // so we must finish all ePaper SPI transactions first.

    // 1. Initialize ePaper SPI bus and driver
    Serial.println("[DisplayMgr] Init ePaper SPI...");
    SPI.begin(Pins::EP_SCK, -1, Pins::EP_MOSI, Pins::EP_CS);
    delay(100);
    epd.init(115200, true, 10, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    delay(200);
    epd.setRotation(1);
    epd.setTextColor(GxEPD_BLACK);
    epd.setFullWindow();
    delay(100);

    // 2. Draw initial ePaper content while SPI bus is still pristine
    Serial.println("[DisplayMgr] Drawing initial ePaper screen...");
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);
        epd.setCursor(10, 50);
        epd.setTextSize(2);
        epd.print("StikE Initialized");
    } while (epd.nextPage());

    // 3. Hibernate ePaper — done with FSPI until sleep mode
    // Give the ePaper controller enough time to finish the waveform before powering down.
    delay(2000); // extended settle time
    Serial.println("[DisplayMgr] Hibernating ePaper...");
    epd.hibernate();
    delay(100);

    // === TFT: Now safe to initialize HSPI for the TFT ===

    // 4. Initialize TFT and backlight
    Serial.println("[DisplayMgr] Init TFT...");
    pinMode(Pins::LCD_BL, OUTPUT);
    digitalWrite(Pins::LCD_BL, HIGH);
    
    tft.init();
    tft.setRotation(1);
    tft.setTextFont(1);
    tft.fillScreen(TFT_BLACK); // Clear entire GRAM including edge pixels
    tftOn = true;

    // 5. Create GUI sprite – allocate with dimensions matching the visible area after rotation (width=128, height=160)
    Serial.println("[DisplayMgr] Creating GUI sprite...");
    guiSprite = new TFT_eSprite(&tft);
    if (guiSprite) {
        guiSprite->setColorDepth(16);
        // After rotation, tft.width() == 160, tft.height() == 128. We need the opposite order for the sprite to fully cover the screen.
        int spriteW = tft.height(); // 128
        int spriteH = tft.width();  // 160
        if (guiSprite->createSprite(spriteW, spriteH)) {
            guiSprite->setSwapBytes(true);
            guiSprite->setTextFont(1);
            Serial.printf("[DisplayMgr] Sprite %dx%d allocated\n",
                          guiSprite->width(), guiSprite->height());
            Serial.println("[DisplayMgr] GUI sprite allocated successfully");
        } else {
            Serial.println("[DisplayMgr] ERROR: Failed to allocate GUI sprite");
            delete guiSprite;
            guiSprite = nullptr;
        }
    }
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

    epd.setFullWindow();
    delay(100);
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
    
    delay(500);
    epd.hibernate(); // Hibernate to match DualDisplayTest behavior
    delay(100);
}

void DisplayManager::updateEpaperPartial(int viewIndex) {
    // Re-assert ePaper SPI bus pins before drawing.
    // TFT init (HSPI) may have altered GPIO matrix routing for FSPI.
    SPI.begin(Pins::EP_SCK, -1, Pins::EP_MOSI, Pins::EP_CS);
    delay(100);
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
        Serial.println("[SYS_TEST] drawActiveGUI: guiSprite is null, bailing");
        return;
    }

    // Memory telemetry
    Serial.printf("[SYS_TEST] drawActiveGUI: FreeHeap before operations = %u\n", ESP.getFreeHeap());

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(0, 0, 90);

    // Diagnostics: report entry into active GUI drawing and sprite size
    Serial.println("[SYS_TEST] drawActiveGUI start");
    Serial.printf("[SYS_TEST] sprite=%dx%d, headerBg=0x%04X, taskCount=%u, selectedIndex=%d\n",
                  W, H, headerBg, taskCount, selectedIndex);

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
        // Additional runtime diagnostic of what we're drawing for each list item
        Serial.printf("[SYS_TEST] draw line %d: title='%s', completed=%d, line='%s'\n",
                      i, tasks[i].title, tasks[i].isCompleted ? 1 : 0, line);

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

// End of UI draw: push to screen
    Serial.println("[SYS_TEST] drawActiveGUI end, about to pushSprite");
    guiSprite->pushSprite(offsetX, offsetY);
    Serial.printf("[SYS_TEST] drawActiveGUI pushSprite complete, FreeHeap after = %u\n", ESP.getFreeHeap());
}

void DisplayManager::drawSmokeTest() {
    if (!guiSprite) {
        return;
    }
    // Simple, bright smoke test to verify draw path end-to-end
    guiSprite->fillSprite(TFT_MAGENTA);
    guiSprite->pushSprite(0, 0);
    Serial.println("[SYS_TEST] Smoke test drawn MAGENTA");
}

void DisplayManager::drawTestFullRed() {
    if (!guiSprite) {
        return;
    }
    guiSprite->fillSprite(TFT_RED);
    guiSprite->pushSprite(0, 0);
    Serial.println("[SYS_TEST] Full-screen RED test drawn");
}

void DisplayManager::drawTestOverlay() {
    if (!guiSprite) {
        return;
    }
    // Draw a bright white cross on black to verify overlay rendering
    const int W = guiSprite->width();
    const int H = guiSprite->height();
    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->drawLine(0, 0, W - 1, H - 1, TFT_WHITE);
    guiSprite->drawLine(W - 1, 0, 0, H - 1, TFT_WHITE);
    guiSprite->pushSprite(0, 0);
    Serial.println("[SYS_TEST] Overlay cross drawn");
}

// Direct hardware color fill test (bypassing the sprite pipeline)
void DisplayManager::drawDirectColorFrame(uint16_t color) {
    // This is a direct write to the TFT to verify the bus can render a frame
    if (!guiSprite) {
        tft.fillScreen(color);
        Serial.printf("[SYS_TEST] Direct color frame drawn: 0x%04X\n", color);
        return;
    }
    // If sprite exists, we still perform a direct fill on the TFT to bypass sprite buffer
    tft.fillScreen(color);
    Serial.printf("[SYS_TEST] Direct color frame drawn (sprite present) 0x%04X\n", color);
}

// Simple sprite test - tiny sprite to verify sprite path works
void DisplayManager::drawActiveGUISimpleTest() {
    if (!guiSprite) {
        Serial.println("[SYS_TEST] Simple test: guiSprite is null, cannot test");
        return;
    }
    
    Serial.println("[SYS_TEST] Simple sprite test: creating 64x32 sprite");
    
    // Create a tiny sprite to test basic sprite functionality
    TFT_eSprite* testSprite = new TFT_eSprite(&tft);
    if (!testSprite) {
        Serial.println("[SYS_TEST] Simple test: failed to create test sprite");
        return;
    }
    
    testSprite->setColorDepth(16);
    if (!testSprite->createSprite(64, 32)) {
        Serial.println("[SYS_TEST] Simple test: failed to allocate 64x32 sprite");
        delete testSprite;
        return;
    }
    
    Serial.println("[SYS_TEST] Simple test: 64x32 sprite allocated, drawing blue");
    testSprite->fillSprite(TFT_BLUE);
    testSprite->pushSprite(10, 10);
    Serial.println("[SYS_TEST] Simple test: pushed blue rect at 10,10");
    
    delay(500);
    
    // Now try yellow
    testSprite->fillSprite(TFT_YELLOW);
    testSprite->pushSprite(20, 20);
    Serial.println("[SYS_TEST] Simple test: pushed yellow rect at 20,20");
    
    delete testSprite;
    Serial.println("[SYS_TEST] Simple sprite test complete");
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

    guiSprite->pushSprite(offsetX, offsetY);
}

void DisplayManager::clearFullHardwareScreen() {
    // Blast a large area of the raw hardware to clear ghost pixels outside the sprite.
    // We do this before pushSprite so any offset-induced fringe is wiped.
    // Use direct TFT fill, ignoring the sprite entirely.
    tft.fillScreen(TFT_BLACK);
}

// --- alignFlashPhase: static state for flashing between two colors each draw call ---
// Tracks which of two frames we're on so every call to drawAlignGUI automatically
// alternates, making "live" pixels obvious vs static GRAM ghosts.
static bool _alignPhase = false;
static uint32_t _alignLastFlip = 0;

void DisplayManager::drawAlignGUI() {
    if (!guiSprite) return;

    // Flip the phase every ~400ms
    uint32_t now = millis();
    if (now - _alignLastFlip > 400) {
        _alignPhase = !_alignPhase;
        _alignLastFlip = now;
    }

    const int W = guiSprite->width();
    const int H = guiSprite->height();

    // --- PHASE 0: Checkerboard (reveals every pixel position) ---
    // --- PHASE 1: Solid dark blue with markers (different from Phase 0) ---
    // This way: any pixel that doesn't change between phases is a static GRAM ghost!

    if (_alignPhase) {
        // Phase A: 8x8 checkerboard so you can count exact pixel positions
        const int CELL = 8;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                bool even = ((x / CELL) + (y / CELL)) % 2 == 0;
                guiSprite->drawPixel(x, y, even ? TFT_WHITE : TFT_BLUE);
            }
        }
        // Overlay: bright cyan border exactly at sprite edges (pixels 0 and W-1/H-1)
        guiSprite->drawRect(0, 0, W, H, TFT_CYAN);
    } else {
        // Phase B: solid dark background with yellow border + info text
        guiSprite->fillSprite(guiSprite->color565(0, 0, 60));
        guiSprite->drawRect(0, 0, W, H, TFT_YELLOW);
        guiSprite->drawRect(1, 1, W-2, H-2, TFT_YELLOW);
    }

    // --- Corner crosshairs: always drawn over whatever phase bg ---
    // Each crosshair is a + at the 4 corners, 10px arms.
    // These let you see EXACTLY where the logical pixel (0,0), (W-1,0), etc. land.
    const int ARM = 10;
    const uint16_t XH_COL = TFT_RED;
    // Top-left
    guiSprite->drawFastHLine(0, 0, ARM, XH_COL);
    guiSprite->drawFastVLine(0, 0, ARM, XH_COL);
    // Top-right
    guiSprite->drawFastHLine(W-ARM, 0, ARM, XH_COL);
    guiSprite->drawFastVLine(W-1,   0, ARM, XH_COL);
    // Bottom-left
    guiSprite->drawFastHLine(0,     H-1, ARM, XH_COL);
    guiSprite->drawFastVLine(0,     H-ARM, ARM, XH_COL);
    // Bottom-right
    guiSprite->drawFastHLine(W-ARM, H-1, ARM, XH_COL);
    guiSprite->drawFastVLine(W-1,   H-ARM, ARM, XH_COL);
    // Centre crosshair
    guiSprite->drawFastHLine(W/2 - ARM/2, H/2, ARM, XH_COL);
    guiSprite->drawFastVLine(W/2,         H/2 - ARM/2, ARM, XH_COL);

    // --- Text overlay (phase B only, so it doesn't fight the checkerboard) ---
    if (!_alignPhase) {
        guiSprite->setTextSize(1);
        guiSprite->setTextColor(TFT_WHITE, guiSprite->color565(0, 0, 60));
        guiSprite->setCursor(14, 20);
        guiSprite->print("ALIGN MODE");
        guiSprite->setCursor(14, 34);
        guiSprite->printf("X=%d Y=%d", offsetX, offsetY);
        guiSprite->setCursor(14, 48);
        guiSprite->print("Arrow=move");
        guiSprite->setCursor(14, 62);
        guiSprite->print("ESC=done");
        // Phase indicator so you know it's alive
        guiSprite->setTextColor(TFT_GREEN, guiSprite->color565(0, 0, 60));
        guiSprite->setCursor(14, 76);
        guiSprite->print("PHASE B");
    }

    // Always clear hardware first so any fringe from old offset is wiped
    clearFullHardwareScreen();
    guiSprite->pushSprite(offsetX, offsetY);
}

