#include <Arduino.h>
#include <esp_sleep.h>
#include "systems_test.h"
#include "display_mgr.h"
#include "keyboard_mgr.h"
#include "state_types.h"

#ifdef STike_SYSTEM_TEST

extern DisplayManager displayMgr;
extern KeyboardManager keyboardMgr;

bool SystemsTest::s_testMode = true;

SystemsTest::SystemsTest()
    : m_keyHistory{0}
    , m_historyIndex(0)
    , m_historyCount(0)
    , m_lastKey(0)
    , m_currentTest(TEST_NORMAL)
    , m_tftTestActive(false)
    , m_epaperTestActive(false)
    , m_testCycle(0) {
}

void SystemsTest::init() {
    Serial.println("\n=== STIK E SYSTEMS TEST MODE ===");
    Serial.println("Type 'help' for commands");
    Serial.println("==============================");
    
    memset(m_keyHistory, 0, sizeof(m_keyHistory));
    m_historyIndex = 0;
    m_historyCount = 0;
    m_lastKey = 0;
    m_currentTest = TEST_NORMAL;
    
    Serial.println("\n[I] Key Mapping:");
    Serial.println("  0x1B = ESC  | Enter sleep mode");
    Serial.println("  'n'       | Add new task");
    Serial.println("  'j'/'0x34' | Navigate down");
    Serial.println("  'k'/'0x35' | Navigate up");
    Serial.println("  'x'       | Toggle task");
    Serial.println("  't'       | Run TFT test");
    Serial.println("  'e'       | Run ePaper test");
    Serial.println("  's'       | Run sleep test");
    Serial.println("  '?'       | Show this help");
}

void SystemsTest::update() {
    if (!s_testMode) return;
    
    extern KeyboardManager keyboardMgr;
    char key = keyboardMgr.getKeyPress();
    if (key != 0) {
        Serial.printf("[TEST] Key Pressed: %c (0x%02X)\n", (key >= 32 && key <= 126) ? key : '?', key);
        handleKeyPress(key);
    }
}

void SystemsTest::handleSerialInput() {
    if (!Serial.available()) return;
    
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.toLowerCase();
    
    if (input.length() == 0) return;
    
    char cmd = input[0];
    
    Serial.printf("\n>>> CMD: %s\n", input.c_str());
    
    switch (cmd) {
        case '?':
        case 'h':
            showHelp();
            break;
        case 's':
            if (input == "status") {
                showStatus();
            } else {
                Serial.println("[TEST] Starting sleep test...");
                m_currentTest = TEST_SLEEP;
                m_tftTestActive = false;
            }
            break;
        case 't':
            Serial.println("[TEST] Starting TFT color test...");
            m_currentTest = TEST_TFT_COLORS;
            m_tftTestActive = true;
            drawTFTTestPattern();
            break;
        case 'e':
            Serial.println("[TEST] Starting ePaper pattern test...");
            m_currentTest = TEST_EPAPER_PATTERNS;
            m_epaperTestActive = true;
            drawEpaperTestPattern();
            break;
        case 'k':
            Serial.println("[TEST] Keyboard test mode");
            m_currentTest = TEST_KEYBOARD;
            break;
        case 'c':
            if (input == "clear") {
                memset(m_keyHistory, 0, sizeof(m_keyHistory));
                m_historyIndex = 0;
                m_historyCount = 0;
                Serial.println("[TEST] Key history cleared");
            }
            break;
        case 'r':
            Serial.println("[TEST] Resetting to normal mode...");
            m_currentTest = TEST_NORMAL;
            m_tftTestActive = false;
            m_epaperTestActive = false;
            break;
        case 'q':
            Serial.println("\n=== EXITING TEST MODE ===");
            s_testMode = false;
            break;
        case 'i':
            keyboardMgr.scanBus();
            break;
        default:
            Serial.printf("[TEST] Unknown command: %c\n", cmd);
            Serial.println("Type 'help' for available commands");
            break;
    }
    
    drawKeyDisplay(m_lastKey);
}

