#include "survivor.h"
#include "display.h"
#include "fx.h"
#include "pick.h"
#include "hw_config.h"
#include "esp_random.h"
#include <stdio.h>
#include <math.h>

// --- arena (full screen below a thin HUD band) ---
#define AX0 2
#define AY0 13
#define AX1 (SCREEN_WIDTH - 2)
#define AY1 (SCREEN_HEIGHT - 2)

#define MAX_ENEMIES 48
#define MAX_BULLETS 24
#define MAX_GEMS    32
#define START_HP    5
#define IFRAME_MS   600        // invulnerability after a hit
#define MAGNET      26.0f      // XP pickup attraction radius
#define BOSS_MS     45000      // mini-boss cadence

// Upgrade kinds offered at level-up.
enum { UPG_FIRERATE, UPG_DAMAGE, UPG_MULTISHOT, UPG_PIERCE, UPG_SPEED, UPG_HP, UPG_COUNT };
static const char *const UPG_NAME[UPG_COUNT] = {
    "FIRE RATE", "DAMAGE", "MULTISHOT", "PIERCE", "SPEED", "MAX HP"
};

typedef struct { float x, y; int hp; bool alive; bool boss; } enemy_t;
typedef struct { float x, y, vx, vy; int pierce; bool alive; } bullet_t;
typedef struct { float x, y; bool alive; } gem_t;

// --- player + run state ---
static float    s_px, s_py, s_dirx, s_diry;
static int      s_hp, s_maxhp;
static int      s_invuln;            // ms of remaining i-frames
static int      s_level, s_xp, s_xp_next, s_kills;
static uint32_t s_run_ms;
static bool     s_over;

// weapon stats
static int      s_fire_interval, s_fire_acc;
static int      s_damage, s_multishot, s_pierce;
static float    s_bullet_speed, s_move_speed;

// spawning
static int      s_spawn_acc;
static uint32_t s_last_boss;

static pick_t   s_pick;          // level-up choice

static enemy_t  s_enemy[MAX_ENEMIES];
static bullet_t s_bullet[MAX_BULLETS];
static gem_t    s_gem[MAX_GEMS];

