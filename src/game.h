#pragma once

#include "config.h"

// ------------------- GAME STATE --------------------
enum class GameState
{
  WAITING_FOR_START,
  PLAYING,
  GAME_OVER
};

// ------------------- GAME OBJECTS -------------------
struct Paddle
{
  int x;
  int y;
  int w;
  int h;
};

struct Ball
{
  int x;
  int y;
  int dx;
  int dy;
  int r;
};

// ------------------- GAME STATE VARIABLES -------------------
extern GameState currentState;
extern Paddle player;
extern Paddle enemy;
extern Ball ball;
extern int playerScore;
extern int enemyScore;

/*
Resets the ball to the center of the screen with initial speed.
*/
void resetBall();

/*
Initializes a new game session.
*/
void startGame();

/*
Updates game state including ball physics, collisions, scoring, and AI.
*/
void updateGame();

/*
Moves the player paddle up.
*/
void movePlayerUp();

/*
Moves the player paddle down.
*/
void movePlayerDown();
