#pragma once

#include <stdint.h>
#include <stdbool.h>

// Generic, game-agnostic input. The network layer translates wire messages
// into these; the engine routes them to the active game (or the menu).
typedef enum {
    INPUT_NONE,
    INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT,
    INPUT_PRIMARY,   // single action button
    INPUT_TILT,      // analog steering, value in ev.analog (degrees)
    INPUT_MOVE,      // 2-axis analog stick: ev.analog = x, ev.analog2 = y (each -1..1)
    INPUT_SELECT,    // menu: choose / launch
    INPUT_NAV,       // menu: move highlight (ev.analog sign, or LEFT/RIGHT)
    INPUT_BACK       // return to menu / dismiss game-over
} input_kind_t;

typedef struct {
    input_kind_t kind;
    int          player;   // 0 or 1 (slot id from the network layer)
    float        analog;   // INPUT_TILT/INPUT_NAV value, or INPUT_MOVE x-axis
    float        analog2;  // INPUT_MOVE y-axis, else 0
} input_event_t;

// One game = one vtable. Each game keeps its own mutable state file-static in
// its own .c. reset/on_input/tick run under the engine mutex; render may run
// unlocked (one torn frame is acceptable).
typedef struct {
    const char *id;            // stable wire id: "survivor","descender","pong","runner"
    const char *title;         // menu label
    uint8_t     min_players;   // 1 or 2
    bool        scored;        // true = has a numeric high score (false = win-based)
    const char *controls;      // JSON-array control descriptor for the phone (see proto)

    void (*reset)(void);                       // start a fresh round
    void (*on_input)(const input_event_t *ev); // handle one input event
    void (*on_leave)(int player);              // optional: that player's phone dropped mid-round
    void (*tick)(uint32_t dt_ms);              // advance the simulation
    void (*render)(void);                      // draw the world via gfx_* (no clear/present)
    bool (*is_over)(void);                     // round finished?
    int  (*winner)(void);                      // -1 none/draw, else player id
    int  (*score)(void);                       // -1 n/a, else game-specific score
} game_module_t;
