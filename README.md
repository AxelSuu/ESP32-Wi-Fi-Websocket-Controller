## ESP32 Wi-Fi GameBox

A wireless, self-contained multi-game console running on an ESP32-S3 with an
SSD1327 grayscale LED. The ESP32 hosts its own Wi-Fi network and serves a web
controller that reshapes itself per game, pick a game from the on-device menu and
your phone turns into the right control surface. No app install required.

<table>
  <tr>
    <td><img src="imgs/pic1.jpeg"></td>
    <td><img src="imgs/pic2.jpeg"></td>
  </tr>
</table>

<table>
  <tr>
    <td><img src="imgs/wifi.png"></td>
    <td><img src="imgs/Websocket_controller.png"></td>
  </tr>
</table>

## Games

| Game  | Players | Controller            | How it plays |
|-------|---------|-----------------------|--------------|
| **Survivor** | 1 | Analog joystick      | Single-stick roguelite: weapons auto-fire, survive an escalating swarm, level up to pick upgrades |
| **Descender** | 1 | Tilt / ◀▶ + FIRE   | Downwell-like faller: drop an endless well, shoot downward to brake and kill, stomp enemies, chain combos, pick gun upgrades each depth |
| **Blocks** | 1 | ◀▶ + ROTATE + DROP | Tetris-like: stack falling tetrominoes, clear lines for points, speed ramps with level; tops out when the well fills |
| **Runner** | 1 | Tilt / ◀▶          | Top-down endless dodger: weave through traffic, grab coins/shields/slow-mo, bank near-miss bonuses, pick a perk each stretch; one crash ends the run |
| **Pong**  | 1–2 | Up / Down + Menu     | Classic paddle; vs adaptive AI, or a 2nd phone takes the right paddle; first to 3 |

Single-player games (Survivor, Descender, Blocks, Runner) end on a run-ending mistake and the **high
score per game is saved** (NVS), shown on the game-over screen, and listed on a **HIGH SCORES**
menu entry (with a device-side reset). Left idle on the attract screen, the console drops into a
**self-playing Pong demo** until someone joins. Pong is single-player vs AI
until a second phone connects, which takes over the right paddle (a mid-round disconnect drops it
back to AI).

If P1's phone drops mid-game (screen lock, Wi-Fi blip), the game **pauses** on the OLED and
resumes when it reconnects; with no phone at all for 2 minutes it returns to the attract screen.
Game buttons act on touch-down, and the d-pad (plus Pong's UP/DOWN and Descender's FIRE)
**auto-repeats while held**.

The controller also shows **round-trip latency** in its footer, plays WebAudio blips, has a
reconnect toast, and a **brightness** slider in the ⚙ menu (saved to NVS). Your last-played
game is remembered across reboots.

## Hardware Requirements

- **ESP32-S3** development board (8 MB flash; uses a custom partition table)
- **SSD1327 OLED display** (128×128 panel, top 128×96 used, 4-bit grayscale, SPI)
- Jumper wires

## Display Wiring (SPI)

| SSD1327 Pin | ESP32-S3 GPIO |
|-------------|---------------|
| CS          | GPIO 6        |
| DC          | GPIO 5        |
| RST         | GPIO 4        |
| MOSI (SDA)  | GPIO 11       |
| SCLK (SCL)  | GPIO 12       |
| VCC         | 3.3V          |
| GND         | GND           |

Pins live in [`main/hw_config.h`](main/hw_config.h).

## How It Works

