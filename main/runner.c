#include "runner.h"
#include "display.h"
#include "fx.h"
#include "pick.h"
#include "hw_config.h"
#include "esp_random.h"
#include <stdio.h>

#define RX0 3
#define RX1 (SCREEN_WIDTH - 4)
#define TOP 10                       // play-area top (below a thin HUD)
#define PLAYER_Y (SCREEN_HEIGHT - 12)

#define MAX_OBS   12
#define PERK_DIST 90                 // distance (HUD units) between perk picks

// TUNABLE feel constants.
#define BASE_SPEED 1.6f
#define SPEED_K    0.012f
#define SPEED_CAP  3.6f
#define HIT_R      5.0f
#define NEARMISS_R 10.0f

enum { OB_ROCK, OB_COIN, OB_SHIELD, OB_SLOW };
enum { PERK_MULT, PERK_MAGNET, PERK_BRAKE, PERK_COINVAL, PERK_COUNT };
static const char *const PERK_NAME[PERK_COUNT] = { "SCORE x", "MAGNET", "BRAKE", "RICH COINS" };

typedef struct { float x, y; bool alive; bool passed; int kind; } obs_t;

static float    s_px, s_steer;
static float    s_distf;
static int      s_dist, s_bonus, s_coins;
static int      s_shield, s_mult, s_coinval;
static int      s_slow_ms;
static bool     s_magnet;
static float    s_brake;             // subtracted from the speed cap
static int      s_invuln;
static int      s_spawn_acc;
static bool     s_over;

static pick_t   s_pick;
static int      s_next_perk;

static obs_t    s_obs[MAX_OBS];

