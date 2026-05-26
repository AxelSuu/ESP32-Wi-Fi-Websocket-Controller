#include "network.h"
#include "config.h"
#include "game.h"
#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "network";

EventGroupHandle_t net_event_group;

static httpd_handle_t s_server = NULL;
static int s_ws_fds[MAX_WS_CLIENTS];
static SemaphoreHandle_t s_ws_fd_mutex;

static void ws_fd_add(int fd)
{
    xSemaphoreTake(s_ws_fd_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == -1) {
            s_ws_fds[i] = fd;
            break;
        }
    }
    xSemaphoreGive(s_ws_fd_mutex);
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

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS handshake fd=%d", httpd_req_to_sockfd(req));
        ws_fd_add(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));

    // Get frame length first
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ws_fd_remove(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_PING || ws_pkt.len == 0) {
        return ESP_OK;
    }

    uint8_t buf[64] = {0};
    ws_pkt.payload  = buf;
    size_t recv_len = ws_pkt.len < sizeof(buf) - 1 ? ws_pkt.len : sizeof(buf) - 1;
    ret = httpd_ws_recv_frame(req, &ws_pkt, recv_len);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        char *msg = (char *)buf;
        if (strstr(msg, "\"up\""))   game_move_player_up();
        if (strstr(msg, "\"down\"")) game_move_player_down();
    }
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
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

static esp_err_t start_handler(httpd_req_t *req)
{
    game_start();
    httpd_resp_sendstr(req, "Game Started!");
    return ESP_OK;
}

static void start_webserver(void)
{
    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets  = 7;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start httpd");
        return;
    }

    httpd_uri_t uri_index = {
        .uri     = "/",
        .method  = HTTP_GET,
        .handler = index_handler,
    };
    httpd_register_uri_handler(s_server, &uri_index);

    httpd_uri_t uri_start = {
        .uri     = "/start",
        .method  = HTTP_GET,
        .handler = start_handler,
    };
    httpd_register_uri_handler(s_server, &uri_start);

    httpd_uri_t uri_ws = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &uri_ws);

    ESP_LOGI(TAG, "HTTP server started");
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

    start_webserver();
    xEventGroupSetBits(net_event_group, NETWORK_READY_BIT);
}

void network_wifi_init_ap(void)
{
    s_ws_fd_mutex = xSemaphoreCreateMutex();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) s_ws_fds[i] = -1;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_AP_START, wifi_ap_start_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .ap = {
            .ssid            = WIFI_SSID,
            .ssid_len        = sizeof(WIFI_SSID) - 1,
            .password        = WIFI_PASSWORD,
            .channel         = WIFI_AP_CHANNEL,
            .max_connection  = WIFI_MAX_CONN,
            .authmode        = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s", WIFI_SSID);
}
