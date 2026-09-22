#include "network.h"
#include "net_config.h"
#include "engine.h"
#include "game_module.h"
#include "proto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "mdns.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define WS_RX_BUF_SIZE 128

// Socket budget. Three related ceilings:
//   WIFI_MAX_CONN     (net_config.h) — stations the SoftAP will associate.
//   MAX_WS_CLIENTS    (net_config.h) — player slots = long-lived WS sockets.
//   HTTPD_MAX_SOCKETS (here)         — httpd's total fd table.
// Each WS client holds one socket for its whole session; HTTP GET / and
// POST /update need a few more short-lived sockets on top. So the invariant is
// MAX_WS_CLIENTS + (HTTP control headroom) <= HTTPD_MAX_SOCKETS, and a station
// can associate (WIFI_MAX_CONN) without necessarily owning a WS slot.
#define HTTPD_MAX_SOCKETS 7
_Static_assert(MAX_WS_CLIENTS + 2 <= HTTPD_MAX_SOCKETS,
               "httpd needs socket headroom for HTTP control beyond the WS clients");

static const char *TAG = "network";

EventGroupHandle_t net_event_group;

static httpd_handle_t s_server = NULL;
static int s_ws_fds[MAX_WS_CLIENTS];
static SemaphoreHandle_t s_ws_fd_mutex;

// Per-unit SoftAP SSID (32-char max per 802.11 + NUL). Filled by net_derive_ssid().
static char s_ssid[33];

const char *net_ssid(void)
{
    return s_ssid;
}

// Pick this unit's SSID once at bring-up: a provisioned name in the "factory"
// NVS namespace wins (written by the flashing jig); otherwise derive a stable
// per-unit name from the SoftAP MAC so two consoles never collide.
static void net_derive_ssid(void)
{
    nvs_handle_t h;
    if (nvs_open("factory", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof s_ssid;
        esp_err_t e = nvs_get_str(h, "ssid", s_ssid, &len);
        nvs_close(h);
        if (e == ESP_OK && s_ssid[0]) return;
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ssid, sizeof s_ssid, "%s-%02X%02X", WIFI_SSID_PREFIX, mac[4], mac[5]);
}

// --- WS client fd table; the slot index is the player id ---

static int ws_fd_add(int fd)
{
    int slot = -1;
    xSemaphoreTake(s_ws_fd_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == -1) {
            s_ws_fds[i] = fd;
            slot = i;
            break;
        }
    }
    xSemaphoreGive(s_ws_fd_mutex);
    return slot;
}

static void ws_fd_remove(int fd)
{
    xSemaphoreTake(s_ws_fd_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = -1;
            break;
        }
    }
    xSemaphoreGive(s_ws_fd_mutex);
}

static int slot_of_fd(int fd)
{
    int slot = -1;
    xSemaphoreTake(s_ws_fd_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) { slot = i; break; }
    }
    xSemaphoreGive(s_ws_fd_mutex);
    return slot;
}

int net_player_count(void)
{
    int n = 0;
    xSemaphoreTake(s_ws_fd_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) if (s_ws_fds[i] != -1) n++;
    xSemaphoreGive(s_ws_fd_mutex);
    return n;
}

bool net_player_connected(int player)
{
    if (player < 0 || player >= MAX_WS_CLIENTS) return false;
    xSemaphoreTake(s_ws_fd_mutex, portMAX_DELAY);
    bool on = (s_ws_fds[player] != -1);
    xSemaphoreGive(s_ws_fd_mutex);
    return on;
}

// Release a client slot and notify the engine — idempotent, so the graceful
// CLOSE-frame path and the close_fn safety net can both call it. Runs on the
// httpd task; never holds the engine mutex, so the engine may take it freely.
static void ws_cleanup_fd(int fd)
{
    int slot = slot_of_fd(fd);
    if (slot < 0) return;                  // already cleaned up / not one of ours
    ws_fd_remove(fd);
    engine_on_player_disconnect(slot);
}

