#include "pick.h"
#include "display.h"
#include "fx.h"
#include "hw_config.h"
#include "esp_random.h"

// A confirm press within this long of the overlay opening, or of the previous
// confirm press, is ignored and re-arms the guard. So mashing or holding FIRE
// through a level-up can't auto-pick the first option; a press after a short
// pause (the player reading the choices) goes through.
#define PICK_GUARD_MS 300

void pick_open(pick_t *p, int n_kinds)
{
    for (int i = 0; i < 3; i++) {
        int k;
        do k = (int)(esp_random() % (uint32_t)n_kinds);
        while ((i > 0 && k == p->choices[0]) || (i > 1 && k == p->choices[1]));
        p->choices[i] = k;
    }
    p->idx      = 0;
    p->guard_ms = PICK_GUARD_MS;
    p->open     = true;
    fx_flash();
}

void pick_tick(pick_t *p, uint32_t dt_ms)
{
    if (p->guard_ms > 0) p->guard_ms -= (int)dt_ms;
}

int pick_input(pick_t *p, const input_event_t *ev)
{
    if (!p->open) return -1;
    switch (ev->kind) {
    case INPUT_NAV:   p->idx = (p->idx + (ev->analog < 0 ? 2 : 1)) % 3; break;
    case INPUT_LEFT:  p->idx = (p->idx + 2) % 3;                         break;
    case INPUT_RIGHT: p->idx = (p->idx + 1) % 3;                         break;
    case INPUT_SELECT:
    case INPUT_PRIMARY:
        if (p->guard_ms > 0) { p->guard_ms = PICK_GUARD_MS; break; }
        p->open = false;
        return p->choices[p->idx];
    default: break;
    }
    return -1;
}

void pick_render(const pick_t *p, const char *title, const char *const *names)
{
    if (!p->open) return;
    gfx_rect(8, 24, SCREEN_WIDTH - 16, 56, 0x0);
    gfx_frame(8, 24, SCREEN_WIDTH - 16, 56, 0xF);
    gfx_text((SCREEN_WIDTH - gfx_text_width(title, 1)) / 2, 28, title, 0xF);
    for (int i = 0; i < 3; i++) {
        int  y   = 40 + i * 12;
        bool sel = (i == p->idx);
        if (sel) gfx_rect(10, y - 1, SCREEN_WIDTH - 20, 11, 0x4);
        gfx_text(14, y, names[p->choices[i]], sel ? 0xF : 0xA);
    }
}
