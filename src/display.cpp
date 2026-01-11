#include "display.h"
#include "config.h"
#include "game.h"
#include <WiFi.h>
#include <SPI.h>

// ------------------- DISPLAY INSTANCE -------------------
Adafruit_SSD1327 display(SCREEN_WIDTH, SCREEN_HEIGHT,
                         OLED_MOSI, OLED_SCLK,
                         OLED_DC, OLED_RST, OLED_CS);

// ------------------- LOCAL FUNCTIONS -------------------

static void drawStartScreen()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1327_WHITE);
  
  display.setCursor(25, 10);
  display.println("ESP32 PONG");
  
  display.setCursor(10, 30);
  display.println("Connect to WiFi:");
  display.setCursor(15, 42);
  display.println("ESP32-Pong");
  display.setCursor(10, 58);
  display.println("Open browser and");
  display.setCursor(10, 70);
  display.println("click START GAME");
  
  display.setCursor(5, 85);
  display.printf("IP: %s", WiFi.softAPIP().toString().c_str());
  
  display.display();
}

static void drawGameOverScreen()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1327_WHITE);
  
  display.setCursor(25, 10);
  display.println("GAME OVER!");
  
  display.setCursor(30, 30);
  if (playerScore >= WIN_SCORE)
  {
    display.println("YOU WIN!");
  }
  else
  {
    display.println("AI WINS!");
  }
  
  display.setCursor(20, 50);
  display.printf("Score: %d - %d", playerScore, enemyScore);
  
  display.setCursor(5, 75);
  display.println("Press START to");
  display.setCursor(5, 85);
  display.println("play again");
  
  display.display();
}

static void drawPlayingScreen()
{
  display.clearDisplay();

  display.fillRect(player.x, player.y, player.w, player.h, SSD1327_WHITE);
  display.fillRect(enemy.x, enemy.y, enemy.w, enemy.h, SSD1327_WHITE);

  display.fillCircle(ball.x, ball.y, ball.r, SSD1327_WHITE);

  display.setTextSize(1);
  display.setCursor(30, 5);
  display.print(playerScore);
  display.setCursor(90, 5);
  display.print(enemyScore);

  for (int i{0}; i < SCREEN_HEIGHT; i += 4)
  {
    display.drawPixel(SCREEN_WIDTH / 2, i, SSD1327_WHITE);
  }

  display.display();
}

// ------------------- PUBLIC FUNCTIONS -------------------

bool initDisplay()
{
  if (!display.begin(0x3D))
  {
    return false;
  }
  display.clearDisplay();
  display.setTextColor(SSD1327_WHITE);
  display.display();
  return true;
}

void drawGame()
{
  switch (currentState)
  {
    case GameState::WAITING_FOR_START:
      drawStartScreen();
      break;
    case GameState::GAME_OVER:
      drawGameOverScreen();
      break;
    case GameState::PLAYING:
      drawPlayingScreen();
      break;
  }
}