static float frnd(void) { return (float)(esp_random() & 0xFFFF) / 65535.0f; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void survivor_reset(void)
{
    s_px = (AX0 + AX1) / 2.0f;
    s_py = (AY0 + AY1) / 2.0f;
    s_dirx = s_diry = 0;
    s_hp = s_maxhp = START_HP;
    s_invuln = 0;
    s_level = 1;
    s_xp = 0;
    s_xp_next = 5;
    s_kills = 0;
    s_run_ms = 0;
    s_over = false;
    s_pick.open = false;

    s_fire_interval = 600;
    s_fire_acc = 0;
    s_damage = 1;
    s_multishot = 1;
    s_pierce = 0;
    s_bullet_speed = 3.2f;
    s_move_speed = 1.4f;

    s_spawn_acc = 0;
    s_last_boss = 0;

    for (int i = 0; i < MAX_ENEMIES; i++) s_enemy[i].alive = false;
    for (int i = 0; i < MAX_BULLETS; i++) s_bullet[i].alive = false;
    for (int i = 0; i < MAX_GEMS; i++)    s_gem[i].alive = false;
}

static void apply_upgrade(int u)
{
    switch (u) {
    case UPG_FIRERATE:
        s_fire_interval = s_fire_interval * 82 / 100;
        if (s_fire_interval < 120) s_fire_interval = 120;
        break;
    case UPG_DAMAGE:    s_damage++; break;
    case UPG_MULTISHOT: if (s_multishot < 5) s_multishot++; break;
    case UPG_PIERCE:    s_pierce++; break;
    case UPG_SPEED:     s_move_speed += 0.3f; break;
    case UPG_HP:        s_maxhp++; s_hp++; break;
    }
}

static void survivor_on_input(const input_event_t *ev)
{
    if (ev->player != 0) return;
    if (s_pick.open) {
        int u = pick_input(&s_pick, ev);
        if (u >= 0) apply_upgrade(u);
        return;
    }
    if (ev->kind == INPUT_MOVE) {
        s_dirx = clampf(ev->analog, -1.0f, 1.0f);
        s_diry = clampf(ev->analog2, -1.0f, 1.0f);
    }
}

// Phone dropped: let go of the stick so the run doesn't resume mid-drag.
static void survivor_on_leave(int player)
{
    if (player == 0) s_dirx = s_diry = 0;
}

static void spawn_enemy(uint32_t now)
{
    int slot = -1;
    for (int i = 0; i < MAX_ENEMIES; i++) if (!s_enemy[i].alive) { slot = i; break; }
    if (slot < 0) return;

    bool boss = (now - s_last_boss >= BOSS_MS);
    if (boss) s_last_boss = now;

    // Spawn on a random edge of the arena.
    float x, y;
    if (frnd() < 0.5f) { x = (frnd() < 0.5f) ? AX0 : AX1; y = AY0 + frnd() * (AY1 - AY0); }
    else               { y = (frnd() < 0.5f) ? AY0 : AY1; x = AX0 + frnd() * (AX1 - AX0); }

    s_enemy[slot].x = x;
    s_enemy[slot].y = y;
    s_enemy[slot].boss = boss;
    s_enemy[slot].hp = boss ? (15 + (int)(s_run_ms / 8000)) : (1 + (int)(s_run_ms / 18000));
    s_enemy[slot].alive = true;
}

static void spawn_gem(float x, float y)
{
    for (int i = 0; i < MAX_GEMS; i++)
        if (!s_gem[i].alive) { s_gem[i].x = x; s_gem[i].y = y; s_gem[i].alive = true; return; }
}

static int nearest_enemy(void)
{
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!s_enemy[i].alive) continue;
        float dx = s_enemy[i].x - s_px, dy = s_enemy[i].y - s_py;
        float d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void fire(void)
{
    int target = nearest_enemy();
    if (target < 0) return;
    float dx = s_enemy[target].x - s_px, dy = s_enemy[target].y - s_py;
    float base = atan2f(dy, dx);
    float spread = 0.18f;
    for (int k = 0; k < s_multishot; k++) {
        int slot = -1;
        for (int i = 0; i < MAX_BULLETS; i++) if (!s_bullet[i].alive) { slot = i; break; }
        if (slot < 0) return;
        float a = base + (k - (s_multishot - 1) / 2.0f) * spread;
        s_bullet[slot].x = s_px;
        s_bullet[slot].y = s_py;
        s_bullet[slot].vx = cosf(a) * s_bullet_speed;
        s_bullet[slot].vy = sinf(a) * s_bullet_speed;
        s_bullet[slot].pierce = s_pierce;
        s_bullet[slot].alive = true;
    }
}

static void survivor_tick(uint32_t dt_ms)
{
    if (s_over) return;
    if (s_pick.open) { pick_tick(&s_pick, dt_ms); return; }   // level-up pauses the world
    s_run_ms += dt_ms;
    if (s_invuln > 0) s_invuln -= (int)dt_ms;

    // Player movement.
    s_px = clampf(s_px + s_dirx * s_move_speed, AX0, AX1);
    s_py = clampf(s_py + s_diry * s_move_speed, AY0, AY1);

    // Spawn (rate ramps up over time).
    int interval = 1200 - (int)(s_run_ms / 30);
    if (interval < 250) interval = 250;
    s_spawn_acc += (int)dt_ms;
    if (s_spawn_acc >= interval) { s_spawn_acc = 0; spawn_enemy(s_run_ms); }

    // Enemies home in; contact damages the player.
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!s_enemy[i].alive) continue;
        float dx = s_px - s_enemy[i].x, dy = s_py - s_enemy[i].y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > 0.01f) {
            float sp = s_enemy[i].boss ? 0.45f : 0.7f;
            s_enemy[i].x += dx / d * sp;
            s_enemy[i].y += dy / d * sp;
        }
        float hitr = s_enemy[i].boss ? 6.0f : 4.0f;
        if (d < hitr && s_invuln <= 0) {
            s_hp--;
            s_invuln = IFRAME_MS;
            fx_shake(2, 6);
            if (s_hp <= 0) { s_over = true; return; }
        }
    }

    // Auto-fire.
    s_fire_acc += (int)dt_ms;
    if (s_fire_acc >= s_fire_interval) { s_fire_acc = 0; fire(); }

    // Bullets travel + hit enemies.
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!s_bullet[b].alive) continue;
        s_bullet[b].x += s_bullet[b].vx;
        s_bullet[b].y += s_bullet[b].vy;
        if (s_bullet[b].x < AX0 || s_bullet[b].x > AX1 ||
            s_bullet[b].y < AY0 || s_bullet[b].y > AY1) { s_bullet[b].alive = false; continue; }
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!s_enemy[e].alive) continue;
            float dx = s_enemy[e].x - s_bullet[b].x, dy = s_enemy[e].y - s_bullet[b].y;
            if (dx * dx + dy * dy < 16.0f) {
                s_enemy[e].hp -= s_damage;
                if (s_enemy[e].hp <= 0) {
                    s_enemy[e].alive = false;
                    s_kills++;
                    spawn_gem(s_enemy[e].x, s_enemy[e].y);
                    fx_spark((int)s_enemy[e].x, (int)s_enemy[e].y);
                }
                if (s_bullet[b].pierce > 0) s_bullet[b].pierce--;
                else { s_bullet[b].alive = false; break; }
            }
        }
    }

    // XP gems: magnet toward player, collect on contact.
    for (int g = 0; g < MAX_GEMS; g++) {
        if (!s_gem[g].alive) continue;
        float dx = s_px - s_gem[g].x, dy = s_py - s_gem[g].y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < MAGNET && d > 0.01f) { s_gem[g].x += dx / d * 1.6f; s_gem[g].y += dy / d * 1.6f; }
        if (d < 4.0f) {
            s_gem[g].alive = false;
            if (++s_xp >= s_xp_next) { s_xp -= s_xp_next; s_xp_next += 3; s_level++; pick_open(&s_pick, UPG_COUNT); }
        }
    }
}

