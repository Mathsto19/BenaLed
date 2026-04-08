#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "app_config.h"

void register_captive_uri(httpd_handle_t server, const char *uri);
httpd_handle_t start_webserver(void);
esp_err_t matrix_ws_handler(httpd_req_t *req);

#endif