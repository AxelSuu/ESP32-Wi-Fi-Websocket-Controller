#pragma once

#define SCREEN_WIDTH        128
#define SCREEN_HEIGHT       96

#define PADDLE_SPEED        10
#define PADDLE_WIDTH        4
#define PADDLE_HEIGHT       20
#define BALL_RADIUS         3
#define BALL_INITIAL_SPEED_X 4
#define BALL_INITIAL_SPEED_Y 1
#define WIN_SCORE           3

#define AI_MIN_REACTION_DELAY   250
#define AI_MAX_REACTION_DELAY   500
#define AI_TARGET_OFFSET_RANGE  10
#define AI_MOVE_SPEED           2
#define AI_MISTAKE_CHANCE       10
#define SPEED_INCREASE_INTERVAL 10000

#define OLED_CS     6
#define OLED_DC     5
#define OLED_RST    4
#define OLED_MOSI   11
#define OLED_SCLK   12

#define WIFI_SSID       "ESP32-Pong"
#define WIFI_PASSWORD   "12345678"
#define WIFI_AP_CHANNEL 1
#define WIFI_MAX_CONN   4

#define SPIFFS_BASE_PATH "/spiffs"
#define MAX_WS_CLIENTS   4
