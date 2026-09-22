#include "network.h"
#include "display.h"
#include "engine.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "main";

#define FRAME_MS          30
#define NET_READY_TIMEOUT pdMS_TO_TICKS(15000)  // AP bring-up budget before degrading
#define HEALTH_FRAMES     150                    // ~4.5 s of healthy loop → confirm OTA image

// Cancel a pending OTA rollback once the image has proven healthy (network up +
// the loop has rendered for a while). A bricked image never reaches this, so the
// bootloader reverts on the next boot. No-op unless we're running an OTA image
// still in PENDING_VERIFY — a normal `idf.py flash` image is never gated.
static void ota_mark_valid_if_pending(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t   state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            ESP_LOGI(TAG, "OTA image confirmed healthy; rollback cancelled");
    }
}

static void game_task(void *arg)
{
    EventBits_t bits = xEventGroupWaitBits(net_event_group,
                                           NETWORK_READY_BIT | NETWORK_FAILED_BIT,
                                           pdFALSE, pdFALSE, NET_READY_TIMEOUT);

    // From here on, watch this task for hangs — a stuck tick()/render now trips
    // the task watchdog and reboots (which, for a bad OTA image, triggers rollback).
    esp_err_t werr = esp_task_wdt_add(NULL);
    if (werr != ESP_OK) ESP_LOGW(TAG, "TWDT subscribe failed: %s", esp_err_to_name(werr));

    if (!(bits & NETWORK_READY_BIT)) {
        ESP_LOGE(TAG, "network not ready (bits=0x%x) — degraded mode", (unsigned)bits);
        for (;;) {
            engine_render_error("NETWORK DOWN");
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
        }
    }

    ESP_LOGI(TAG, "Network ready, starting engine loop");
    int        healthy = 0;
    bool       marked  = false;
    TickType_t wake    = xTaskGetTickCount();
    for (;;) {
        engine_update(FRAME_MS);
        engine_render();
        esp_task_wdt_reset();
        if (!marked && ++healthy >= HEALTH_FRAMES) {
            ota_mark_valid_if_pending();
            marked = true;
        }
        // Fixed FRAME_MS cadence (the sim advances FRAME_MS per frame). vTaskDelay
        // counts from "now", so a frame whose work crossed a tick boundary ran a
        // whole tick long; DelayUntil absorbs work time up to FRAME_MS. After a
        // long stall (e.g. flash writes) resync instead of bursting catch-up frames.
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS)) == pdFALSE)
            wake = xTaskGetTickCount();
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    net_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(display_init());
    engine_init();
    network_wifi_init_ap();

    xTaskCreatePinnedToCore(game_task, "game", 4096, NULL, 5, NULL, 0);
}
