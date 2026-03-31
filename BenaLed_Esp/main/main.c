#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_config.h"
#include "httpd.h"
#include "wifi_lib.h"
#include "dns_task.h"
#include "webserver.h"

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_spiffs();
    init_wifi_softap();
    start_webserver();
    init_dns_task(NULL);
}