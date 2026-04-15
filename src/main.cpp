#include <Arduino.h>
#include "epd_test.h"
#include "GxEPD2_Example.h"
// Example function from GxEPD2_Example.cpp

void setup() {
  Serial.begin(115200);
  delay(1000);  // Short delay to ensure serial is ready

  if (psramInit()) {
    Serial.println("PSRAM is enabled!");
    Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("PSRAM is not enabled or not found.");
  }

  
}

void loop() {
  // Run the e-paper display example
  example();
  
  // Example completes in endless loop within itself - 
  // normally you would call it once per update cycle
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}