#include "descender.h"
#include "display.h"
#include "fx.h"
#include "pick.h"
#include "hw_config.h"
#include "esp_random.h"
#include <stdio.h>

// --- play area (below an 11px HUD band); the well scrolls past a fixed player row ---
#define PX0 3
#define PX1 (SCREEN_WIDTH - 4)
#define PY0 12
#define PY1 (SCREEN_HEIGHT - 2)
#define PLAYER_ROW 34            // player's fixed screen row; the world scrolls past it

#define MAX_ENEMIES 16
#define MAX_BULLETS 12
#define START_HP    4
#define IFRAME_MS   700          // invulnerability after a hit
#define SEGMENT     220.0f       // world depth between upgrade picks

// TUNABLE feel constants (adjust on hardware).
#define GRAV        0.020f       // downward accel per ms
#define VTERM       2.6f         // terminal fall speed (world units / 30 ms)
#define RECOIL      1.5f         // upward kick per shot
#define VUP_MAX     2.2f         // cap on upward (recoil/bounce) speed
#define BOUNCE_0    2.4f         // base stomp rebound
#define HSPEED      1.7f         // horizontal speed at full steer
#define BULLET_SPD  3.4f         // downward bullet speed

enum { UPG_FIRERATE, UPG_DAMAGE, UPG_MULTISHOT, UPG_HP, UPG_BOUNCE, UPG_COUNT };
static const char *const UPG_NAME[UPG_COUNT] = { "FIRE RATE", "DAMAGE", "MULTISHOT", "MAX HP", "STOMP+" };

typedef struct { float x, wy; int hp; bool alive; bool boss; bool spiky; } enemy_t;
typedef struct { float x, wy; bool alive; } bullet_t;

// --- player + run state ---
static float    s_px;            // player screen x (horizontal does not scroll)
static float    s_wy;            // player world depth (down +)
static float    s_vy;            // vertical world velocity (down +)
static float    s_steer;         // -1..1 horizontal intent (from tilt)
static int      s_hp, s_maxhp;
static int      s_invuln;        // ms of remaining i-frames
static int      s_combo, s_best_combo, s_kills;
static int      s_depth;         // max depth reached, in "metres" (= wy / 4)
static uint32_t s_run_ms;
static bool     s_over;

// weapon
static int      s_fire_cd, s_fire_interval, s_damage, s_multishot;
static float    s_bounce;

// spawning
static int      s_spawn_acc;
static uint32_t s_last_boss;

// upgrade choice
static pick_t   s_pick;
static float    s_next_seg;

static enemy_t  s_enemy[MAX_ENEMIES];
static bullet_t s_bullet[MAX_BULLETS];

