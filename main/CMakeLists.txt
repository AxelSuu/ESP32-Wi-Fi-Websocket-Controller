idf_component_register(
    SRCS "main.c" "game.c" "display.c" "network.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_http_server esp_wifi nvs_flash spiffs
        driver esp_driver_spi esp_driver_gpio esp_timer esp_hw_support
        freertos esp_netif esp_event
)

spiffs_create_partition_image(storage ../spiffs_image FLASH_IN_PROJECT)