static void survivor_render(void)
{
    // HUD band.
    gfx_rect(0, 0, SCREEN_WIDTH, 11, 0x2);
    for (int i = 0; i < s_maxhp && i < 16; i++)            // HP pips
        gfx_rect(2 + i * 5, 3, 3, 5, i < s_hp ? 0xF : 0x4);
    char hud[16];
    snprintf(hud, sizeof hud, "%lus", (unsigned long)(s_run_ms / 1000));
    gfx_text(SCREEN_WIDTH / 2 - 9, 2, hud, 0xC);
    snprintf(hud, sizeof hud, "LV%d", s_level);
    gfx_text(SCREEN_WIDTH - 24, 2, hud, 0xF);
    int xpw = s_xp_next ? (SCREEN_WIDTH * s_xp / s_xp_next) : 0;   // XP bar under HUD
    gfx_hline(0, 11, xpw, 0xF);

    // Gems, enemies, bullets, player.
    for (int g = 0; g < MAX_GEMS; g++)
        if (s_gem[g].alive) gfx_pixel((int)s_gem[g].x, (int)s_gem[g].y, 0x8);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!s_enemy[i].alive) continue;
        if (s_enemy[i].boss) gfx_circle((int)s_enemy[i].x, (int)s_enemy[i].y, 4, 0xF);
        else                 gfx_rect((int)s_enemy[i].x - 1, (int)s_enemy[i].y - 1, 3, 3, 0xA);
    }
    for (int b = 0; b < MAX_BULLETS; b++)
        if (s_bullet[b].alive) gfx_pixel((int)s_bullet[b].x, (int)s_bullet[b].y, 0xF);
    if (s_invuln <= 0 || (s_invuln / 80) & 1)             // blink while invulnerable
        gfx_circle((int)s_px, (int)s_py, 2, 0xF);

    pick_render(&s_pick, "LEVEL UP", UPG_NAME);
}

static bool survivor_is_over(void) { return s_over; }
static int  survivor_winner(void)  { return -1; }
static int  survivor_score(void)   { return s_kills + (int)(s_run_ms / 1000); }

const game_module_t SURVIVOR = {
    .id          = "survivor",
    .title       = "SURVIVOR",
    .min_players = 1,
    .scored      = true,
    .controls    = "[{\"w\":\"joystick\"},{\"w\":\"pick\"}]",
    .reset       = survivor_reset,
    .on_input    = survivor_on_input,
    .on_leave    = survivor_on_leave,
    .tick        = survivor_tick,
    .render      = survivor_render,
    .is_over     = survivor_is_over,
    .winner      = survivor_winner,
    .score       = survivor_score,
};