static float frnd(void) { return (float)(esp_random() & 0xFFFF) / 65535.0f; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void descender_reset(void)
{
    s_px = (PX0 + PX1) / 2.0f;
    s_wy = 0;
    s_vy = 0.6f;
    s_steer = 0;
    s_hp = s_maxhp = START_HP;
    s_invuln = 0;
    s_combo = s_best_combo = s_kills = 0;
    s_depth = 0;
    s_run_ms = 0;
    s_over = false;

    s_fire_cd = 0;
    s_fire_interval = 150;
    s_damage = 1;
    s_multishot = 1;
    s_bounce = BOUNCE_0;

    s_spawn_acc = 0;
    s_last_boss = 0;

    s_pick.open = false;
    s_next_seg = SEGMENT;

    for (int i = 0; i < MAX_ENEMIES; i++) s_enemy[i].alive = false;
    for (int i = 0; i < MAX_BULLETS; i++) s_bullet[i].alive = false;
}

static void apply_upgrade(int u)
{
    switch (u) {
    case UPG_FIRERATE:
        s_fire_interval = s_fire_interval * 80 / 100;
        if (s_fire_interval < 60) s_fire_interval = 60;
        break;
    case UPG_DAMAGE:    s_damage++;                          break;
    case UPG_MULTISHOT: if (s_multishot < 4) s_multishot++;  break;
    case UPG_HP:        s_maxhp++; s_hp++;                   break;
    case UPG_BOUNCE:    s_bounce += 0.6f;                    break;
    }
}

static void fire(void)
{
    if (s_fire_cd > 0) return;
    s_fire_cd = s_fire_interval;
    for (int k = 0; k < s_multishot; k++) {
        int slot = -1;
        for (int i = 0; i < MAX_BULLETS; i++) if (!s_bullet[i].alive) { slot = i; break; }
        if (slot < 0) break;
        s_bullet[slot].x  = s_px + (k - (s_multishot - 1) / 2.0f) * 3.0f;
        s_bullet[slot].wy = s_wy + 2.0f;
        s_bullet[slot].alive = true;
    }
    s_vy -= RECOIL;
    if (s_vy < -VUP_MAX) s_vy = -VUP_MAX;
}

static void descender_on_input(const input_event_t *ev)
{
    if (ev->player != 0) return;
    if (s_pick.open) {
        int u = pick_input(&s_pick, ev);
        if (u >= 0) apply_upgrade(u);
        return;
    }
    switch (ev->kind) {
    case INPUT_TILT:    s_steer = clampf(ev->analog / 22.0f, -1.0f, 1.0f);  break;
    case INPUT_LEFT:    s_px = clampf(s_px - 6.0f, PX0, PX1);               break;
    case INPUT_RIGHT:   s_px = clampf(s_px + 6.0f, PX0, PX1);               break;
    case INPUT_PRIMARY: fire();                                            break;
    default: break;
    }
}

// Phone dropped: centre the steering so the run doesn't resume mid-tilt.
static void descender_on_leave(int player)
{
    if (player == 0) s_steer = 0;
}

static void spawn_enemy(void)
{
    int slot = -1;
    for (int i = 0; i < MAX_ENEMIES; i++) if (!s_enemy[i].alive) { slot = i; break; }
    if (slot < 0) return;

    bool boss = (s_run_ms - s_last_boss >= 30000u);
    if (boss) s_last_boss = s_run_ms;

    s_enemy[slot].x     = PX0 + 3 + frnd() * (PX1 - PX0 - 6);
    s_enemy[slot].wy    = s_wy + (PY1 - PLAYER_ROW) + 6 + frnd() * 26.0f;  // just below the view
    s_enemy[slot].boss  = boss;
    s_enemy[slot].spiky = !boss && (frnd() < 0.4f);                        // spiky = must be shot
    s_enemy[slot].hp    = boss ? (8 + s_depth / 40) : (1 + s_depth / 120);
    s_enemy[slot].alive = true;
}

static void kill_enemy(int e)
{
    s_enemy[e].alive = false;
    s_kills++;
    if (++s_combo > s_best_combo) s_best_combo = s_combo;
    fx_spark((int)s_px, PLAYER_ROW);
}

static void descender_tick(uint32_t dt_ms)
{
    if (s_over) return;
    if (s_pick.open) { pick_tick(&s_pick, dt_ms); return; }   // the upgrade pick pauses the world
    s_run_ms += dt_ms;
    if (s_invuln > 0) s_invuln -= (int)dt_ms;
    if (s_fire_cd > 0) s_fire_cd -= (int)dt_ms;

    float step = (float)dt_ms / 30.0f;

    // Horizontal (tilt) + vertical (gravity) motion.
    s_px = clampf(s_px + s_steer * HSPEED * step, PX0, PX1);
    s_vy += GRAV * (float)dt_ms;
    if (s_vy > VTERM) s_vy = VTERM;
    s_wy += s_vy * step;
    if (s_wy < 0) s_wy = 0;

    int d = (int)(s_wy / 4.0f);
    if (d > s_depth) s_depth = d;

    // Cross a depth segment -> offer an upgrade.
    if (s_wy >= s_next_seg) { s_next_seg += SEGMENT; pick_open(&s_pick, UPG_COUNT); return; }

    // Spawn cadence ramps with depth.
    int interval = 900 - s_depth * 3;
    if (interval < 240) interval = 240;
    s_spawn_acc += (int)dt_ms;
    if (s_spawn_acc >= interval) { s_spawn_acc = 0; spawn_enemy(); }

    // Bullets travel downward; hit enemies.
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!s_bullet[b].alive) continue;
        s_bullet[b].wy += BULLET_SPD * step;
        if (s_bullet[b].wy - s_wy > (PY1 - PLAYER_ROW) + 30) { s_bullet[b].alive = false; continue; }
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!s_enemy[e].alive) continue;
            float dx = s_enemy[e].x - s_bullet[b].x, dy = s_enemy[e].wy - s_bullet[b].wy;
            if (dx * dx + dy * dy < 16.0f) {
                s_enemy[e].hp -= s_damage;
                if (s_enemy[e].hp <= 0) kill_enemy(e);
                s_bullet[b].alive = false;
                break;
            }
        }
    }

    // Enemies vs player: stomp non-spiky from above (kill + bounce); else contact damages.
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!s_enemy[e].alive) continue;
        if (s_enemy[e].wy < s_wy - (PLAYER_ROW - PY0) - 6) { s_enemy[e].alive = false; continue; }
        float dx = s_enemy[e].x - s_px, dy = s_enemy[e].wy - s_wy;
        float hitr = s_enemy[e].boss ? 6.0f : 4.5f;
        if (dx * dx + dy * dy < hitr * hitr) {
            if (s_vy > 0.4f && dy > 0 && !s_enemy[e].boss && !s_enemy[e].spiky) {
                kill_enemy(e);
                s_vy = -s_bounce;
                if (s_vy < -VUP_MAX) s_vy = -VUP_MAX;
            } else if (s_invuln <= 0) {
                s_hp--;
                s_combo = 0;
                s_invuln = IFRAME_MS;
                fx_shake(2, 6);
                if (s_hp <= 0) { s_over = true; return; }
            }
        }
    }
}

