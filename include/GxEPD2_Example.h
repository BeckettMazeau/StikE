#ifndef _GxEPD2_Example_H_
#define _GxEPD2_Example_H_

#include <Arduino.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <GxEPD2_BW.h>

// Enable GxEPD2 GFX compatibility (0 = disable, 1 = enable)
#define ENABLE_GxEPD2_GFX 0

// Struct for 3-color bitmap pairs
struct bitmap_pair {
    const unsigned char *black;
    const unsigned char *red;
};

// Global display instance declaration (defined in .cpp)
extern GxEPD2::GxEPD2<GxEPD2_128x250, GxEPD2_Driver_v2> display;

// Text display functions
void helloWorld();
void helloWorldForDummies();
void helloFullScreenPartialMode();
void helloArduino();
void helloEpaper();
void helloStripe(uint16_t pw_xe);
void showFont();
void drawFont();
void printText(int x, int y, const char *text, uint8_t font);

// Graphics test functions
void drawGrid();
void drawCornerTest();
void stripeTest(uint16_t pw_xe = 0);
void showBox();
void drawGraphics();

// Partial update function
void showPartialUpdate();

// Value display functions
void showValue(double value);
void showDate(uint8_t w, uint8_t d, uint8_t m, uint8_t y);
void showTime(uint16_t t);

// Bitmap drawing functions (B&W) - for 128x250 display
void drawBitmaps128x250();

// Setup and loop functions
void setup();
void loop();

#endif // _GxEPD2_Example_H_