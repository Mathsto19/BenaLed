idf_component_register(
    SRCS "main.c"
         "app_config.c" 
         "lib/httpd.c"
         "lib/dns_lib.c"
         "lib/wifi_lib.c"
         "lib/webserver.c"
         "tasks/dns_task.c"

    INCLUDE_DIRS "." "lib" "lib/include" "tasks" "tasks/include"
    REQUIRES esp_http_server esp_wifi esp_event esp_netif spiffs nvs_flash
)

spiffs_create_partition_image(spiffs ../spiffs FLASH_IN_PROJECT)