// httpd close_fn: fires whenever httpd tears down a socket — including abrupt
// drops (phone leaves Wi-Fi / backgrounds) that never send a WS CLOSE frame, so
// stale fds can't linger and overcount net_player_count(). Replacing the default
// close_fn means we own closing the socket.
static void ws_close_fn(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    ws_cleanup_fd(sockfd);
    close(sockfd);
}

// --- async send / broadcast ---

typedef struct { httpd_handle_t hd; int fd; char *json; } ws_send_ctx_t;

static void ws_async_send(void *arg)
{
    ws_send_ctx_t *c = arg;
    httpd_ws_frame_t f = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)c->json,
        .len     = strlen(c->json),
    };
    httpd_ws_send_frame_async(c->hd, c->fd, &f);
    free(c->json);
    free(c);
}

// queue a copy of json to a single fd
static void ws_send_one(int fd, const char *json)
{
    if (!s_server) return;
    ws_send_ctx_t *c = malloc(sizeof *c);
    if (!c) return;
    c->hd = s_server;
    c->fd = fd;
    c->json = strdup(json);
    if (!c->json) { free(c); return; }
    if (httpd_queue_work(s_server, ws_async_send, c) != ESP_OK) {
        free(c->json);
        free(c);
    }
}

// queue a copy of json to every connected fd
static void ws_broadcast(const char *json)
{
    if (!s_server) return;
    int fds[MAX_WS_CLIENTS];
    int n = 0;
    xSemaphoreTake(s_ws_fd_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) if (s_ws_fds[i] != -1) fds[n++] = s_ws_fds[i];
    xSemaphoreGive(s_ws_fd_mutex);
    for (int i = 0; i < n; i++) ws_send_one(fds[i], json);
}

void net_broadcast_json(const char *json)
{
    ws_broadcast(json);
}

void net_broadcast_active(const char *game_id, int players, const char *controls)
{
    char buf[PROTO_ACTIVE_MAX];
    proto_fmt_active(buf, sizeof buf, game_id, players, controls);
    ws_broadcast(buf);
}

void net_broadcast_waiting(int need, int have)
{
    char buf[48];
    proto_fmt_waiting(buf, sizeof buf, need, have);
    ws_broadcast(buf);
}

void net_broadcast_over(int winner, int score)
{
    char buf[56];
    proto_fmt_over(buf, sizeof buf, winner, score);
    ws_broadcast(buf);
}

