#include "httpd.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define TAG "HTTPD"

static bool host_matches(const char *host, const char *candidate)
{
    if (host == NULL || candidate == NULL)
    {
        return false;
    }

    size_t host_len = strcspn(host, ":");
    size_t candidate_len = strlen(candidate);
    if (host_len != candidate_len)
    {
        return false;
    }

    return strncasecmp(host, candidate, candidate_len) == 0;
}

static bool should_redirect_host_to_portal(httpd_req_t *req)
{
    char host[128] = {0};
    size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
    if (host_len == 0 || host_len >= sizeof(host))
    {
        return true;
    }

    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK)
    {
        return true;
    }

    char ap_ip[20];
    snprintf(ap_ip, sizeof(ap_ip), "%u.%u.%u.%u",
             (unsigned)AP_IP_BYTES[0],
             (unsigned)AP_IP_BYTES[1],
             (unsigned)AP_IP_BYTES[2],
             (unsigned)AP_IP_BYTES[3]);

    if (host_matches(host, "BenaLed.com") ||
        host_matches(host, ap_ip) ||
        host_matches(host, "localhost"))
    {
        return true;
    }

    return false;
}

const char *benaled_http_method_str(httpd_method_t method)
{
    switch (method)
    {
    case HTTP_GET:
        return "GET";
    case HTTP_POST:
        return "POST";
    case HTTP_HEAD:
        return "HEAD";
    default:
        return "OTHER";
    }
}

void log_http_request(httpd_req_t *req)
{
    char host[128] = {0};
    char ua[192] = {0};

    size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
    if (host_len > 0 && host_len < sizeof(host))
    {
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    }

    size_t ua_len = httpd_req_get_hdr_value_len(req, "User-Agent");
    if (ua_len > 0 && ua_len < sizeof(ua))
    {
        httpd_req_get_hdr_value_str(req, "User-Agent", ua, sizeof(ua));
    }

    ESP_LOGI(TAG,
             "HTTP %s uri='%s' host='%s' ua='%s'",
             benaled_http_method_str(req->method),
             req->uri ? req->uri : "-",
             host[0] ? host : "-",
             ua[0] ? ua : "-");
}

esp_err_t captive_redirect_common(httpd_req_t *req)
{
    log_http_request(req);

    // Evita redirecionar hosts externos (ex.: apps de fundo de Android/Samsung),
    // o que reduz tempestade de conexoes HTTP no captive portal.
    if (!should_redirect_host_to_portal(req))
    {
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Connection", "close");
        return httpd_resp_send(req, NULL, 0);
    }

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", PORTAL_URL "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "close");

    return httpd_resp_send(req, NULL, 0);
}

esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    return captive_redirect_common(req);
}

esp_err_t captive_redirect_head_handler(httpd_req_t *req)
{
    return captive_redirect_common(req);
}

esp_err_t send_file(httpd_req_t *req, const char *path, const char *content_type)
{
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        ESP_LOGE(TAG, "Nao foi possivel abrir: %s", path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Arquivo nao encontrado");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_set_hdr(req, "Connection", "close");

    char chunk[1024];
    size_t read_bytes = 0;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        esp_err_t err = httpd_resp_send_chunk(req, chunk, read_bytes);
        if (err != ESP_OK)
        {
            fclose(file);
            httpd_resp_sendstr_chunk(req, NULL);
            return err;
        }
    }

    fclose(file);
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t index_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/index.html", "text/html");
}

esp_err_t css_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/style.css", "text/css");
}

esp_err_t js_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/app.js", "application/javascript");
}
esp_err_t gifuct_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/gifuct-js.min.js", "application/javascript");
}

esp_err_t spfc_gif_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/Complemento/spfc.gif", "image/gif");
}

esp_err_t favicon_get_handler(httpd_req_t *req)
{
    return send_file(req, "/spiffs/favicon.ico", "image/x-icon");
}
