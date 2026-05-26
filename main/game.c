#include "game.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"

static const char *TAG = "game";

game_t g_game;
SemaphoreHandle_t g_game_mutex;

static int64_t s_last_speed_increase;
static int64_t s_last_ai_update;
static int s_ai_reaction_delay;
static int s_ai_target_offset;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void reset_ball(void)
{
    g_game.ball.x  = SCREEN_WIDTH / 2;
    g_game.ball.y  = SCREEN_HEIGHT / 2;
    g_game.ball.dx = BALL_INITIAL_SPEED_X;
    g_game.ball.dy = (esp_random() % 2 == 0) ? BALL_INITIAL_SPEED_Y : -BALL_INITIAL_SPEED_Y;
}

void game_init(void)
{
    g_game_mutex = xSemaphoreCreateMutex();
    g_game.state        = GAME_STATE_WAITING;
    g_game.player       = (paddle_t){5,            30, PADDLE_WIDTH, PADDLE_HEIGHT};
    g_game.enemy        = (paddle_t){SCREEN_WIDTH - 9, 30, PADDLE_WIDTH, PADDLE_HEIGHT};
    g_game.ball         = (ball_t){SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2,
                                   BALL_INITIAL_SPEED_X, BALL_INITIAL_SPEED_Y, BALL_RADIUS};
    g_game.player_score = 0;
    g_game.enemy_score  = 0;
}

void game_start(void)
{
    xSemaphoreTake(g_game_mutex, portMAX_DELAY);
    g_game.state        = GAME_STATE_PLAYING;
    g_game.player_score = 0;
    g_game.enemy_score  = 0;
    g_game.player.y     = 30;
    g_game.enemy.y      = 30;
    reset_ball();
    int64_t t            = now_ms();
    s_last_speed_increase = t;
    s_last_ai_update      = t;
    s_ai_reaction_delay   = AI_MIN_REACTION_DELAY +
                            (int)(esp_random() % (AI_MAX_REACTION_DELAY - AI_MIN_REACTION_DELAY));
    s_ai_target_offset    = 0;
    xSemaphoreGive(g_game_mutex);
}

void game_move_player_up(void)
{
    xSemaphoreTake(g_game_mutex, portMAX_DELAY);
    g_game.player.y -= PADDLE_SPEED;
    xSemaphoreGive(g_game_mutex);
}

void game_move_player_down(void)
{
    xSemaphoreTake(g_game_mutex, portMAX_DELAY);
    g_game.player.y += PADDLE_SPEED;
    xSemaphoreGive(g_game_mutex);
}

void game_update(void)
{
    xSemaphoreTake(g_game_mutex, portMAX_DELAY);

    if (g_game.state != GAME_STATE_PLAYING) {
        xSemaphoreGive(g_game_mutex);
        return;
    }

    int64_t now = now_ms();

    // Ball speed increase every SPEED_INCREASE_INTERVAL ms
    if (now - s_last_speed_increase >= SPEED_INCREASE_INTERVAL) {
        g_game.ball.dx += (g_game.ball.dx > 0) ? 1 : -1;
        s_last_speed_increase = now;
        ESP_LOGI(TAG, "Ball speed increased: dx=%d", g_game.ball.dx);
    }

    g_game.ball.x += g_game.ball.dx;
    g_game.ball.y += g_game.ball.dy;

    // Wall collisions (top/bottom)
    if (g_game.ball.y - g_game.ball.r <= 0 || g_game.ball.y + g_game.ball.r >= SCREEN_HEIGHT) {
        g_game.ball.dy *= -1;
    }

    // Player paddle collision
    if (g_game.ball.x - g_game.ball.r <= g_game.player.x + g_game.player.w &&
        g_game.ball.y >= g_game.player.y &&
        g_game.ball.y <= g_game.player.y + g_game.player.h &&
        g_game.ball.dx < 0) {
        g_game.ball.dx *= -1;
    }

    // Enemy paddle collision
    if (g_game.ball.x + g_game.ball.r >= g_game.enemy.x &&
        g_game.ball.y >= g_game.enemy.y &&
        g_game.ball.y <= g_game.enemy.y + g_game.enemy.h &&
        g_game.ball.dx > 0) {
        g_game.ball.dx *= -1;
    }

    // Scoring
    if (g_game.ball.x < 0) {
        g_game.enemy_score++;
        if (g_game.enemy_score >= WIN_SCORE) {
            g_game.state = GAME_STATE_OVER;
            xSemaphoreGive(g_game_mutex);
            return;
        }
        reset_ball();
        s_last_speed_increase = now_ms();
    } else if (g_game.ball.x > SCREEN_WIDTH) {
        g_game.player_score++;
        if (g_game.player_score >= WIN_SCORE) {
            g_game.state = GAME_STATE_OVER;
            xSemaphoreGive(g_game_mutex);
            return;
        }
        reset_ball();
        s_last_speed_increase = now_ms();
    }

    // AI decision update
    if (now - s_last_ai_update > (int64_t)s_ai_reaction_delay) {
        s_last_ai_update    = now;
        s_ai_reaction_delay = AI_MIN_REACTION_DELAY +
                              (int)(esp_random() % (AI_MAX_REACTION_DELAY - AI_MIN_REACTION_DELAY));
        s_ai_target_offset  = (int)(esp_random() % (2 * AI_TARGET_OFFSET_RANGE + 1)) - AI_TARGET_OFFSET_RANGE;
    }

    // AI movement (only when ball approaches)
    if (g_game.ball.dx > 0) {
        int enemy_center = g_game.enemy.y + g_game.enemy.h / 2;
        int target_y     = g_game.ball.y + s_ai_target_offset;

        if (target_y < enemy_center - 3) {
            g_game.enemy.y -= AI_MOVE_SPEED;
        } else if (target_y > enemy_center + 3) {
            g_game.enemy.y += AI_MOVE_SPEED;
        }

        if ((int)(esp_random() % 100) < AI_MISTAKE_CHANCE) {
            g_game.enemy.y += (int)(esp_random() % 13) - 6;
        }
    }

    // Clamp paddles
    if (g_game.enemy.y < 0)                                   g_game.enemy.y = 0;
    if (g_game.enemy.y + g_game.enemy.h > SCREEN_HEIGHT)      g_game.enemy.y = SCREEN_HEIGHT - g_game.enemy.h;
    if (g_game.player.y < 0)                                   g_game.player.y = 0;
    if (g_game.player.y + g_game.player.h > SCREEN_HEIGHT)    g_game.player.y = SCREEN_HEIGHT - g_game.player.h;

    xSemaphoreGive(g_game_mutex);
}
