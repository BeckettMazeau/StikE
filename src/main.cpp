#include <Arduino.h>
#include "epd_test.h"
// put function declarations here:
int myFunction(int, int);

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
  // put your main code here, to run repeatedly:
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}