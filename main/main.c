#include "network.h"
#include "display.h"
#include "game.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "main";

static void game_task(void *arg)
{
    xEventGroupWaitBits(net_event_group, NETWORK_READY_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Network ready, starting game loop");

    for (;;) {
        game_update();
        display_draw();
        vTaskDelay(pdMS_TO_TICKS(30));
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
    game_init();
    network_wifi_init_ap();

    xTaskCreatePinnedToCore(game_task, "game", 4096, NULL, 5, NULL, 0);
}
