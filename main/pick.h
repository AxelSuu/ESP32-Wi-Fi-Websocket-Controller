#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "game_module.h"

// Shared "pick 1 of 3" upgrade overlay for the roguelite games. While it is open
// the game's world is paused: the game forwards input and ticks here instead.
typedef struct {
    bool open;
    int  choices[3];
    int  idx;
    int  guard_ms;   // confirm presses are ignored until this runs out
} pick_t;

// Open with 3 distinct kinds drawn from [0, n_kinds). n_kinds must be >= 3.
void pick_open(pick_t *p, int n_kinds);

// Count the confirm guard down. Call from tick() while open.
void pick_tick(pick_t *p, uint32_t dt_ms);

// NAV / LEFT / RIGHT move the highlight; SELECT / PRIMARY confirm. Returns the
// chosen kind (and closes the overlay), else -1.
int pick_input(pick_t *p, const input_event_t *ev);

// Draw the overlay: title plus the name of each offered kind.
void pick_render(const pick_t *p, const char *title, const char *const *names);