// --- WebSocket handler ---

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd   = httpd_req_to_sockfd(req);
        int slot = ws_fd_add(fd);
        if (slot < 0) {
            // Every player slot is taken. Refuse rather than let this phone's
            // input masquerade as another player's; its page retries later.
            ESP_LOGW(TAG, "WS fd=%d refused: all %d player slots in use", fd, MAX_WS_CLIENTS);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "WS handshake fd=%d slot=%d", fd, slot);
        return ESP_OK;
    }

    int fd = httpd_req_to_sockfd(req);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ws_cleanup_fd(fd);                 // close_fn would catch it too, but be prompt
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_PING || ws_pkt.len == 0) {
        return ESP_OK;
    }

    // Our controller protocol uses tiny payloads; a frame that fills the buffer
    // is malformed. Drop it explicitly rather than truncate-and-parse a partial
    // frame — the browser client auto-reconnects if the stream ever desyncs.
    if (ws_pkt.len >= WS_RX_BUF_SIZE) {
        ESP_LOGW(TAG, "WS frame too large (%u bytes) — dropping", (unsigned)ws_pkt.len);
        return ESP_OK;
    }

    uint8_t buf[WS_RX_BUF_SIZE] = {0};
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);   // len < WS_RX_BUF_SIZE, fits
    if (ret != ESP_OK) return ret;

    if (ws_pkt.type != HTTPD_WS_TYPE_TEXT) return ESP_OK;

    char *msg = (char *)buf;
    char t[16];
    if (!proto_find_str(msg, "t", t, sizeof t)) return ESP_OK;

    int player = slot_of_fd(fd);
    if (player < 0) return ESP_OK;         // not a slotted client

    if (strcmp(t, "hello") == 0) {
        char w[40];
        proto_fmt_welcome(w, sizeof w, player);
        ws_send_one(fd, w);
        // Tell the client which firmware it's talking to (version = app descriptor,
        // auto-derived from git describe unless PROJECT_VER is set).
        char si[80];
        proto_fmt_system_info(si, sizeof si, esp_app_get_description()->version);
        ws_send_one(fd, si);
        engine_on_player_connect(player);
        return ESP_OK;
    }

    // Latency probe: echo the client's timestamp straight back.
    if (strcmp(t, "ping") == 0) {
        double ts = 0;
        proto_find_num(msg, "ts", &ts);
        char p[48];
        proto_fmt_pong(p, sizeof p, ts);
        ws_send_one(fd, p);
        return ESP_OK;
    }

    // Display brightness from the ⚙ menu (0..255). "save":1 (slider release)
    // persists to NVS; live drags (save 0/absent) only apply.
    if (strcmp(t, "brightness") == 0) {
        double v;
        if (proto_find_num(msg, "v", &v)) {
            double save = 0;
            proto_find_num(msg, "save", &save);
            engine_set_brightness((int)v, (int)save);
        }
        return ESP_OK;
    }

    input_event_t ev = { .kind = INPUT_NONE, .player = player, .analog = 0, .analog2 = 0 };

    if (strcmp(t, "nav") == 0) {
        double d = 1;
        proto_find_num(msg, "dir", &d);
        ev.kind = INPUT_NAV;
        ev.analog = (float)d;
    } else if (strcmp(t, "select") == 0) {
        ev.kind = INPUT_SELECT;
    } else if (strcmp(t, "back") == 0) {
        ev.kind = INPUT_BACK;
    } else if (strcmp(t, "tilt") == 0) {
        double g;
        if (proto_find_num(msg, "g", &g)) { ev.kind = INPUT_TILT; ev.analog = (float)g; }
    } else if (strcmp(t, "move") == 0) {
        double x = 0, y = 0;
        proto_find_num(msg, "x", &x);
        proto_find_num(msg, "y", &y);
        ev.kind = INPUT_MOVE;
        ev.analog  = (float)x;
        ev.analog2 = (float)y;
    } else if (strcmp(t, "input") == 0) {
        char e[12];
        if (proto_find_str(msg, "ev", e, sizeof e)) {
            if      (strcmp(e, "up") == 0)      ev.kind = INPUT_UP;
            else if (strcmp(e, "down") == 0)    ev.kind = INPUT_DOWN;
            else if (strcmp(e, "left") == 0)    ev.kind = INPUT_LEFT;
            else if (strcmp(e, "right") == 0)   ev.kind = INPUT_RIGHT;
            else if (strcmp(e, "primary") == 0) ev.kind = INPUT_PRIMARY;
        }
    }

    if (ev.kind != INPUT_NONE) engine_dispatch_input(&ev);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    // The controller ships in the firmware image and changes with it; never let a
    // phone serve a stale cached copy (e.g. an old page missing a new game's surface).
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    FILE *f = fopen(SPIFFS_BASE_PATH "/index.html", "r");
    if (!f) {
        ESP_LOGE(TAG, "index.html not found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "index.html missing");
        return ESP_FAIL;
    }
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        httpd_resp_send_chunk(req, chunk, (ssize_t)n);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// OTA over the SoftAP: the controller POSTs a raw firmware .bin here; we stream
// it into the inactive OTA slot, mark it bootable, and reboot. The new image
// boots PENDING_VERIFY and is confirmed by ota_mark_valid_if_pending() in main.c.
static esp_err_t update_handler(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA slot");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA -> %s (%d bytes)", part->label, req->content_len);

    char  buf[1024];
    int   remaining = req->content_len;
    while (remaining > 0) {
        int want = remaining < (int)sizeof buf ? remaining : (int)sizeof buf;
        int r    = httpd_req_recv(req, buf, want);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            return ESP_FAIL;
        }
        if (esp_ota_write(ota, buf, r) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
            return ESP_FAIL;
        }
        remaining -= r;
    }

    if (esp_ota_end(ota) != ESP_OK) {
        // esp_ota_end validates the image header (magic + chip target), so a
        // wrong-chip .bin is already rejected here.
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "image invalid");
        return ESP_FAIL;
    }

    // Guard against flashing a right-chip but wrong-project image: compare the
    // new image's app descriptor project name to the running firmware's. The
    // slot holds the image but stays unbootable (we never set_boot) on mismatch.
    esp_app_desc_t new_desc;
    if (esp_ota_get_partition_description(part, &new_desc) == ESP_OK) {
        const esp_app_desc_t *cur = esp_app_get_description();
        if (strncmp(new_desc.project_name, cur->project_name,
                    sizeof new_desc.project_name) != 0) {
            ESP_LOGE(TAG, "OTA rejected: project '%s' != '%s'",
                     new_desc.project_name, cur->project_name);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "wrong firmware (project mismatch)");
            return ESP_FAIL;
        }
    }

    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set boot failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA ok; rebooting into %s", part->label);
    httpd_resp_sendstr(req, "OK");
    vTaskDelay(pdMS_TO_TICKS(500));        // let the response flush before reset
    esp_restart();
    return ESP_OK;
}

