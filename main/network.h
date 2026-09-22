#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define NETWORK_READY_BIT  BIT0   // SoftAP up + HTTP server started
#define NETWORK_FAILED_BIT BIT1   // bring-up failed; engine shows a degraded screen

extern EventGroupHandle_t net_event_group;

void network_wifi_init_ap(void);

// The active SoftAP SSID for this unit (per-unit identity; see net_config.h).
// Valid after network_wifi_init_ap(); the engine shows it on the menu/attract
// screen so players know which AP to join.
const char *net_ssid(void);

// --- Server -> client messaging (used by the engine) ---

// Number of currently connected WebSocket clients (= occupied player slots).
int  net_player_count(void);

// Is this player slot's phone currently connected?
bool net_player_connected(int player);

// Broadcast typed messages to all connected phones. Safe to call while the
// engine mutex is held (these only touch the fd table + copied strings).
void net_broadcast_active(const char *game_id, int players, const char *controls);  // controller morph
void net_broadcast_waiting(int need, int have);
void net_broadcast_over(int winner, int score);
void net_broadcast_json(const char *json);
