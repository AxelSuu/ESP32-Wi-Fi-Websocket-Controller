#pragma once

// ------------------- OLED SETUP -------------------
constexpr int SCREEN_WIDTH{128};
constexpr int SCREEN_HEIGHT{96};

// ------------------- GAME CONSTANTS ---------------
constexpr int PADDLE_SPEED{10};
constexpr int PADDLE_WIDTH{4};
constexpr int PADDLE_HEIGHT{20};
constexpr int BALL_RADIUS{3};
constexpr int BALL_INITIAL_SPEED_X{4};
constexpr int BALL_INITIAL_SPEED_Y{1};
constexpr int WIN_SCORE{3};
constexpr int AI_MIN_REACTION_DELAY{250};
constexpr int AI_MAX_REACTION_DELAY{500};
constexpr int AI_TARGET_OFFSET_RANGE{10};
constexpr int AI_MOVE_SPEED{2};
constexpr int AI_MISTAKE_CHANCE{10};  // percentage
constexpr unsigned long SPEED_INCREASE_INTERVAL{10000};

// ---------------- Pin definitions -----------------
constexpr int OLED_CS{6};
constexpr int OLED_DC{5};
constexpr int OLED_RST{4};
constexpr int OLED_MOSI{11};
constexpr int OLED_SCLK{12};

// ------------------- WIFI CONFIG ------------------
constexpr const char* WIFI_SSID{"ESP32-Pong"};
constexpr const char* WIFI_PASSWORD{"12345678"};