static bool start_webserver(void)
{
    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets  = HTTPD_MAX_SOCKETS;
    config.lru_purge_enable  = true;       // reclaim the oldest socket when full
    config.close_fn          = ws_close_fn; // clear player slots on any disconnect

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start httpd");
        return false;
    }

    httpd_uri_t uri_index = {
        .uri     = "/",
        .method  = HTTP_GET,
        .handler = index_handler,
    };
    httpd_register_uri_handler(s_server, &uri_index);

    httpd_uri_t uri_update = {
        .uri     = "/update",
        .method  = HTTP_POST,
        .handler = update_handler,
    };
    httpd_register_uri_handler(s_server, &uri_update);

    httpd_uri_t uri_ws = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &uri_ws);

    ESP_LOGI(TAG, "HTTP server started");
    return true;
}

// Advertise http://gamebox.local over mDNS so phones don't have to type the IP.
// A fixed "gamebox" hostname is safe: each unit is its own isolated SoftAP, so
// the per-unit GameBox-XXXX SSID disambiguates units while .local never collides.
// Best-effort — log and continue on failure so 192.168.4.1 always still works.
static void mdns_start(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init failed: %s (use 192.168.4.1)", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set("gamebox");
    mdns_instance_name_set("GameBox Controller");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS up: http://gamebox.local");
}

static void wifi_ap_start_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "AP started");

    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path              = SPIFFS_BASE_PATH,
        .partition_label        = "storage",
        .max_files              = 5,
        .format_if_mount_failed = false,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted");
    }

    if (start_webserver()) {
        mdns_start();
        xEventGroupSetBits(net_event_group, NETWORK_READY_BIT);
    } else {
        // No HTTP server → no controller. Tell the engine to show a degraded
        // screen instead of leaving the game task blocked forever.
        xEventGroupSetBits(net_event_group, NETWORK_FAILED_BIT);
    }
}

void network_wifi_init_ap(void)
{
    s_ws_fd_mutex = xSemaphoreCreateMutex();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) s_ws_fds[i] = -1;

    net_derive_ssid();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_AP_START, wifi_ap_start_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .ap = {
            .password        = WIFI_PASSWORD,
            .channel         = WIFI_AP_CHANNEL,
            .max_connection  = WIFI_MAX_CONN,
            .authmode        = WIFI_AUTH_WPA2_PSK,
        },
    };
    size_t ssid_len = strlen(s_ssid);
    if (ssid_len > sizeof(wifi_cfg.ap.ssid)) ssid_len = sizeof(wifi_cfg.ap.ssid);
    memcpy(wifi_cfg.ap.ssid, s_ssid, ssid_len);
    wifi_cfg.ap.ssid_len = ssid_len;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s", s_ssid);
}
