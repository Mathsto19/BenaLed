#ifndef HTTPD_H
#define HTTPD_H

#include "app_config.h"

const char *benaled_http_method_str(httpd_method_t method);
void log_http_request(httpd_req_t *req);
esp_err_t captive_redirect_common(httpd_req_t *req);
esp_err_t captive_redirect_handler(httpd_req_t *req);
esp_err_t captive_redirect_head_handler(httpd_req_t *req);
esp_err_t index_get_handler(httpd_req_t *req);
esp_err_t send_file(httpd_req_t *req, const char *path, const char *content_type);
esp_err_t css_get_handler(httpd_req_t *req);
esp_err_t js_get_handler(httpd_req_t *req);
esp_err_t gifuct_get_handler(httpd_req_t *req);
esp_err_t spfc_gif_get_handler(httpd_req_t *req);
esp_err_t favicon_get_handler(httpd_req_t *req);

#endif // HTTPD_H