1. **Power on** the ESP32 — it creates a Wi-Fi access point with a **per-unit SSID**
   (`GameBox-XXXX`, where `XXXX` is from the board's MAC; pw `12345678`), so two consoles in
   one room never collide. With no phone connected the OLED runs an **attract screen** that
   names the exact AP and address.
2. **Connect** your phone/laptop to that network. The OLED switches to the game menu — each
   game has its own icon, the selected row is highlighted, and a footer dot shows each phone.
3. **Open a browser** at **`http://gamebox.local`** (advertised over mDNS), or
   `http://192.168.4.1` as a fallback. `.local` resolves out of the box on iOS/macOS and
   most modern Android; on Windows it needs Bonjour.
4. **Navigate the menu** with Up/Down + SELECT — the phone mirrors the OLED's game list and
   highlights your choice in step with the screen.
5. **Play!** The phone swaps to that game's controls automatically. MENU returns you.

## Architecture

```
                 ┌───────────────────────────────────────────────┐
                 │                  engine.c                       │
                 │  app state machine: MENU / PLAYING / GAMEOVER   │
                 │  game registry[] + active-game vtable pointer   │
                 │  engine_update(dt) → active->tick / is_over     │
                 │  engine_dispatch_input(event) → menu or game    │
                 └───────┬───────────────────────────┬────────────┘
                         │ game_module_t vtable        │ gfx_* primitives
        ┌────────────────┴────────────┐        ┌───────┴───────────────┐
        │ the per-game vtable modules   │ draws │   display.c (gfx_*)    │
        │ each owns its state file-     │ ─────► │  SSD1327 driver + FB   │
        │ static; no global game struct │        │  (no game knowledge)   │
        └────────────────▲─────────────┘        └────────────────────────┘
                         │ generic input_event_t
                 ┌───────┴─────────────────┐
                 │        network.c          │
                 │  WS JSON → input_event    │
                 │  fd→player slot mapping   │
                 │  ws_broadcast() (server→  │
                 │  client: welcome/active/  │
                 │  waiting/over)            │
                 └───────────────────────────┘
```

- A **game is one `game_module_t` vtable** (`reset` / `on_input` / `tick` / `render` /
  `is_over` / `winner` / `score`, optional `on_leave`) defined in its own `.c`, with all
  mutable state file-static.
- **`display.c` knows nothing about games** — it exposes generic `gfx_*` primitives
  (`gfx_clear/pixel/rect/circle/text`, `display_present`). The engine draws the menu &
  game-over card; each game draws its own world.
- **One mutex** (`s_engine_mutex`) guards the active game. The engine takes it around
  `tick`/`on_input`, so games never lock. Renderers read unlocked (one torn frame is OK).
- **Input is generic.** `network.c` parses the WS JSON into an `input_event_t`
  (`kind`, `player`, `analog`) and hands it to the engine — no game-specific parsing.

## WebSocket Protocol

Small flat JSON over `/ws`. `t` = message type.

**Client → Server**

| `t` | Fields | Meaning |
|-----|--------|---------|
| `hello`  | —                          | sent on connect; server replies `welcome` |
| `nav`    | `dir`: `-1` / `1`          | move menu highlight |
| `select` | —                          | launch highlighted game / play again |
| `back`   | —                          | return to menu / dismiss game-over |
| `input`  | `ev`: `up`/`down`/`left`/`right`/`primary` | discrete game input |
| `tilt`   | `g`: float (degrees)       | analog steering (Descender, Runner) |
| `move`   | `x`, `y`: float (−1..1)    | analog joystick (Survivor) |
| `ping`   | `ts`: client epoch-ms      | latency probe; server echoes `pong` |
| `brightness` | `v`: 0–255, `save`: 0/1 | set OLED contrast (`save:1` on release → NVS) |

**Server → Client**

Every server→client message also carries `v`: the wire **schema version**
(`PROTO_SCHEMA_VERSION`, currently `1`) as its first field, so a client can reject a protocol
it doesn't understand. Clients ignore unknown fields, so additive changes don't bump it.

| `t` | Fields | Meaning |
|-----|--------|---------|
| `welcome` | `player`: 0/1                       | assigned player slot |
| `system_info` | `version`: string               | firmware version (sent right after `welcome`) |
| `screen`  | `mode`:`menu`, `games`:[labels], `idx`:n | menu state → **phone mirrors the OLED menu** |
| `active`  | `game`: id, `players`: n, `controls`: [widgets] | a game launched → **phone morphs its controls** |
| `waiting` | `need`: n, `have`: n                | 2-player game waiting for the second phone |
| `over`    | `winner`: -1/0/1, `score`: int      | round ended (`winner` -1 = none/draw; `score` -1 = n/a) |
| `pong`    | `ts`: echoed timestamp              | reply to `ping`; client computes round-trip latency |

The `active` message drives the controller morph — the phone builds its control surface
from the `controls` descriptor, a list of widgets: `joystick`, `tilt`, `pick` (◀ PICK ▶),
`dpad` (`dirs`), and `btn` (`label`, `ev`, and `hold:1` to auto-repeat while held). The `screen` message mirrors the on-device menu so the phone shows the live
game list with the highlighted entry (instead of blind Up/Down). On `over`, single-player
games (Survivor/Descender/Blocks/Runner) report a `score` and the phone shows `Score: n`. The web controller keeps
an exponential-backoff reconnect and buzzes (`navigator.vibrate`) on input and round end.


## Building & Flashing

1. **Install ESP-IDF** (v6.0; the project targets ESP32-S3).
2. **Set up the environment:**
   ```bash
   source $IDF_PATH/export.sh
   ```
3. **Build, flash & monitor:**
   ```bash
   idf.py build flash monitor
   ```

The web controller (`spiffs_image/index.html`) is packed into the `storage` SPIFFS
partition automatically during the build.

## Testing

On-hardware acceptance is the manual matrix in [`TEST_PLAN.md`](TEST_PLAN.md). The pure-C
logic also has **host unit tests** that need no ESP32 — they exercise
`survivor/descender/pong` against a stubbed `display.h`, plus the WS wire protocol
(`proto.c`: JSON parser + message formatters):

```bash
make -C test/host run
```

CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs those host tests, an
`esp-idf v6.0 / esp32s3` build with a size/firmware artifact, and an advisory
`clang-format` check (see [`.clang-format`](.clang-format)).

## Updating firmware (OTA)

The flash uses a **dual-slot OTA partition table** (`ota_0` / `ota_1` / `otadata` in
`partitions.csv`), so the firmware can be updated wirelessly without a USB cable:

1. Build a new image (`idf.py build`) — the app binary is `build/<project>.bin`.
2. On the controller page, tap the **⚙** button (top-right), choose that `.bin`, and tap
   **FLASH**. The phone POSTs it to `/update`; the device streams it into the inactive OTA
   slot, marks it bootable, and reboots into the new image. The controller then reconnects.

> ⚠️ **OTA updates the app only, not the controller page.** `spiffs_image/index.html` lives in
> the `storage` SPIFFS partition, which OTA does **not** touch. If a firmware change also changes
> the controller (e.g. a new game's control surface), you must flash over **USB** (`idf.py flash`,
> which repacks + writes SPIFFS) — otherwise the phone shows the old page (new game in the menu,
> but its controls missing). The page is served `no-store`, so a USB reflash takes effect on the
> next load without a manual browser cache-clear.

**Safety:** a corrupt/truncated upload is rejected (`image invalid`) and a `.bin` built from a
different project is rejected (`wrong firmware (project mismatch)`) — in both cases the running
slot is left untouched. A wrong-chip image is caught by the image header. And with **rollback**
enabled, a freshly flashed image boots in *pending-verify*: it's only made permanent after the
firmware runs healthily for a few seconds; an image that hangs or crashes before then is
**automatically reverted** to the previous slot on reboot. The first USB flash still uses
`idf.py flash`.

## Tilt on iOS

iOS Safari requires a user gesture before granting motion access. On a tilt game's screen (Descender/Runner) tap
**"ENABLE TILT"** once — it calls `DeviceOrientationEvent.requestPermission()` and then
streams throttled (~25 Hz) `tilt` messages. On Android / desktop tilt arms immediately; the
Left/Right buttons always work as a fallback.
