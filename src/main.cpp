#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

#define BTN_X_UP    4
#define BTN_X_DOWN  5
#define BTN_Y_UP    6
#define BTN_Y_DOWN  7
#define BTN_STEP    15

#define PANEL_W 76
#define PANEL_H 284
#define CTRL_W  240
#define CTRL_H  320

int offsetX = 82;
int offsetY = 18;
int stepSize = 1;
const int steps[] = {1, 5, 10};
int stepIndex = 0;

void fillRegion(int x0, int y0, int w, int h, uint16_t color) {
  tft.startWrite();
  tft.setAddrWindow(x0, y0, w, h);
  tft.pushBlock(color, (uint32_t)w * h);
  tft.endWrite();
}

void applyOffsets() {
  // Force TFT_eSPI's offsets to 0 so setAddrWindow uses raw controller coords
  tft.setRotation(0);

  // Override the library's internal offsets — these are protected, so we
  // address the controller directly via the full 240x320 space
  
  // Step 1: fill ENTIRE controller framebuffer white
  fillRegion(0, 0, CTRL_W, CTRL_H, TFT_WHITE);

  // Step 2: fill the panel window black
  fillRegion(offsetX, offsetY, PANEL_W, PANEL_H, TFT_BLACK);

  Serial.printf("OffsetX: %d  OffsetY: %d  Step: %d\n", offsetX, offsetY, stepSize);
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_X_UP,   INPUT_PULLUP);
  pinMode(BTN_X_DOWN, INPUT_PULLUP);
  pinMode(BTN_Y_UP,   INPUT_PULLUP);
  pinMode(BTN_Y_DOWN, INPUT_PULLUP);
  pinMode(BTN_STEP,   INPUT_PULLUP);
  
  tft.init();
  applyOffsets();
}

void loop() {
  bool changed = false;

  if (digitalRead(BTN_STEP) == LOW) {
    stepIndex = (stepIndex + 1) % 3;
    stepSize = steps[stepIndex];
    Serial.printf("Step size: %d\n", stepSize);
    delay(300);
    return;
  }

  if (digitalRead(BTN_X_UP) == LOW)   { offsetX += stepSize; changed = true; }
  if (digitalRead(BTN_X_DOWN) == LOW) { offsetX -= stepSize; changed = true; }
  if (digitalRead(BTN_Y_UP) == LOW)   { offsetY += stepSize; changed = true; }
  if (digitalRead(BTN_Y_DOWN) == LOW) { offsetY -= stepSize; changed = true; }

  if (changed) {
    applyOffsets();
    delay(120);
  }
}