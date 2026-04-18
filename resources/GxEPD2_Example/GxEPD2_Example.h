#ifndef _GxEPD2_Example_H_
#define _GxEPD2_Example_H_

#include <Arduino.h>
#include <GxEPD2_GFX.h>


// Enable GxEPD2 GFX compatibility (0 = disable, 1 = enable)
//#define ENABLE_GxEPD2_GFX 1

// Struct for bitmap pairs (used by color display examples - included for completeness)
struct bitmap_pair {
    const unsigned char *black;
    const unsigned char *red;
};

// Global display instance declaration (defined in .cpp)
//extern GxEPD2<GxEPD2_128x250, GxEPD2_Driver_v2> display;

// ============================================================================
// Text Display Functions
// ============================================================================

void helloWorld();
void helloWorldForDummies();
void helloFullScreenPartialMode();
void helloArduino();
void helloEpaper();
void helloStripe(uint16_t pw_xe);
void showFont(const char name[], const GFXfont *f = nullptr);
void drawFont(const char name[], const GFXfont *f = nullptr);
void printText(int x, int y, const char *text, uint8_t font);
void deepSleepTest();
void deepSleepTestPartialUpdate();
// ============================================================================
// Graphics Test Functions
// ============================================================================

void drawGrid();
void drawCornerTest();
void stripeTest(uint16_t pw_xe = 0);
void showBox(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool partial);
void drawGraphics();
void showPartialUpdate();

// ============================================================================
// Value Display Functions
// ============================================================================

void helloValue(double v, int digits = 0);
void showValue(double value);
void showDate(uint8_t w, uint8_t d, uint8_t m, uint8_t y);
void showTime(uint16_t t);

// ============================================================================
// Bitmap Drawing Functions (for 128x250 monochrome display)
// ============================================================================

void drawBitmaps();
void drawBitmaps128x250();

// ============================================================================
// Setup and Loop Entry Points
// ============================================================================

void setup_example();
void loop();

#endif // _GxEPD2_Example_H_