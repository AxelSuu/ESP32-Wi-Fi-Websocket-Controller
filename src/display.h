#pragma once

#include <Adafruit_SSD1327.h>

// ------------------- DISPLAY INSTANCE -------------------
extern Adafruit_SSD1327 display;

// ------------------- DISPLAY FUNCTIONS -------------------

/*
Initializes the OLED display.
returns true if initialization succeeded, false otherwise.
*/
bool initDisplay();

/*
Main rendering function that delegates to appropriate screen.
*/
void drawGame();
