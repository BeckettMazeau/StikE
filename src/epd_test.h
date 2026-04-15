// Header Guard
#pragma once

// Include libraries
#include <Arduino.h>
#include <string.h>

bool writeToEpD(String text);
bool initializeEpD();
bool clearEpD();
bool epdSquare(int x, int y, int size);
bool epdCircle(int x, int y, int radius);
bool epdText(int x, int y, String text);