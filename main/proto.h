#pragma once

#include <stdbool.h>
#include <stddef.h>

// Hand-rolled helpers for the controller's flat, trusted JSON wire protocol
// (see README "WebSocket Protocol"). Kept free of ESP-IDF dependencies so the
// parsing and message formatting can be unit-tested on the host
// (test/host/test_proto.c) without an ESP32.

// Wire schema version. Every server->client message carries "v":<this> as its
// first field so clients can detect a protocol they don't understand; bump it
// on any breaking change. Clients ignore fields they don't know, so additive
// changes need no bump.
#define PROTO_SCHEMA_VERSION 1

// --- inbound parsing (client -> server) ---

// Find "key" and copy its string value into out (NUL-terminated, capped at cap).
// Returns false if the key is absent or its value is not a JSON string.
bool proto_find_str(const char *msg, const char *key, char *out, size_t cap);

// Find "key" and parse its numeric value into *out. Returns false if the key is
// absent or its value is not a number.
bool proto_find_num(const char *msg, const char *key, double *out);

// --- outbound formatting (server -> client) ---
// Each writes one message into buf with snprintf semantics (returns the number
// of characters that would have been written, NUL excluded).
int proto_fmt_welcome(char *buf, size_t cap, int player);
int proto_fmt_system_info(char *buf, size_t cap, const char *version);
int proto_fmt_active(char *buf, size_t cap, const char *game_id, int players,
                     const char *controls);
#define PROTO_ACTIVE_MAX 256   // buffer for an `active` message; every game's must fit
int proto_fmt_waiting(char *buf, size_t cap, int need, int have);
int proto_fmt_over(char *buf, size_t cap, int winner, int score);

// Latency probe: echo the client's timestamp straight back so it can compute the
// round-trip time. ts is carried as-is (client epoch-ms fits exactly in a double).
int proto_fmt_pong(char *buf, size_t cap, double ts);