static float frnd(void) { return (float)(esp_random() & 0xFFFF) / 65535.0f; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float absf(float v) { return v < 0 ? -v : v; }

static void runner_reset(void)
{
    s_px = (RX0 + RX1) / 2.0f;
    s_steer = 0;
    s_distf = 0; s_dist = 0; s_bonus = 0; s_coins = 0;
    s_shield = 0; s_mult = 1; s_coinval = 5;
    s_slow_ms = 0; s_magnet = false; s_brake = 0;
    s_invuln = 0;
    s_spawn_acc = 0;
    s_over = false;
    s_pick.open = false;
    s_next_perk = PERK_DIST;
    for (int i = 0; i < MAX_OBS; i++) s_obs[i].alive = false;
}

static void apply_perk(int p)
{
    switch (p) {
    case PERK_MULT:    s_mult++;                            break;
    case PERK_MAGNET:  s_magnet = true;                    break;
    case PERK_BRAKE:   if (s_brake < 1.2f) s_brake += 0.6f; break;
    case PERK_COINVAL: s_coinval += 5;                     break;
    }
}

static void runner_on_input(const input_event_t *ev)
{
    if (ev->player != 0) return;
    if (s_pick.open) {
        int p = pick_input(&s_pick, ev);
        if (p >= 0) apply_perk(p);
        return;
    }
    switch (ev->kind) {
    case INPUT_TILT:  s_steer = clampf(ev->analog / 22.0f, -1.0f, 1.0f); break;
    case INPUT_LEFT:  s_px = clampf(s_px - 6.0f, RX0, RX1);             break;
    case INPUT_RIGHT: s_px = clampf(s_px + 6.0f, RX0, RX1);            break;
    default: break;
    }
}

// Phone dropped: centre the steering so the run doesn't resume mid-tilt.
static void runner_on_leave(int player)
{
    if (player == 0) s_steer = 0;
}

static void spawn_obs(void)
{
    int slot = -1;
    for (int i = 0; i < MAX_OBS; i++) if (!s_obs[i].alive) { slot = i; break; }
    if (slot < 0) return;
    float r = frnd();
    int kind = OB_ROCK;
    if (r < 0.12f)      kind = OB_COIN;
    else if (r < 0.16f) kind = OB_SHIELD;
    else if (r < 0.20f) kind = OB_SLOW;
    s_obs[slot].x      = RX0 + 3 + frnd() * (RX1 - RX0 - 6);
    s_obs[slot].y      = TOP;
    s_obs[slot].kind   = kind;
    s_obs[slot].passed = false;
    s_obs[slot].alive  = true;
}

static void runner_tick(uint32_t dt_ms)
{
    if (s_over) return;
    if (s_pick.open) { pick_tick(&s_pick, dt_ms); return; }
    if (s_invuln > 0)  s_invuln  -= (int)dt_ms;
    if (s_slow_ms > 0) s_slow_ms -= (int)dt_ms;

    float step = (float)dt_ms / 30.0f;

    s_px = clampf(s_px + s_steer * 1.8f * step, RX0, RX1);

    float speed = BASE_SPEED + s_dist * SPEED_K;
    float cap = SPEED_CAP - s_brake;
    if (speed > cap) speed = cap;
    if (s_slow_ms > 0) speed *= 0.5f;

    s_distf += speed * step;
    s_dist = (int)(s_distf / 4.0f);

    if (s_dist >= s_next_perk) { s_next_perk += PERK_DIST; pick_open(&s_pick, PERK_COUNT); return; }

    int interval = 520 - s_dist * 2;
    if (interval < 220) interval = 220;
    s_spawn_acc += (int)dt_ms;
    if (s_spawn_acc >= interval) { s_spawn_acc = 0; spawn_obs(); }

    for (int i = 0; i < MAX_OBS; i++) {
        if (!s_obs[i].alive) continue;
        s_obs[i].y += speed * step;
        if (s_magnet && s_obs[i].kind == OB_COIN) {
            float mdx = s_px - s_obs[i].x;
            s_obs[i].x += clampf(mdx, -1.4f, 1.4f);
        }
        float dy = s_obs[i].y - PLAYER_Y;
        float dx = s_obs[i].x - s_px;
        if (absf(dy) < HIT_R && absf(dx) < HIT_R) {
            switch (s_obs[i].kind) {
            case OB_ROCK:
                if (s_invuln > 0) break;
                if (s_shield > 0) { s_shield--; s_invuln = 500; s_obs[i].alive = false; fx_shake(2, 6); }
                else { s_over = true; fx_shake(3, 8); return; }
                break;
            case OB_COIN:
                s_coins++; s_bonus += s_coinval * s_mult; s_obs[i].alive = false;
                fx_spark((int)s_obs[i].x, (int)s_obs[i].y);
                break;
            case OB_SHIELD: if (s_shield < 5) s_shield++; s_obs[i].alive = false; break;
            case OB_SLOW:   s_slow_ms = 2500;             s_obs[i].alive = false; break;
            }
            continue;
        }
        if (s_obs[i].kind == OB_ROCK && !s_obs[i].passed && s_obs[i].y > PLAYER_Y) {
            s_obs[i].passed = true;
            if (absf(dx) < NEARMISS_R) s_bonus += 2 * s_mult;   // near-miss dodge bonus
        }
        if (s_obs[i].y > SCREEN_HEIGHT + 4) s_obs[i].alive = false;
    }
}

static void runner_render(void)
{
    gfx_rect(0, 0, SCREEN_WIDTH, 9, 0x2);
    char hud[20];
    snprintf(hud, sizeof hud, "%dm", s_dist);
    gfx_text(2, 1, hud, 0xF);
    snprintf(hud, sizeof hud, "$%d", s_coins);
    gfx_text(SCREEN_WIDTH / 2 - 8, 1, hud, 0xC);
    for (int i = 0; i < s_shield && i < 6; i++)
        gfx_rect(SCREEN_WIDTH - 6 - i * 5, 2, 3, 4, 0xF);

    // Scrolling lane dashes.
    int phase = (int)s_distf % 10;
    for (int y = TOP + ((10 - phase) % 10); y < SCREEN_HEIGHT; y += 10)
        gfx_vline(SCREEN_WIDTH / 2, y, 5, 0x4);

    for (int i = 0; i < MAX_OBS; i++) {
        if (!s_obs[i].alive) continue;
        int ox = (int)s_obs[i].x, oy = (int)s_obs[i].y;
        switch (s_obs[i].kind) {
        case OB_ROCK:   gfx_rect(ox - 3, oy - 3, 6, 6, 0xA);  break;
        case OB_COIN:   gfx_circle(ox, oy, 2, 0xF);           break;
        case OB_SHIELD: gfx_frame(ox - 3, oy - 3, 6, 6, 0xF); break;
        case OB_SLOW:   gfx_rect(ox - 2, oy - 2, 4, 4, 0x6);  break;
        }
    }

    if (s_invuln <= 0 || (s_invuln / 80) & 1) {
        int px = (int)s_px;
        gfx_rect(px - 3, PLAYER_Y - 4, 6, 8, 0xF);
        gfx_rect(px - 1, PLAYER_Y - 2, 2, 3, 0x4);
    }

    if (s_slow_ms > 0) gfx_text(SCREEN_WIDTH - 30, SCREEN_HEIGHT - 8, "SLOW", 0x8);

    pick_render(&s_pick, "PERK", PERK_NAME);
}

static bool runner_is_over(void) { return s_over; }
static int  runner_winner(void)  { return -1; }
static int  runner_score(void)   { return s_dist + s_bonus; }

const game_module_t RUNNER = {
    .id          = "runner",
    .title       = "RUNNER",
    .min_players = 1,
    .scored      = true,
    .controls    = "[{\"w\":\"tilt\"},{\"w\":\"dpad\",\"dirs\":[\"left\",\"right\"]},{\"w\":\"pick\"}]",
    .reset       = runner_reset,
    .on_input    = runner_on_input,
    .on_leave    = runner_on_leave,
    .tick        = runner_tick,
    .render      = runner_render,
    .is_over     = runner_is_over,
    .winner      = runner_winner,
    .score       = runner_score,
};
