#pragma once

#include <cstdint>

#ifdef STike_SYSTEM_TEST

class SystemsTest {
public:
    SystemsTest();
    
    void init();
    void update();
    void handleSerialInput();
    void handleKeyPress(char key);
    
    static void setTestMode(bool enabled) { s_testMode = enabled; }
    static bool isTestMode() { return s_testMode; }
    
private:
    static bool s_testMode;
    
    void drawTFTTestPattern();
    void drawEpaperTestPattern();
    void drawKeyDisplay(char key);
    void logKeyPress(char key);
    void showHelp();
    void showStatus();
    void drawKeyHistory();
    
    char m_keyHistory[10];
    uint8_t m_historyIndex;
    uint8_t m_historyCount;
    char m_lastKey;
    uint8_t m_currentTest;
    bool m_tftTestActive;
    bool m_epaperTestActive;
    uint32_t m_testCycle;
    
    enum TestSubMode {
        TEST_NORMAL,
        TEST_TFT_COLORS,
        TEST_EPAPER_PATTERNS,
        TEST_SLEEP,
        TEST_KEYBOARD
    };
    
    const char* getKeyName(char key);
    uint16_t getKeyColor(char key);
};

#endif // STike_SYSTEM_TEST