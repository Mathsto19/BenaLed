#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

typedef enum
{
    OLED_MODE_EXHIBITION = 0,
    OLED_MODE_TECHNICAL = 1,
} oled_mode_t;

typedef struct
{
    bool queue_mode_enabled;
    uint16_t queue_count;
    oled_mode_t oled_mode;
} admin_oled_status_t;

void register_captive_uri(httpd_handle_t server, const char *uri);
httpd_handle_t start_webserver(void);
esp_err_t matrix_ws_handler(httpd_req_t *req);
void webserver_get_admin_oled_status(admin_oled_status_t *out_status);

#endif