void SystemsTest::handleKeyPress(char key) {
    if (key == 0) return;

    // Log and remember the key
    Serial.printf("[TEST] Key Pressed: %c (0x%02X)\n", (key >= 32 && key <= 126) ? key : '?', key);
    logKeyPress(key);
    m_lastKey = key;

    // Update rolling history of last 10 keys
    for (int i = 8; i >= 0; --i) m_keyHistory[i+1] = m_keyHistory[i];
    m_keyHistory[0] = (key >= 32 && key <= 126) ? key : '?';
    m_historyCount = (m_historyCount < 10) ? m_historyCount + 1 : 10;

    // TFT visual feedback: random background, large key display
    uint16_t color = random(0x0000, 0xFFFF);
    TFT_eSPI& tft = displayMgr.getTFT();
    tft.fillScreen(color);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(5);
    tft.setCursor(20, 50);
    if (key >= 32 && key <= 126) {
        tft.printf("%c", key);
    } else {
        tft.printf("0x%02X", (uint8_t)key);
    }

    // ePaper partial refresh showing history string
    // Build history string
    char historyStr[12] = {0}; // max 10 chars + null
    for (int i = 0; i < m_historyCount; ++i) {
        historyStr[i] = m_keyHistory[i];
    }
    // Use partial window (fixed size) to avoid full refresh
    displayMgr.getEPD().setPartialWindow(0, 0, 200, 50);
    displayMgr.getEPD().firstPage();
    do {
        displayMgr.getEPD().fillScreen(GxEPD_WHITE);
        displayMgr.getEPD().setTextColor(GxEPD_BLACK);
        displayMgr.getEPD().setCursor(5, 20);
        displayMgr.getEPD().print("History: ");
        displayMgr.getEPD().print(historyStr);
    } while (displayMgr.getEPD().nextPage());

    // ESC key triggers light sleep test
    if (key == 0x1B) {
        Serial.println("[TEST] ESC pressed – entering light sleep (5s)");
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(10, 50);
        tft.print("SLEEPING...");
        delay(100);
        esp_sleep_enable_timer_wakeup(5000000ULL);
        esp_light_sleep_start();
        Serial.println("[TEST] Wakeup from light sleep");
        tft.fillScreen(TFT_GREEN);
        tft.setCursor(10, 50);
        tft.print("AWAKE!");
    }

    // Update on-screen key display (existing routine) for consistency
    drawKeyDisplay(key);
}

void SystemsTest::drawTFTTestPattern() {
    TFT_eSPI& tft = displayMgr.getTFT();
    
    const uint16_t colors[] = {
        TFT_RED, TFT_GREEN, TFT_BLUE, 
        TFT_YELLOW, TFT_CYAN, TFT_MAGENTA,
        TFT_WHITE, TFT_BLACK
    };
    const char* colorNames[] = {
        "RED", "GREEN", "BLUE",
        "YELLOW", "CYAN", "MAGENTA",
        "WHITE", "BLACK"
    };
    
    tft.fillScreen(TFT_BLACK);
    
    int barHeight = tft.height() / 8;
    for (int i = 0; i < 8; i++) {
        tft.fillRect(0, i * barHeight, tft.width(), barHeight, colors[i]);
        tft.setTextColor(TFT_BLACK);
        tft.setCursor(5, i * barHeight + 2);
        tft.print(colorNames[i]);
    }
    
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(5, tft.height() - 15);
    tft.printf("Test #%u | Cycle: %lu", m_currentTest, m_testCycle);
    
    Serial.printf("[TEST] TFT color band displayed (cycle %lu)\n", m_testCycle);
}

void SystemsTest::drawEpaperTestPattern() {
    uint8_t pattern = m_testCycle % 4;
    
    Serial.printf("[TEST] ePaper pattern #%u (cycle %lu)\n", pattern, m_testCycle);
#ifndef DIAG_UI_ONLY
    displayMgr.updateEpaperPartial(pattern);
#endif
    
    delay(100);
    
    Serial.println("[TEST] ePaper partial update complete");
}

void SystemsTest::drawKeyDisplay(char key) {
    TFT_eSPI& tft = displayMgr.getTFT();
    
    tft.fillScreen(TFT_BLACK);
    
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(5, 5);
    tft.print("=== KEYBOARD TEST ===");
    
    if (key == 0) {
        tft.setTextColor(TFT_YELLOW);
        tft.setCursor(5, 30);
        tft.print("Waiting for key...");
        return;
    }
    
    uint16_t color = getKeyColor(key);
    const char* name = getKeyName(key);
    
    tft.fillRect(2, 25, tft.width() - 4, 40, color);
    tft.setTextColor(TFT_BLACK);
    tft.setCursor(10, 30);
    tft.printf("Key: 0x%02X '%c'", (uint8_t)key, (key >= 32 && key < 127) ? key : '?');
    tft.setCursor(10, 45);
    tft.print(name);
    
    tft.setTextColor(TFT_LIGHTGREY);
    tft.setCursor(5, 75);
    tft.print("History:");
    
    for (int i = 0; i < 5 && i < m_historyCount; i++) {
        int idx = (m_historyIndex - 5 + i + 10) % 10;
        if (m_keyHistory[idx] != 0) {
            int y = 90 + i * 12;
            tft.setCursor(10, y);
            tft.printf("%c", m_keyHistory[idx]);
        }
    }
    
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(5, tft.height() - 15);
    tft.print("j=down k=up t=tft e=epaper ?=help");
    
    Serial.printf("[TEST] Key display updated: 0x%02X '%s'\n", (uint8_t)key, name);
}

