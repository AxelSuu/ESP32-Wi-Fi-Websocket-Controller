idf_component_register(
    SRCS "fx.c" "gfx.c" "pick.c" "survivor.c" "descender.c" "puzzler.c" "runner.c" "persist.c" "proto.c" "main.c" "engine.c" "pong.c" "display.c" "network.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_http_server esp_wifi nvs_flash spiffs app_update
        driver esp_driver_spi esp_driver_gpio esp_timer esp_hw_support
        freertos esp_netif esp_event mdns esp_system espcoredump
)

spiffs_create_partition_image(storage ../spiffs_image FLASH_IN_PROJECT)
