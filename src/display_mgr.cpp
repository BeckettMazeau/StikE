#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>
#include "display_mgr.h"
#include "state_types.h"

DisplayManager::DisplayManager()
    : tft()
    , guiSprite(nullptr)
    , epd(GxEPD2_213_B74(Pins::EP_CS, Pins::EP_DC, Pins::EP_RST, Pins::EP_BUSY))
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
    epd.init(115200);
    delay(200);
    epd.setRotation(1);
    epd.setTextSize(2);
    epd.setTextColor(GxEPD_BLACK);
    epd.setFullWindow();
    delay(100);

    // 2. Draw initial ePaper content while SPI bus is still pristine
    Serial.println("[DisplayMgr] Drawing initial ePaper screen...");
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);
        epd.setCursor(0, 50);
        epd.print("StikE Init");
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

    // 5. Create GUI sprite – allocate with dimensions matching the visible area after rotation
    Serial.println("[DisplayMgr] Creating GUI sprite...");
    guiSprite = new TFT_eSprite(&tft);
    if (guiSprite) {
        guiSprite->setColorDepth(16);
        // Create sprite with dimensions matching the current rotation (e.g., 160x128 in landscape)
        if (guiSprite->createSprite(tft.width(), tft.height())) {
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

namespace {
bool isWithin24Hours(const CalendarEvent& ev, uint16_t curYear, uint8_t curMonth, uint8_t curDay, uint8_t curHour) {
    // Issue 10: If the system year is 2000 (sentinel — no real clock set), show all events.
    if (curYear <= 2000) return true;

    // Very simplified "within 24 hours" check
    if (ev.year == curYear && ev.month == curMonth) {
        if (ev.day == curDay) {
            return ev.hour >= curHour; // Future today
        }
        if (ev.day == curDay + 1) {
            return ev.hour < curHour; // Before same hour tomorrow
        }
    }
    // Handle month wrap (simplified)
    if (ev.year == curYear && ev.month == curMonth + 1 && ev.day == 1 && curDay >= 28) {
        return ev.hour < curHour;
    }
    // Handle year wrap
    if (ev.year == curYear + 1 && ev.month == 1 && ev.day == 1 && curMonth == 12 && curDay == 31) {
        return ev.hour < curHour;
    }
    return false;
}

// Issue 4: Word-boundary-aware string truncation for ePaper display.
// Copies at most maxChars of src into dst (including NUL), breaking at a word
// boundary if the string is longer than maxChars. Appends "." if truncated.
// dst must be at least maxChars+2 bytes.
void truncateAtWord(char* dst, const char* src, int maxChars) {
    int srcLen = strlen(src);
    if (srcLen <= maxChars) {
        strncpy(dst, src, maxChars + 1);
        dst[maxChars] = '\0';
        return;
    }
    // Find the last space at or before maxChars-1 (leave 1 char for '.')
    int cutAt = maxChars - 1;
    for (int i = cutAt; i > 0; i--) {
        if (src[i] == ' ') { cutAt = i; break; }
    }
    strncpy(dst, src, cutAt);
    dst[cutAt] = '.';
    dst[cutAt + 1] = '\0';
}
}


void DisplayManager::prepareEpaperViews(const TaskItem tasks[], uint32_t taskCount,
                                        const CalendarEvent events[], uint32_t eventCount,
                                        uint16_t curYear, uint8_t curMonth, uint8_t curDay, uint8_t curHour) {
    epaperViewCount = 0;

    // Collect all eligible items first
    static EpaperItem allItems[MAX_TASKS + MAX_CALENDAR_EVENTS];
    uint32_t eligibleCount = 0;

    // 1. Collect all upcoming events within 24 hours
    static CalendarEvent eligibleEvents[MAX_CALENDAR_EVENTS];
    uint32_t eligibleEventCount = 0;
    for (uint32_t i = 0; i < eventCount; ++i) {
        if (isWithin24Hours(events[i], curYear, curMonth, curDay, curHour)) {
            eligibleEvents[eligibleEventCount++] = events[i];
        }
    }

    // Sort eligibleEvents by time (soonest first)
    for (uint32_t i = 0; i < eligibleEventCount; i++) {
        for (uint32_t j = i + 1; j < eligibleEventCount; j++) {
            bool swap = false;
            if (eligibleEvents[i].year > eligibleEvents[j].year) swap = true;
            else if (eligibleEvents[i].year == eligibleEvents[j].year) {
                if (eligibleEvents[i].month > eligibleEvents[j].month) swap = true;
                else if (eligibleEvents[i].month == eligibleEvents[j].month) {
                    if (eligibleEvents[i].day > eligibleEvents[j].day) swap = true;
                    else if (eligibleEvents[i].day == eligibleEvents[j].day) {
                        if (eligibleEvents[i].hour > eligibleEvents[j].hour) swap = true;
                        else if (eligibleEvents[i].hour == eligibleEvents[j].hour) {
                            if (eligibleEvents[i].minute > eligibleEvents[j].minute) swap = true;
                        }
                    }
                }
            }
            if (swap) {
                CalendarEvent tmp = eligibleEvents[i];
                eligibleEvents[i] = eligibleEvents[j];
                eligibleEvents[j] = tmp;
            }
        }
    }

    // 2. Take maximum 2 soonest events
    uint32_t eventsToTake = (eligibleEventCount > 2) ? 2 : eligibleEventCount;
    for (uint32_t i = 0; i < eventsToTake; i++) {
        allItems[eligibleCount].type = EpaperViewType::EVENT;
        allItems[eligibleCount].event = eligibleEvents[i];
        eligibleCount++;
    }

    // 3. Collect tasks that are NOT completed
    for (uint32_t i = 0; i < taskCount && eligibleCount < (MAX_TASKS + MAX_CALENDAR_EVENTS); ++i) {
        if (!tasks[i].isCompleted) {
            allItems[eligibleCount].type = EpaperViewType::TASK;
            allItems[eligibleCount].task = tasks[i];
            eligibleCount++;
        }
    }

    // Chunk into screens
    uint32_t itemIdx = 0;
    while (itemIdx < eligibleCount && epaperViewCount < EPAPER_VIEW_COUNT) {
        EpaperViewItem& view = epaperViews[epaperViewCount];
        view.itemCount = 0;
        for (uint32_t j = 0; j < ITEMS_PER_EPAPER_SCREEN && itemIdx < eligibleCount; ++j) {
            view.items[j] = allItems[itemIdx++];
            view.itemCount++;
        }
        epaperViewCount++;
    }

    Serial.printf("[DisplayMgr] Prepared %u ePaper screens with %u items total\n", epaperViewCount, eligibleCount);
}

void DisplayManager::drawEpaperView(int index) {
    if (index < 0 || index >= static_cast<int>(epaperViewCount)) {
        return;
    }

    const EpaperViewItem& view = epaperViews[index];

    // Re-initialize ePaper to wake from hibernate before drawing
    epd.init(115200);
    delay(200);
    epd.setRotation(1);
    epd.setTextSize(2);
    epd.setTextColor(GxEPD_BLACK);
    epd.setFullWindow();
    delay(100);

    const int W = epd.width();   // 250 px (landscape)
    const int H = epd.height();  // 122 px

    // Layout constants (text size 2: each glyph ~12px wide, ~16px tall)
    const int GLYPH_W = 12;
    const int GLYPH_H = 16;
    const int HEADER_Y = 4;
    const int RULE_Y   = 22;
    const int BODY_TOP = 26;           // First item baseline
    const int ITEM_STRIDE = (H - BODY_TOP) / ITEMS_PER_EPAPER_SCREEN; // ~32 px
    const int BOTTOM_GUARD = H - 4;   // Nothing should draw below this

    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        // Header
        char header[32];
        snprintf(header, sizeof(header), "STIKE | %d/%u", index + 1, epaperViewCount);
        epd.setCursor(10, HEADER_Y);
        epd.print(header);
        epd.drawFastHLine(0, RULE_Y, W, GxEPD_BLACK);

        // Draw items
        for (int i = 0; i < view.itemCount; i++) {
            const EpaperItem& item = view.items[i];
            int yItem = BODY_TOP + i * ITEM_STRIDE;

            // Safety guard: stop rendering if we’d exceed the display height
            if (yItem + GLYPH_H > BOTTOM_GUARD) break;

            epd.setCursor(10, yItem);

            if (item.type == EpaperViewType::TASK) {
                // Issue 3: No [ ] prefix. Use a dash bullet instead.
                // Issue 4: Word-boundary truncation.
                // Available width: W - 10 (left margin) - 10 (right margin) = 230 px
                // Prefix "- " = 2 chars * GLYPH_W = 24 px, leaving 206 px = ~17 chars
                const int maxTitleChars = (W - 20 - 2 * GLYPH_W) / GLYPH_W;
                char truncTitle[24];
                truncateAtWord(truncTitle, item.task.title, maxTitleChars);
                epd.printf("- %s", truncTitle);
            } else {
                // Event: "HH:MM Title"
                // Prefix "HH:MM " = 6 chars * GLYPH_W = 72 px, leaving 158 px = ~13 chars
                const int maxTitleChars = (W - 20 - 6 * GLYPH_W) / GLYPH_W;
                char truncTitle[20];
                truncateAtWord(truncTitle, item.event.title, maxTitleChars);
                epd.printf("%02d:%02d %s", item.event.hour, item.event.minute, truncTitle);
            }

            // Separator (only if not the last item on screen, and won't clip)
            int sepY = yItem + GLYPH_H + 2;
            if (i < view.itemCount - 1 && sepY < BOTTOM_GUARD) {
                epd.drawFastHLine(10, sepY, W - 20, GxEPD_BLACK);
            }
        }
    } while (epd.nextPage());

    delay(500);
    epd.hibernate();
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
// Body height = 128 - 14 - 12 = 102px. At LINE_H=12, floor(102/12) = 8 full rows + 6px spare.
// We keep MAX_LIST_LINES at 8 for safety (avoids last row clipping into footer).
constexpr int MAX_LIST_LINES = 8;
}

void DisplayManager::drawActiveGUI(const TaskItem tasks[], uint32_t taskCount, int selectedIndex, int topIndex, int viewMode) {
    if (!guiSprite) {
        Serial.println("[SYS_TEST] drawActiveGUI: guiSprite is null, bailing");
        return;
    }

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(0, 0, 90);
    const bool listFull = (taskCount >= MAX_TASKS);

    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->setTextSize(1);

    // --- Header ---
    guiSprite->fillRect(0, 0, W, HEADER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, 3);
    if (taskCount > 0) {
        char hdr[24];
        const char* modeStr = (viewMode == 0) ? "ACT" : ((viewMode == 1) ? "CMP" : "ALL");
        snprintf(hdr, sizeof(hdr), "%s %u/%u", modeStr, (unsigned)(selectedIndex + 1), (unsigned)taskCount);
        guiSprite->print(hdr);
    } else {
        const char* modeStr = (viewMode == 0) ? "ACTIVE" : ((viewMode == 1) ? "COMPLETED" : "ALL");
        guiSprite->printf("== %s ==", modeStr);
    }

    // --- Scroll indicators (drawn in header right margin) ---
    if (topIndex > 0) {
        // More items above
        guiSprite->setTextColor(TFT_YELLOW, headerBg);
        guiSprite->setCursor(W - 10, 3);
        guiSprite->print('^');
    }
    bool moreBelow = (static_cast<int>(taskCount) > topIndex + MAX_LIST_LINES);
    if (moreBelow) {
        // More items below — draw in body area top-right to remain visible
        guiSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
        guiSprite->setCursor(W - 10, HEADER_H + 2);
        guiSprite->print('v');
    }

    // --- Body: render the scroll window [topIndex .. topIndex+MAX_LIST_LINES) ---
    int windowEnd = topIndex + MAX_LIST_LINES;
    if (windowEnd > static_cast<int>(taskCount)) windowEnd = static_cast<int>(taskCount);

    for (int i = topIndex; i < windowEnd; ++i) {
        int row = i - topIndex;
        int y = HEADER_H + 2 + row * LINE_H;
        bool selected = (i == selectedIndex);

        // Build display line; title truncated to fit within the row width.
        // With textSize=1, each char is 6px wide. Row width W minus checkbox prefix (4 chars = 24px)
        // and optional due-date suffix (14 chars = 84px) leaves ~8 chars for title with due,
        // or 24 chars without. We format conservatively.
        char line[40];
        if (tasks[i].hasDueDate) {
            snprintf(line, sizeof(line), "[%c] %.10s %02d/%02d %02d:%02d",
                     tasks[i].isCompleted ? 'x' : ' ',
                     tasks[i].title,
                     tasks[i].dueMonth, tasks[i].dueDay,
                     tasks[i].dueHour, tasks[i].dueMinute);
        } else {
            snprintf(line, sizeof(line), "[%c] %.24s",
                     tasks[i].isCompleted ? 'x' : ' ',
                     tasks[i].title);
        }

        if (selected) {
            guiSprite->fillRect(0, y - 1, W - 12, LINE_H, TFT_WHITE);
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
        guiSprite->print("No tasks – press N");
    }

    // --- Footer ---
    const uint16_t footerBg = listFull ? guiSprite->color565(90, 0, 0) : headerBg;
    guiSprite->fillRect(0, H - FOOTER_H, W, FOOTER_H, footerBg);
    guiSprite->setTextColor(TFT_WHITE, footerBg);
    guiSprite->setCursor(4, H - FOOTER_H + 2);
    if (listFull) {
        guiSprite->print("LIST FULL (max 20)");
    } else {
        guiSprite->print("N:New E:Edit D:Del");
    }

    guiSprite->pushSprite(offsetX, offsetY);
    Serial.printf("[GUI] drawActiveGUI: top=%d sel=%d count=%u\n", topIndex, selectedIndex, taskCount);
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

void DisplayManager::drawAddViewGUI(const char* currentInput, int activeField, bool hasDue, int y, int m, int d, int h, int min) {
    if (!guiSprite) return;

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(0, 0, 90);

    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->setTextSize(1);

    // Header
    guiSprite->fillRect(0, 0, W, HEADER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, 3);
    guiSprite->print("== NEW TASK ==");

    int curY = HEADER_H + 6;
    
    // Field 0: Title
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, curY);
    guiSprite->print("Title:");
    curY += 10;
    // Title input box — show only the last N chars so the cursor stays visible.
    // textSize=1: each glyph is 6px wide. Box inner width = (W-14)px.
    {
        int maxVisible = (W - 14) / 6;  // e.g. (152/6) = 25 visible chars
        const char* displayStr = currentInput;
        int inputLen = strlen(currentInput);
        if (inputLen > maxVisible) displayStr = currentInput + (inputLen - maxVisible);
        guiSprite->fillRect(4, curY, W - 8, 12, activeField == 0 ? TFT_WHITE : TFT_DARKGREY);
        guiSprite->setTextColor(activeField == 0 ? TFT_BLACK : TFT_WHITE, activeField == 0 ? TFT_WHITE : TFT_DARKGREY);
        guiSprite->setCursor(6, curY + 2);
        guiSprite->printf("%s%s", displayStr, activeField == 0 ? "_" : "");
    }
    curY += 18;

    // Field 1: Has Due Date
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, curY);
    guiSprite->print("Has Due Date? (Y/N):");
    curY += 10;
    guiSprite->fillRect(4, curY, 40, 12, activeField == 1 ? TFT_YELLOW : TFT_DARKGREY);
    guiSprite->setTextColor(TFT_BLACK, activeField == 1 ? TFT_YELLOW : TFT_DARKGREY);
    guiSprite->setCursor(6, curY + 2);
    guiSprite->print(hasDue ? "YES" : "NO");
    curY += 18;

    if (hasDue) {
        // Field 2, 3, 4: Date
        guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
        guiSprite->setCursor(4, curY);
        guiSprite->print("Date & Time:");
        curY += 10;
        
        // Day
        guiSprite->fillRect(4, curY, 20, 12, activeField == 2 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 2 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(6, curY + 2);
        guiSprite->printf("%02d", d);
        
        // Month
        guiSprite->fillRect(30, curY, 20, 12, activeField == 3 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 3 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(32, curY + 2);
        guiSprite->printf("%02d", m);

        // Year
        guiSprite->fillRect(56, curY, 40, 12, activeField == 4 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 4 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(58, curY + 2);
        guiSprite->printf("%04d", y);

        // Hour
        guiSprite->fillRect(100, curY, 20, 12, activeField == 5 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 5 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(102, curY + 2);
        guiSprite->printf("%02d", h);

        // Minute
        guiSprite->fillRect(126, curY, 20, 12, activeField == 6 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 6 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(128, curY + 2);
        guiSprite->printf("%02d", min);
    }

    // Footer
    guiSprite->fillRect(0, H - FOOTER_H, W, FOOTER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, H - FOOTER_H + 2);
    guiSprite->print("ENT:Next BS:Back");

    guiSprite->pushSprite(offsetX, offsetY);
}

void DisplayManager::drawEditViewGUI(const char* currentInput, int activeField, bool hasDue, int y, int m, int d, int h, int min) {
    // Re-use logic with different header
    if (!guiSprite) return;

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(0, 0, 90);

    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->setTextSize(1);

    // Header
    guiSprite->fillRect(0, 0, W, HEADER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, 3);
    guiSprite->print("== EDIT TASK ==");

    int curY = HEADER_H + 6;
    
    // Field 0: Title
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, curY);
    guiSprite->print("Title:");
    curY += 10;
    // Title input box — show only the last N chars so the cursor stays visible.
    {
        int maxVisible = (W - 14) / 6;
        const char* displayStr = currentInput;
        int inputLen = strlen(currentInput);
        if (inputLen > maxVisible) displayStr = currentInput + (inputLen - maxVisible);
        guiSprite->fillRect(4, curY, W - 8, 12, activeField == 0 ? TFT_WHITE : TFT_DARKGREY);
        guiSprite->setTextColor(activeField == 0 ? TFT_BLACK : TFT_WHITE, activeField == 0 ? TFT_WHITE : TFT_DARKGREY);
        guiSprite->setCursor(6, curY + 2);
        guiSprite->printf("%s%s", displayStr, activeField == 0 ? "_" : "");
    }
    curY += 18;


    // Field 1: Has Due Date
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, curY);
    guiSprite->print("Has Due Date? (Y/N):");
    curY += 10;
    guiSprite->fillRect(4, curY, 40, 12, activeField == 1 ? TFT_YELLOW : TFT_DARKGREY);
    guiSprite->setTextColor(TFT_BLACK, activeField == 1 ? TFT_YELLOW : TFT_DARKGREY);
    guiSprite->setCursor(6, curY + 2);
    guiSprite->print(hasDue ? "YES" : "NO");
    curY += 18;

    if (hasDue) {
        // Field 2, 3, 4: Date
        guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
        guiSprite->setCursor(4, curY);
        guiSprite->print("Date & Time:");
        curY += 10;
        
        // Day
        guiSprite->fillRect(4, curY, 20, 12, activeField == 2 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 2 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(6, curY + 2);
        guiSprite->printf("%02d", d);
        
        // Month
        guiSprite->fillRect(30, curY, 20, 12, activeField == 3 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 3 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(32, curY + 2);
        guiSprite->printf("%02d", m);

        // Year
        guiSprite->fillRect(56, curY, 40, 12, activeField == 4 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 4 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(58, curY + 2);
        guiSprite->printf("%04d", y);

        // Hour
        guiSprite->fillRect(100, curY, 20, 12, activeField == 5 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 5 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(102, curY + 2);
        guiSprite->printf("%02d", h);

        // Minute
        guiSprite->fillRect(126, curY, 20, 12, activeField == 6 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setTextColor(TFT_BLACK, activeField == 6 ? TFT_YELLOW : TFT_DARKGREY);
        guiSprite->setCursor(128, curY + 2);
        guiSprite->printf("%02d", min);
    }

    // Footer
    guiSprite->fillRect(0, H - FOOTER_H, W, FOOTER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, H - FOOTER_H + 2);
    guiSprite->print("ENT:Next BS:Back");

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

void DisplayManager::drawCalendarGUI(CalendarView view, int year, int month, int day, const CalendarEvent events[], uint32_t eventCount, int selectedEventIdx) {
    if (!guiSprite) return;

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(0, 60, 0); 

    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->setTextSize(1);

    // --- Header ---
    guiSprite->fillRect(0, 0, W, HEADER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, 3);
    char headerBuf[32];
    if (view == CalendarView::MONTH) snprintf(headerBuf, sizeof(headerBuf), "MONTH: %04d-%02d", year, month);
    else if (view == CalendarView::WEEK) snprintf(headerBuf, sizeof(headerBuf), "WEEK of %02d", day);
    else snprintf(headerBuf, sizeof(headerBuf), "DAY: %04d-%02d-%02d", year, month, day);
    guiSprite->print(headerBuf);

    // --- Body ---
    int bodyY = HEADER_H + 2;
    int bodyH = H - HEADER_H - FOOTER_H - 4;

    if (view == CalendarView::MONTH) {
        int cellW = W / 7;
        int cellH = bodyH / 5;
        for (int i = 0; i < 31; i++) {
            int row = i / 7;
            int col = i % 7;
            int x = col * cellW;
            int y = bodyY + row * cellH;
            
            int dayNum = i + 1;
            int dayEvents = 0;
            for (uint32_t e = 0; e < eventCount; e++) {
                if (events[e].day == dayNum && events[e].month == month) dayEvents++;
            }

            if (dayNum == day) {
                guiSprite->fillRect(x, y, cellW, cellH, TFT_WHITE);
                guiSprite->setTextColor(TFT_BLACK, TFT_WHITE);
            } else {
                guiSprite->drawRect(x, y, cellW, cellH, TFT_DARKGREY);
                guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
            }
            guiSprite->setCursor(x + 2, y + 2);
            guiSprite->print(dayNum);

            // Small indicator for event count
            if (dayEvents > 0) {
                uint16_t countCol = (dayNum == day) ? TFT_BLUE : TFT_YELLOW;
                guiSprite->setTextColor(countCol, (dayNum == day) ? TFT_WHITE : TFT_BLACK);
                guiSprite->setCursor(x + cellW - 8, y + cellH - 8);
                guiSprite->print(dayEvents);
            }
        }
    } else if (view == CalendarView::WEEK) {
        int colW = W / 7;
        // Draw 7 vertical bars
        for (int i = 0; i < 7; i++) {
            int x = i * colW;
            guiSprite->drawRect(x, bodyY, colW, bodyH, TFT_DARKGREY);
            guiSprite->setCursor(x + 2, bodyY + 2);
            guiSprite->print(i + 1); 
        }
        // Draw all events for the week (placeholder: just show this month's events on their day)
        for (uint32_t i = 0; i < eventCount; i++) {
            if (events[i].month == month) {
                int dayOfWeek = (events[i].day - 1) % 7;
                int x = dayOfWeek * colW;
                int y = bodyY + (events[i].hour * bodyH / 24);
                int h = (events[i].duration * bodyH / (24 * 60));
                if (h < 3) h = 3;
                guiSprite->fillRect(x + 1, y, colW - 2, h, TFT_BLUE);
            }
        }
    } else if (view == CalendarView::DAY) {
        // Day View: Simple vertical list of events to avoid overlap
        int lineH = 14;
        int maxLines = bodyH / lineH;
        
        // 1. Filter events for today (matching year, month, day)
        int dayEvents[MAX_CALENDAR_EVENTS];
        int dayCount = 0;
        for (uint32_t i = 0; i < eventCount; i++) {
            if (events[i].day == day && events[i].month == month && events[i].year == year) {
                if (dayCount < (int)MAX_CALENDAR_EVENTS) {
                    dayEvents[dayCount++] = i;
                }
            }
        }

        if (dayCount == 0) {
            guiSprite->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            guiSprite->setCursor(4, bodyY + 10);
            guiSprite->print("(No events today)");
        } else {
            // 2. Determine scroll window for vertical list
            int startLine = 0;
            if (selectedEventIdx >= maxLines) {
                startLine = selectedEventIdx - maxLines + 1;
            }

            // 3. Draw visible event items
            for (int i = 0; i < maxLines && (startLine + i) < dayCount; i++) {
                int eventIdx = dayEvents[startLine + i];
                const CalendarEvent& ev = events[eventIdx];
                int y = bodyY + i * lineH;
                bool selected = (startLine + i == selectedEventIdx);

                if (selected) {
                    guiSprite->fillRect(0, y, W, lineH, TFT_WHITE);
                    guiSprite->setTextColor(TFT_BLACK, TFT_WHITE);
                } else {
                    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
                }

                // Time Column: HH:MM - HH:MM
                int endMin = (ev.hour * 60 + ev.minute + ev.duration);
                char timeBuf[16];
                snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d-%02d:%02d", 
                         ev.hour, ev.minute, (endMin / 60) % 24, endMin % 60);
                
                guiSprite->setCursor(4, y + 3);
                guiSprite->print(timeBuf);

                // Title Column (offset to the right)
                guiSprite->drawFastVLine(82, y, lineH, selected ? TFT_BLACK : TFT_DARKGREY);
                guiSprite->setCursor(86, y + 3);
                
                char titleBuf[24];
                strncpy(titleBuf, ev.title, 23);
                titleBuf[23] = '\0';
                guiSprite->print(titleBuf);
            }
        }
    }

    guiSprite->pushSprite(offsetX, offsetY);
}

void DisplayManager::drawEventDetailGUI(const CalendarEvent& event) {
    if (!guiSprite) return;
    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(0, 60, 0);

    // textSize=1: glyph width = 6px. Max chars fitting in W-8 px.
    const int maxChars = (W - 8) / 6;

    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->setTextSize(1);

    // Header
    guiSprite->fillRect(0, 0, W, HEADER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, 3);
    guiSprite->print("EVENT DETAILS");

    int y = HEADER_H + 10;

    // Title (clamped)
    guiSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
    guiSprite->setCursor(4, y);
    guiSprite->print("Title:");
    y += 10;
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, y);
    char titleBuf[32]; snprintf(titleBuf, maxChars + 1, "%s", event.title);
    guiSprite->print(titleBuf);
    y += 14;

    // Time
    guiSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
    guiSprite->setCursor(4, y);
    guiSprite->print("Time:");
    y += 10;
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, y);
    int endMin = (event.hour * 60 + event.minute + event.duration);
    guiSprite->printf("%02d:%02d - %02d:%02d (%dmin)", event.hour, event.minute, (endMin / 60) % 24, endMin % 60, event.duration);
    y += 14;

    // Location (clamped)
    if (event.location[0]) {
        guiSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
        guiSprite->setCursor(4, y);
        guiSprite->print("Loc:");
        y += 10;
        guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
        guiSprite->setCursor(4, y);
        char locBuf[32]; snprintf(locBuf, maxChars + 1, "%s", event.location);
        guiSprite->print(locBuf);
        y += 14;
    }

    // Notes (clamped to 2 lines)
    if (event.notes[0] && y + 24 < H) {
        guiSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
        guiSprite->setCursor(4, y);
        guiSprite->print("Notes:");
        y += 10;
        guiSprite->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        // Line 1
        char noteLine[32];
        snprintf(noteLine, maxChars + 1, "%s", event.notes);
        guiSprite->setCursor(4, y);
        guiSprite->print(noteLine);
        // Line 2 if notes are longer and space permits
        if ((int)strlen(event.notes) > maxChars && y + 10 < H - 2) {
            y += 10;
            snprintf(noteLine, maxChars + 1, "%s", event.notes + maxChars);
            guiSprite->setCursor(4, y);
            guiSprite->print(noteLine);
        }
    }

    // Footer hint
    guiSprite->fillRect(0, H - FOOTER_H, W, FOOTER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, H - FOOTER_H + 2);
    guiSprite->print("BS:Back");

    guiSprite->pushSprite(offsetX, offsetY);
}

void DisplayManager::drawHelpGUI(SystemState fromState) {
    if (!guiSprite) return;
    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(60, 60, 60);

    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->setTextSize(1);

    // Header
    guiSprite->fillRect(0, 0, W, HEADER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, 3);
    guiSprite->print("HELP & KEYBINDINGS");

    int y = HEADER_H + 5;
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    
    auto printHelpLine = [&](const char* key, const char* desc) {
        guiSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
        guiSprite->print(key);
        guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
        guiSprite->print(": ");
        guiSprite->println(desc);
        y += 10;
        guiSprite->setCursor(4, y);
    };

    guiSprite->setCursor(4, y);
    printHelpLine("Fn+A", "Align Mode");
    printHelpLine("Fn+C", "Calendar");
    printHelpLine("Fn+H", "Help Screen");
    printHelpLine("BS", "Back / Delete");
    printHelpLine("ENT", "Select / Save");
    
    y += 5;
    guiSprite->setCursor(4, y);
    guiSprite->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    guiSprite->println("--- SCREEN SPECIFIC ---");
    y += 10;
    guiSprite->setCursor(4, y);

    if (fromState == SystemState::STATE_UI_LIST) {
        printHelpLine("N", "New Task");
        printHelpLine("E", "Edit Task");
        printHelpLine("D", "Delete Task");
    } else if (fromState == SystemState::STATE_UI_CALENDAR) {
        printHelpLine("N", "New Event");
        printHelpLine("V", "Cycle View");
        printHelpLine("Arrows", "Navigate");
    }

    guiSprite->pushSprite(offsetX, offsetY);
}

void DisplayManager::drawAddEventGUI(const char* title, int hour, int duration, int activeField) {
    if (!guiSprite) return;
    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerBg = guiSprite->color565(0, 60, 0);

    guiSprite->fillSprite(TFT_BLACK);
    guiSprite->setTextSize(1);

    // Header
    guiSprite->fillRect(0, 0, W, HEADER_H, headerBg);
    guiSprite->setTextColor(TFT_WHITE, headerBg);
    guiSprite->setCursor(4, 3);
    guiSprite->print("ADD CALENDAR EVENT");

    // Fields
    int y = HEADER_H + 10;
    
    // Field 0: Title
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, y);
    guiSprite->print("Title:");
    y += 10;
    guiSprite->fillRect(4, y, W - 8, 12, activeField == 0 ? TFT_WHITE : TFT_DARKGREY);
    guiSprite->setTextColor(activeField == 0 ? TFT_BLACK : TFT_WHITE, activeField == 0 ? TFT_WHITE : TFT_DARKGREY);
    guiSprite->setCursor(6, y + 2);
    guiSprite->printf("%s%s", title, activeField == 0 ? "_" : "");
    y += 20;

    // Field 1: Start Time
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, y);
    guiSprite->print("Start Hour (0-23):");
    y += 10;
    guiSprite->fillRect(4, y, 40, 12, activeField == 1 ? TFT_YELLOW : TFT_DARKGREY);
    guiSprite->setTextColor(TFT_BLACK, activeField == 1 ? TFT_YELLOW : TFT_DARKGREY);
    guiSprite->setCursor(6, y + 2);
    guiSprite->printf("%02d:00", hour);
    if (activeField == 1) {
        guiSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
        guiSprite->setCursor(50, y + 2);
        guiSprite->print("< Use Arrows >");
    }
    y += 20;

    // Field 2: Duration
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, y);
    guiSprite->print("Duration (minutes):");
    y += 10;
    guiSprite->fillRect(4, y, 40, 12, activeField == 2 ? TFT_YELLOW : TFT_DARKGREY);
    guiSprite->setTextColor(TFT_BLACK, activeField == 2 ? TFT_YELLOW : TFT_DARKGREY);
    guiSprite->setCursor(6, y + 2);
    guiSprite->printf("%d", duration);
    if (activeField == 2) {
        guiSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
        guiSprite->setCursor(50, y + 2);
        guiSprite->print("< Use Arrows >");
    }

    guiSprite->pushSprite(offsetX, offsetY);
}


