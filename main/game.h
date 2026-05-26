#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>

typedef enum {
    GAME_STATE_WAITING,
    GAME_STATE_PLAYING,
    GAME_STATE_OVER
} game_state_t;

typedef struct {
    int x, y, w, h;
} paddle_t;

typedef struct {
    int x, y, dx, dy, r;
} ball_t;

typedef struct {
    game_state_t state;
    paddle_t player;
    paddle_t enemy;
    ball_t ball;
    int player_score;
    int enemy_score;
} game_t;

extern game_t g_game;
extern SemaphoreHandle_t g_game_mutex;

void game_init(void);
void game_start(void);
void game_update(void);
void game_move_player_up(void);
void game_move_player_down(void);
