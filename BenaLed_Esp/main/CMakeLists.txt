idf_component_register(
    SRCS "main.c"
         "app_config.c" 
         "lib/httpd.c"
         "lib/dns_lib.c"
         "lib/wifi_lib.c"
         "lib/webserver.c"
         "tasks/dns_task.c"
         "tasks/matrix_task.c"
         "tasks/oled_task.c"

    INCLUDE_DIRS "." "lib" "lib/include" "tasks" "tasks/include"

)

spiffs_create_partition_image(spiffs ../spiffs FLASH_IN_PROJECT)
