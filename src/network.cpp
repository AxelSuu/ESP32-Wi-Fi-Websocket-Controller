#include "network.h"
#include "config.h"
#include "game.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <FS.h>
#include <SPIFFS.h>

// ------------------- SERVER INSTANCES -------------------
static WebServer server{80};
static WebSocketsServer webSocket{81};

// ------------------- WEBSOCKET HANDLER -------------------

static void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length)
{
  if (type == WStype_TEXT && currentState == GameState::PLAYING)
  {
    String msg{String((char*)payload)};
    if (msg.indexOf("up") >= 0) movePlayerUp();
    if (msg.indexOf("down") >= 0) movePlayerDown();
  }
}

// ------------------- PUBLIC FUNCTIONS -------------------

void initNetwork()
{
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("WiFi started: ESP32-Pong");

  server.on("/", HTTP_GET, []()
  {
    File file{SPIFFS.open("/index.html", "r")};
    if (!file)
    {
      server.send(500, "text/plain", "index.html not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  
  server.on("/start", HTTP_GET, []()
  {
    startGame();
    server.send(200, "text/plain", "Game Started!");
  });
  
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void handleNetwork()
{
  server.handleClient();
  webSocket.loop();
}