static int screen_y(float wy) { return (int)(PLAYER_ROW + (wy - s_wy)); }

static void descender_render(void)
{
    // HUD band: HP pips, depth, combo.
    gfx_rect(0, 0, SCREEN_WIDTH, 11, 0x2);
    for (int i = 0; i < s_maxhp && i < 16; i++)
        gfx_rect(2 + i * 5, 3, 3, 5, i < s_hp ? 0xF : 0x4);
    char hud[20];
    snprintf(hud, sizeof hud, "%dm", s_depth);
    gfx_text(SCREEN_WIDTH / 2 - 10, 2, hud, 0xC);
    if (s_combo > 1) {
        snprintf(hud, sizeof hud, "x%d", s_combo);
        gfx_text(SCREEN_WIDTH - 22, 2, hud, 0xF);
    }

    // Scrolling well walls (dashes convey speed/depth).
    int phase = (int)s_wy & 7;
    for (int y = PY0; y <= PY1; y++) {
        if (((y + phase) & 7) < 4) { gfx_pixel(PX0 - 1, y, 0x6); gfx_pixel(PX1 + 1, y, 0x6); }
    }

    // Enemies + bullets.
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!s_enemy[e].alive) continue;
        int ey = screen_y(s_enemy[e].wy);
        if (ey < PY0 - 4 || ey > PY1 + 4) continue;
        int ex = (int)s_enemy[e].x;
        if (s_enemy[e].boss)       gfx_circle(ex, ey, 4, 0xF);
        else if (s_enemy[e].spiky) gfx_frame(ex - 2, ey - 2, 5, 5, 0xD);   // hollow = shoot it
        else                       gfx_rect(ex - 2, ey - 2, 4, 4, 0xA);     // solid = stompable
    }
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!s_bullet[b].alive) continue;
        gfx_vline((int)s_bullet[b].x, screen_y(s_bullet[b].wy), 2, 0xF);
    }

    // Player ship (downward chevron), blinking while invulnerable.
    if (s_invuln <= 0 || (s_invuln / 80) & 1) {
        int px = (int)s_px;
        gfx_hline(px - 2, PLAYER_ROW - 2, 5, 0xF);
        gfx_hline(px - 1, PLAYER_ROW - 1, 3, 0xF);
        gfx_pixel(px, PLAYER_ROW, 0xF);
    }

    pick_render(&s_pick, "UPGRADE", UPG_NAME);
}

static bool descender_is_over(void) { return s_over; }
static int  descender_winner(void)  { return -1; }
static int  descender_score(void)   { return s_depth + s_kills * 2 + s_best_combo * 2; }

const game_module_t DESCENDER = {
    .id          = "descender",
    .title       = "DESCENDER",
    .min_players = 1,
    .scored      = true,
    .controls    = "[{\"w\":\"tilt\"},{\"w\":\"dpad\",\"dirs\":[\"left\",\"right\"]},"
                   "{\"w\":\"btn\",\"label\":\"FIRE\",\"ev\":\"primary\",\"hold\":1}]",
    .reset       = descender_reset,
    .on_input    = descender_on_input,
    .on_leave    = descender_on_leave,
    .tick        = descender_tick,
    .render      = descender_render,
    .is_over     = descender_is_over,
    .winner      = descender_winner,
    .score       = descender_score,
};