void SystemsTest::logKeyPress(char key) {
    if (m_historyCount < 10) {
        m_keyHistory[m_historyCount++] = key;
    } else {
        m_keyHistory[m_historyIndex] = key;
        m_historyIndex = (m_historyIndex + 1) % 10;
    }
    
    const char* name = getKeyName(key);
    const char* action = "";
    
    switch (key) {
        case 0x1B: action = " SLEEP_REQ"; break;
        case 'n': case 'N': action = " TASK_ADDED"; break;
        case 'j': case 'J': case 0x34: action = " KEY_DOWN"; break;
        case 'k': case 'K': case 0x35: action = " KEY_UP"; break;
        case 'x': case 'X': action = " TASK_TOGGLE"; break;
        default: action = ""; break;
    }
    
    Serial.printf("[KEY] 0x%02X '%c'%s | %s\n", 
        (uint8_t)key, 
        (key >= 32 && key < 127) ? key : '?',
        action,
        name
    );
}

void SystemsTest::showHelp() {
    Serial.println("\n=== TEST COMMANDS ===");
    Serial.println("  t - TFT color test");
    Serial.println("  e - ePaper pattern test");
    Serial.println("  s - Sleep test");
    Serial.println("  k - Keyboard test");
    Serial.println("  r - Reset to normal");
    Serial.println("  c - Clear key history");
    Serial.println("  i - Run I2C Bus Scanner");
    Serial.println("  status - Show system status");
    Serial.println("  ?/h - Show this help");
    Serial.println("  q - Exit test mode");
    Serial.println("===================\n");
}

void SystemsTest::showStatus() {
    Serial.println("\n=== SYSTEM STATUS ===");
    Serial.printf("  Test Mode: %s\n", s_testMode ? "ACTIVE" : "INACTIVE");
    Serial.printf("  Current State: %d\n", (int)m_currentTest);
    Serial.printf("  Keys Pressed: %u\n", m_historyCount);
    Serial.printf("  Last Key: 0x%02X\n", (uint8_t)m_lastKey);
    Serial.printf("  TFT Active: %s\n", m_tftTestActive ? "YES" : "NO");
    Serial.printf("  ePaper Active: %s\n", m_epaperTestActive ? "YES" : "NO");
    Serial.printf("  Test Cycle: %lu\n", m_testCycle);
    Serial.println("=====================\n");
}

void SystemsTest::drawKeyHistory() {
    // Already drawn in drawKeyDisplay
}

const char* SystemsTest::getKeyName(char key) {
    switch (key) {
        case 0x1B: return "ESC";
        case 'n': return "NEW TASK";
        case 'j': return "DOWN";
        case 'k': return "UP";
        case 'x': return "TOGGLE";
        case 't': return "TFT TEST";
        case 'e': return "EPAPER TEST";
        case 's': return "SLEEP";
        case '?': return "HELP";
        case 0xB4: return "LEFT ARROW";
        case 0xB5: return "UP ARROW";
        case 0xB6: return "DOWN ARROW";
        case 0xB7: return "RIGHT ARROW";
        default:
            if (key >= 'a' && key <= 'z') return "LETTER";
            if (key >= 'A' && key <= 'Z') return "LETTER";
            if (key >= '0' && key <= '9') return "DIGIT";
            return "UNKNOWN";
    }
}

uint16_t SystemsTest::getKeyColor(char key) {
    switch (key) {
        case 0x1B: return TFT_RED;
        case 'n': case 'N': return TFT_GREEN;
        case 'j': case 'J': case 0x34: return TFT_BLUE;
        case 'k': case 'K': case 0x35: return TFT_CYAN;
        case 'x': case 'X': return TFT_MAGENTA;
        case 't': case 'e': case 's': return TFT_YELLOW;
        default: return TFT_WHITE;
    }
}

#endif // STike_SYSTEM_TEST