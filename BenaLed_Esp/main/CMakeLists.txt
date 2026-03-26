idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server esp_wifi esp_event esp_netif spiffs nvs_flash
)

spiffs_create_partition_image(spiffs ../spiffs FLASH_IN_PROJECT)