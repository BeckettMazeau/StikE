#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <LittleFS.h>
#include <FS.h>
#include "display_mgr.h"
#include "state_types.h"
#include "icons.h"

DisplayManager::DisplayManager()
    : tft()
    , guiSprite(nullptr)
    , epd(GxEPD2_213_B74(Pins::EP_CS, Pins::EP_DC, Pins::EP_RST, Pins::EP_BUSY))
    , tftOn(false)
    ,epaperViewCount(0)
    , currentSelectionY(0)
    , targetSelectionY(0)
    , fullRedrawPending(true) {
    memset(rowHashes, 0, sizeof(rowHashes));
}

DisplayManager::~DisplayManager() {
    if (guiSprite) {
        delete guiSprite;
        guiSprite = nullptr;
    }
}

int DisplayManager::getDaysInMonth(int y, int m) {
    if (m == 2) {
        return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 29 : 28;
    }
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    if (m < 1 || m > 12) return 31; // Safety
    return 31;
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
    tft.setSwapBytes(true);
    tft.setTextFont(1);
    tft.fillScreen(TFT_BLACK); // Clear entire GRAM including edge pixels
    tftOn = true;

    // 5. Initialize LittleFS and load Smooth Font
    Serial.println("[DisplayMgr] Init LittleFS...");
    if (!LittleFS.begin()) {
        Serial.println("[DisplayMgr] ERROR: LittleFS mount failed");
    } else {
        // We look for a font file named "Outfit-Medium-12.vlw" or similar
        // If not found, we'll fall back to default, but the logic is enabled.
        if (LittleFS.exists("/Outfit-Medium-12.vlw")) {
            Serial.println("[DisplayMgr] Loading Outfit-Medium-12 font...");
            tft.loadFont("Outfit-Medium-12", LittleFS);
        } else if (LittleFS.exists("/Inter-Regular-12.vlw")) {
            Serial.println("[DisplayMgr] Loading Inter-Regular-12 font...");
            tft.loadFont("Inter-Regular-12", LittleFS);
        } else {
            Serial.println("[DisplayMgr] No VLW font found on LittleFS, using default");
        }
    }

    // 6. Create GUI sprite
    Serial.println("[DisplayMgr] Creating GUI sprite...");
    guiSprite = new TFT_eSprite(&tft);
    if (guiSprite) {
        guiSprite->setColorDepth(16);
        if (guiSprite->createSprite(tft.width(), tft.height())) {
            guiSprite->setSwapBytes(true);
            // If font was loaded to tft, it's shared/inherited? 
            // Actually for sprites we need to loadFont on the sprite too if we want smooth fonts there.
            if (LittleFS.exists("/Outfit-Medium-12.vlw")) {
                guiSprite->loadFont("Outfit-Medium-12", LittleFS);
            } else if (LittleFS.exists("/Inter-Regular-12.vlw")) {
                guiSprite->loadFont("Inter-Regular-12", LittleFS);
            } else {
                guiSprite->setTextFont(1);
            }
            Serial.printf("[DisplayMgr] Sprite %dx%d allocated\n",
                          guiSprite->width(), guiSprite->height());
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
        // First screen is a Bento Grid with max 5 items (1 large + 4 small)
        // Subsequent screens are list view with max 6 items
        uint32_t perScreen = (epaperViewCount == 0) ? 5 : ITEMS_PER_EPAPER_SCREEN;
        for (uint32_t j = 0; j < perScreen && itemIdx < eligibleCount; ++j) {
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

    const int W = epd.width();   // 250 px
    const int H = epd.height();  // 122 px

    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        // Header
        char header[32];
        snprintf(header, sizeof(header), "STIKE | %d/%u", index + 1, epaperViewCount);
        epd.setTextSize(2);
        epd.setCursor(10, 4);
        epd.print(header);
        epd.drawFastHLine(0, 22, W, GxEPD_BLACK);

        if (index == 0) {
            // === Bento Grid View (First Screen) ===
            if (view.itemCount > 0) {
                // Top Half: Most Urgent Item
                const EpaperItem& urgent = view.items[0];
                epd.drawRect(5, 26, W - 10, 44, GxEPD_BLACK);
                epd.drawRect(6, 27, W - 12, 42, GxEPD_BLACK); // Bold effect
                
                epd.drawBitmap(15, 36, urgent.type == EpaperViewType::TASK ? task_icon : event_icon, 16, 16, GxEPD_BLACK);
                
                epd.setTextSize(2);
                epd.setCursor(40, 32);
                if (urgent.type == EpaperViewType::TASK) {
                    epd.print(urgent.task.title);
                    if (urgent.task.hasDueDate) {
                        epd.setTextSize(1);
                        epd.setCursor(40, 52);
                        epd.printf("Due: %02d/%02d %02d:%02d", urgent.task.dueMonth, urgent.task.dueDay, urgent.task.dueHour, urgent.task.dueMinute);
                    }
                } else {
                    epd.print(urgent.event.title);
                    epd.setTextSize(1);
                    epd.setCursor(40, 52);
                    epd.printf("At: %02d:%02d | %s", urgent.event.hour, urgent.event.minute, urgent.event.location);
                }

                // Bottom Half: 2-column grid for remaining 4 items
                for (int i = 1; i < view.itemCount; i++) {
                    const EpaperItem& item = view.items[i];
                    int gridIdx = i - 1;
                    int col = gridIdx % 2;
                    int row = gridIdx / 2;
                    int xOff = 5 + col * (W / 2);
                    int yOff = 75 + row * 22;
                    int tileW = (W / 2) - 10;

                    epd.drawBitmap(xOff, yOff, item.type == EpaperViewType::TASK ? task_icon : event_icon, 16, 16, GxEPD_BLACK);
                    epd.setTextSize(1);
                    epd.setCursor(xOff + 20, yOff);
                    if (item.type == EpaperViewType::TASK) {
                        char trunc[16];
                        strncpy(trunc, item.task.title, 15); trunc[15] = '\0';
                        epd.print(trunc);
                        if (item.task.hasDueDate) {
                            epd.setCursor(xOff + 20, yOff + 10);
                            epd.printf("%02d/%02d", item.task.dueMonth, item.task.dueDay);
                        }
                    } else {
                        char trunc[16];
                        strncpy(trunc, item.event.title, 15); trunc[15] = '\0';
                        epd.print(trunc);
                        epd.setCursor(xOff + 20, yOff + 10);
                        epd.printf("%02d:%02d", item.event.hour, item.event.minute);
                    }
                }
            }
        } else {
            // === Dense List View (Subsequent Screens) ===
            const int BODY_TOP = 26;
            const int ITEM_STRIDE = 16; 
            for (int i = 0; i < view.itemCount; i++) {
                const EpaperItem& item = view.items[i];
                int yItem = BODY_TOP + i * ITEM_STRIDE;
                if (yItem + 16 > H) break;

                epd.drawBitmap(10, yItem, item.type == EpaperViewType::TASK ? task_icon : event_icon, 16, 16, GxEPD_BLACK);
                
                epd.setTextSize(1);
                epd.setCursor(30, yItem);
                if (item.type == EpaperViewType::TASK) {
                    epd.print(item.task.title);
                    if (item.task.hasDueDate) {
                        epd.setCursor(180, yItem);
                        epd.printf("%02d/%02d %02d:%02d", item.task.dueMonth, item.task.dueDay, item.task.dueHour, item.task.dueMinute);
                    }
                } else {
                    epd.print(item.event.title);
                    epd.setCursor(180, yItem);
                    epd.printf("%02d:%02d", item.event.hour, item.event.minute);
                }
                
                if (i < view.itemCount - 1) {
                    epd.drawFastHLine(10, yItem + 15, W - 20, GxEPD_BLACK);
                }
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
constexpr int FOOTER_H = 19;
constexpr int LINE_H   = 12;
// Body height = 128 - 14 - 19 = 95px. At LINE_H=12, floor(95/12) = 7 full rows + 11px spare.
// We keep MAX_LIST_LINES at 7 for safety (avoids last row clipping into footer).
constexpr int MAX_LIST_LINES = 7;

// Gradient helper: draws a vertical linear gradient manually using pixel strips
void drawVGradient(TFT_eSprite* sprite, int x, int y, int w, int h, uint16_t color1, uint16_t color2) {
    if (!sprite) return;
    for (int i = 0; i < h; i++) {
        // Interpolate colors
        uint8_t r1 = (color1 >> 11) & 0x1F;
        uint8_t g1 = (color1 >> 5) & 0x3F;
        uint8_t b1 = color1 & 0x1F;
        
        uint8_t r2 = (color2 >> 11) & 0x1F;
        uint8_t g2 = (color2 >> 5) & 0x3F;
        uint8_t b2 = color2 & 0x1F;
        
        uint8_t r = r1 + (r2 - r1) * i / h;
        uint8_t g = g1 + (g2 - g1) * i / h;
        uint8_t b = b1 + (b2 - b1) * i / h;
        
        uint16_t color = (r << 11) | (g << 5) | b;
        sprite->drawFastHLine(x, y + i, w, color);
    }
}
}

void DisplayManager::updateAnimations() {
    // Easing: current = current + (target - current) * factor
    float diff = targetSelectionY - currentSelectionY;
    if (abs(diff) > 0.1f) {
        currentSelectionY += diff * 0.3f; // 0.3f is the easing factor
    } else {
        currentSelectionY = targetSelectionY;
    }
}

bool DisplayManager::isAnimating() const {
    return abs(targetSelectionY - currentSelectionY) > 0.1f;
}

void DisplayManager::drawActiveGUI(const TaskItem tasks[], const int filteredIndices[], uint32_t filteredCount, int selectedIndex, int topIndex, int viewMode) {
    if (!guiSprite) {
        Serial.println("[SYS_TEST] drawActiveGUI: guiSprite is null, bailing");
        return;
    }

    const uint16_t headerStart = guiSprite->color565(0, 0, 140);
    const uint16_t headerEnd   = guiSprite->color565(0, 0, 60);
    const bool listFull = (filteredCount >= MAX_TASKS);
    const int W = guiSprite->width();
    const int H = guiSprite->height();

    guiSprite->fillSprite(TFT_BLACK);

    // --- Header ---
    drawVGradient(guiSprite, 0, 0, W, HEADER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, 3);
    if (filteredCount > 0) {
        char hdr[24];
        const char* modeStr = (viewMode == 0) ? "ACT" : ((viewMode == 1) ? "CMP" : "ALL");
        snprintf(hdr, sizeof(hdr), "%s %u/%u", modeStr, (unsigned)(selectedIndex + 1), (unsigned)filteredCount);
        guiSprite->print(hdr);
    } else {
        const char* modeStr = (viewMode == 0) ? "ACTIVE" : ((viewMode == 1) ? "COMPLETED" : "ALL");
        guiSprite->printf("== %s ==", modeStr);
    }

    // --- Scroll indicators (drawn in header right margin) ---
    if (topIndex > 0) {
        // More items above
        guiSprite->setTextColor(TFT_YELLOW);
        guiSprite->setCursor(W - 10, 3);
        guiSprite->print('^');
    }
    bool moreBelow = (static_cast<int>(filteredCount) > topIndex + MAX_LIST_LINES);
    if (moreBelow) {
        // More items below — draw in body area top-right to remain visible
        guiSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
        guiSprite->setCursor(W - 10, HEADER_H + 2);
        guiSprite->print('v');
    }

    // --- Body: render the scroll window [topIndex .. topIndex+MAX_LIST_LINES) ---
    int windowEnd = topIndex + MAX_LIST_LINES;
    if (windowEnd > static_cast<int>(filteredCount)) windowEnd = static_cast<int>(filteredCount);

    for (int i = topIndex; i < windowEnd; ++i) {
        int realIdx = filteredIndices[i];
        int row = i - topIndex;
        int y = HEADER_H + 2 + row * LINE_H;
        bool selected = (i == selectedIndex);

        if (selected) {
            targetSelectionY = y;
        }

        char line[40];
        if (tasks[realIdx].hasDueDate) {
            snprintf(line, sizeof(line), "[%c] %.10s %02d/%02d %02d:%02d",
                     tasks[realIdx].isCompleted ? 'x' : ' ',
                     tasks[realIdx].title,
                     tasks[realIdx].dueMonth, tasks[realIdx].dueDay,
                     tasks[realIdx].dueHour, tasks[realIdx].dueMinute);
        } else {
            snprintf(line, sizeof(line), "[%c] %.24s",
                     tasks[realIdx].isCompleted ? 'x' : ' ',
                     tasks[realIdx].title);
        }

        guiSprite->setCursor(4, y);
        guiSprite->print(line);
    }

    // Draw Smooth Selection Bar
    if (selectedIndex >= 0 && selectedIndex >= topIndex && selectedIndex < windowEnd) {
        int realIdx = filteredIndices[selectedIndex];
        // Use currentSelectionY for the sliding effect
        uint16_t barStart = TFT_WHITE;
        uint16_t barEnd = guiSprite->color565(200, 200, 255);
        drawVGradient(guiSprite, 0, (int)currentSelectionY - 1, W - 12, LINE_H, barStart, barEnd);
        
        // Re-draw the selected text on top of the sliding bar
        int row = selectedIndex - topIndex;
        int y = HEADER_H + 2 + row * LINE_H;
        
        char line[40];
        if (tasks[realIdx].hasDueDate) {
            snprintf(line, sizeof(line), "[%c] %.10s %02d/%02d %02d:%02d",
                     tasks[realIdx].isCompleted ? 'x' : ' ',
                     tasks[realIdx].title,
                     tasks[realIdx].dueMonth, tasks[realIdx].dueDay,
                     tasks[realIdx].dueHour, tasks[realIdx].dueMinute);
        } else {
            snprintf(line, sizeof(line), "[%c] %.24s",
                     tasks[realIdx].isCompleted ? 'x' : ' ',
                     tasks[realIdx].title);
        }
        guiSprite->setTextColor(TFT_BLACK);
        guiSprite->setCursor(4, y);
        guiSprite->print(line);
    }

    if (filteredCount == 0) {
        guiSprite->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        guiSprite->setCursor(4, HEADER_H + 8);
        guiSprite->print("No tasks – press N");
    }

    // --- Footer ---
    const uint16_t footerStart = listFull ? guiSprite->color565(140, 0, 0) : headerStart;
    const uint16_t footerEnd   = listFull ? guiSprite->color565(60, 0, 0) : headerEnd;
    
    drawVGradient(guiSprite, 0, H - FOOTER_H, W, FOOTER_H, footerStart, footerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, H - FOOTER_H + 6);
    if (listFull) {
        guiSprite->print("LIST FULL (max 20)");
    } else {
        guiSprite->print("[N]NEW  [E]EDIT  [BS]DEL");
    }

    pushDirtySprite(offsetX, offsetY);
    Serial.printf("[GUI] drawActiveGUI: top=%d sel=%d count=%u\n", topIndex, selectedIndex, filteredCount);
}

void DisplayManager::drawSmokeTest() {
    if (!guiSprite) {
        return;
    }
    // Simple, bright smoke test to verify draw path end-to-end
    guiSprite->fillSprite(TFT_MAGENTA);
    pushDirtySprite(offsetX, offsetY);
    Serial.println("[SYS_TEST] Smoke test drawn MAGENTA");
}

void DisplayManager::drawTestFullRed() {
    if (!guiSprite) {
        return;
    }
    guiSprite->fillSprite(TFT_RED);
    pushDirtySprite(offsetX, offsetY);
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
    pushDirtySprite(offsetX, offsetY);
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
    const uint16_t headerStart = guiSprite->color565(0, 0, 140);
    const uint16_t headerEnd   = guiSprite->color565(0, 0, 60);

    guiSprite->fillSprite(TFT_BLACK);

    // Header
    drawVGradient(guiSprite, 0, 0, W, HEADER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, 3);
    guiSprite->print("== NEW TASK ==");

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
    drawVGradient(guiSprite, 0, H - FOOTER_H, W, FOOTER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, H - FOOTER_H + 6);
    guiSprite->print("[ENT]NEXT [BS]BACK");

    pushDirtySprite(offsetX, offsetY);
}

void DisplayManager::drawEditViewGUI(const char* currentInput, int activeField, bool hasDue, int y, int m, int d, int h, int min) {
    if (!guiSprite) return;

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerStart = guiSprite->color565(0, 0, 140);
    const uint16_t headerEnd   = guiSprite->color565(0, 0, 60);

    guiSprite->fillSprite(TFT_BLACK);

    // Header
    drawVGradient(guiSprite, 0, 0, W, HEADER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
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
    drawVGradient(guiSprite, 0, H - FOOTER_H, W, FOOTER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, H - FOOTER_H + 6);
    guiSprite->print("[ENT]NEXT [BS]BACK");

    pushDirtySprite(offsetX, offsetY);
}

void DisplayManager::drawQuickAddGUI(const char* currentInput) {
    if (!guiSprite) return;

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerStart = guiSprite->color565(0, 140, 0); // Green for Quick Add
    const uint16_t headerEnd   = guiSprite->color565(0, 60, 0);

    guiSprite->fillSprite(TFT_BLACK);

    // Header
    drawVGradient(guiSprite, 0, 0, W, HEADER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, 3);
    guiSprite->print("== QUICK ADD ==");

    int curY = HEADER_H + 20;
    
    // Title Field
    guiSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    guiSprite->setCursor(4, curY);
    guiSprite->print("Task Title:");
    curY += 14;
    {
        int maxVisible = (W - 14) / 6;
        const char* displayStr = currentInput;
        int inputLen = strlen(currentInput);
        if (inputLen > maxVisible) displayStr = currentInput + (inputLen - maxVisible);
        guiSprite->fillRect(4, curY, W - 8, 12, TFT_WHITE);
        guiSprite->setTextColor(TFT_BLACK, TFT_WHITE);
        guiSprite->setCursor(6, curY + 2);
        guiSprite->printf("%s_", displayStr);
    }

    // Footer
    drawVGradient(guiSprite, 0, H - FOOTER_H, W, FOOTER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, H - FOOTER_H + 6);
    guiSprite->print("ENT:Save ESC:Cancel");

    pushDirtySprite(offsetX, offsetY);
}

void DisplayManager::clearFullHardwareScreen() {
    tft.fillScreen(TFT_BLACK);
    forceFullRedraw();
}

void DisplayManager::forceFullRedraw() {
    fullRedrawPending = true;
}

void DisplayManager::pushDirtySprite(int x, int y) {
    if (!guiSprite) return;
    
    int w = guiSprite->width();
    int h = guiSprite->height();
    uint16_t* ptr = (uint16_t*)guiSprite->getPointer();
    
    int firstDirty = -1;
    int lastDirty = -1;
    
    // We iterate row by row and compare hashes to find dirty regions.
    // To minimize SPI transaction overhead, we batch contiguous dirty rows.
    for (int i = 0; i < h; i++) {
        // DJB2-style hash for the row
        uint32_t hash = 5381;
        uint16_t* rowPtr = ptr + i * w;
        for (int j = 0; j < w; j++) {
            hash = ((hash << 5) + hash) + rowPtr[j];
        }
        
        if (fullRedrawPending || hash != rowHashes[i]) {
            rowHashes[i] = hash;
            if (firstDirty == -1) firstDirty = i;
            lastDirty = i;
        } else {
            // Contiguous block ended, push it if we have one
            if (firstDirty != -1) {
                tft.pushImage(x, y + firstDirty, w, lastDirty - firstDirty + 1, ptr + firstDirty * w);
                firstDirty = -1;
            }
        }
    }
    
    // Final block if the last row was dirty
    if (firstDirty != -1) {
        tft.pushImage(x, y + firstDirty, w, lastDirty - firstDirty + 1, ptr + firstDirty * w);
    }
    
    fullRedrawPending = false;
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
    pushDirtySprite(offsetX, offsetY);
}

void DisplayManager::drawCalendarGUI(CalendarView view, int year, int month, int day, const CalendarEvent events[], uint32_t eventCount, int selectedEventIdx) {
    if (!guiSprite) return;

    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerStart = guiSprite->color565(0, 100, 0); // Calendar green
    const uint16_t headerEnd   = guiSprite->color565(0, 40, 0);

    guiSprite->fillSprite(TFT_BLACK);

    // --- Header ---
    drawVGradient(guiSprite, 0, 0, W, HEADER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
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
        int days = getDaysInMonth(year, month);
        for (int i = 0; i < days; i++) {
            int row = i / 7;
            int col = i % 7;
            int x = col * cellW;
            int y = bodyY + row * cellH;
            
            int dayNum = i + 1;
            int dayEvents = 0;
            for (uint32_t e = 0; e < eventCount; e++) {
                if (events[e].day == dayNum && events[e].month == month && events[e].year == year) dayEvents++;
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
            if (events[i].month == month && events[i].year == year) {
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

    pushDirtySprite(offsetX, offsetY);
}

void DisplayManager::drawEventDetailGUI(const CalendarEvent& event, int scrollOffset) {
    if (!guiSprite) return;
    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerStart = guiSprite->color565(0, 100, 0);
    const uint16_t headerEnd   = guiSprite->color565(0, 40, 0);

    // textSize=1: glyph width = 6px. Max chars fitting in W-8 px.
    const int maxChars = (W - 12) / 6;

    guiSprite->fillSprite(TFT_BLACK);

    // Body area bounds
    const int bodyY = HEADER_H;
    const int bodyH = H - HEADER_H - FOOTER_H;

    // Helper to draw text with vertical clipping to body area
    auto drawBodyText = [&](int x, int y, const char* text, uint16_t color) {
        if (y >= bodyY && y < bodyY + bodyH - 8) {
            guiSprite->setTextColor(color, TFT_BLACK);
            guiSprite->setCursor(x, y);
            guiSprite->print(text);
        }
    };

    int curY = HEADER_H + 5 - scrollOffset;

    // Title
    drawBodyText(4, curY, "Title:", TFT_YELLOW);
    curY += 10;
    // Wrap title if it exceeds maxChars (though it's only 24 chars, it might fit)
    {
        int len = strlen(event.title);
        int pos = 0;
        while (pos < len || (pos == 0 && len == 0)) {
            char line[40];
            int take = (len - pos > maxChars) ? maxChars : (len - pos);
            strncpy(line, event.title + pos, take);
            line[take] = '\0';
            drawBodyText(4, curY, line, TFT_WHITE);
            curY += 10;
            pos += take;
            if (pos >= len) break;
        }
    }
    curY += 4;

    // Time
    drawBodyText(4, curY, "Time:", TFT_YELLOW);
    curY += 10;
    int endMin = (event.hour * 60 + event.minute + event.duration);
    char timeBuf[64];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d-%02d:%02d (%dm)", event.hour, event.minute, (endMin / 60) % 24, endMin % 60, event.duration);
    drawBodyText(4, curY, timeBuf, TFT_WHITE);
    curY += 14;

    // Location
    if (event.location[0]) {
        drawBodyText(4, curY, "Location:", TFT_YELLOW);
        curY += 10;
        int len = strlen(event.location);
        int pos = 0;
        while (pos < len) {
            char line[40];
            int take = (len - pos > maxChars) ? maxChars : (len - pos);
            strncpy(line, event.location + pos, take);
            line[take] = '\0';
            drawBodyText(4, curY, line, TFT_WHITE);
            curY += 10;
            pos += take;
        }
        curY += 4;
    }

    // Notes
    if (event.notes[0]) {
        drawBodyText(4, curY, "Notes:", TFT_YELLOW);
        curY += 10;
        int len = strlen(event.notes);
        int pos = 0;
        while (pos < len) {
            char line[64];
            int take = (len - pos > maxChars) ? maxChars : (len - pos);
            strncpy(line, event.notes + pos, take);
            line[take] = '\0';
            drawBodyText(4, curY, line, TFT_LIGHTGREY);
            curY += 10;
            pos += take;
        }
    }

    // --- Header ---
    drawVGradient(guiSprite, 0, 0, W, HEADER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, 3);
    guiSprite->print("EVENT DETAILS");

    // Scroll indicators
    if (scrollOffset > 0) {
        guiSprite->setTextColor(TFT_YELLOW);
        guiSprite->setCursor(W - 10, HEADER_H + 2);
        guiSprite->print('^');
    }
    if (curY > bodyY + bodyH) {
        guiSprite->setTextColor(TFT_YELLOW);
        guiSprite->setCursor(W - 10, H - FOOTER_H - 10);
        guiSprite->print('v');
    }

    // --- Footer ---
    drawVGradient(guiSprite, 0, H - FOOTER_H, W, FOOTER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, H - FOOTER_H + 6);
    guiSprite->print("UP/DN:Scroll BS:Back");

    pushDirtySprite(offsetX, offsetY);
}

void DisplayManager::drawHelpGUI(SystemState fromState) {
    if (!guiSprite) return;
    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerStart = guiSprite->color565(80, 80, 80);
    const uint16_t headerEnd   = guiSprite->color565(30, 30, 30);

    guiSprite->fillSprite(TFT_BLACK);

    // Header
    drawVGradient(guiSprite, 0, 0, W, HEADER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
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
        printHelpLine("BS", "Delete Task");
    } else if (fromState == SystemState::STATE_UI_CALENDAR) {
        printHelpLine("N", "New Event");
        printHelpLine("V", "Cycle View");
        printHelpLine("Arrows", "Navigate");
    }

    pushDirtySprite(offsetX, offsetY);
}

void DisplayManager::drawAddEventGUI(const char* title, int hour, int duration, int activeField) {
    if (!guiSprite) return;
    const int W = guiSprite->width();
    const int H = guiSprite->height();
    const uint16_t headerStart = guiSprite->color565(0, 100, 0);
    const uint16_t headerEnd   = guiSprite->color565(0, 40, 0);

    guiSprite->fillSprite(TFT_BLACK);

    // Header
    drawVGradient(guiSprite, 0, 0, W, HEADER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
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

    // Footer
    drawVGradient(guiSprite, 0, H - FOOTER_H, W, FOOTER_H, headerStart, headerEnd);
    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setCursor(4, H - FOOTER_H + 6);
    guiSprite->print("[ENT]SAVE  [ESC]CANCEL");

    pushDirtySprite(offsetX, offsetY);
}



void DisplayManager::setTFTBrightness(uint8_t brightness) {
    analogWrite(Pins::LCD_BL, brightness);
}

void DisplayManager::drawSettingsGUI(int selectedItem, uint8_t brightness, uint16_t sleepTimeout, const char* wifiSSID, const char* wifiPassword, const char* gcalURL, bool isEditingSetting, const char* inputBuffer) {
    if (!tftOn) return;

    guiSprite->fillSprite(TFT_BLACK);

    // Title Bar
    guiSprite->fillRect(0, 0, 160, 14, TFT_ORANGE);
    guiSprite->setTextColor(TFT_BLACK);
    guiSprite->setTextDatum(TL_DATUM);
    guiSprite->drawString(" SYSTEM SETTINGS", 2, 3);

    // Help hint
    guiSprite->setTextColor(0x7BEF); // Light Gray
    if (isEditingSetting) {
        guiSprite->drawString("ENTER: Save  ESC: Cancel", 2, 114);
    } else {
        guiSprite->drawString("ESC: Exit  < >/ENTER", 2, 114);
    }

    guiSprite->setTextColor(TFT_WHITE);
    guiSprite->setTextDatum(ML_DATUM);

    // Settings Items
    const int itemHeight = 16;
    int startY = 24;

    // We have 6 items
    int scrollOffset = 0;
    if (selectedItem > 3) {
        scrollOffset = (selectedItem - 3) * itemHeight;
    }

    const char* labels[] = {"Brightness", "AutoSleep", "WiFi SSID", "WiFi Pass", "GCal URL", "Sync Cal"};

    for (int i = 0; i < 6; i++) {
        int y = startY + (i * itemHeight) - scrollOffset;

        if (y < 20 || y > 110) continue; // Skip drawing items outside the viewable area

        if (i == selectedItem) {
            guiSprite->fillRect(0, y - 8, 160, itemHeight, 0x3186); // Dark Gray
            guiSprite->drawRect(0, y - 8, 160, itemHeight, TFT_ORANGE);
            guiSprite->setTextColor(TFT_ORANGE);
        } else {
            guiSprite->setTextColor(TFT_WHITE);
        }

        guiSprite->drawString(labels[i], 5, y);

        // Right-aligned values
        guiSprite->setTextDatum(MR_DATUM);

        char displayStr[128];
        displayStr[0] = '\0';

        if (i == 0) { // Brightness
            snprintf(displayStr, sizeof(displayStr), "%d", brightness);
        } else if (i == 1) { // AutoSleep
            snprintf(displayStr, sizeof(displayStr), "%d min", sleepTimeout);
        } else if (i == 2) { // WiFi SSID
            if (i == selectedItem && isEditingSetting) {
                snprintf(displayStr, sizeof(displayStr), "%s_", inputBuffer);
            } else {
                snprintf(displayStr, sizeof(displayStr), "%s", wifiSSID);
            }
        } else if (i == 3) { // WiFi Pass
            if (i == selectedItem && isEditingSetting) {
                snprintf(displayStr, sizeof(displayStr), "%s_", inputBuffer);
            } else {
                int len = strlen(wifiPassword);
                if (len > 0) {
                    for(int p=0; p<len && p<8; p++) displayStr[p] = '*';
                    displayStr[min(len, 8)] = '\0';
                }
            }
        } else if (i == 4) { // GCal URL
            if (i == selectedItem && isEditingSetting) {
                snprintf(displayStr, sizeof(displayStr), "%s_", inputBuffer);
            } else {
                snprintf(displayStr, sizeof(displayStr), "%s", gcalURL);
            }
        } else if (i == 5) { // Sync Calendar
            snprintf(displayStr, sizeof(displayStr), "Press ENTER");
        }

        // Truncate if it's too long to fit on screen
        if (strlen(displayStr) > 12) {
            char trunc[16];
            strncpy(trunc, displayStr, 10);
            trunc[10] = '.'; trunc[11] = '.'; trunc[12] = '\0';
            guiSprite->drawString(trunc, 155, y);
        } else {
            guiSprite->drawString(displayStr, 155, y);
        }

        guiSprite->setTextDatum(ML_DATUM);
    }

    pushDirtySprite(0, 0);
}
