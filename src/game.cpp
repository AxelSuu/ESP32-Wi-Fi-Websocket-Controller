#include "game.h"
#include <Arduino.h>

// ------------------- GAME STATE VARIABLES -------------------
GameState currentState{GameState::WAITING_FOR_START};

Paddle player{5, 30, PADDLE_WIDTH, PADDLE_HEIGHT};
Paddle enemy{SCREEN_WIDTH - 9, 30, PADDLE_WIDTH, PADDLE_HEIGHT};
Ball ball{SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2,
  BALL_INITIAL_SPEED_X, BALL_INITIAL_SPEED_Y, BALL_RADIUS};

int playerScore{0};
int enemyScore{0};

// ------------------- AI STATE -------------------
static int aiReactionDelay{0};
static int aiTargetOffset{0};
static unsigned long lastAiUpdate{0};
static unsigned long gameStartTime{0};
static unsigned long lastSpeedIncrease{0};

// ------------------- GAME FUNCTIONS -------------------

void resetBall()
{
  ball.x = SCREEN_WIDTH / 2;
  ball.y = SCREEN_HEIGHT / 2;
  ball.dx = BALL_INITIAL_SPEED_X;
  ball.dy = (random(2) == 0) ? BALL_INITIAL_SPEED_Y : -BALL_INITIAL_SPEED_Y;
}

void startGame()
{
  currentState = GameState::PLAYING;
  playerScore = 0;
  enemyScore = 0;
  resetBall();
  
  player.y = 30;
  enemy.y = 30;

  gameStartTime = millis();
  lastSpeedIncrease = gameStartTime;
}

void movePlayerUp()
{
  player.y -= PADDLE_SPEED;
}

void movePlayerDown()
{
  player.y += PADDLE_SPEED;
}

void updateGame()
{
  if (currentState != GameState::PLAYING) return;

  unsigned long currentTime{millis()};
  
  // Ball speed increase over time
  if (currentTime - lastSpeedIncrease >= SPEED_INCREASE_INTERVAL)
  {
    if (ball.dx > 0)
    {
      ball.dx += 1;
    }
    else
    {
      ball.dx -= 1;
    }
    lastSpeedIncrease = currentTime;
    Serial.printf("Ball speed increased! New dx: %d\n", ball.dx);
  }
  
  ball.x += ball.dx;
  ball.y += ball.dy;

  // Wall collisions
  if (ball.y - ball.r <= 0 || ball.y + ball.r >= SCREEN_HEIGHT)
  {
    ball.dy *= -1;
  }

  // Player paddle collision
  if (ball.x - ball.r <= player.x + player.w &&
      ball.y >= player.y && ball.y <= player.y + player.h &&
      ball.dx < 0)
  {
    ball.dx *= -1;
  }

  // Enemy paddle collision
  if (ball.x + ball.r >= enemy.x &&
      ball.y >= enemy.y && ball.y <= enemy.y + enemy.h &&
      ball.dx > 0)
  {
    ball.dx *= -1;
  }

  // Scoring
  if (ball.x < 0)
  {
    enemyScore++;
    if (enemyScore >= WIN_SCORE)
    {
      currentState = GameState::GAME_OVER;
      return;
    }
    resetBall();
    lastSpeedIncrease = millis();
  }
  else if (ball.x > SCREEN_WIDTH)
  {
    playerScore++;
    if (playerScore >= WIN_SCORE)
    {
      currentState = GameState::GAME_OVER;
      return;
    }
    resetBall();
    lastSpeedIncrease = millis();
  }

  // AI decision update with random delay
  if (currentTime - lastAiUpdate > static_cast<unsigned long>(aiReactionDelay))
  {
    lastAiUpdate = currentTime;
    aiReactionDelay = random(AI_MIN_REACTION_DELAY, AI_MAX_REACTION_DELAY);
    aiTargetOffset = random(-AI_TARGET_OFFSET_RANGE, AI_TARGET_OFFSET_RANGE);
  }
  
  // AI movement (only when ball approaches)
  if (ball.dx > 0)
  {
    int enemyCenter{enemy.y + enemy.h / 2};
    int targetY{ball.y + aiTargetOffset};
    
    if (targetY < enemyCenter - 3)
    {
      enemy.y -= AI_MOVE_SPEED;
    }
    else if (targetY > enemyCenter + 3)
    {
      enemy.y += AI_MOVE_SPEED;
    }
    
    if (random(100) < AI_MISTAKE_CHANCE)
    {
      enemy.y += random(-6, 6);
    }
  }

  // Clamp paddles to screen bounds
  if (enemy.y < 0)
  {
    enemy.y = 0;
  }
  if (enemy.y + enemy.h > SCREEN_HEIGHT) 
  {
    enemy.y = SCREEN_HEIGHT - enemy.h;
  }
  if (player.y < 0) 
  {
    player.y = 0;
  }
  if (player.y + player.h > SCREEN_HEIGHT)
  {
    player.y = SCREEN_HEIGHT - player.h;
  }
}
