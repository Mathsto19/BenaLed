#ifndef HTTPD_H
#define HTTPD_H

#include "app_config.h"

esp_err_t send_file(httpd_req_t *req, const char *path, const char *content_type);

#endif // HTTPD_H