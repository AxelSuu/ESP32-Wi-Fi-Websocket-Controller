#include <Arduino.h>
#include <SPIFFS.h>

#include "game.h"
#include "display.h"
#include "network.h"

/*
System setup function.
 
Initializes serial communication, SPIFFS filesystem, OLED display,
WiFi access point, HTTP server routes, and WebSocket server.
*/
void setup()
{
  Serial.begin(115200);

  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS mount failed");
    return;
  }

  if (!initDisplay())
  {
    Serial.println("SSD1327 allocation failed");
    for (;;);
  }

  initNetwork();
}

/*
main loop.

Handles HTTP clients, WebSocket events, game updates, and rendering at ~30 FPS.
*/
void loop()
{
  handleNetwork();
  updateGame();
  drawGame();

  delay(30);
}
